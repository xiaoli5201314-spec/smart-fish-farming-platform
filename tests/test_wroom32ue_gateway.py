from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "esp32_wroom32ue_water_gateway"


class Esp32Wroom32ueGatewayTests(unittest.TestCase):
    def test_project_targets_classic_esp32_and_uses_safe_led_pins(self):
        platformio = (PROJECT / "platformio.ini").read_text(encoding="utf-8")
        config = (PROJECT / "include" / "pond_config.h").read_text(encoding="utf-8")
        source = (PROJECT / "src" / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("board = esp32dev", platformio)
        self.assertIn("constexpr uint8_t kRunLedPin = 25;", config)
        self.assertIn("constexpr uint8_t kErrLedPin = 26;", config)
        self.assertIn("constexpr uint8_t kRs485RxPin = 18;", config)
        self.assertIn("constexpr uint8_t kRs485TxPin = 17;", config)
        self.assertNotIn("Adafruit_SSD1306", source)
        self.assertIn("maintainLedIndicators", source)


if __name__ == "__main__":
    unittest.main()
