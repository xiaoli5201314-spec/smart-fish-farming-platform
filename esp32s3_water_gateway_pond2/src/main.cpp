#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "pond_config.h"

namespace modbus
{

constexpr size_t kMaxReplyDataBytes = 32;

struct Reply
{
  uint8_t address = 0;
  uint8_t function = 0;
  uint8_t byteCount = 0;
  uint8_t data[kMaxReplyDataBytes] = {0};
};

uint16_t crc16(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i)
  {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
    {
      if ((crc & 0x0001U) != 0)
      {
        crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
      }
      else
      {
        crc >>= 1U;
      }
    }
  }
  return crc;
}

} // namespace modbus

struct SensorRuntime
{
  bool online = false;
  uint32_t lastAttemptMs = 0;
  uint32_t lastSuccessMs = 0;
  uint8_t consecutiveFailures = 0;
};

struct WaterReading
{
  float ph = 0.0F;
  float doMgL = 0.0F;
  float doSaturation = 0.0F;
  float waterTemp = 0.0F;
  bool phOnline = false;
  bool doOnline = false;
};

struct GatewayState
{
  SensorRuntime phRuntime;
  SensorRuntime doRuntime;
  WaterReading reading;
  uint32_t lastRefreshMs = 0;
  uint32_t lastAutoPollMs = 0;
  uint32_t totalPollCycles = 0;
  uint8_t onlineSensors = 0;
};

enum class ControlMode : uint8_t
{
  Run = 0,
  Pause = 1,
  Schedule = 2
};

struct ControlState
{
  ControlMode mode = ControlMode::Run;
  bool collecting = true;
  bool timeSynced = false;
  uint32_t publishIntervalSeconds = cfg::kDefaultMqttPublishIntervalSeconds;
  uint16_t scheduleStartMinutes = 8U * 60U;
  uint16_t scheduleEndMinutes = 18U * 60U;
  char scheduleStartText[6] = "08:00";
  char scheduleEndText[6] = "18:00";
  char timeText[32] = "";
};

HardwareSerial rs485Serial(1);
WebServer server(cfg::kHttpPort);
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
Adafruit_SSD1306 display(cfg::kScreenWidth, cfg::kScreenHeight, &Wire, -1);

GatewayState g_gateway;
ControlState g_control;
bool g_wifiStarted = false;
bool g_wifiWasConnected = false;
bool g_httpStarted = false;
bool g_displayReady = false;
bool g_mqttWasConnected = false;
bool g_timeSyncConfigured = false;
bool g_controlStatusDirty = true;
bool g_forceMqttLatestPublish = true;
uint32_t g_lastWifiAttemptMs = 0;
uint32_t g_lastMqttReconnectAttemptMs = 0;
uint32_t g_lastMqttPublishMs = 0;
uint32_t g_mqttPublishCount = 0;
uint32_t g_mqttPublishFailureCount = 0;
uint32_t g_lastDisplayRefreshMs = 0;
uint32_t g_lastTimeSyncAttemptMs = 0;
uint32_t g_lastControlStatusPublishMs = 0;
uint8_t g_oledAddress = 0;

void setupHttpServer();
void setupMqttClient();
bool mqttConfigured();
void publishControlStatus(bool force = false);
bool shouldCollectData();
void onMqttMessage(char *topic, byte *payload, unsigned int length);

uint16_t readBeU16(const uint8_t *data)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
}

float pow10Digits(uint16_t digits)
{
  if (digits > 4U)
  {
    digits = 4U;
  }

  float value = 1.0F;
  for (uint16_t i = 0; i < digits; ++i)
  {
    value *= 10.0F;
  }
  return value;
}

String escapeJson(const String &input)
{
  String output;
  output.reserve(input.length() + 8U);

  for (size_t i = 0; i < input.length(); ++i)
  {
    const char ch = input.charAt(i);
    switch (ch)
    {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      output += ch;
      break;
    }
  }

  return output;
}

void appendFieldPrefix(String &json, bool &firstField, const char *key)
{
  if (!firstField)
  {
    json += ',';
  }
  firstField = false;
  json += '"';
  json += key;
  json += "\":";
}

void appendStringField(String &json, bool &firstField, const char *key, const String &value)
{
  appendFieldPrefix(json, firstField, key);
  json += '"';
  json += escapeJson(value);
  json += '"';
}

void appendBoolField(String &json, bool &firstField, const char *key, bool value)
{
  appendFieldPrefix(json, firstField, key);
  json += value ? "true" : "false";
}

void appendUIntField(String &json, bool &firstField, const char *key, uint32_t value)
{
  appendFieldPrefix(json, firstField, key);
  json += String(value);
}

void appendIntField(String &json, bool &firstField, const char *key, int value)
{
  appendFieldPrefix(json, firstField, key);
  json += String(value);
}

void appendFloatField(String &json, bool &firstField, const char *key, float value, uint8_t digits)
{
  appendFieldPrefix(json, firstField, key);
  if (std::isfinite(value))
  {
    json += String(value, static_cast<unsigned int>(digits));
  }
  else
  {
    json += "0";
  }
}

bool probeI2CAddress(uint8_t address)
{
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool initDisplay()
{
  for (uint8_t address : {0x3C, 0x3D})
  {
    if (!probeI2CAddress(address))
    {
      continue;
    }

    if (display.begin(SSD1306_SWITCHCAPVCC, address))
    {
      g_oledAddress = address;
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextWrap(false);
      display.display();
      return true;
    }
  }

  return false;
}

void clearRs485Input()
{
  while (rs485Serial.available() > 0)
  {
    rs485Serial.read();
  }
}

size_t readBytesWithTimeout(uint8_t *buffer, size_t length, uint32_t timeoutMs)
{
  const uint32_t startedAt = millis();
  uint32_t lastByteAt = startedAt;
  size_t offset = 0;

  while (offset < length)
  {
    while (rs485Serial.available() > 0 && offset < length)
    {
      buffer[offset++] = static_cast<uint8_t>(rs485Serial.read());
      lastByteAt = millis();
    }

    const uint32_t nowMs = millis();
    if (offset == 0)
    {
      if ((nowMs - startedAt) >= timeoutMs)
      {
        break;
      }
    }
    else if ((nowMs - lastByteAt) >= timeoutMs)
    {
      break;
    }

    delay(1);
  }

  return offset;
}

const char *modbusErrorText(uint8_t errorCode)
{
  switch (errorCode)
  {
  case 1:
    return "function code error";
  case 2:
    return "register address error";
  case 3:
    return "register count error";
  case 4:
    return "value out of range";
  case 5:
    return "crc error";
  case 6:
    return "read only register";
  default:
    return "unknown modbus exception";
  }
}

bool readModbusReply(uint8_t address, uint8_t function, modbus::Reply *reply)
{
  uint8_t header[3] = {0};
  if (readBytesWithTimeout(header, sizeof(header), cfg::kModbusReplyTimeoutMs) != sizeof(header))
  {
    return false;
  }

  if (header[0] != address)
  {
    Serial.printf("Modbus address mismatch: expected=%u actual=%u\n", address, header[0]);
    return false;
  }

  const uint8_t actualFunction = header[1];
  if (actualFunction == static_cast<uint8_t>(function | 0x80U))
  {
    uint8_t errorCrc[2] = {0};
    readBytesWithTimeout(errorCrc, sizeof(errorCrc), cfg::kModbusReplyTimeoutMs);
    Serial.printf("Modbus exception: address=%u function=0x%02X code=%u (%s)\n",
                  address,
                  actualFunction,
                  header[2],
                  modbusErrorText(header[2]));
    return false;
  }

  if (actualFunction != function)
  {
    Serial.printf("Modbus function mismatch: expected=0x%02X actual=0x%02X\n", function, actualFunction);
    return false;
  }

  const uint8_t byteCount = header[2];
  if (byteCount > sizeof(reply->data))
  {
    Serial.printf("Modbus reply too long: %u bytes\n", byteCount);
    return false;
  }

  uint8_t tail[modbus::kMaxReplyDataBytes + 2] = {0};
  const size_t tailLength = static_cast<size_t>(byteCount) + 2U;
  if (readBytesWithTimeout(tail, tailLength, cfg::kModbusReplyTimeoutMs) != tailLength)
  {
    return false;
  }

  uint8_t frame[3 + modbus::kMaxReplyDataBytes] = {0};
  frame[0] = header[0];
  frame[1] = header[1];
  frame[2] = header[2];
  std::memcpy(frame + 3, tail, byteCount);

  const uint16_t expectedCrc = modbus::crc16(frame, 3U + byteCount);
  const uint16_t actualCrc =
      static_cast<uint16_t>(tail[byteCount]) |
      static_cast<uint16_t>(tail[byteCount + 1U] << 8U);
  if (expectedCrc != actualCrc)
  {
    Serial.printf("CRC mismatch: address=%u function=0x%02X\n", address, function);
    return false;
  }

  reply->address = header[0];
  reply->function = header[1];
  reply->byteCount = byteCount;
  std::memcpy(reply->data, tail, byteCount);
  return true;
}

bool readHoldingRegisters(uint8_t address, uint16_t startRegister, uint16_t registerCount, modbus::Reply *reply)
{
  uint8_t frame[8] = {0};
  frame[0] = address;
  frame[1] = 0x03;
  frame[2] = static_cast<uint8_t>(startRegister >> 8U);
  frame[3] = static_cast<uint8_t>(startRegister & 0xFFU);
  frame[4] = static_cast<uint8_t>(registerCount >> 8U);
  frame[5] = static_cast<uint8_t>(registerCount & 0xFFU);

  const uint16_t crc = modbus::crc16(frame, 6);
  frame[6] = static_cast<uint8_t>(crc & 0xFFU);
  frame[7] = static_cast<uint8_t>(crc >> 8U);

  clearRs485Input();
  rs485Serial.write(frame, sizeof(frame));
  rs485Serial.flush();
  delay(cfg::kRequestGapMs);
  return readModbusReply(address, 0x03, reply);
}

bool readPh(float *ph)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(cfg::kPhAddress, 0x0000, 1, &reply))
  {
    return false;
  }
  if (reply.byteCount != 2)
  {
    return false;
  }

  *ph = static_cast<float>(readBeU16(reply.data)) / 100.0F;
  return std::isfinite(*ph);
}

bool readDissolvedOxygen(float *doMgL, float *waterTemp)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(cfg::kDoAddress, 0x0000, 4, &reply))
  {
    return false;
  }
  if (reply.byteCount != 8)
  {
    return false;
  }

  const uint16_t doRaw = readBeU16(reply.data);
  const uint16_t doDigits = readBeU16(reply.data + 2);
  const uint16_t tempRaw = readBeU16(reply.data + 4);
  const uint16_t tempDigits = readBeU16(reply.data + 6);
  if (doDigits > 4U || tempDigits > 4U)
  {
    Serial.printf("Unexpected DO decimal digits: do=%u temp=%u\n", doDigits, tempDigits);
    return false;
  }

  *doMgL = static_cast<float>(doRaw) / pow10Digits(doDigits);
  *waterTemp = static_cast<float>(tempRaw) / pow10Digits(tempDigits);
  return std::isfinite(*doMgL) && std::isfinite(*waterTemp);
}

bool readDissolvedOxygenSaturation(float *saturation)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(cfg::kDoAddress, 0x0004, 2, &reply))
  {
    return false;
  }
  if (reply.byteCount != 4)
  {
    return false;
  }

  const uint16_t satRaw = readBeU16(reply.data);
  const uint16_t satDigits = readBeU16(reply.data + 2);
  if (satDigits > 4U)
  {
    Serial.printf("Unexpected DO saturation decimal digits: %u\n", satDigits);
    return false;
  }

  *saturation = static_cast<float>(satRaw) / pow10Digits(satDigits);
  return std::isfinite(*saturation);
}

void updateRuntimeState(SensorRuntime *runtime, bool success, uint32_t nowMs)
{
  runtime->lastAttemptMs = nowMs;
  if (success)
  {
    runtime->online = true;
    runtime->lastSuccessMs = nowMs;
    runtime->consecutiveFailures = 0;
    return;
  }

  if (runtime->consecutiveFailures < 255U)
  {
    runtime->consecutiveFailures++;
  }

  if (runtime->lastSuccessMs == 0)
  {
    runtime->online = false;
    return;
  }

  const bool tooManyFailures = runtime->consecutiveFailures >= cfg::kSensorOfflineFailureThreshold;
  const bool tooLongSinceSuccess = (nowMs - runtime->lastSuccessMs) >= cfg::kSensorOfflineGraceMs;
  if (tooManyFailures || tooLongSinceSuccess)
  {
    runtime->online = false;
  }
}

uint8_t countOnlineSensors()
{
  uint8_t count = 0;
  count += g_gateway.phRuntime.online ? 1 : 0;
  count += g_gateway.doRuntime.online ? 1 : 0;
  return count;
}

void refreshGatewayMeta()
{
  g_gateway.reading.phOnline = g_gateway.phRuntime.online;
  g_gateway.reading.doOnline = g_gateway.doRuntime.online;
  g_gateway.onlineSensors = countOnlineSensors();
  g_gateway.lastRefreshMs = millis();
}

bool refreshPh(bool verbose)
{
  const uint32_t nowMs = millis();
  float ph = 0.0F;
  const bool success = readPh(&ph);
  updateRuntimeState(&g_gateway.phRuntime, success, nowMs);
  if (success)
  {
    g_gateway.reading.ph = ph;
    if (verbose)
    {
      Serial.printf("[pH @ %u] pH=%.2f\n", cfg::kPhAddress, ph);
    }
  }
  else if (verbose)
  {
    Serial.printf("[pH @ %u] read failed, check wiring, power, address and A/B.\n", cfg::kPhAddress);
  }
  refreshGatewayMeta();
  return success;
}

bool refreshDo(bool verbose)
{
  const uint32_t nowMs = millis();
  float doMgL = 0.0F;
  float waterTemp = 0.0F;
  float saturation = 0.0F;

  const bool measurementOk = readDissolvedOxygen(&doMgL, &waterTemp);
  delay(cfg::kRequestGapMs);
  const bool saturationOk = readDissolvedOxygenSaturation(&saturation);
  const bool success = measurementOk;

  updateRuntimeState(&g_gateway.doRuntime, success, nowMs);
  if (success)
  {
    g_gateway.reading.doMgL = doMgL;
    g_gateway.reading.waterTemp = waterTemp;
    if (saturationOk)
    {
      g_gateway.reading.doSaturation = saturation;
    }

    if (verbose)
    {
      Serial.printf("[DO @ %u] DO=%.2f mg/L saturation=%s%.2f%% temp=%.2f C\n",
                    cfg::kDoAddress,
                    doMgL,
                    saturationOk ? "" : "(old) ",
                    g_gateway.reading.doSaturation,
                    waterTemp);
    }
  }
  else if (verbose)
  {
    Serial.printf("[DO @ %u] read failed, check wiring, power, address and A/B.\n", cfg::kDoAddress);
  }

  refreshGatewayMeta();
  return success;
}

void pollAllSensors(bool verbose)
{
  if (verbose)
  {
    Serial.println("Polling pH and dissolved oxygen sensors...");
  }

  refreshPh(verbose);
  delay(cfg::kInterSensorGapMs);
  refreshDo(verbose);

  g_gateway.totalPollCycles++;
  g_gateway.lastAutoPollMs = millis();
  refreshGatewayMeta();

  if (verbose)
  {
    Serial.printf("Poll done: online=%u/2 cycles=%lu\n",
                  g_gateway.onlineSensors,
                  static_cast<unsigned long>(g_gateway.totalPollCycles));
  }
}

bool wifiConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

String localIpString()
{
  if (!wifiConnected())
  {
    return "0.0.0.0";
  }
  return WiFi.localIP().toString();
}

String mqttBrokerString()
{
  String broker = cfg::kMqttBrokerHost;
  broker += ':';
  broker += String(cfg::kMqttBrokerPort);
  return broker;
}

const char *controlModeToString(ControlMode mode)
{
  switch (mode)
  {
  case ControlMode::Pause:
    return "pause";
  case ControlMode::Schedule:
    return "schedule";
  case ControlMode::Run:
  default:
    return "run";
  }
}

bool parseControlModeText(const String &modeText, ControlMode *mode)
{
  String normalized = modeText;
  normalized.trim();
  normalized.toLowerCase();

  if (normalized == "run")
  {
    *mode = ControlMode::Run;
    return true;
  }
  if (normalized == "pause")
  {
    *mode = ControlMode::Pause;
    return true;
  }
  if (normalized == "schedule")
  {
    *mode = ControlMode::Schedule;
    return true;
  }

  return false;
}

bool extractJsonStringField(const String &payload, const char *key, String &value)
{
  const String needle = String("\"") + key + "\"";
  const int keyPos = payload.indexOf(needle);
  if (keyPos < 0)
  {
    return false;
  }

  const int colonPos = payload.indexOf(':', keyPos + static_cast<int>(needle.length()));
  if (colonPos < 0)
  {
    return false;
  }

  int valuePos = colonPos + 1;
  while (valuePos < static_cast<int>(payload.length()))
  {
    const char ch = payload.charAt(static_cast<unsigned int>(valuePos));
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
    {
      break;
    }
    valuePos++;
  }

  if (valuePos >= static_cast<int>(payload.length()) || payload.charAt(static_cast<unsigned int>(valuePos)) != '"')
  {
    return false;
  }

  valuePos++;
  String decoded;
  while (valuePos < static_cast<int>(payload.length()))
  {
    const char ch = payload.charAt(static_cast<unsigned int>(valuePos));
    if (ch == '"')
    {
      value = decoded;
      return true;
    }

    if (ch == '\\' && (valuePos + 1) < static_cast<int>(payload.length()))
    {
      valuePos++;
      const char escaped = payload.charAt(static_cast<unsigned int>(valuePos));
      switch (escaped)
      {
      case '"':
        decoded += '"';
        break;
      case '\\':
        decoded += '\\';
        break;
      case 'n':
        decoded += '\n';
        break;
      case 'r':
        decoded += '\r';
        break;
      case 't':
        decoded += '\t';
        break;
      default:
        decoded += escaped;
        break;
      }
      valuePos++;
      continue;
    }

    decoded += ch;
    valuePos++;
  }

  return false;
}

bool extractJsonBoolField(const String &payload, const char *key, bool &value)
{
  const String needle = String("\"") + key + "\"";
  const int keyPos = payload.indexOf(needle);
  if (keyPos < 0)
  {
    return false;
  }

  const int colonPos = payload.indexOf(':', keyPos + static_cast<int>(needle.length()));
  if (colonPos < 0)
  {
    return false;
  }

  int valuePos = colonPos + 1;
  while (valuePos < static_cast<int>(payload.length()))
  {
    const char ch = payload.charAt(static_cast<unsigned int>(valuePos));
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
    {
      break;
    }
    valuePos++;
  }

  if (payload.startsWith("true", static_cast<unsigned int>(valuePos)))
  {
    value = true;
    return true;
  }
  if (payload.startsWith("false", static_cast<unsigned int>(valuePos)))
  {
    value = false;
    return true;
  }
  if (payload.startsWith("1", static_cast<unsigned int>(valuePos)))
  {
    value = true;
    return true;
  }
  if (payload.startsWith("0", static_cast<unsigned int>(valuePos)))
  {
    value = false;
    return true;
  }

  return false;
}

bool extractJsonUIntField(const String &payload, const char *key, uint32_t &value)
{
  const String needle = String("\"") + key + "\"";
  const int keyPos = payload.indexOf(needle);
  if (keyPos < 0)
  {
    return false;
  }

  const int colonPos = payload.indexOf(':', keyPos + static_cast<int>(needle.length()));
  if (colonPos < 0)
  {
    return false;
  }

  int valuePos = colonPos + 1;
  while (valuePos < static_cast<int>(payload.length()))
  {
    const char ch = payload.charAt(static_cast<unsigned int>(valuePos));
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
    {
      break;
    }
    valuePos++;
  }

  if (valuePos >= static_cast<int>(payload.length()))
  {
    return false;
  }

  uint32_t parsed = 0;
  bool hasDigit = false;
  while (valuePos < static_cast<int>(payload.length()))
  {
    const char ch = payload.charAt(static_cast<unsigned int>(valuePos));
    if (ch < '0' || ch > '9')
    {
      break;
    }
    const uint32_t digit = static_cast<uint32_t>(ch - '0');
    if (parsed > (0xFFFFFFFFUL - digit) / 10UL)
    {
      return false;
    }
    parsed = parsed * 10UL + digit;
    hasDigit = true;
    valuePos++;
  }

  if (!hasDigit)
  {
    return false;
  }

  value = parsed;
  return true;
}

bool isSupportedPublishIntervalSeconds(uint32_t seconds)
{
  switch (seconds)
  {
  case 2UL:
  case 60UL:
  case 300UL:
  case 1800UL:
  case 3600UL:
  case 21600UL:
  case 86400UL:
    return true;
  default:
    return false;
  }
}

bool parseScheduleTimeText(const String &text, uint16_t &minutes, char *buffer, size_t bufferSize)
{
  int hour = -1;
  int minute = -1;
  if (sscanf(text.c_str(), "%d:%d", &hour, &minute) != 2)
  {
    return false;
  }

  if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
  {
    return false;
  }

  minutes = static_cast<uint16_t>(hour * 60 + minute);
  if (bufferSize > 0)
  {
    snprintf(buffer, bufferSize, "%02d:%02d", hour, minute);
  }
  return true;
}

bool isWithinScheduleWindow(uint16_t currentMinutes, uint16_t startMinutes, uint16_t endMinutes)
{
  if (startMinutes == endMinutes)
  {
    return true;
  }

  if (startMinutes < endMinutes)
  {
    return currentMinutes >= startMinutes && currentMinutes < endMinutes;
  }

  return currentMinutes >= startMinutes || currentMinutes < endMinutes;
}

void maintainTimeSync()
{
  if (!wifiConnected())
  {
    return;
  }

  const uint32_t nowMs = millis();
  if (!g_timeSyncConfigured || (!g_control.timeSynced && (nowMs - g_lastTimeSyncAttemptMs) >= cfg::kTimeSyncRetryIntervalMs))
  {
    configTzTime(cfg::kTimeZone, cfg::kNtpServerPrimary, cfg::kNtpServerSecondary, cfg::kNtpServerTertiary);
    g_timeSyncConfigured = true;
    g_lastTimeSyncAttemptMs = nowMs;
    Serial.printf("Time sync configured: %s\n", cfg::kTimeZone);
  }

  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 20))
  {
    char timeBuffer[32];
    if (strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeInfo) > 0)
    {
      const bool wasSynced = g_control.timeSynced;
      g_control.timeSynced = true;
      std::strncpy(g_control.timeText, timeBuffer, sizeof(g_control.timeText) - 1U);
      g_control.timeText[sizeof(g_control.timeText) - 1U] = '\0';
      if (!wasSynced)
      {
        g_controlStatusDirty = true;
        Serial.printf("Time sync ready: %s\n", g_control.timeText);
      }
    }
  }
}

bool shouldCollectData()
{
  bool collecting = true;

  switch (g_control.mode)
  {
  case ControlMode::Pause:
    collecting = false;
    break;
  case ControlMode::Schedule:
  {
    if (g_control.timeSynced)
    {
      struct tm timeInfo;
      if (getLocalTime(&timeInfo, 10))
      {
        const uint16_t currentMinutes = static_cast<uint16_t>(timeInfo.tm_hour * 60 + timeInfo.tm_min);
        collecting = isWithinScheduleWindow(currentMinutes,
                                            g_control.scheduleStartMinutes,
                                            g_control.scheduleEndMinutes);
        char timeBuffer[32];
        if (strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeInfo) > 0)
        {
          std::strncpy(g_control.timeText, timeBuffer, sizeof(g_control.timeText) - 1U);
          g_control.timeText[sizeof(g_control.timeText) - 1U] = '\0';
        }
      }
      else
      {
        collecting = true;
      }
    }
    else
    {
      collecting = true;
    }
    break;
  }
  case ControlMode::Run:
  default:
    collecting = true;
    break;
  }

  if (g_control.collecting != collecting)
  {
    g_control.collecting = collecting;
    g_controlStatusDirty = true;
    Serial.printf("Control collecting changed: mode=%s collecting=%s\n",
                  controlModeToString(g_control.mode),
                  g_control.collecting ? "yes" : "no");
  }

  return g_control.collecting;
}

String buildControlStatusJson()
{
  String json;
  json.reserve(384);
  bool firstField = true;

  json = "{";
  appendBoolField(json, firstField, "ok", true);
  appendStringField(json, firstField, "pondId", cfg::kPondId);
  appendStringField(json, firstField, "gatewayId", cfg::kGatewayId);
  appendStringField(json, firstField, "mode", controlModeToString(g_control.mode));
  appendBoolField(json, firstField, "collecting", g_control.collecting);
  appendStringField(json, firstField, "scheduleStart", g_control.scheduleStartText);
  appendStringField(json, firstField, "scheduleEnd", g_control.scheduleEndText);
  appendBoolField(json, firstField, "timeSynced", g_control.timeSynced);
  appendStringField(json, firstField, "timeText", g_control.timeText);
  appendUIntField(json, firstField, "publishIntervalSeconds", g_control.publishIntervalSeconds);
  appendStringField(json, firstField, "commandTopic", cfg::kMqttTopicControlCmd);
  appendStringField(json, firstField, "statusTopic", cfg::kMqttTopicControlStatus);
  json += "}";
  return json;
}

void publishControlStatus(bool force)
{
  if (!mqttConfigured() || !mqttClient.connected())
  {
    return;
  }

  if (!force && !g_controlStatusDirty)
  {
    return;
  }

  const String payload = buildControlStatusJson();
  if ((payload.length() + std::strlen(cfg::kMqttTopicControlStatus) + 16U) > cfg::kMqttBufferSize)
  {
    Serial.printf("Control status payload too large: topic=%s payloadBytes=%u buffer=%u\n",
                  cfg::kMqttTopicControlStatus,
                  static_cast<unsigned int>(payload.length()),
                  cfg::kMqttBufferSize);
    return;
  }

  if (mqttClient.publish(cfg::kMqttTopicControlStatus, payload.c_str(), true))
  {
    g_controlStatusDirty = false;
    g_lastControlStatusPublishMs = millis();
    Serial.printf("Control status published: topic=%s mode=%s collecting=%s\n",
                  cfg::kMqttTopicControlStatus,
                  controlModeToString(g_control.mode),
                  g_control.collecting ? "yes" : "no");
  }
  else
  {
    Serial.printf("Control status publish failed: topic=%s state=%d\n",
                  cfg::kMqttTopicControlStatus,
                  mqttClient.state());
  }
}

void applyControlCommand(const String &payload)
{
  String modeText;
  if (!extractJsonStringField(payload, "mode", modeText))
  {
    bool collecting = true;
    if (!extractJsonBoolField(payload, "collecting", collecting))
    {
      Serial.println("Control command ignored: missing mode");
      return;
    }
    modeText = collecting ? "run" : "pause";
  }

  ControlMode requestedMode = ControlMode::Run;
  if (!parseControlModeText(modeText, &requestedMode))
  {
    Serial.printf("Control command ignored: invalid mode=%s\n", modeText.c_str());
    return;
  }

  if (requestedMode == ControlMode::Schedule)
  {
    String startText;
    String endText;
    if (!extractJsonStringField(payload, "scheduleStart", startText) ||
        !extractJsonStringField(payload, "scheduleEnd", endText))
    {
      Serial.println("Control command ignored: schedule requires start and end");
      return;
    }

    uint16_t startMinutes = 0;
    uint16_t endMinutes = 0;
    char startBuffer[6] = {0};
    char endBuffer[6] = {0};
    if (!parseScheduleTimeText(startText, startMinutes, startBuffer, sizeof(startBuffer)) ||
        !parseScheduleTimeText(endText, endMinutes, endBuffer, sizeof(endBuffer)))
    {
      Serial.println("Control command ignored: invalid schedule time");
      return;
    }

    g_control.scheduleStartMinutes = startMinutes;
    g_control.scheduleEndMinutes = endMinutes;
    std::strncpy(g_control.scheduleStartText, startBuffer, sizeof(g_control.scheduleStartText) - 1U);
    g_control.scheduleStartText[sizeof(g_control.scheduleStartText) - 1U] = '\0';
    std::strncpy(g_control.scheduleEndText, endBuffer, sizeof(g_control.scheduleEndText) - 1U);
    g_control.scheduleEndText[sizeof(g_control.scheduleEndText) - 1U] = '\0';
  }

  g_control.mode = requestedMode;
  if (requestedMode == ControlMode::Run)
  {
    g_control.collecting = true;
  }
  else if (requestedMode == ControlMode::Pause)
  {
    g_control.collecting = false;
  }
  else
  {
    g_control.collecting = true;
  }

  g_controlStatusDirty = true;

  Serial.printf("Control command applied: mode=%s collecting=%s\n",
                controlModeToString(g_control.mode),
                g_control.collecting ? "yes" : "no");
}

void applyPublishConfig(const String &payload)
{
  uint32_t seconds = 0;
  if (!extractJsonUIntField(payload, "publishIntervalSeconds", seconds))
  {
    Serial.println("Publish config ignored: missing publishIntervalSeconds");
    return;
  }

  if (!isSupportedPublishIntervalSeconds(seconds))
  {
    Serial.printf("Publish config ignored: unsupported interval=%lu seconds\n",
                  static_cast<unsigned long>(seconds));
    return;
  }

  g_control.publishIntervalSeconds = seconds;
  g_forceMqttLatestPublish = true;
  g_controlStatusDirty = true;
  Serial.printf("Publish interval applied: %lu seconds\n", static_cast<unsigned long>(seconds));
}

void onMqttMessage(char *topic, byte *payload, unsigned int length)
{
  if (topic == nullptr)
  {
    return;
  }

  const bool isControlCommand = std::strcmp(topic, cfg::kMqttTopicControlCmd) == 0;
  const bool isPublishConfig = std::strcmp(topic, cfg::kMqttTopicConfig) == 0;
  if (!isControlCommand && !isPublishConfig)
  {
    return;
  }

  String message;
  message.reserve(length);
  for (unsigned int index = 0; index < length; ++index)
  {
    message += static_cast<char>(payload[index]);
  }

  if (isPublishConfig)
  {
    applyPublishConfig(message);
  }
  else
  {
    applyControlCommand(message);
  }
}

void setupHttpServer();

void startWifi()
{
  if (g_wifiStarted && (millis() - g_lastWifiAttemptMs) < cfg::kWifiRetryIntervalMs)
  {
    return;
  }

  g_wifiStarted = true;
  g_lastWifiAttemptMs = millis();
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.setHostname(cfg::kHostname);
  WiFi.begin(cfg::kWifiSsid, cfg::kWifiPassword);
  Serial.printf("WiFi connecting: ssid=%s hostname=%s\n", cfg::kWifiSsid, cfg::kHostname);
}

void maintainWifi()
{
  if (wifiConnected())
  {
    if (!g_wifiWasConnected)
    {
      g_wifiWasConnected = true;
      Serial.printf("WiFi connected: ip=%s rssi=%d\n", localIpString().c_str(), WiFi.RSSI());
      if (!g_httpStarted)
      {
        setupHttpServer();
        g_httpStarted = true;
        Serial.printf("HTTP server started: http://%s/api/water/latest\n", localIpString().c_str());
      }
    }
    return;
  }

  if (g_wifiWasConnected)
  {
    g_wifiWasConnected = false;
    Serial.println("WiFi disconnected");
  }

  if ((millis() - g_lastWifiAttemptMs) >= cfg::kWifiRetryIntervalMs)
  {
    startWifi();
  }
}

bool mqttConfigured()
{
  return std::strlen(cfg::kMqttBrokerHost) > 0 &&
         std::strcmp(cfg::kMqttBrokerHost, "ALIYUN_MQTT_HOST_OR_IP") != 0;
}

String mqttClientIdString()
{
  String clientId = cfg::kMqttClientId;
  clientId += "-";
  clientId += WiFi.macAddress();
  clientId.replace(":", "");
  return clientId;
}

String buildMqttLatestJson()
{
  String json;
  json.reserve(320);
  bool firstField = true;

  json = "{";
  appendStringField(json, firstField, "pondId", cfg::kPondId);
  appendStringField(json, firstField, "gatewayId", cfg::kGatewayId);
  appendStringField(json, firstField, "sampledAt", g_control.timeText);
  appendBoolField(json, firstField, "timeSynced", g_control.timeSynced);
  appendFloatField(json, firstField, "ph", g_gateway.phRuntime.online ? g_gateway.reading.ph : 0.0F, 2);
  appendFloatField(json, firstField, "dissolvedOxygen", g_gateway.doRuntime.online ? g_gateway.reading.doMgL : 0.0F, 2);
  appendFloatField(json, firstField, "doMgL", g_gateway.doRuntime.online ? g_gateway.reading.doMgL : 0.0F, 2);
  appendFloatField(json, firstField, "doSaturation", g_gateway.doRuntime.online ? g_gateway.reading.doSaturation : 0.0F, 1);
  appendFloatField(json, firstField, "waterTemp", g_gateway.doRuntime.online ? g_gateway.reading.waterTemp : 0.0F, 2);
  appendFloatField(json, firstField, "chipTempC", temperatureRead(), 1);
  appendBoolField(json, firstField, "phOnline", g_gateway.phRuntime.online);
  appendBoolField(json, firstField, "doOnline", g_gateway.doRuntime.online);
  appendUIntField(json, firstField, "onlineSensors", g_gateway.onlineSensors);
  appendUIntField(json, firstField, "pollCycles", g_gateway.totalPollCycles);
  json += "}";
  return json;
}

String buildMqttStatusJson(bool online)
{
  String json;
  json.reserve(384);
  bool firstField = true;

  json = "{";
  appendBoolField(json, firstField, "ok", true);
  appendBoolField(json, firstField, "online", online);
  appendStringField(json, firstField, "pondId", cfg::kPondId);
  appendStringField(json, firstField, "gatewayId", cfg::kGatewayId);
  appendStringField(json, firstField, "ip", localIpString());
  appendStringField(json, firstField, "broker", mqttBrokerString());
  appendStringField(json, firstField, "latestTopic", cfg::kMqttTopicLatest);
  appendUIntField(json, firstField, "published", g_mqttPublishCount);
  appendUIntField(json, firstField, "failed", g_mqttPublishFailureCount);
  json += "}";
  return json;
}

String buildGatewayJson()
{
  String json;
  json.reserve(1200);
  bool firstDataField = true;

  json = "{\"ok\":true,\"data\":{";
  appendStringField(json, firstDataField, "pondId", cfg::kPondId);
  appendStringField(json, firstDataField, "gatewayId", cfg::kGatewayId);
  appendBoolField(json, firstDataField, "connected", wifiConnected());
  appendStringField(json, firstDataField, "deviceIp", localIpString());
  appendUIntField(json, firstDataField, "updateMs", g_gateway.lastRefreshMs);

  appendFieldPrefix(json, firstDataField, "gateway");
  json += '{';
  bool firstGatewayField = true;
  appendStringField(json, firstGatewayField, "name", "ESP32-S3 pond water gateway");
  appendStringField(json, firstGatewayField, "ssid", cfg::kWifiSsid);
  appendStringField(json, firstGatewayField, "ip", localIpString());
  appendBoolField(json, firstGatewayField, "wifiConnected", wifiConnected());
  appendIntField(json, firstGatewayField, "rssi", wifiConnected() ? WiFi.RSSI() : 0);
  appendStringField(json, firstGatewayField, "mqttTopic", cfg::kMqttTopicLatest);
  appendBoolField(json, firstGatewayField, "mqttConfigured", mqttConfigured());
  appendBoolField(json, firstGatewayField, "mqttConnected", mqttClient.connected());
  appendFloatField(json, firstGatewayField, "chipTempC", temperatureRead(), 1);
  appendUIntField(json, firstGatewayField, "publishIntervalSeconds", g_control.publishIntervalSeconds);
  appendUIntField(json, firstGatewayField, "pollIntervalMs", cfg::kAutoPollIntervalMs);
  appendUIntField(json, firstGatewayField, "onlineSensors", g_gateway.onlineSensors);
  appendUIntField(json, firstGatewayField, "configuredSensors", 2);
  appendUIntField(json, firstGatewayField, "pollCycles", g_gateway.totalPollCycles);
  json += '}';

  appendFieldPrefix(json, firstDataField, "metrics");
  json += '{';
  bool firstMetricsField = true;
  appendFloatField(json, firstMetricsField, "ph", g_gateway.phRuntime.online ? g_gateway.reading.ph : 0.0F, 2);
  appendFloatField(json, firstMetricsField, "dissolvedOxygen", g_gateway.doRuntime.online ? g_gateway.reading.doMgL : 0.0F, 2);
  appendFloatField(json, firstMetricsField, "doMgL", g_gateway.doRuntime.online ? g_gateway.reading.doMgL : 0.0F, 2);
  appendFloatField(json, firstMetricsField, "doSaturation", g_gateway.doRuntime.online ? g_gateway.reading.doSaturation : 0.0F, 1);
  appendFloatField(json, firstMetricsField, "waterTemp", g_gateway.doRuntime.online ? g_gateway.reading.waterTemp : 0.0F, 2);
  json += '}';

  appendFieldPrefix(json, firstDataField, "sensors");
  json += '[';
  json += '{';
  bool firstPhField = true;
  appendStringField(json, firstPhField, "address", String(cfg::kPhAddress));
  appendStringField(json, firstPhField, "name", "Henan Bingsheng digital pH sensor");
  appendBoolField(json, firstPhField, "online", g_gateway.phRuntime.online);
  appendFloatField(json, firstPhField, "ph", g_gateway.phRuntime.online ? g_gateway.reading.ph : 0.0F, 2);
  json += "},{";
  bool firstDoField = true;
  appendStringField(json, firstDoField, "address", String(cfg::kDoAddress));
  appendStringField(json, firstDoField, "name", "Henan Bingsheng small dissolved oxygen sensor");
  appendBoolField(json, firstDoField, "online", g_gateway.doRuntime.online);
  appendFloatField(json, firstDoField, "doMgL", g_gateway.doRuntime.online ? g_gateway.reading.doMgL : 0.0F, 2);
  appendFloatField(json, firstDoField, "doSaturation", g_gateway.doRuntime.online ? g_gateway.reading.doSaturation : 0.0F, 1);
  appendFloatField(json, firstDoField, "temperatureC", g_gateway.doRuntime.online ? g_gateway.reading.waterTemp : 0.0F, 2);
  json += "}]";

  json += "}}";
  return json;
}

void setupMqttClient()
{
  if (!mqttConfigured())
  {
    Serial.println("MQTT is not configured yet. Set kMqttBrokerHost after the cloud server is ready.");
    return;
  }

  mqttClient.setServer(cfg::kMqttBrokerHost, cfg::kMqttBrokerPort);
  mqttClient.setSocketTimeout(cfg::kMqttSocketTimeoutSeconds);
  mqttClient.setCallback(onMqttMessage);
  if (!mqttClient.setBufferSize(cfg::kMqttBufferSize))
  {
    Serial.printf("MQTT buffer resize failed, requested=%u bytes\n", cfg::kMqttBufferSize);
  }
}

bool connectMqtt()
{
  if (!mqttConfigured() || !wifiConnected())
  {
    return false;
  }

  const String clientId = mqttClientIdString();
  bool connected = false;
  if (std::strlen(cfg::kMqttUsername) > 0)
  {
    connected = mqttClient.connect(clientId.c_str(), cfg::kMqttUsername, cfg::kMqttPassword);
  }
  else
  {
    connected = mqttClient.connect(clientId.c_str());
  }

  if (!connected)
  {
    Serial.printf("MQTT connect failed: broker=%s state=%d\n",
                  mqttBrokerString().c_str(),
                  mqttClient.state());
    return false;
  }

  g_mqttWasConnected = true;
  Serial.printf("MQTT connected: broker=%s topic=%s\n",
                mqttBrokerString().c_str(),
                cfg::kMqttTopicLatest);

  if (mqttClient.subscribe(cfg::kMqttTopicControlCmd, 0))
  {
    Serial.printf("MQTT subscribed: %s\n", cfg::kMqttTopicControlCmd);
  }
  else
  {
    Serial.printf("MQTT subscribe failed: %s state=%d\n",
                  cfg::kMqttTopicControlCmd,
                  mqttClient.state());
  }

  if (mqttClient.subscribe(cfg::kMqttTopicConfig, 0))
  {
    Serial.printf("MQTT subscribed: %s\n", cfg::kMqttTopicConfig);
  }
  else
  {
    Serial.printf("MQTT subscribe failed: %s state=%d\n",
                  cfg::kMqttTopicConfig,
                  mqttClient.state());
  }

  g_controlStatusDirty = true;
  const String statusPayload = buildMqttStatusJson(true);
  mqttClient.publish(cfg::kMqttTopicStatus, statusPayload.c_str(), true);
  return true;
}

void maintainMqtt()
{
  if (!mqttConfigured())
  {
    return;
  }

  if (!wifiConnected())
  {
    if (g_mqttWasConnected)
    {
      g_mqttWasConnected = false;
      Serial.println("MQTT paused: WiFi disconnected");
    }
    return;
  }

  if (mqttClient.connected())
  {
    mqttClient.loop();
    return;
  }

  if (g_mqttWasConnected)
  {
    g_mqttWasConnected = false;
    Serial.printf("MQTT disconnected: state=%d\n", mqttClient.state());
  }

  const uint32_t nowMs = millis();
  if ((nowMs - g_lastMqttReconnectAttemptMs) < cfg::kMqttReconnectIntervalMs)
  {
    return;
  }

  g_lastMqttReconnectAttemptMs = nowMs;
  connectMqtt();
}

bool shouldPublishMqttLatest(uint32_t nowMs)
{
  if (g_forceMqttLatestPublish || g_mqttPublishCount == 0U)
  {
    return true;
  }

  const uint32_t intervalMs = g_control.publishIntervalSeconds * 1000UL;
  return (nowMs - g_lastMqttPublishMs) >= intervalMs;
}

bool publishMqttLatest()
{
  if (!mqttConfigured() || !mqttClient.connected())
  {
    return false;
  }

  const String payload = buildMqttLatestJson();
  if ((payload.length() + std::strlen(cfg::kMqttTopicLatest) + 16U) > cfg::kMqttBufferSize)
  {
    g_mqttPublishFailureCount++;
    Serial.printf("MQTT payload too large: topic=%s payloadBytes=%u buffer=%u\n",
                  cfg::kMqttTopicLatest,
                  static_cast<unsigned int>(payload.length()),
                  cfg::kMqttBufferSize);
    return false;
  }

  const bool ok = mqttClient.publish(cfg::kMqttTopicLatest, payload.c_str(), cfg::kMqttRetainLatest);
  if (ok)
  {
    g_lastMqttPublishMs = millis();
    g_mqttPublishCount++;
    g_forceMqttLatestPublish = false;
    return true;
  }

  g_mqttPublishFailureCount++;
  Serial.printf("MQTT publish failed: topic=%s payloadBytes=%u buffer=%u state=%d\n",
                cfg::kMqttTopicLatest,
                static_cast<unsigned int>(payload.length()),
                cfg::kMqttBufferSize,
                mqttClient.state());
  return false;
}

void printMqttStatus()
{
  Serial.printf("MQTT configured: %s\n", mqttConfigured() ? "yes" : "no");
  Serial.printf("MQTT broker: %s\n", mqttBrokerString().c_str());
  Serial.printf("MQTT connected: %s state=%d\n", mqttClient.connected() ? "yes" : "no", mqttClient.state());
  Serial.printf("MQTT latest topic: %s\n", cfg::kMqttTopicLatest);
  Serial.printf("MQTT status topic: %s\n", cfg::kMqttTopicStatus);
  Serial.printf("MQTT control cmd topic: %s\n", cfg::kMqttTopicControlCmd);
  Serial.printf("MQTT control status topic: %s\n", cfg::kMqttTopicControlStatus);
  Serial.printf("MQTT config topic: %s\n", cfg::kMqttTopicConfig);
  Serial.printf("MQTT publish interval: %lu seconds\n",
                static_cast<unsigned long>(g_control.publishIntervalSeconds));
  Serial.printf("Control mode: %s collecting: %s timeSynced: %s\n",
                controlModeToString(g_control.mode),
                g_control.collecting ? "yes" : "no",
                g_control.timeSynced ? "yes" : "no");
  Serial.printf("Control window: %s - %s time: %s\n",
                g_control.scheduleStartText,
                g_control.scheduleEndText,
                g_control.timeText[0] != '\0' ? g_control.timeText : "--");
  Serial.printf("MQTT publish count: %lu failed: %lu lastPublishMs: %lu\n",
                static_cast<unsigned long>(g_mqttPublishCount),
                static_cast<unsigned long>(g_mqttPublishFailureCount),
                static_cast<unsigned long>(g_lastMqttPublishMs));
  Serial.printf("Control status publish time: %lu\n",
                static_cast<unsigned long>(g_lastControlStatusPublishMs));
}

void sendJsonResponse(int statusCode, const String &payload)
{
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(statusCode, "application/json; charset=utf-8", payload);
}

void handleRoot()
{
  String payload = "{\"ok\":true,\"service\":\"fish-pond-water-gateway\",\"pondId\":\"";
  payload += escapeJson(cfg::kPondId);
  payload += "\",\"endpoint\":\"/api/water/latest\",\"health\":\"/api/water/health\",\"mqttTopic\":\"";
  payload += escapeJson(cfg::kMqttTopicLatest);
  payload += "\"}";
  sendJsonResponse(200, payload);
}

void handleHealth()
{
  String payload;
  payload.reserve(480);
  bool firstField = true;

  payload = "{";
  appendBoolField(payload, firstField, "ok", true);
  appendStringField(payload, firstField, "pondId", cfg::kPondId);
  appendStringField(payload, firstField, "gatewayId", cfg::kGatewayId);
  appendBoolField(payload, firstField, "wifiConnected", wifiConnected());
  appendStringField(payload, firstField, "ip", localIpString());
  appendUIntField(payload, firstField, "onlineSensors", g_gateway.onlineSensors);
  appendBoolField(payload, firstField, "mqttConfigured", mqttConfigured());
  appendBoolField(payload, firstField, "mqttConnected", mqttClient.connected());
  appendStringField(payload, firstField, "mqttBroker", mqttBrokerString());
  appendStringField(payload, firstField, "mqttTopic", cfg::kMqttTopicLatest);
  appendUIntField(payload, firstField, "publishIntervalSeconds", g_control.publishIntervalSeconds);
  appendStringField(payload, firstField, "controlMode", controlModeToString(g_control.mode));
  appendBoolField(payload, firstField, "collecting", g_control.collecting);
  appendBoolField(payload, firstField, "timeSynced", g_control.timeSynced);
  appendStringField(payload, firstField, "scheduleStart", g_control.scheduleStartText);
  appendStringField(payload, firstField, "scheduleEnd", g_control.scheduleEndText);
  appendStringField(payload, firstField, "timeText", g_control.timeText);
  appendUIntField(payload, firstField, "updateMs", g_gateway.lastRefreshMs);
  payload += "}";

  sendJsonResponse(200, payload);
}

void handleLatest()
{
  sendJsonResponse(200, buildGatewayJson());
}

void handleNotFound()
{
  sendJsonResponse(404, "{\"ok\":false,\"message\":\"not found\"}");
}

void setupHttpServer()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/water/health", HTTP_GET, handleHealth);
  server.on("/api/water/latest", HTTP_GET, handleLatest);
  server.onNotFound(handleNotFound);
  server.begin();
}

void drawOledStatus(bool force)
{
  if (!g_displayReady)
  {
    return;
  }

  const uint32_t nowMs = millis();
  if (!force && (nowMs - g_lastDisplayRefreshMs) < cfg::kDisplayRefreshMs)
  {
    return;
  }
  g_lastDisplayRefreshMs = nowMs;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(cfg::kPondId);
  display.print(" ");
  display.print(wifiConnected() ? "WiFi" : "noWiFi");

  display.setCursor(0, 12);
  display.print("IP ");
  display.print(localIpString());

  display.setCursor(0, 24);
  display.print("pH ");
  if (g_gateway.phRuntime.online)
  {
    display.print(g_gateway.reading.ph, 2);
  }
  else
  {
    display.print("--");
  }

  display.setCursor(64, 24);
  display.print("DO ");
  if (g_gateway.doRuntime.online)
  {
    display.print(g_gateway.reading.doMgL, 1);
  }
  else
  {
    display.print("--");
  }

  display.setCursor(0, 36);
  display.print("Temp ");
  if (g_gateway.doRuntime.online)
  {
    display.print(g_gateway.reading.waterTemp, 1);
    display.print("C");
  }
  else
  {
    display.print("--");
  }

  display.setCursor(0, 48);
  display.print("MQTT ");
  if (!mqttConfigured())
  {
    display.print("config");
  }
  else
  {
    display.print(mqttClient.connected() ? "online" : "offline");
  }

  display.display();
}

void printWifiStatus()
{
  Serial.printf("Pond: %s gateway: %s\n", cfg::kPondId, cfg::kGatewayId);
  Serial.printf("WiFi SSID: %s\n", cfg::kWifiSsid);
  Serial.printf("WiFi connected: %s\n", wifiConnected() ? "yes" : "no");
  Serial.printf("IP: %s RSSI: %d\n", localIpString().c_str(), wifiConnected() ? WiFi.RSSI() : 0);
  Serial.printf("HTTP latest: http://%s/api/water/latest\n", localIpString().c_str());
}

void printHelp()
{
  Serial.println();
  Serial.println("=== Fish pond water gateway commands ===");
  Serial.println("p : test pH sensor once");
  Serial.println("d : test dissolved oxygen sensor once");
  Serial.println("a : poll both sensors once");
  Serial.println("j : print current JSON");
  Serial.println("i : print WiFi/IP/API information");
  Serial.println("m : print MQTT information");
  Serial.println("q : publish current JSON to MQTT once");
  Serial.println("h : show help again");
  Serial.println();
}

void handleConsoleCommand(char cmd)
{
  switch (cmd)
  {
  case 'p':
  case 'P':
    refreshPh(true);
    break;
  case 'd':
  case 'D':
    refreshDo(true);
    break;
  case 'a':
  case 'A':
    pollAllSensors(true);
    break;
  case 'j':
  case 'J':
    Serial.println(buildGatewayJson());
    break;
  case 'i':
  case 'I':
    printWifiStatus();
    break;
  case 'm':
  case 'M':
    printMqttStatus();
    break;
  case 'q':
  case 'Q':
    if (publishMqttLatest())
    {
      Serial.println("MQTT publish success.");
    }
    else
    {
      Serial.println("MQTT publish skipped or failed. Check WiFi, cloud host, port, username and password.");
    }
    break;
  case 'h':
  case 'H':
    printHelp();
    break;
  case '\r':
  case '\n':
  case ' ':
    break;
  default:
    Serial.printf("Unknown command: %c\n", cmd);
    printHelp();
    break;
  }
}

void setup()
{
  Serial.begin(cfg::kUsbDebugBaud);
  delay(300);

  Serial.println();
  Serial.printf("ESP32-S3 fish pond water gateway booting: %s (%s)\n", cfg::kPondId, cfg::kGatewayId);
  Serial.printf("RS485: RX=%u TX=%u baud=%lu pH address=%u DO address=%u\n",
                cfg::kRs485RxPin,
                cfg::kRs485TxPin,
                static_cast<unsigned long>(cfg::kRs485Baud),
                cfg::kPhAddress,
                cfg::kDoAddress);

  rs485Serial.begin(cfg::kRs485Baud, SERIAL_8N1, cfg::kRs485RxPin, cfg::kRs485TxPin);
  clearRs485Input();

  Wire.begin(cfg::kOledSdaPin, cfg::kOledSclPin);
  Wire.setClock(400000);
  g_displayReady = initDisplay();
  if (g_displayReady)
  {
    Serial.printf("OLED detected at 0x%02X\n", g_oledAddress);
  }
  else
  {
    Serial.println("OLED not detected, continuing without display.");
  }

  setupMqttClient();
  startWifi();
  printHelp();
  drawOledStatus(true);
  pollAllSensors(false);
}

void loop()
{
  while (Serial.available() > 0)
  {
    handleConsoleCommand(static_cast<char>(Serial.read()));
  }

  maintainWifi();
  maintainMqtt();
  if (g_httpStarted)
  {
    server.handleClient();
  }

  maintainTimeSync();
  const bool collecting = shouldCollectData();

  const uint32_t nowMs = millis();
  if (collecting && (nowMs - g_gateway.lastAutoPollMs) >= cfg::kAutoPollIntervalMs)
  {
    pollAllSensors(false);
  }

  if (collecting && shouldPublishMqttLatest(nowMs))
  {
    publishMqttLatest();
  }

  publishControlStatus(false);

  drawOledStatus(false);
}


