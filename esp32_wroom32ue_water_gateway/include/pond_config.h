#pragma once

#include <Arduino.h>

namespace cfg
{

constexpr uint32_t kUsbDebugBaud = 115200;
constexpr uint32_t kRs485Baud = 9600;
constexpr uint8_t kRs485RxPin = 18;
constexpr uint8_t kRs485TxPin = 17;
constexpr uint8_t kRunLedPin = 25;
constexpr uint8_t kErrLedPin = 26;
constexpr uint8_t kLedOnLevel = LOW;
constexpr uint8_t kLedOffLevel = HIGH;

constexpr uint16_t kHttpPort = 80;
constexpr char kWifiSsid[] = "";
constexpr char kWifiPassword[] = "";

// The other team can provide the Aliyun MQTT host, port, username and password.
constexpr char kMqttBrokerHost[] = "";
constexpr uint16_t kMqttBrokerPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

#ifndef POND_NUMBER
#define POND_NUMBER 1
#endif

#if POND_NUMBER == 1
constexpr char kPondId[] = "pond_1";
constexpr char kGatewayId[] = "water_gateway_pond_1";
constexpr char kHostname[] = "fish-water-pond-1";
constexpr char kMqttClientId[] = "fish-water-pond-1";
constexpr char kMqttTopicLatest[] = "fish/pond/pond_1/water/latest";
constexpr char kMqttTopicStatus[] = "fish/pond/pond_1/water/status";
constexpr char kMqttTopicControlCmd[] = "fish/pond/pond_1/water/cmd";
constexpr char kMqttTopicControlStatus[] = "fish/pond/pond_1/water/control/status";
constexpr char kMqttTopicConfig[] = "fish/pond/pond_1/water/config";
#elif POND_NUMBER == 2
constexpr char kPondId[] = "pond_2";
constexpr char kGatewayId[] = "water_gateway_pond_2";
constexpr char kHostname[] = "fish-water-pond-2";
constexpr char kMqttClientId[] = "fish-water-pond-2";
constexpr char kMqttTopicLatest[] = "fish/pond/pond_2/water/latest";
constexpr char kMqttTopicStatus[] = "fish/pond/pond_2/water/status";
constexpr char kMqttTopicControlCmd[] = "fish/pond/pond_2/water/cmd";
constexpr char kMqttTopicControlStatus[] = "fish/pond/pond_2/water/control/status";
constexpr char kMqttTopicConfig[] = "fish/pond/pond_2/water/config";
#elif POND_NUMBER == 3
constexpr char kPondId[] = "pond_3";
constexpr char kGatewayId[] = "water_gateway_pond_3";
constexpr char kHostname[] = "fish-water-pond-3";
constexpr char kMqttClientId[] = "fish-water-pond-3";
constexpr char kMqttTopicLatest[] = "fish/pond/pond_3/water/latest";
constexpr char kMqttTopicStatus[] = "fish/pond/pond_3/water/status";
constexpr char kMqttTopicControlCmd[] = "fish/pond/pond_3/water/cmd";
constexpr char kMqttTopicControlStatus[] = "fish/pond/pond_3/water/control/status";
constexpr char kMqttTopicConfig[] = "fish/pond/pond_3/water/config";
#else
#error "POND_NUMBER must be 1, 2 or 3"
#endif

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
constexpr uint32_t kMqttReconnectIntervalMs = 10000;
constexpr uint16_t kMqttSocketTimeoutSeconds = 2;
constexpr uint16_t kMqttBufferSize = 2048;
constexpr bool kMqttRetainLatest = true;
constexpr uint32_t kTimeSyncRetryIntervalMs = 60000;
constexpr uint8_t kSensorOfflineFailureThreshold = 3;
constexpr uint32_t kSensorOfflineGraceMs = 20000;

} // namespace cfg
