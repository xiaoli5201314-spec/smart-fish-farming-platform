#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace
{

  // 0.96 寸 SSD1306 OLED 常见分辨率。
  constexpr int kScreenWidth = 128;
  constexpr int kScreenHeight = 64;

  // OLED 接线：如果你后续换了接线，只需要改这里。
  constexpr int kOledSdaPin = 8;
  constexpr int kOledSclPin = 9;

  // 红外避障传感器 OUT 接线。
  constexpr int kSensorPin = 4;

  // 可选清零按键：一端接 GPIO37，另一端接 GND；不接也不影响计数。
  constexpr int kResetButtonPin = 37;

  // 多数 MH-Sensor-Series / Flying-Fish 红外模块检测到物体时 OUT 为低电平。
  // 如果你实测刚好相反，把 LOW 改成 HIGH 即可。
  constexpr int kSensorActiveLevel = LOW;

  // 手机热点信息：如果你换热点，只改这里即可。
  constexpr char kWifiSsid[] = "";
  constexpr char kWifiPassword[] = "";

  // 中国标准时间 UTC+8，不使用夏令时。
  constexpr long kGmtOffsetSec = 8 * 60 * 60;
  constexpr int kDaylightOffsetSec = 0;
  constexpr char kNtpServer1[] = "ntp.aliyun.com";
  constexpr char kNtpServer2[] = "ntp.tencent.com";
  constexpr char kNtpServer3[] = "pool.ntp.org";

  constexpr uint32_t kSerialBaud = 115200;
  constexpr uint32_t kSensorDebounceMs = 35;
  constexpr uint32_t kRearmStableMs = 180;
  constexpr uint32_t kMinCountIntervalMs = 260;
  constexpr uint32_t kDisplayRefreshMs = 250;
  constexpr uint32_t kResetHoldMs = 900;
  constexpr uint32_t kWifiRetryIntervalMs = 15000;
  constexpr time_t kValidEpoch = 1700000000; // 大于这个值就认为 NTP 已经校时成功。

  Adafruit_SSD1306 display(kScreenWidth, kScreenHeight, &Wire, -1);

  uint8_t oledAddress = 0;
  bool displayReady = false;

  uint32_t fishCount = 0;
  bool rawDetected = false;
  bool stableDetected = false;
  bool lastRawDetected = false;
  bool armedForNextFish = true;
  uint32_t rawChangedAtMs = 0;
  uint32_t lastCountAtMs = 0;
  uint32_t lastDisplayAtMs = 0;
  uint32_t resetPressedAtMs = 0;
  bool resetWasPressed = false;

  bool wifiStarted = false;
  bool ntpConfigured = false;
  bool timeSynced = false;
  uint32_t lastWifiAttemptMs = 0;

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
        oledAddress = address;
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextWrap(false);
        display.display();
        return true;
      }
    }

    return false;
  }

  bool readSensorDetected()
  {
    return digitalRead(kSensorPin) == kSensorActiveLevel;
  }

  bool isTimeValid()
  {
    return time(nullptr) > kValidEpoch;
  }

  void startWifi()
  {
    if (strlen(kWifiSsid) == 0)
    {
      return;
    }

    lastWifiAttemptMs = millis();
    wifiStarted = true;
    ntpConfigured = false;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false);
    WiFi.begin(kWifiSsid, kWifiPassword);

    Serial.printf("WiFi connecting: %s\r\n", kWifiSsid);
  }

  void updateTimeSync()
  {
    if (timeSynced)
    {
      return;
    }

    const uint32_t nowMs = millis();
    if (!wifiStarted || (WiFi.status() != WL_CONNECTED && nowMs - lastWifiAttemptMs >= kWifiRetryIntervalMs))
    {
      startWifi();
      return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      return;
    }

    if (!ntpConfigured)
    {
      configTime(kGmtOffsetSec, kDaylightOffsetSec, kNtpServer1, kNtpServer2, kNtpServer3);
      ntpConfigured = true;
      Serial.print(F("WiFi connected, IP: "));
      Serial.println(WiFi.localIP());
      Serial.println(F("NTP sync started"));
    }

    if (isTimeValid())
    {
      timeSynced = true;
      Serial.println(F("Time synced"));
    }
  }

  void formatDateTime(char *dateBuf, size_t dateSize, char *timeBuf, size_t timeSize)
  {
    if (!timeSynced && !isTimeValid())
    {
      snprintf(dateBuf, dateSize, "Date --/--/--");
      if (WiFi.status() == WL_CONNECTED)
      {
        snprintf(timeBuf, timeSize, "NTP syncing");
      }
      else
      {
        snprintf(timeBuf, timeSize, "WiFi waiting");
      }
      return;
    }

    timeSynced = true;

    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, 5))
    {
      snprintf(dateBuf, dateSize, "Date --/--/--");
      snprintf(timeBuf, timeSize, "Time --:--");
      return;
    }

    strftime(dateBuf, dateSize, "%Y-%m-%d", &timeInfo);
    strftime(timeBuf, timeSize, "%H:%M:%S", &timeInfo);
  }

  void drawBootScreen(const __FlashStringHelper *line1, const __FlashStringHelper *line2 = nullptr)
  {
    if (!displayReady)
    {
      return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("Fish Counter"));
    display.println();
    display.println(line1);
    if (line2 != nullptr)
    {
      display.println(line2);
    }
    display.display();
  }

  void drawCenteredCount()
  {
    const String countText = String(fishCount);
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    display.setTextSize(3);
    display.getTextBounds(countText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((kScreenWidth - w) / 2, 38);
    display.print(countText);
  }

  void drawMainScreen()
  {
    if (!displayReady)
    {
      return;
    }

    char dateText[16] = {};
    char timeText[16] = {};
    formatDateTime(dateText, sizeof(dateText), timeText, sizeof(timeText));

    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(F("Fish Outlet"));
    display.setCursor(timeSynced ? 104 : 98, 0);
    display.print(timeSynced ? F("OK") : F("SYNC"));

    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setCursor(0, 14);
    display.print(dateText);

    display.setCursor(0, 25);
    display.print(timeText);

    display.setCursor(86, 25);
    display.print(F("COUNT"));

    drawCenteredCount();

    display.display();
  }

  void countOneFish(uint32_t nowMs)
  {
    fishCount++;
    lastCountAtMs = nowMs;
    armedForNextFish = false;

    Serial.printf("Fish counted: %lu\r\n", static_cast<unsigned long>(fishCount));
    drawMainScreen();
  }

  void updateCounterState()
  {
    const uint32_t nowMs = millis();
    rawDetected = readSensorDetected();

    if (rawDetected != lastRawDetected)
    {
      lastRawDetected = rawDetected;
      rawChangedAtMs = nowMs;
    }

    // 输入状态稳定一小段时间后，才认为传感器状态真的发生了变化。
    if ((nowMs - rawChangedAtMs) >= kSensorDebounceMs && stableDetected != rawDetected)
    {
      stableDetected = rawDetected;

      if (stableDetected && armedForNextFish && (nowMs - lastCountAtMs) >= kMinCountIntervalMs)
      {
        countOneFish(nowMs);
      }
    }

    // 鱼离开出鱼口并保持无遮挡一段时间后，允许计下一条。
    if (!stableDetected && !armedForNextFish && (nowMs - rawChangedAtMs) >= kRearmStableMs)
    {
      armedForNextFish = true;
    }
  }

  void updateResetButton()
  {
    const uint32_t nowMs = millis();
    const bool pressed = digitalRead(kResetButtonPin) == LOW;

    if (pressed && !resetWasPressed)
    {
      resetWasPressed = true;
      resetPressedAtMs = nowMs;
    }

    if (!pressed)
    {
      resetWasPressed = false;
      return;
    }

    if (resetWasPressed && (nowMs - resetPressedAtMs) >= kResetHoldMs)
    {
      fishCount = 0;
      armedForNextFish = true;
      lastCountAtMs = nowMs;
      resetWasPressed = false;
      Serial.println(F("Counter reset"));
      drawMainScreen();
    }
  }

  void refreshDisplayPeriodically()
  {
    const uint32_t nowMs = millis();
    if (nowMs - lastDisplayAtMs < kDisplayRefreshMs)
    {
      return;
    }

    lastDisplayAtMs = nowMs;
    drawMainScreen();
  }

} // namespace

void setup()
{
  Serial.begin(kSerialBaud);
  delay(300);

  pinMode(kSensorPin, INPUT_PULLUP);
  pinMode(kResetButtonPin, INPUT_PULLUP);

  Wire.begin(kOledSdaPin, kOledSclPin);
  Wire.setClock(400000);

  displayReady = initDisplay();
  if (displayReady)
  {
    Serial.printf("OLED detected at 0x%02X\r\n", oledAddress);
    drawBootScreen(F("OLED ready"), F("Starting counter..."));
    delay(600);
  }
  else
  {
    Serial.println(F("OLED not found on 0x3C/0x3D"));
  }

  rawDetected = readSensorDetected();
  lastRawDetected = rawDetected;
  stableDetected = rawDetected;
  rawChangedAtMs = millis();
  armedForNextFish = !stableDetected;

  startWifi();

  Serial.println(F("Fish outlet counter started"));
  drawMainScreen();
}

void loop()
{
  updateTimeSync();
  updateCounterState();
  updateResetButton();
  refreshDisplayPeriodically();
}
