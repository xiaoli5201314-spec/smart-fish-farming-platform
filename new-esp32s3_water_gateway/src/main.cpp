#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <cmath>
#include <cstring>

namespace cfg
{

  constexpr uint32_t kUsbDebugBaud = 115200;
  constexpr uint32_t kRs485Baud = 9600;

  constexpr uint8_t kRs485RxPin = 18;
  constexpr uint8_t kRs485TxPin = 17;

  constexpr uint32_t kModbusReplyTimeoutMs = 450;
  constexpr uint32_t kRequestGapMs = 60;
  constexpr uint32_t kAutoPollIntervalMs = 2000;
  constexpr uint32_t kWifiRetryIntervalMs = 15000;
  constexpr uint32_t kDisplayRefreshMs = 600;
  constexpr uint8_t kSensorOfflineFailureThreshold = 3;
  constexpr uint32_t kSensorOfflineGraceMs = 20000;

  constexpr int kScreenWidth = 128;
  constexpr int kScreenHeight = 64;
  constexpr int kOledSdaPin = 8;
  constexpr int kOledSclPin = 9;

  constexpr uint16_t kHttpPort = 80;
  constexpr char kWifiSsid[] = "";
  constexpr char kWifiPassword[] = "";
  constexpr char kHostname[] = "fish-water-gateway";

  constexpr char kMqttBrokerHost[] = "";
  constexpr uint16_t kMqttBrokerPort = 1883;
  constexpr char kMqttClientId[] = "fish-water-gateway";
  constexpr char kMqttUsername[] = "";
  constexpr char kMqttPassword[] = "";
  constexpr char kMqttTopicLatest[] = "fish/water/latest";
  constexpr char kMqttTopicStatus[] = "fish/water/status";
  constexpr uint32_t kMqttReconnectIntervalMs = 10000;
  constexpr uint16_t kMqttSocketTimeoutSeconds = 2;
  constexpr uint16_t kMqttBufferSize = 4096;
  constexpr bool kMqttRetainLatest = true;

  constexpr uint8_t kCodAddress = 1;
  constexpr uint8_t kPhEcAddress = 2;
  constexpr uint8_t kAmmoniaAddress = 3;
  constexpr uint8_t kNitriteAddress = 4;
  constexpr uint8_t kNitrateAddress = 5;

  enum class SensorSlot : uint8_t
  {
    None = 0,
    Cod,
    PhEc,
    Ammonia,
    Nitrite,
    Nitrate
  };

  constexpr SensorSlot kDefaultSensor = SensorSlot::None;

} // namespace cfg

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
        if (crc & 0x0001U)
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

namespace sensor
{

  struct PhEcReading
  {
    float ph = 0.0F;
    uint16_t ecUsCm = 0;
    float temperatureC = 0.0F;
    uint16_t tdsPpm = 0;
    float salinityPpt = 0.0F;
  };

  struct CodReading
  {
    float temperatureC = 0.0F;
    float codMgL = 0.0F;
  };

  struct IonReading
  {
    float concentrationPpm = 0.0F;
    float electrodeMv = 0.0F;
    float temperatureC = 0.0F;
    bool concentrationOverflow = false;
  };

  struct IonParams
  {
    uint16_t calibrationMask = 0;
    float electrodeEfficiencyPercent = 0.0F;
    uint16_t calibratedPoints = 0;
    uint8_t deviceAddress = 0;
    uint32_t baudRate = 0;
    bool autoTemperatureCompensation = false;
    uint8_t ionValence = 0;
  };

} // namespace sensor

struct SensorRuntime
{
  bool online = false;
  uint32_t lastAttemptMs = 0;
  uint32_t lastSuccessMs = 0;
  uint8_t consecutiveFailures = 0;
};

struct GatewayState
{
  SensorRuntime codRuntime;
  SensorRuntime phEcRuntime;
  SensorRuntime ammoniaRuntime;
  SensorRuntime nitriteRuntime;
  SensorRuntime nitrateRuntime;

  sensor::CodReading codReading;
  sensor::PhEcReading phEcReading;
  sensor::IonReading ammoniaReading;
  sensor::IonReading nitriteReading;
  sensor::IonReading nitrateReading;

  uint32_t lastRefreshMs = 0;
  uint32_t lastAutoPollMs = 0;
  uint32_t totalPollCycles = 0;
  uint8_t onlineSensors = 0;
};

HardwareSerial rs485Serial(1);
WebServer server(cfg::kHttpPort);
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
Adafruit_SSD1306 display(cfg::kScreenWidth, cfg::kScreenHeight, &Wire, -1);

GatewayState g_gateway;
cfg::SensorSlot g_activeSensor = cfg::kDefaultSensor;

bool g_wifiStarted = false;
bool g_wifiWasConnected = false;
bool g_httpStarted = false;
bool g_displayReady = false;
bool g_mqttWasConnected = false;
uint32_t g_lastWifiAttemptMs = 0;
uint32_t g_lastMqttReconnectAttemptMs = 0;
uint32_t g_lastMqttPublishMs = 0;
uint32_t g_mqttPublishCount = 0;
uint32_t g_mqttPublishFailureCount = 0;
uint8_t g_lastAnnouncedOnlineSensors = 255;
uint8_t g_oledAddress = 0;
uint32_t g_lastDisplayRefreshMs = 0;

const cfg::SensorSlot kSensorOrder[] = {
    cfg::SensorSlot::Cod,
    cfg::SensorSlot::PhEc,
    cfg::SensorSlot::Ammonia,
    cfg::SensorSlot::Nitrite,
    cfg::SensorSlot::Nitrate};

void setupHttpServer();
void drawOledStatus(bool force);
void setupMqttClient();
void maintainMqtt();
bool publishMqttLatest();
void printMqttStatus();

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

const char *sensorName(cfg::SensorSlot slot)
{
  switch (slot)
  {
  case cfg::SensorSlot::Cod:
    return "COD sensor";
  case cfg::SensorSlot::PhEc:
    return "PH-EC combo";
  case cfg::SensorSlot::Ammonia:
    return "ammonia ion";
  case cfg::SensorSlot::Nitrite:
    return "nitrite ion";
  case cfg::SensorSlot::Nitrate:
    return "nitrate ion";
  case cfg::SensorSlot::None:
  default:
    return "not selected";
  }
}

const char *sensorShortName(cfg::SensorSlot slot)
{
  switch (slot)
  {
  case cfg::SensorSlot::Cod:
    return "COD";
  case cfg::SensorSlot::PhEc:
    return "PH-EC";
  case cfg::SensorSlot::Ammonia:
    return "ammonia";
  case cfg::SensorSlot::Nitrite:
    return "nitrite";
  case cfg::SensorSlot::Nitrate:
    return "nitrate";
  case cfg::SensorSlot::None:
  default:
    return "unknown";
  }
}

uint8_t sensorAddress(cfg::SensorSlot slot)
{
  switch (slot)
  {
  case cfg::SensorSlot::Cod:
    return cfg::kCodAddress;
  case cfg::SensorSlot::PhEc:
    return cfg::kPhEcAddress;
  case cfg::SensorSlot::Ammonia:
    return cfg::kAmmoniaAddress;
  case cfg::SensorSlot::Nitrite:
    return cfg::kNitriteAddress;
  case cfg::SensorSlot::Nitrate:
    return cfg::kNitrateAddress;
  case cfg::SensorSlot::None:
  default:
    return 0;
  }
}

SensorRuntime *runtimeFor(cfg::SensorSlot slot)
{
  switch (slot)
  {
  case cfg::SensorSlot::Cod:
    return &g_gateway.codRuntime;
  case cfg::SensorSlot::PhEc:
    return &g_gateway.phEcRuntime;
  case cfg::SensorSlot::Ammonia:
    return &g_gateway.ammoniaRuntime;
  case cfg::SensorSlot::Nitrite:
    return &g_gateway.nitriteRuntime;
  case cfg::SensorSlot::Nitrate:
    return &g_gateway.nitrateRuntime;
  case cfg::SensorSlot::None:
  default:
    return nullptr;
  }
}

uint16_t readBeU16(const uint8_t *data)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
}

float decodeWordSwappedFloat(const uint8_t *data)
{
  const uint32_t raw =
      (static_cast<uint32_t>(data[2]) << 24U) |
      (static_cast<uint32_t>(data[3]) << 16U) |
      (static_cast<uint32_t>(data[0]) << 8U) |
      static_cast<uint32_t>(data[1]);
  float value = 0.0F;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

float decodeReversedFloat(const uint8_t *data)
{
  const uint32_t raw =
      (static_cast<uint32_t>(data[3]) << 24U) |
      (static_cast<uint32_t>(data[2]) << 16U) |
      (static_cast<uint32_t>(data[1]) << 8U) |
      static_cast<uint32_t>(data[0]);
  float value = 0.0F;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

uint32_t baudRateFromCode(uint16_t code)
{
  switch (code)
  {
  case 0:
    return 1200;
  case 1:
    return 2400;
  case 2:
    return 4800;
  case 3:
    return 9600;
  case 4:
    return 19200;
  default:
    return 0;
  }
}

void formatClockFromMillis(uint32_t sourceMs, char *buffer, size_t bufferSize)
{
  const uint32_t totalSeconds = sourceMs / 1000U;
  const uint32_t hours = (totalSeconds / 3600U) % 24U;
  const uint32_t minutes = (totalSeconds / 60U) % 60U;
  const uint32_t seconds = totalSeconds % 60U;
  snprintf(buffer, bufferSize, "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
}

String updateTimeString()
{
  char buffer[16] = {0};
  formatClockFromMillis(g_gateway.lastRefreshMs == 0 ? millis() : g_gateway.lastRefreshMs, buffer, sizeof(buffer));
  return String(buffer);
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

    const uint32_t now = millis();
    if (offset == 0)
    {
      if (now - startedAt >= timeoutMs)
      {
        break;
      }
    }
    else if (now - lastByteAt >= timeoutMs)
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

  const uint8_t actualFunction = header[1];
  if (header[0] != address)
  {
    return false;
  }

  if (actualFunction == static_cast<uint8_t>(function | 0x80U))
  {
    uint8_t errorFrame[2] = {0};
    if (readBytesWithTimeout(errorFrame, sizeof(errorFrame), cfg::kModbusReplyTimeoutMs) != sizeof(errorFrame))
    {
      return false;
    }

    Serial.printf("Modbus exception: address=%u function=0x%02X code=%u (%s)\n",
                  address,
                  actualFunction,
                  header[2],
                  modbusErrorText(header[2]));
    return false;
  }

  if (actualFunction != function)
  {
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

  uint8_t frame[3 + modbus::kMaxReplyDataBytes + 2] = {0};
  frame[0] = header[0];
  frame[1] = header[1];
  frame[2] = header[2];
  std::memcpy(frame + 3, tail, tailLength);

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

bool readPhEcSensor(sensor::PhEcReading *reading)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(cfg::kPhEcAddress, 0x0000, 0x0005, &reply))
  {
    return false;
  }

  if (reply.byteCount != 10)
  {
    return false;
  }

  reading->ph = static_cast<float>(readBeU16(reply.data)) / 100.0F;
  reading->ecUsCm = readBeU16(reply.data + 2);
  reading->temperatureC = static_cast<float>(readBeU16(reply.data + 4)) / 10.0F;
  reading->tdsPpm = readBeU16(reply.data + 6);
  reading->salinityPpt = static_cast<float>(readBeU16(reply.data + 8)) / 100.0F;
  return true;
}

bool readCodSensor(sensor::CodReading *reading)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(cfg::kCodAddress, 0x2600, 0x0004, &reply))
  {
    return false;
  }

  if (reply.byteCount != 8)
  {
    return false;
  }

  reading->temperatureC = decodeReversedFloat(reply.data);
  reading->codMgL = decodeReversedFloat(reply.data + 4);
  return true;
}

bool readIonMeasurement(uint8_t address, sensor::IonReading *reading)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(address, 0x0000, 0x000A, &reply))
  {
    return false;
  }

  if (reply.byteCount != 20)
  {
    return false;
  }

  reading->concentrationPpm = decodeWordSwappedFloat(reply.data);
  reading->electrodeMv = decodeWordSwappedFloat(reply.data + 4);
  reading->temperatureC = decodeWordSwappedFloat(reply.data + 16);
  reading->concentrationOverflow = (reading->concentrationPpm >= 20000.5F);
  return true;
}

bool readIonParams(uint8_t address, sensor::IonParams *params)
{
  modbus::Reply reply;
  if (!readHoldingRegisters(address, 0x0019, 0x000C, &reply))
  {
    return false;
  }

  if (reply.byteCount != 24)
  {
    return false;
  }

  params->calibrationMask = readBeU16(reply.data);
  params->electrodeEfficiencyPercent = static_cast<float>(readBeU16(reply.data + 6)) / 10.0F;
  params->calibratedPoints = readBeU16(reply.data + 8);
  params->deviceAddress = static_cast<uint8_t>(readBeU16(reply.data + 10));
  params->baudRate = baudRateFromCode(readBeU16(reply.data + 12));
  params->autoTemperatureCompensation = (readBeU16(reply.data + 14) == 1U);
  params->ionValence = static_cast<uint8_t>(readBeU16(reply.data + 20));
  return true;
}

bool readSelectedIonParams(cfg::SensorSlot slot, sensor::IonParams *params)
{
  switch (slot)
  {
  case cfg::SensorSlot::Ammonia:
    return readIonParams(cfg::kAmmoniaAddress, params);
  case cfg::SensorSlot::Nitrite:
    return readIonParams(cfg::kNitriteAddress, params);
  case cfg::SensorSlot::Nitrate:
    return readIonParams(cfg::kNitrateAddress, params);
  case cfg::SensorSlot::Cod:
  case cfg::SensorSlot::PhEc:
  case cfg::SensorSlot::None:
  default:
    return false;
  }
}

void printIonParams(const sensor::IonParams &params)
{
  Serial.printf("Params: address=%u baud=%lu atc=%s ionValence=%u points=%u efficiency=%.1f%% mask=0x%04X\n",
                params.deviceAddress,
                static_cast<unsigned long>(params.baudRate),
                params.autoTemperatureCompensation ? "auto" : "manual",
                params.ionValence,
                params.calibratedPoints,
                params.electrodeEfficiencyPercent,
                params.calibrationMask);
}

void printPhEcReading(const sensor::PhEcReading &reading)
{
  Serial.printf("[PH-EC @ %u] pH=%.2f EC=%u uS/cm temp=%.1f C TDS=%u ppm salinity=%.2f PPT\n",
                cfg::kPhEcAddress,
                reading.ph,
                reading.ecUsCm,
                reading.temperatureC,
                reading.tdsPpm,
                reading.salinityPpt);
}

void printCodReading(const sensor::CodReading &reading)
{
  Serial.printf("[COD @ %u] COD=%.2f mg/L temp=%.2f C\n",
                cfg::kCodAddress,
                reading.codMgL,
                reading.temperatureC);
}

void printIonReading(const char *label, uint8_t address, const sensor::IonReading &reading)
{
  Serial.printf("[%s @ %u] concentration=%.3f ppm electrode=%.2f mV temp=%.2f C",
                label,
                address,
                reading.concentrationPpm,
                reading.electrodeMv,
                reading.temperatureC);
  if (reading.concentrationOverflow)
  {
    Serial.print(" (displayed concentration is at upper limit)");
  }
  Serial.println();
}

void printReadFailed(cfg::SensorSlot slot)
{
  Serial.printf("[%s @ %u] read failed, check wiring, power, address and TTL485 level.\n",
                sensorName(slot),
                sensorAddress(slot));
}

void updateRuntimeState(SensorRuntime *runtime, bool success, uint32_t nowMs)
{
  if (runtime == nullptr)
  {
    return;
  }

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

  const bool exceededFailureThreshold =
      runtime->consecutiveFailures >= cfg::kSensorOfflineFailureThreshold;
  const bool exceededOfflineGrace =
      (nowMs - runtime->lastSuccessMs) >= cfg::kSensorOfflineGraceMs;

  if (exceededFailureThreshold || exceededOfflineGrace)
  {
    runtime->online = false;
  }
}

uint8_t countOnlineSensors()
{
  uint8_t count = 0;
  count += g_gateway.codRuntime.online ? 1 : 0;
  count += g_gateway.phEcRuntime.online ? 1 : 0;
  count += g_gateway.ammoniaRuntime.online ? 1 : 0;
  count += g_gateway.nitriteRuntime.online ? 1 : 0;
  count += g_gateway.nitrateRuntime.online ? 1 : 0;
  return count;
}

void refreshGatewayMeta()
{
  g_gateway.onlineSensors = countOnlineSensors();
  g_gateway.lastRefreshMs = millis();
}

bool refreshSensor(cfg::SensorSlot slot, bool verbose)
{
  const uint32_t nowMs = millis();
  SensorRuntime *runtime = runtimeFor(slot);
  bool success = false;

  switch (slot)
  {
  case cfg::SensorSlot::Cod:
  {
    sensor::CodReading reading;
    success = readCodSensor(&reading);
    updateRuntimeState(runtime, success, nowMs);
    if (success)
    {
      g_gateway.codReading = reading;
      if (verbose)
      {
        printCodReading(reading);
      }
    }
    break;
  }
  case cfg::SensorSlot::PhEc:
  {
    sensor::PhEcReading reading;
    success = readPhEcSensor(&reading);
    updateRuntimeState(runtime, success, nowMs);
    if (success)
    {
      g_gateway.phEcReading = reading;
      if (verbose)
      {
        printPhEcReading(reading);
      }
    }
    break;
  }
  case cfg::SensorSlot::Ammonia:
  {
    sensor::IonReading reading;
    success = readIonMeasurement(cfg::kAmmoniaAddress, &reading);
    updateRuntimeState(runtime, success, nowMs);
    if (success)
    {
      g_gateway.ammoniaReading = reading;
      if (verbose)
      {
        printIonReading("ammonia", cfg::kAmmoniaAddress, reading);
      }
    }
    break;
  }
  case cfg::SensorSlot::Nitrite:
  {
    sensor::IonReading reading;
    success = readIonMeasurement(cfg::kNitriteAddress, &reading);
    updateRuntimeState(runtime, success, nowMs);
    if (success)
    {
      g_gateway.nitriteReading = reading;
      if (verbose)
      {
        printIonReading("nitrite", cfg::kNitriteAddress, reading);
      }
    }
    break;
  }
  case cfg::SensorSlot::Nitrate:
  {
    sensor::IonReading reading;
    success = readIonMeasurement(cfg::kNitrateAddress, &reading);
    updateRuntimeState(runtime, success, nowMs);
    if (success)
    {
      g_gateway.nitrateReading = reading;
      if (verbose)
      {
        printIonReading("nitrate", cfg::kNitrateAddress, reading);
      }
    }
    break;
  }
  case cfg::SensorSlot::None:
  default:
    return false;
  }

  refreshGatewayMeta();
  if (!success && verbose)
  {
    printReadFailed(slot);
  }
  return success;
}

void printPollSummary()
{
  Serial.printf("Poll summary: %u/5 sensors online, last update %s\n",
                g_gateway.onlineSensors,
                updateTimeString().c_str());
}

void pollAllSensors(bool verbose)
{
  for (size_t index = 0; index < (sizeof(kSensorOrder) / sizeof(kSensorOrder[0])); ++index)
  {
    refreshSensor(kSensorOrder[index], verbose);
    if (g_httpStarted)
    {
      server.handleClient();
    }
    if (mqttClient.connected())
    {
      mqttClient.loop();
    }
    delay(cfg::kRequestGapMs);
  }

  g_gateway.lastAutoPollMs = millis();
  g_gateway.totalPollCycles++;
  if (verbose)
  {
    g_lastAnnouncedOnlineSensors = g_gateway.onlineSensors;
    printPollSummary();
  }

  drawOledStatus(false);
  publishMqttLatest();
}

bool wifiConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

String localIpString()
{
  if (!wifiConnected())
  {
    return String("0.0.0.0");
  }
  return WiFi.localIP().toString();
}

String apiUrlString()
{
  return String("http://") + localIpString() + "/api/water/latest";
}

String mqttBrokerString()
{
  return String(cfg::kMqttBrokerHost) + ":" + String(cfg::kMqttBrokerPort);
}

float resolvedPhEcTdsPpm(const sensor::PhEcReading &reading)
{
  if (reading.tdsPpm > 0)
  {
    return static_cast<float>(reading.tdsPpm);
  }
  return static_cast<float>(reading.ecUsCm) * 0.5F;
}

float resolvedPhEcSalinityPpt(const sensor::PhEcReading &reading)
{
  if (reading.salinityPpt > 0.0F)
  {
    return reading.salinityPpt;
  }
  return resolvedPhEcTdsPpm(reading) / 1000.0F;
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
  display.println(F("Water Gateway"));

  display.setCursor(0, 14);
  display.print(F("WiFi: "));
  display.println(wifiConnected() ? F("OK") : F("WAIT"));

  display.setCursor(0, 28);
  display.println(F("IP:"));

  display.setCursor(0, 40);
  if (wifiConnected())
  {
    display.println(localIpString());
  }
  else
  {
    display.println(F("0.0.0.0"));
  }

  display.setCursor(0, 56);
  display.printf("Sensors: %u/5", g_gateway.onlineSensors);
  display.display();
}

void printWifiStatus()
{
  Serial.printf("IP: %s\n", localIpString().c_str());
  Serial.printf("HTTP latest: %s\n", apiUrlString().c_str());
  printMqttStatus();
}

void startWifi()
{
  if (std::strlen(cfg::kWifiSsid) == 0)
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
}

void maintainWifi()
{
  const bool connected = wifiConnected();
  if (connected && !g_wifiWasConnected)
  {
    g_wifiWasConnected = true;
    if (!g_httpStarted)
    {
      setupHttpServer();
      g_httpStarted = true;
    }
    Serial.printf("WiFi IP: %s\n", localIpString().c_str());
    drawOledStatus(true);
    return;
  }

  if (!connected && g_wifiWasConnected)
  {
    g_wifiWasConnected = false;
    drawOledStatus(true);
  }

  if (!connected)
  {
    const uint32_t nowMs = millis();
    if (!g_wifiStarted || (nowMs - g_lastWifiAttemptMs) >= cfg::kWifiRetryIntervalMs)
    {
      startWifi();
    }
  }
}

String escapeJson(const String &input)
{
  String output;
  output.reserve(input.length() + 8);

  for (size_t index = 0; index < input.length(); ++index)
  {
    const char ch = input[index];
    switch (ch)
    {
    case '\\':
    case '"':
      output += '\\';
      output += ch;
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

void appendIntField(String &json, bool &firstField, const char *key, int value)
{
  appendFieldPrefix(json, firstField, key);
  json += String(value);
}

void appendUIntField(String &json, bool &firstField, const char *key, uint32_t value)
{
  appendFieldPrefix(json, firstField, key);
  json += String(value);
}

void appendFloatField(String &json, bool &firstField, const char *key, float value, uint8_t digits)
{
  appendFieldPrefix(json, firstField, key);
  json += String(value, static_cast<unsigned int>(digits));
}

void appendOptionalFloatField(String &json, bool &firstField, const char *key, bool enabled, float value, uint8_t digits)
{
  if (!enabled || !std::isfinite(value))
  {
    return;
  }

  appendFloatField(json, firstField, key, value, digits);
}

void appendOptionalUIntField(String &json, bool &firstField, const char *key, bool enabled, uint32_t value)
{
  if (!enabled)
  {
    return;
  }

  appendUIntField(json, firstField, key, value);
}

void appendMetricsJson(String &json)
{
  bool firstField = true;
  json += '{';

  bool hasWaterTemp = false;
  float waterTemp = 0.0F;
  if (g_gateway.phEcRuntime.online)
  {
    hasWaterTemp = true;
    waterTemp = g_gateway.phEcReading.temperatureC;
  }
  else if (g_gateway.codRuntime.online)
  {
    hasWaterTemp = true;
    waterTemp = g_gateway.codReading.temperatureC;
  }
  else if (g_gateway.ammoniaRuntime.online)
  {
    hasWaterTemp = true;
    waterTemp = g_gateway.ammoniaReading.temperatureC;
  }
  else if (g_gateway.nitriteRuntime.online)
  {
    hasWaterTemp = true;
    waterTemp = g_gateway.nitriteReading.temperatureC;
  }
  else if (g_gateway.nitrateRuntime.online)
  {
    hasWaterTemp = true;
    waterTemp = g_gateway.nitrateReading.temperatureC;
  }

  appendOptionalFloatField(json, firstField, "waterTemp", hasWaterTemp, waterTemp, 1);
  appendOptionalFloatField(json, firstField, "ph", g_gateway.phEcRuntime.online, g_gateway.phEcReading.ph, 2);
  appendOptionalUIntField(json, firstField, "conductivity", g_gateway.phEcRuntime.online, g_gateway.phEcReading.ecUsCm);

  if (g_gateway.phEcRuntime.online)
  {
    appendFloatField(json, firstField, "tds", resolvedPhEcTdsPpm(g_gateway.phEcReading), 0);
    appendFloatField(json, firstField, "salinity", resolvedPhEcSalinityPpt(g_gateway.phEcReading), 2);
  }

  appendOptionalFloatField(json, firstField, "cod", g_gateway.codRuntime.online, g_gateway.codReading.codMgL, 2);
  appendOptionalFloatField(json, firstField, "ammonia", g_gateway.ammoniaRuntime.online, g_gateway.ammoniaReading.concentrationPpm, 3);
  appendOptionalFloatField(json, firstField, "nitrite", g_gateway.nitriteRuntime.online, g_gateway.nitriteReading.concentrationPpm, 3);
  appendOptionalFloatField(json, firstField, "nitrate", g_gateway.nitrateRuntime.online, g_gateway.nitrateReading.concentrationPpm, 3);

  json += '}';
}

void appendCodSensorJson(String &json)
{
  bool firstField = true;
  json += '{';
  appendStringField(json, firstField, "address", "01");
  appendStringField(json, firstField, "name", "COD sensor");
  appendBoolField(json, firstField, "online", g_gateway.codRuntime.online);
  if (g_gateway.codRuntime.online)
  {
    appendFloatField(json, firstField, "value", g_gateway.codReading.codMgL, 2);
    appendFloatField(json, firstField, "codMgL", g_gateway.codReading.codMgL, 2);
    appendFloatField(json, firstField, "temperatureC", g_gateway.codReading.temperatureC, 2);
  }
  json += '}';
}

void appendPhEcSensorJson(String &json)
{
  bool firstField = true;
  json += '{';
  appendStringField(json, firstField, "address", "02");
  appendStringField(json, firstField, "name", "PH-EC combo");
  appendBoolField(json, firstField, "online", g_gateway.phEcRuntime.online);
  if (g_gateway.phEcRuntime.online)
  {
    appendFloatField(json, firstField, "value", g_gateway.phEcReading.ph, 2);
    appendFloatField(json, firstField, "ph", g_gateway.phEcReading.ph, 2);
    appendUIntField(json, firstField, "conductivity", g_gateway.phEcReading.ecUsCm);
    appendUIntField(json, firstField, "ecUsCm", g_gateway.phEcReading.ecUsCm);
    appendFloatField(json, firstField, "temperatureC", g_gateway.phEcReading.temperatureC, 1);
    appendUIntField(json, firstField, "tdsPpm", g_gateway.phEcReading.tdsPpm);
    appendFloatField(json, firstField, "tds", resolvedPhEcTdsPpm(g_gateway.phEcReading), 0);
    appendFloatField(json, firstField, "salinityPpt", resolvedPhEcSalinityPpt(g_gateway.phEcReading), 2);
    appendFloatField(json, firstField, "salinity", resolvedPhEcSalinityPpt(g_gateway.phEcReading), 2);
  }
  json += '}';
}

void appendIonSensorJson(String &json, const char *address, const char *name, bool online, const sensor::IonReading &reading)
{
  bool firstField = true;
  json += '{';
  appendStringField(json, firstField, "address", address);
  appendStringField(json, firstField, "name", name);
  appendBoolField(json, firstField, "online", online);
  if (online)
  {
    appendFloatField(json, firstField, "value", reading.concentrationPpm, 3);
    appendFloatField(json, firstField, "concentrationPpm", reading.concentrationPpm, 3);
    appendFloatField(json, firstField, "electrodeMv", reading.electrodeMv, 2);
    appendFloatField(json, firstField, "temperatureC", reading.temperatureC, 2);
    appendBoolField(json, firstField, "overflow", reading.concentrationOverflow);
  }
  json += '}';
}

void appendSensorsJson(String &json)
{
  json += '[';
  appendCodSensorJson(json);
  json += ',';
  appendPhEcSensorJson(json);
  json += ',';
  appendIonSensorJson(json, "03", "ammonia ion", g_gateway.ammoniaRuntime.online, g_gateway.ammoniaReading);
  json += ',';
  appendIonSensorJson(json, "04", "nitrite ion", g_gateway.nitriteRuntime.online, g_gateway.nitriteReading);
  json += ',';
  appendIonSensorJson(json, "05", "nitrate ion", g_gateway.nitrateRuntime.online, g_gateway.nitrateReading);
  json += ']';
}

String buildGatewayJson()
{
  String json;
  json.reserve(2200);

  const String ip = localIpString();
  const String updateTime = updateTimeString();

  bool firstDataField = true;
  json = "{\"ok\":true,\"data\":{";

  appendBoolField(json, firstDataField, "connected", true);
  appendStringField(json, firstDataField, "deviceIp", ip);
  appendStringField(json, firstDataField, "updateTime", updateTime);

  appendFieldPrefix(json, firstDataField, "gateway");
  json += '{';
  bool firstGatewayField = true;
  appendStringField(json, firstGatewayField, "name", "ESP32-S3 water gateway");
  appendStringField(json, firstGatewayField, "ssid", cfg::kWifiSsid);
  appendStringField(json, firstGatewayField, "ip", ip);
  appendBoolField(json, firstGatewayField, "wifiConnected", wifiConnected());
  appendIntField(json, firstGatewayField, "rssi", wifiConnected() ? WiFi.RSSI() : 0);
  appendUIntField(json, firstGatewayField, "pollIntervalMs", cfg::kAutoPollIntervalMs);
  appendUIntField(json, firstGatewayField, "onlineSensors", g_gateway.onlineSensors);
  appendUIntField(json, firstGatewayField, "configuredSensors", 5);
  appendUIntField(json, firstGatewayField, "pollCycles", g_gateway.totalPollCycles);
  json += '}';

  appendFieldPrefix(json, firstDataField, "metrics");
  appendMetricsJson(json);

  appendFieldPrefix(json, firstDataField, "sensors");
  appendSensorsJson(json);

  json += "}}";
  return json;
}

float resolvedWaterTempC()
{
  if (g_gateway.phEcRuntime.online)
  {
    return g_gateway.phEcReading.temperatureC;
  }
  if (g_gateway.codRuntime.online)
  {
    return g_gateway.codReading.temperatureC;
  }
  if (g_gateway.ammoniaRuntime.online)
  {
    return g_gateway.ammoniaReading.temperatureC;
  }
  if (g_gateway.nitriteRuntime.online)
  {
    return g_gateway.nitriteReading.temperatureC;
  }
  if (g_gateway.nitrateRuntime.online)
  {
    return g_gateway.nitrateReading.temperatureC;
  }

  return 0.0F;
}

String buildMqttLatestJson()
{
  String json;
  json.reserve(180);

  bool firstField = true;
  json = "{";
  appendFloatField(json, firstField, "waterTemp", resolvedWaterTempC(), 1);
  appendFloatField(json, firstField, "ph", g_gateway.phEcRuntime.online ? g_gateway.phEcReading.ph : 0.0F, 2);
  appendUIntField(json, firstField, "conductivity", g_gateway.phEcRuntime.online ? g_gateway.phEcReading.ecUsCm : 0);
  appendFloatField(json, firstField, "tds", g_gateway.phEcRuntime.online ? resolvedPhEcTdsPpm(g_gateway.phEcReading) : 0.0F, 0);
  appendFloatField(json, firstField, "salinity", g_gateway.phEcRuntime.online ? resolvedPhEcSalinityPpt(g_gateway.phEcReading) : 0.0F, 2);
  appendFloatField(json, firstField, "cod", g_gateway.codRuntime.online ? g_gateway.codReading.codMgL : 0.0F, 2);
  appendFloatField(json, firstField, "ammonia", g_gateway.ammoniaRuntime.online ? g_gateway.ammoniaReading.concentrationPpm : 0.0F, 3);
  appendFloatField(json, firstField, "nitrite", g_gateway.nitriteRuntime.online ? g_gateway.nitriteReading.concentrationPpm : 0.0F, 3);
  appendFloatField(json, firstField, "nitrate", g_gateway.nitrateRuntime.online ? g_gateway.nitrateReading.concentrationPpm : 0.0F, 3);
  json += "}";
  return json;
}

bool mqttConfigured()
{
  return std::strlen(cfg::kMqttBrokerHost) > 0;
}

String mqttClientIdString()
{
  String clientId = cfg::kMqttClientId;
  clientId += "-";
  clientId += WiFi.macAddress();
  clientId.replace(":", "");
  return clientId;
}

String buildMqttStatusJson(bool online)
{
  String json;
  json.reserve(320);

  bool firstField = true;
  json = "{";
  appendBoolField(json, firstField, "ok", true);
  appendBoolField(json, firstField, "online", online);
  appendStringField(json, firstField, "device", cfg::kMqttClientId);
  appendStringField(json, firstField, "ip", localIpString());
  appendStringField(json, firstField, "broker", mqttBrokerString());
  appendStringField(json, firstField, "latestTopic", cfg::kMqttTopicLatest);
  appendUIntField(json, firstField, "published", g_mqttPublishCount);
  appendUIntField(json, firstField, "failed", g_mqttPublishFailureCount);
  json += "}";
  return json;
}

void setupMqttClient()
{
  if (!mqttConfigured())
  {
    return;
  }

  mqttClient.setServer(cfg::kMqttBrokerHost, cfg::kMqttBrokerPort);
  mqttClient.setSocketTimeout(cfg::kMqttSocketTimeoutSeconds);
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
  Serial.printf("MQTT broker: %s\n", mqttBrokerString().c_str());
  Serial.printf("MQTT connected: %s state=%d\n", mqttClient.connected() ? "yes" : "no", mqttClient.state());
  Serial.printf("MQTT latest topic: %s\n", cfg::kMqttTopicLatest);
  Serial.printf("MQTT status topic: %s\n", cfg::kMqttTopicStatus);
  Serial.printf("MQTT publish count: %lu failed: %lu lastPublishMs: %lu\n",
                static_cast<unsigned long>(g_mqttPublishCount),
                static_cast<unsigned long>(g_mqttPublishFailureCount),
                static_cast<unsigned long>(g_lastMqttPublishMs));
}

void sendJsonResponse(int statusCode, const String &payload)
{
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(statusCode, "application/json; charset=utf-8", payload);
}

void handleRoot()
{
  String payload = "{\"ok\":true,\"service\":\"fish-water-gateway\",\"endpoint\":\"/api/water/latest\",\"health\":\"/api/water/health\",\"mqttTopic\":\"";
  payload += escapeJson(cfg::kMqttTopicLatest);
  payload += "\"}";
  sendJsonResponse(200, payload);
}

void handleHealth()
{
  String payload;
  payload.reserve(384);

  payload = "{\"ok\":true,\"wifiConnected\":";
  payload += wifiConnected() ? "true" : "false";
  payload += ",\"ip\":\"";
  payload += escapeJson(localIpString());
  payload += "\",\"onlineSensors\":";
  payload += String(g_gateway.onlineSensors);
  payload += ",\"mqttConnected\":";
  payload += mqttClient.connected() ? "true" : "false";
  payload += ",\"mqttBroker\":\"";
  payload += escapeJson(mqttBrokerString());
  payload += "\",\"mqttTopic\":\"";
  payload += escapeJson(cfg::kMqttTopicLatest);
  payload += "\",\"updateTime\":\"";
  payload += escapeJson(updateTimeString());
  payload += "\"}";

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

void printHelp()
{
  Serial.println();
  Serial.println("=== Water gateway commands ===");
  Serial.println("1 : test COD sensor once (address 01)");
  Serial.println("2 : test PH-EC sensor once (address 02)");
  Serial.println("3 : test ammonia sensor once (address 03)");
  Serial.println("4 : test nitrite sensor once (address 04)");
  Serial.println("5 : test nitrate sensor once (address 05)");
  Serial.println("r : retest the last selected sensor");
  Serial.println("a : poll all 5 configured sensors once");
  Serial.println("p : read ion-electrode params for 3/4/5");
  Serial.println("i : print WiFi/IP/API information");
  Serial.println("j : print current /api/water/latest JSON");
  Serial.println("m : print MQTT information");
  Serial.println("q : publish current water JSON to MQTT once");
  Serial.println("h : show help again");
  Serial.println();
}

void setActiveSensor(cfg::SensorSlot slot)
{
  g_activeSensor = slot;
  if (slot == cfg::SensorSlot::None)
  {
    Serial.println("No sensor selected yet. Type 1~5 to start.");
    return;
  }

  Serial.printf("Selected sensor: %s (address %u)\n", sensorName(slot), sensorAddress(slot));
}

void handleConsoleCommand(char cmd)
{
  switch (cmd)
  {
  case '1':
    setActiveSensor(cfg::SensorSlot::Cod);
    refreshSensor(g_activeSensor, true);
    break;
  case '2':
    setActiveSensor(cfg::SensorSlot::PhEc);
    refreshSensor(g_activeSensor, true);
    break;
  case '3':
    setActiveSensor(cfg::SensorSlot::Ammonia);
    refreshSensor(g_activeSensor, true);
    break;
  case '4':
    setActiveSensor(cfg::SensorSlot::Nitrite);
    refreshSensor(g_activeSensor, true);
    break;
  case '5':
    setActiveSensor(cfg::SensorSlot::Nitrate);
    refreshSensor(g_activeSensor, true);
    break;
  case 'r':
  case 'R':
    if (g_activeSensor == cfg::SensorSlot::None)
    {
      Serial.println("No previous sensor. Type 1~5 first.");
      break;
    }
    refreshSensor(g_activeSensor, true);
    break;
  case 'a':
  case 'A':
    pollAllSensors(true);
    break;
  case 'p':
  case 'P':
  {
    sensor::IonParams params;
    if (g_activeSensor != cfg::SensorSlot::Ammonia &&
        g_activeSensor != cfg::SensorSlot::Nitrite &&
        g_activeSensor != cfg::SensorSlot::Nitrate)
    {
      Serial.println("Params command only works for sensors 3, 4 and 5.");
      break;
    }

    if (!readSelectedIonParams(g_activeSensor, &params))
    {
      Serial.println("Ion-electrode params read failed.");
      break;
    }

    printIonParams(params);
    break;
  }
  case 'i':
  case 'I':
    printWifiStatus();
    printPollSummary();
    break;
  case 'j':
  case 'J':
    Serial.println(buildGatewayJson());
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
      Serial.println("MQTT publish skipped or failed. Check WiFi, broker IP and MQTT status.");
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
  Serial.println("ESP32-S3 water gateway booting...");

  rs485Serial.begin(cfg::kRs485Baud, SERIAL_8N1, cfg::kRs485RxPin, cfg::kRs485TxPin);
  clearRs485Input();

  Wire.begin(cfg::kOledSdaPin, cfg::kOledSclPin);
  Wire.setClock(400000);
  g_displayReady = initDisplay();
  if (g_displayReady)
  {
    Serial.printf("OLED detected at 0x%02X\n", g_oledAddress);
  }

  setupMqttClient();
  startWifi();
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
  drawOledStatus(false);

  const uint32_t nowMs = millis();
  if (nowMs - g_gateway.lastAutoPollMs >= cfg::kAutoPollIntervalMs)
  {
    pollAllSensors(false);
  }
}
