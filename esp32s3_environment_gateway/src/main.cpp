// 文件说明：ESP32-S3 环境传感器网关主程序。
// 当前已实现：RS485 Modbus-RTU 风速风向、雨量、大气压力、温湿度光照采集，以及 MQTT 上报。

#include <Arduino.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "environment_config.h"

namespace modbus
{

constexpr size_t kMaxReplyDataBytes = 64;

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
      crc = (crc & 0x0001U) != 0U
                ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

} // namespace modbus

struct SensorRuntime
{
  bool online = false;
  uint8_t consecutiveFailures = 0;
  uint32_t lastSuccessMs = 0;
};

struct WindReading
{
  float speedMs = 0.0F;
  uint16_t level = 0;
  float speedKmh = 0.0F;
  float directionDeg = 0.0F;
  uint16_t directionCode = 0;
};

struct RainReading
{
  float todayMm = 0.0F;
  float instantMm = 0.0F;
  float yesterdayMm = 0.0F;
  float totalMm = 0.0F;
  float hourMm = 0.0F;
  float previousHourMm = 0.0F;
  float max24hMm = 0.0F;
  uint16_t max24hPeriod = 0;
  float min24hMm = 0.0F;
  uint16_t min24hPeriod = 0;
};

struct PressureReading
{
  float pressureHpa = 0.0F;
};

struct AirReading
{
  float temperatureC = 0.0F;
  float humidityPct = 0.0F;
  float illuminanceLux = 0.0F;
};

struct EnvironmentState
{
  SensorRuntime windRuntime;
  SensorRuntime rainRuntime;
  SensorRuntime pressureRuntime;
  SensorRuntime airRuntime;
  WindReading wind;
  RainReading rain;
  PressureReading pressure;
  AirReading air;
  uint32_t lastPollMs = 0;
  uint32_t pollCycles = 0;
  uint32_t published = 0;
  uint32_t publishFailures = 0;
};

HardwareSerial rs485Serial(1);
WiFiClient mqttTransport;
PubSubClient mqttClient(mqttTransport);
WebServer httpServer(envcfg::kHttpPort);

EnvironmentState g_state;
String g_deviceId;
String g_hostname;
String g_latestTopic;
String g_statusTopic;
bool g_wifiStarted = false;
bool g_wifiWasConnected = false;
bool g_mqttWasConnected = false;
bool g_httpStarted = false;
bool g_forcePublish = true;
uint32_t g_lastWifiAttemptMs = 0;
uint32_t g_lastMqttReconnectAttemptMs = 0;
uint32_t g_lastMqttPublishMs = 0;

void setupHttpServer();

// -------------------- 通用 JSON 辅助函数 --------------------

String escapeJson(const String &input)
{
  String output;
  output.reserve(input.length() + 8U);
  for (size_t i = 0; i < input.length(); ++i)
  {
    const char ch = input.charAt(i);
    switch (ch)
    {
    case '\"':
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
  json += '\"';
  json += key;
  json += "\":";
}

void appendStringField(String &json, bool &firstField, const char *key, const String &value)
{
  appendFieldPrefix(json, firstField, key);
  json += '\"';
  json += escapeJson(value);
  json += '\"';
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

void appendNullableFloatField(String &json, bool &firstField, const char *key, float value, bool valid, uint8_t digits)
{
  appendFieldPrefix(json, firstField, key);
  if (valid && std::isfinite(value))
  {
    json += String(value, static_cast<unsigned int>(digits));
  }
  else
  {
    // 传感器离线时用 null，避免把“没有采到”误认为真实的 0。
    json += "null";
  }
}

void appendNullableUIntField(String &json, bool &firstField, const char *key, uint16_t value, bool valid)
{
  appendFieldPrefix(json, firstField, key);
  json += valid ? String(value) : String("null");
}

// -------------------- RS485 Modbus-RTU --------------------

uint16_t readBeU16(const uint8_t *data)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
}

void setRs485Direction(bool transmit)
{
  if (envcfg::kRs485DirectionPin < 0)
  {
    return;
  }

  const bool level = transmit ? envcfg::kRs485TransmitLevel : !envcfg::kRs485TransmitLevel;
  digitalWrite(envcfg::kRs485DirectionPin, level ? HIGH : LOW);
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
  const uint32_t startedMs = millis();
  uint32_t lastByteMs = startedMs;
  size_t offset = 0;

  while (offset < length)
  {
    while (rs485Serial.available() > 0 && offset < length)
    {
      buffer[offset++] = static_cast<uint8_t>(rs485Serial.read());
      lastByteMs = millis();
    }

    const uint32_t nowMs = millis();
    if (offset == 0U)
    {
      if ((nowMs - startedMs) >= timeoutMs)
      {
        break;
      }
    }
    else if ((nowMs - lastByteMs) >= timeoutMs)
    {
      break;
    }
    delay(1);
  }

  return offset;
}

bool readModbusReply(uint8_t address, uint8_t function, modbus::Reply *reply)
{
  uint8_t header[3] = {0};
  if (readBytesWithTimeout(header, sizeof(header), envcfg::kModbusReplyTimeoutMs) != sizeof(header))
  {
    return false;
  }

  if (header[0] != address || header[1] != function)
  {
    Serial.printf("Modbus header mismatch: expected=%02X/%02X actual=%02X/%02X\n",
                  address, function, header[0], header[1]);
    return false;
  }

  const uint8_t byteCount = header[2];
  if (byteCount > sizeof(reply->data))
  {
    Serial.printf("Modbus response too long: %u bytes\n", byteCount);
    return false;
  }

  uint8_t tail[modbus::kMaxReplyDataBytes + 2U] = {0};
  const size_t tailLength = static_cast<size_t>(byteCount) + 2U;
  if (readBytesWithTimeout(tail, tailLength, envcfg::kModbusReplyTimeoutMs) != tailLength)
  {
    return false;
  }

  uint8_t frame[3U + modbus::kMaxReplyDataBytes] = {0};
  frame[0] = header[0];
  frame[1] = header[1];
  frame[2] = header[2];
  memcpy(frame + 3, tail, byteCount);

  const uint16_t expectedCrc = modbus::crc16(frame, 3U + byteCount);
  const uint16_t actualCrc = static_cast<uint16_t>(tail[byteCount]) |
                             static_cast<uint16_t>(static_cast<uint16_t>(tail[byteCount + 1U]) << 8U);
  if (expectedCrc != actualCrc)
  {
    Serial.printf("Modbus CRC mismatch: address=%02X function=%02X\n", address, function);
    return false;
  }

  reply->address = header[0];
  reply->function = header[1];
  reply->byteCount = byteCount;
  memcpy(reply->data, tail, byteCount);
  return true;
}

bool readHoldingRegisters(uint8_t address, uint16_t startRegister, uint16_t count, modbus::Reply *reply)
{
  if (count == 0U || count > 30U)
  {
    return false;
  }

  uint8_t frame[8] = {0};
  frame[0] = address;
  frame[1] = 0x03;
  frame[2] = static_cast<uint8_t>(startRegister >> 8U);
  frame[3] = static_cast<uint8_t>(startRegister & 0xFFU);
  frame[4] = static_cast<uint8_t>(count >> 8U);
  frame[5] = static_cast<uint8_t>(count & 0xFFU);

  const uint16_t crc = modbus::crc16(frame, 6);
  frame[6] = static_cast<uint8_t>(crc & 0xFFU);
  frame[7] = static_cast<uint8_t>(crc >> 8U);

  clearRs485Input();
  setRs485Direction(true);
  delayMicroseconds(100);
  rs485Serial.write(frame, sizeof(frame));
  rs485Serial.flush();
  setRs485Direction(false);
  // 手动收发模式必须先切回接收，再等待传感器响应，避免错过首字节。
  delay(envcfg::kModbusTurnaroundDelayMs);

  return readModbusReply(address, 0x03, reply);
}

bool readWindSensor(WindReading *reading)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(envcfg::kWindSensorAddress, 0x0000, 5, &reply) || reply.byteCount != 10U)
  {
    return false;
  }

  reading->speedMs = static_cast<float>(readBeU16(reply.data + 0)) / 10.0F;
  reading->level = readBeU16(reply.data + 2);
  reading->speedKmh = static_cast<float>(readBeU16(reply.data + 4)) / 100.0F;
  reading->directionDeg = static_cast<float>(readBeU16(reply.data + 6)) / 10.0F;
  reading->directionCode = readBeU16(reply.data + 8);

  return std::isfinite(reading->speedMs) && std::isfinite(reading->speedKmh) &&
         std::isfinite(reading->directionDeg) && reading->directionDeg <= 360.0F;
}

bool readRainSensor(RainReading *reading)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(envcfg::kRainSensorAddress, 0x0000, 10, &reply) || reply.byteCount != 20U)
  {
    return false;
  }

  // 雨量计寄存器 0-6 的单位是 0.1 mm，寄存器 7 和 9 是小时段编号。
  reading->todayMm = static_cast<float>(readBeU16(reply.data + 0)) / 10.0F;
  reading->instantMm = static_cast<float>(readBeU16(reply.data + 2)) / 10.0F;
  reading->yesterdayMm = static_cast<float>(readBeU16(reply.data + 4)) / 10.0F;
  reading->totalMm = static_cast<float>(readBeU16(reply.data + 6)) / 10.0F;
  reading->hourMm = static_cast<float>(readBeU16(reply.data + 8)) / 10.0F;
  reading->previousHourMm = static_cast<float>(readBeU16(reply.data + 10)) / 10.0F;
  reading->max24hMm = static_cast<float>(readBeU16(reply.data + 12)) / 10.0F;
  reading->max24hPeriod = readBeU16(reply.data + 14);
  reading->min24hMm = static_cast<float>(readBeU16(reply.data + 16)) / 10.0F;
  reading->min24hPeriod = readBeU16(reply.data + 18);

  return std::isfinite(reading->todayMm) && std::isfinite(reading->instantMm) &&
         std::isfinite(reading->totalMm);
}

bool readPressureSensor(PressureReading *reading)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(envcfg::kPressureSensorAddress, 0x0000, 1, &reply) || reply.byteCount != 2U)
  {
    return false;
  }

  // XDA-22 的压力寄存器单位为 0.1 hPa，例如 10056 表示 1005.6 hPa。
  reading->pressureHpa = static_cast<float>(readBeU16(reply.data)) / 10.0F;
  return std::isfinite(reading->pressureHpa) && reading->pressureHpa >= 300.0F &&
         reading->pressureHpa <= 1100.0F;
}

bool readAirSensor(AirReading *reading)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(envcfg::kAirSensorAddress, 0x0000, 3, &reply) || reply.byteCount != 6U)
  {
    return false;
  }

  // 温度为有符号 16 位数值，温湿度保留 1 位小数，光照原始值乘 10 得到 Lux。
  const int16_t temperatureRaw = static_cast<int16_t>(readBeU16(reply.data + 0));
  reading->temperatureC = static_cast<float>(temperatureRaw) / 10.0F;
  reading->humidityPct = static_cast<float>(readBeU16(reply.data + 2)) / 10.0F;
  reading->illuminanceLux = static_cast<float>(readBeU16(reply.data + 4)) * 10.0F;

  return std::isfinite(reading->temperatureC) && reading->temperatureC >= -40.0F &&
         reading->temperatureC <= 80.0F && std::isfinite(reading->humidityPct) &&
         reading->humidityPct >= 0.0F && reading->humidityPct <= 100.0F &&
         std::isfinite(reading->illuminanceLux) && reading->illuminanceLux <= 200000.0F;
}

void updateRuntime(SensorRuntime *runtime, bool success, uint32_t nowMs)
{
  if (success)
  {
    runtime->online = true;
    runtime->consecutiveFailures = 0;
    runtime->lastSuccessMs = nowMs;
    return;
  }

  if (runtime->consecutiveFailures < 255U)
  {
    ++runtime->consecutiveFailures;
  }

  // 连续三次失败后标记离线；短暂一次丢帧不会马上改变在线状态。
  if (runtime->lastSuccessMs == 0U || runtime->consecutiveFailures >= 3U ||
      (nowMs - runtime->lastSuccessMs) >= 20000U)
  {
    runtime->online = false;
  }
}

void pollSensors(bool verbose)
{
  const uint32_t startedMs = millis();
  WindReading wind;
  RainReading rain;
  PressureReading pressure;
  AirReading air;

  const bool windOk = readWindSensor(&wind);
  updateRuntime(&g_state.windRuntime, windOk, millis());
  if (windOk)
  {
    g_state.wind = wind;
  }

  delay(envcfg::kSensorGapMs);

  const bool rainOk = readRainSensor(&rain);
  updateRuntime(&g_state.rainRuntime, rainOk, millis());
  if (rainOk)
  {
    g_state.rain = rain;
  }

  delay(envcfg::kSensorGapMs);

  const bool pressureOk = readPressureSensor(&pressure);
  updateRuntime(&g_state.pressureRuntime, pressureOk, millis());
  if (pressureOk)
  {
    g_state.pressure = pressure;
  }

  delay(envcfg::kSensorGapMs);

  const bool airOk = readAirSensor(&air);
  updateRuntime(&g_state.airRuntime, airOk, millis());
  if (airOk)
  {
    g_state.air = air;
  }

  g_state.lastPollMs = millis();
  ++g_state.pollCycles;

  if (verbose || (!windOk && !rainOk && !pressureOk && !airOk))
  {
    Serial.printf("Poll #%lu: wind=%s rain=%s pressure=%s air=%s elapsed=%lums\n",
                  static_cast<unsigned long>(g_state.pollCycles),
                  windOk ? "ok" : "fail",
                  rainOk ? "ok" : "fail",
                  pressureOk ? "ok" : "fail",
                  airOk ? "ok" : "fail",
                  static_cast<unsigned long>(g_state.lastPollMs - startedMs));
  }

  if (windOk)
  {
    Serial.printf("  wind: %.2f m/s, level=%u, %.2f km/h, %.1f deg, code=%u\n",
                  g_state.wind.speedMs, g_state.wind.level, g_state.wind.speedKmh,
                  g_state.wind.directionDeg, g_state.wind.directionCode);
  }
  if (rainOk)
  {
    Serial.printf("  rain: today=%.1f mm, instant=%.1f mm, total=%.1f mm\n",
                  g_state.rain.todayMm, g_state.rain.instantMm, g_state.rain.totalMm);
  }
  if (pressureOk)
  {
    Serial.printf("  pressure: %.1f hPa\n", g_state.pressure.pressureHpa);
  }
  if (airOk)
  {
    Serial.printf("  air: temperature=%.1f C, humidity=%.1f %%RH, light=%.0f Lux\n",
                  g_state.air.temperatureC, g_state.air.humidityPct, g_state.air.illuminanceLux);
  }
}

// -------------------- 网络与 MQTT --------------------

bool wifiConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

String localIpString()
{
  return wifiConnected() ? WiFi.localIP().toString() : String("0.0.0.0");
}

String buildDeviceId()
{
  const uint64_t efuseMac = ESP.getEfuseMac();
  char id[20] = {0};
  snprintf(id, sizeof(id), "%04X%08X",
           static_cast<unsigned int>(efuseMac >> 32U),
           static_cast<unsigned int>(efuseMac & 0xFFFFFFFFULL));
  return String(id);
}

void startWifi()
{
  // 与原水质网关保持一致：未到重试时间时不重复调用 WiFi.begin。
  if (g_wifiStarted && (millis() - g_lastWifiAttemptMs) < envcfg::kWifiRetryIntervalMs)
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
  WiFi.setHostname(g_hostname.c_str());
  WiFi.begin(envcfg::kWifiSsid, envcfg::kWifiPassword);
  Serial.printf("WiFi connecting: ssid=%s hostname=%s\n", envcfg::kWifiSsid, g_hostname.c_str());
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
        Serial.printf("HTTP server started: http://%s/api/environment/latest\n", localIpString().c_str());
      }
    }
    return;
  }

  if (g_wifiWasConnected)
  {
    g_wifiWasConnected = false;
    g_mqttWasConnected = false;
    Serial.println("WiFi disconnected; MQTT will reconnect after WiFi.");
  }

  const uint32_t nowMs = millis();
  if (!g_wifiStarted || (nowMs - g_lastWifiAttemptMs) >= envcfg::kWifiRetryIntervalMs)
  {
    startWifi();
  }
}

String buildLatestJson();
String buildStatusJson(bool online);

void setupMqtt()
{
  mqttClient.setServer(envcfg::kMqttBrokerHost, envcfg::kMqttBrokerPort);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(envcfg::kMqttSocketTimeoutSeconds);
  mqttClient.setBufferSize(envcfg::kMqttBufferSize);
}

bool connectMqtt()
{
  if (!wifiConnected())
  {
    return false;
  }

  String clientId = String("weather-gateway-") + g_deviceId;
  // 使用 MQTT 遗嘱：设备意外断网时，服务器会把状态 Topic 更新为 offline。
  const String offlineStatus = buildStatusJson(false);
  const bool connected = mqttClient.connect(clientId.c_str(), envcfg::kMqttUsername, envcfg::kMqttPassword,
                                            g_statusTopic.c_str(), 0, true, offlineStatus.c_str());
  if (!connected)
  {
    Serial.printf("MQTT connect failed: state=%d broker=%s:%u\n",
                  mqttClient.state(), envcfg::kMqttBrokerHost, envcfg::kMqttBrokerPort);
    return false;
  }

  g_mqttWasConnected = true;
  g_forcePublish = true;
  Serial.printf("MQTT connected: topic=%s\n", g_latestTopic.c_str());
  const String status = buildStatusJson(true);
  mqttClient.publish(g_statusTopic.c_str(), status.c_str(), true);
  return true;
}

void maintainMqtt()
{
  if (!wifiConnected())
  {
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
  if ((nowMs - g_lastMqttReconnectAttemptMs) < envcfg::kMqttReconnectIntervalMs)
  {
    return;
  }
  g_lastMqttReconnectAttemptMs = nowMs;
  connectMqtt();
}

bool publishLatest()
{
  if (!mqttClient.connected())
  {
    return false;
  }

  const String payload = buildLatestJson();
  if ((payload.length() + g_latestTopic.length() + 16U) > envcfg::kMqttBufferSize)
  {
    ++g_state.publishFailures;
    Serial.printf("MQTT payload too large: %u bytes\n", static_cast<unsigned int>(payload.length()));
    return false;
  }

  if (!mqttClient.publish(g_latestTopic.c_str(), payload.c_str(), envcfg::kMqttRetainLatest))
  {
    ++g_state.publishFailures;
    Serial.printf("MQTT publish failed: state=%d\n", mqttClient.state());
    return false;
  }

  ++g_state.published;
  g_lastMqttPublishMs = millis();
  g_forcePublish = false;
  Serial.printf("MQTT published #%lu: %s\n", static_cast<unsigned long>(g_state.published), payload.c_str());
  return true;
}

// -------------------- JSON、HTTP 与串口调试 --------------------

String buildLatestJson()
{
  const bool windOnline = g_state.windRuntime.online;
  const bool rainOnline = g_state.rainRuntime.online;
  const bool pressureOnline = g_state.pressureRuntime.online;
  const bool airOnline = g_state.airRuntime.online;
  String json;
  json.reserve(1300);
  bool first = true;
  json = "{";
  appendStringField(json, first, "deviceId", g_deviceId);
  appendNullableFloatField(json, first, "windSpeedMs", g_state.wind.speedMs, windOnline, 2);
  appendNullableUIntField(json, first, "windLevel", g_state.wind.level, windOnline);
  appendNullableFloatField(json, first, "windSpeedKmh", g_state.wind.speedKmh, windOnline, 2);
  appendNullableFloatField(json, first, "windDirectionDeg", g_state.wind.directionDeg, windOnline, 1);
  appendNullableUIntField(json, first, "windDirectionCode", g_state.wind.directionCode, windOnline);
  appendNullableFloatField(json, first, "rainTodayMm", g_state.rain.todayMm, rainOnline, 1);
  appendNullableFloatField(json, first, "rainInstantMm", g_state.rain.instantMm, rainOnline, 1);
  appendNullableFloatField(json, first, "rainYesterdayMm", g_state.rain.yesterdayMm, rainOnline, 1);
  appendNullableFloatField(json, first, "rainTotalMm", g_state.rain.totalMm, rainOnline, 1);
  appendNullableFloatField(json, first, "rainHourMm", g_state.rain.hourMm, rainOnline, 1);
  appendNullableFloatField(json, first, "rainPreviousHourMm", g_state.rain.previousHourMm, rainOnline, 1);
  appendNullableFloatField(json, first, "rain24hMaxMm", g_state.rain.max24hMm, rainOnline, 1);
  appendNullableUIntField(json, first, "rain24hMaxPeriod", g_state.rain.max24hPeriod, rainOnline);
  appendNullableFloatField(json, first, "rain24hMinMm", g_state.rain.min24hMm, rainOnline, 1);
  appendNullableUIntField(json, first, "rain24hMinPeriod", g_state.rain.min24hPeriod, rainOnline);
  appendNullableFloatField(json, first, "pressureHpa", g_state.pressure.pressureHpa, pressureOnline, 1);
  appendNullableFloatField(json, first, "airTemperatureC", g_state.air.temperatureC, airOnline, 1);
  appendNullableFloatField(json, first, "relativeHumidityPct", g_state.air.humidityPct, airOnline, 1);
  appendNullableFloatField(json, first, "illuminanceLux", g_state.air.illuminanceLux, airOnline, 0);
  appendBoolField(json, first, "windOnline", windOnline);
  appendBoolField(json, first, "rainOnline", rainOnline);
  appendBoolField(json, first, "pressureOnline", pressureOnline);
  appendBoolField(json, first, "airOnline", airOnline);
  appendUIntField(json, first, "onlineSensors", static_cast<uint32_t>(windOnline) +
                                                    static_cast<uint32_t>(rainOnline) +
                                                    static_cast<uint32_t>(pressureOnline) +
                                                    static_cast<uint32_t>(airOnline));
  appendUIntField(json, first, "pollCycles", g_state.pollCycles);
  json += "}";
  return json;
}

String buildStatusJson(bool online)
{
  String json;
  json.reserve(650);
  bool first = true;
  json = "{";
  appendBoolField(json, first, "ok", true);
  appendBoolField(json, first, "online", online);
  appendStringField(json, first, "deviceId", g_deviceId);
  appendStringField(json, first, "ip", localIpString());
  appendBoolField(json, first, "wifiConnected", wifiConnected());
  appendBoolField(json, first, "mqttConnected", mqttClient.connected());
  appendStringField(json, first, "latestTopic", g_latestTopic);
  appendStringField(json, first, "statusTopic", g_statusTopic);
  appendUIntField(json, first, "onlineSensors",
                  static_cast<uint32_t>(g_state.windRuntime.online) +
                      static_cast<uint32_t>(g_state.rainRuntime.online) +
                      static_cast<uint32_t>(g_state.pressureRuntime.online) +
                      static_cast<uint32_t>(g_state.airRuntime.online));
  appendUIntField(json, first, "configuredSensors", 4);
  appendUIntField(json, first, "published", g_state.published);
  appendUIntField(json, first, "publishFailures", g_state.publishFailures);
  json += "}";
  return json;
}

void sendJsonResponse(int code, const String &payload)
{
  httpServer.sendHeader("Cache-Control", "no-store");
  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(code, "application/json; charset=utf-8", payload);
}

void handleRoot()
{
  const String payload = "{\"ok\":true,\"service\":\"esp32s3-environment-gateway\",\"latest\":\"/api/environment/latest\",\"health\":\"/api/environment/health\"}";
  sendJsonResponse(200, payload);
}

void handleLatest()
{
  sendJsonResponse(200, buildLatestJson());
}

void handleHealth()
{
  sendJsonResponse(200, buildStatusJson(wifiConnected() && mqttClient.connected()));
}

void handleNotFound()
{
  sendJsonResponse(404, "{\"ok\":false,\"message\":\"not found\"}");
}

void setupHttpServer()
{
  httpServer.on("/", HTTP_GET, handleRoot);
  httpServer.on("/api/environment/latest", HTTP_GET, handleLatest);
  httpServer.on("/api/environment/health", HTTP_GET, handleHealth);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
}

void printInfo()
{
  Serial.printf("deviceId: %s\n", g_deviceId.c_str());
  Serial.printf("WiFi: %s, IP=%s, RSSI=%d\n", wifiConnected() ? "connected" : "offline", localIpString().c_str(), wifiConnected() ? WiFi.RSSI() : 0);
  Serial.printf("MQTT: %s:%u, connected=%s\n", envcfg::kMqttBrokerHost, envcfg::kMqttBrokerPort, mqttClient.connected() ? "yes" : "no");
  Serial.printf("latest topic: %s\n", g_latestTopic.c_str());
  Serial.printf("status topic: %s\n", g_statusTopic.c_str());
  Serial.printf("HTTP latest: http://%s/api/environment/latest\n", localIpString().c_str());
  Serial.printf("RS485: RX=%d TX=%d baud=%lu wind=0x%02X rain=0x%02X pressure=0x%02X air=0x%02X\n",
                envcfg::kRs485RxPin, envcfg::kRs485TxPin,
                static_cast<unsigned long>(envcfg::kRs485Baud),
                envcfg::kWindSensorAddress, envcfg::kRainSensorAddress,
                envcfg::kPressureSensorAddress, envcfg::kAirSensorAddress);
}

void printHelp()
{
  Serial.println();
  Serial.println("=== ESP32-S3 environment gateway ===");
  Serial.println("p : poll all four environment sensors now");
  Serial.println("j : print current JSON");
  Serial.println("i : print WiFi, MQTT, topic and RS485 information");
  Serial.println("m : publish current JSON immediately");
  Serial.println("h : show this help");
  Serial.println();
}

void handleConsoleCommand(char command)
{
  switch (command)
  {
  case 'p':
  case 'P':
    pollSensors(true);
    break;
  case 'j':
  case 'J':
    Serial.println(buildLatestJson());
    break;
  case 'i':
  case 'I':
    printInfo();
    break;
  case 'm':
  case 'M':
    Serial.println(publishLatest() ? "publish success" : "publish failed or MQTT offline");
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
    Serial.printf("Unknown command: %c\n", command);
    printHelp();
    break;
  }
}

void setup()
{
  Serial.begin(envcfg::kDebugBaud);
  delay(300);

  g_deviceId = buildDeviceId();
  g_hostname = String("environment-gw-") + g_deviceId;
  g_latestTopic = String(envcfg::kMqttTopicPrefix) + "/" + g_deviceId + envcfg::kMqttLatestSuffix;
  g_statusTopic = String(envcfg::kMqttTopicPrefix) + "/" + g_deviceId + envcfg::kMqttStatusSuffix;

  Serial.println();
  Serial.printf("ESP32-S3 environment gateway booting: %s\n", g_deviceId.c_str());
  printInfo();

  if (envcfg::kRs485DirectionPin >= 0)
  {
    pinMode(envcfg::kRs485DirectionPin, OUTPUT);
    setRs485Direction(false);
  }
  rs485Serial.begin(envcfg::kRs485Baud, SERIAL_8N1, envcfg::kRs485RxPin, envcfg::kRs485TxPin);
  clearRs485Input();

  setupMqtt();
  startWifi();
  printHelp();

  // 上电先读一次，便于现场用串口马上判断两个传感器是否在线。
  pollSensors(true);
}

void loop()
{
  while (Serial.available() > 0)
  {
    handleConsoleCommand(static_cast<char>(Serial.read()));
  }

  maintainWifi();
  maintainMqtt();
  httpServer.handleClient();

  const uint32_t nowMs = millis();
  if ((nowMs - g_state.lastPollMs) >= envcfg::kSensorPollIntervalMs)
  {
    pollSensors(false);
  }

  if (mqttClient.connected() &&
      (g_forcePublish || (nowMs - g_lastMqttPublishMs) >= envcfg::kMqttPublishIntervalMs))
  {
    publishLatest();
  }

  delay(2);
}
