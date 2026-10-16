#pragma once

#include <Arduino.h>

// 环境传感器网关配置。这里集中放置现场需要核对的引脚、网络和设备地址。
namespace envcfg
{

constexpr uint32_t kDebugBaud = 115200;

// RS485 使用 ESP32-S3 的 UART1。当前硬件沿用旧水质网关的接线。
constexpr uint32_t kRs485Baud = 9600;
constexpr int8_t kRs485RxPin = 18;
constexpr int8_t kRs485TxPin = 17;

// -1 表示当前 RS485 收发器为自动收发，不需要额外的 DE/RE 控制脚。
// 如果后续硬件把 DE/RE 接到 GPIO，可改成对应 GPIO，并确认有效电平。
constexpr int8_t kRs485DirectionPin = -1;
constexpr bool kRs485TransmitLevel = true;

// 现场 Wi-Fi 与原来的水质网关保持一致。
constexpr char kWifiSsid[] = "";
constexpr char kWifiPassword[] = "";

// 阿里云 MQTT 测试服务器。正式部署前建议更换密码并启用 TLS。
constexpr char kMqttBrokerHost[] = "";
constexpr uint16_t kMqttBrokerPort = 1883;
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";

// 新环境网关不再区分鱼塘，设备 ID 由 ESP32-S3 eFuse MAC 自动生成。
constexpr char kMqttTopicPrefix[] = "fish/device";
constexpr char kMqttLatestSuffix[] = "/weather/latest";
constexpr char kMqttStatusSuffix[] = "/weather/status";

// 四台传感器必须使用不同的 Modbus 地址。四份资料的默认地址都是 0x01。
constexpr uint8_t kWindSensorAddress = 0x01;
constexpr uint8_t kRainSensorAddress = 0x02;
constexpr uint8_t kPressureSensorAddress = 0x03;
constexpr uint8_t kAirSensorAddress = 0x04;

constexpr uint32_t kSensorPollIntervalMs = 5000;
constexpr uint32_t kMqttPublishIntervalMs = 60000;
constexpr uint32_t kModbusReplyTimeoutMs = 500;
constexpr uint32_t kModbusTurnaroundDelayMs = 10;
constexpr uint32_t kSensorGapMs = 100;
constexpr uint32_t kWifiRetryIntervalMs = 15000;
constexpr uint32_t kMqttReconnectIntervalMs = 10000;
constexpr uint16_t kMqttBufferSize = 2048;
constexpr uint16_t kMqttSocketTimeoutSeconds = 3;
constexpr uint16_t kHttpPort = 80;
constexpr bool kMqttRetainLatest = true;

} // namespace envcfg
