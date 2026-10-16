#include <Arduino.h>

namespace cfg
{

  constexpr uint32_t kUsbDebugBaud = 115200;
  constexpr uint32_t kPiUartBaud = 115200;

  // 树莓派与 ESP32-S3 的串口连接
  // 树莓派 TX 接 ESP32 RX，树莓派 RX 接 ESP32 TX
  constexpr uint8_t kPiRxPin = 44;
  constexpr uint8_t kPiTxPin = 43;

  // 根据你提供的原理图定义驱动器接口引脚
  constexpr uint8_t kPul1Pin = 4;
  constexpr uint8_t kDir1Pin = 6;
  constexpr uint8_t kPul2Pin = 10;
  constexpr uint8_t kDir2Pin = 11;

  // 外接 LED 控制引脚
  // LED 正极接 5V，LED 负极接该 GPIO，因此这里是低电平点亮、高电平熄灭。
  // 注意实际接线时 LED 必须串联限流电阻，不能直接把 LED 两端硬接到 5V 和 GPIO。
  constexpr uint8_t kLedPin = 7;
  constexpr uint8_t kLedOnLevel = LOW;
  constexpr uint8_t kLedOffLevel = HIGH;

  // 渔网前进方向映射：两个驱动器保持同方向。
  // true  表示渔网前进时两个驱动器 DIR 信号生效
  // false 表示渔网前进时两个驱动器 DIR 信号不生效
  constexpr bool kNetForwardDirEffective = false;

  // 两个电机共用的脉冲频率
  // 这里先给一个比较保守的速度，后面可按实际效果再调
  constexpr uint32_t kStepFrequencyHz = 7000;

  // 切换方向后重新开始出脉冲前的稳定时间
  constexpr uint32_t kStopBeforeDirUs = 500;
  constexpr uint32_t kDirSetupUs = 500;

  constexpr uint8_t kPul1Channel = 0;
  constexpr uint8_t kPul2Channel = 1;
  constexpr uint8_t kPwmResolutionBits = 8;
  constexpr uint32_t kPwmDutyActive = (1U << kPwmResolutionBits) / 2U;

  constexpr uint8_t kFrameHead = 0xAA;
  constexpr uint8_t kFrameTail = 0xBB;

  constexpr uint8_t kCmdForward = 0x01;
  constexpr uint8_t kCmdReverse = 0x02;
  constexpr uint8_t kCmdStop = 0x03;
  constexpr uint8_t kCmdLedOn = 0x04;
  constexpr uint8_t kCmdLedOff = 0x05;

} // namespace cfg

enum class MotionState : uint8_t
{
  Stopped,
  Forward,
  Reverse,
};

enum class FrameState : uint8_t
{
  WaitHead,
  WaitCmd,
  WaitTail,
};

HardwareSerial piSerial(1);

MotionState g_motionState = MotionState::Stopped;
FrameState g_frameState = FrameState::WaitHead;
uint8_t g_pendingCmd = 0;

void debugPrint(const char *msg)
{
  Serial.println(msg);
}

// 软件层面的方向/控制信号输出。
// 当前工程已经实测可采用驱动器共阳极直接接法：
// PUL+ / DIR+ 接 3.3V，PUL- / DIR- 接 ESP32-S3 对应 GPIO。
// 如果实际方向和界面显示相反，优先调整上面的两个方向映射常量。
void writeDriverSignal(uint8_t pin, bool signalEffective)
{
  digitalWrite(pin, signalEffective ? HIGH : LOW);
}

void stopPulseOutput()
{
  ledcWrite(cfg::kPul1Channel, 0);
  ledcWrite(cfg::kPul2Channel, 0);
}

void startPulseOutput()
{
  ledcWrite(cfg::kPul1Channel, cfg::kPwmDutyActive);
  ledcWrite(cfg::kPul2Channel, cfg::kPwmDutyActive);
}

void setLed(bool on)
{
  digitalWrite(cfg::kLedPin, on ? cfg::kLedOnLevel : cfg::kLedOffLevel);
}

void setDirection(MotionState state)
{
  const bool dirEffective =
      (state == MotionState::Forward)
          ? cfg::kNetForwardDirEffective
          : !cfg::kNetForwardDirEffective;

  writeDriverSignal(cfg::kDir1Pin, dirEffective);
  writeDriverSignal(cfg::kDir2Pin, dirEffective);
}

void applyMotion(MotionState target)
{
  if (target == MotionState::Stopped)
  {
    stopPulseOutput();
    g_motionState = MotionState::Stopped;
    debugPrint("Motor stopped");
    return;
  }

  stopPulseOutput();
  delayMicroseconds(cfg::kStopBeforeDirUs);

  setDirection(target);
  delayMicroseconds(cfg::kDirSetupUs);

  startPulseOutput();
  g_motionState = target;

  if (target == MotionState::Forward)
  {
    debugPrint("Net forward");
  }
  else
  {
    debugPrint("Net reverse");
  }
}

void handlePacket(uint8_t cmd)
{
  switch (cmd)
  {
  case cfg::kCmdForward:
    applyMotion(MotionState::Forward);
    break;
  case cfg::kCmdReverse:
    applyMotion(MotionState::Reverse);
    break;
  case cfg::kCmdStop:
    applyMotion(MotionState::Stopped);
    break;
  case cfg::kCmdLedOn:
    setLed(true);
    debugPrint("LED on");
    break;
  case cfg::kCmdLedOff:
    setLed(false);
    debugPrint("LED off");
    break;
  default:
    Serial.print("Unknown cmd: 0x");
    Serial.println(cmd, HEX);
    break;
  }
}

void parseIncomingByte(uint8_t data)
{
  switch (g_frameState)
  {
  case FrameState::WaitHead:
    if (data == cfg::kFrameHead)
    {
      g_frameState = FrameState::WaitCmd;
    }
    break;

  case FrameState::WaitCmd:
    g_pendingCmd = data;
    g_frameState = FrameState::WaitTail;
    break;

  case FrameState::WaitTail:
    if (data == cfg::kFrameTail)
    {
      handlePacket(g_pendingCmd);
      g_frameState = FrameState::WaitHead;
    }
    else if (data == cfg::kFrameHead)
    {
      // 如果这里直接又收到了帧头，就快速重新同步到新数据帧
      g_frameState = FrameState::WaitCmd;
    }
    else
    {
      g_frameState = FrameState::WaitHead;
    }
    break;
  }
}

void initPulsePwm()
{
  ledcSetup(cfg::kPul1Channel, cfg::kStepFrequencyHz, cfg::kPwmResolutionBits);
  ledcSetup(cfg::kPul2Channel, cfg::kStepFrequencyHz, cfg::kPwmResolutionBits);
  ledcAttachPin(cfg::kPul1Pin, cfg::kPul1Channel);
  ledcAttachPin(cfg::kPul2Pin, cfg::kPul2Channel);
  stopPulseOutput();
}

void setupPins()
{
  pinMode(cfg::kDir1Pin, OUTPUT);
  pinMode(cfg::kDir2Pin, OUTPUT);
  pinMode(cfg::kLedPin, OUTPUT);

  writeDriverSignal(cfg::kDir1Pin, false);
  writeDriverSignal(cfg::kDir2Pin, false);
  setLed(false);

  initPulsePwm();
}

void setupUart()
{
  piSerial.begin(cfg::kPiUartBaud, SERIAL_8N1, cfg::kPiRxPin, cfg::kPiTxPin);
  debugPrint("Pi UART ready");
}

void setup()
{
  Serial.begin(cfg::kUsbDebugBaud);
  delay(300);
  debugPrint("ESP32-S3 stepper controller booting...");

  setupPins();
  setupUart();
  applyMotion(MotionState::Stopped);
}

void loop()
{
  while (piSerial.available() > 0)
  {
    const uint8_t data = static_cast<uint8_t>(piSerial.read());
    parseIncomingByte(data);
  }

  delay(1);
}
