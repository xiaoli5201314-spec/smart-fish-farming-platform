#pragma once

#include <Arduino.h>

namespace cfg
{

constexpr uint32_t kUsbDebugBaud = 115200;
constexpr uint32_t kRs485Baud = 9600;
constexpr uint8_t kRs485RxPin = 18;
constexpr uint8_t kRs485TxPin = 17;

constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr int kOledSdaPin = 8;
constexpr int kOledSclPin = 9;

constexpr uint16_t kHttpPort = 80;
constexpr char kWifiSsid[] = "";
constexpr char kWifiPassword[] = "";

// The other team can provide the Aliyun MQTT host, port, username and password.
constexpr char kMqttBrokerHost[] = "";
constexpr uint16_t kMqttBrokerPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

constexpr char kPondId[] = "pond_3";
constexpr char kGatewayId[] = "water_gateway_pond_3";
constexpr char kHostname[] = "fish-water-pond-3";
constexpr char kMqttClientId[] = "fish-water-pond-3";
constexpr char kMqttTopicLatest[] = "fish/pond/pond_3/water/latest";
constexpr char kMqttTopicStatus[] = "fish/pond/pond_3/water/status";
constexpr char kMqttTopicControlCmd[] = "fish/pond/pond_3/water/cmd";
constexpr char kMqttTopicControlStatus[] = "fish/pond/pond_3/water/control/status";
constexpr char kMqttTopicConfig[] = "fish/pond/pond_3/water/config";

constexpr char kTimeZone[] = "CST-8";
constexpr char kNtpServerPrimary[] = "ntp.aliyun.com";
constexpr char kNtpServerSecondary[] = "pool.ntp.org";
constexpr char kNtpServerTertiary[] = "time.google.com";

// Keep pH and dissolved oxygen at different addresses on the same RS485 bus.
constexpr uint8_t kPhAddress = 1;
constexpr uint8_t kDoAddress = 2;

constexpr uint32_t kModbusReplyTimeoutMs = 450;
constexpr uint32_t kRequestGapMs = 80;
constexpr uint32_t kInterSensorGapMs = 500;
constexpr uint32_t kAutoPollIntervalMs = 2000;
constexpr uint32_t kDefaultMqttPublishIntervalSeconds = 3600UL;
constexpr uint32_t kWifiRetryIntervalMs = 15000;
constexpr uint32_t kDisplayRefreshMs = 800;
constexpr uint32_t kMqttReconnectIntervalMs = 10000;
constexpr uint16_t kMqttSocketTimeoutSeconds = 2;
constexpr uint16_t kMqttBufferSize = 2048;
constexpr bool kMqttRetainLatest = true;
constexpr uint32_t kTimeSyncRetryIntervalMs = 60000;
constexpr uint8_t kSensorOfflineFailureThreshold = 3;
constexpr uint32_t kSensorOfflineGraceMs = 20000;

} // namespace cfg
