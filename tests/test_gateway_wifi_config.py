from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
POND_PROJECTS = (
    "new-esp32s3_water_gateway",
    "esp32s3_water_gateway_pond1",
    "esp32s3_water_gateway_pond2",
    "esp32s3_water_gateway_pond3",
)


class GatewayWifiConfigTests(unittest.TestCase):
    def test_water_gateways_disable_wifi_sleep_and_enable_reconnect(self):
        required_snippets = (
            "WiFi.persistent(false);",
            "WiFi.setSleep(false);",
            "WiFi.setAutoReconnect(true);",
            "WiFi.setTxPower(WIFI_POWER_15dBm);",
        )

        for project in POND_PROJECTS:
            with self.subTest(project=project):
                source = (ROOT / project / "src" / "main.cpp").read_text(encoding="utf-8")
                for snippet in required_snippets:
                    self.assertIn(snippet, source)


if __name__ == "__main__":
    unittest.main()
