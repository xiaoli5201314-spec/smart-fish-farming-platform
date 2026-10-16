from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
POND_PROJECTS = (
    ("esp32s3_water_gateway_pond1", "pond_1"),
    ("esp32s3_water_gateway_pond2", "pond_2"),
    ("esp32s3_water_gateway_pond3", "pond_3"),
)


class GatewayPublishIntervalTests(unittest.TestCase):
    def test_sensor_polling_is_independent_from_configurable_mqtt_publishing(self):
        poll_block = re.compile(
            r"if \(collecting && \(nowMs - g_gateway\.lastAutoPollMs\) >= cfg::kAutoPollIntervalMs\)"
            r"\s*\{(?P<body>.*?)\}",
            re.DOTALL,
        )

        for project, pond_id in POND_PROJECTS:
            with self.subTest(project=project):
                config = (ROOT / project / "include" / "pond_config.h").read_text(encoding="utf-8")
                source = (ROOT / project / "src" / "main.cpp").read_text(encoding="utf-8")

                self.assertIn("constexpr uint32_t kAutoPollIntervalMs = 2000;", config)
                self.assertIn("constexpr uint32_t kDefaultMqttPublishIntervalSeconds = 3600UL;", config)
                self.assertIn(
                    f'constexpr char kMqttTopicConfig[] = "fish/pond/{pond_id}/water/config";',
                    config,
                )

                match = poll_block.search(source)
                self.assertIsNotNone(match)
                self.assertIn("pollAllSensors(false);", match.group("body"))
                self.assertNotIn("publishMqttLatest();", match.group("body"))
                self.assertIn("shouldPublishMqttLatest(nowMs)", source)
                self.assertIn("g_control.publishIntervalSeconds", source)

    def test_gateway_accepts_only_fixed_interval_options_and_reports_them(self):
        supported_seconds = (2, 60, 300, 1800, 3600, 21600, 86400)

        for project, _ in POND_PROJECTS:
            with self.subTest(project=project):
                source = (ROOT / project / "src" / "main.cpp").read_text(encoding="utf-8")

                self.assertIn("isSupportedPublishIntervalSeconds", source)
                for seconds in supported_seconds:
                    self.assertIn(f"case {seconds}UL:", source)
                self.assertIn('extractJsonUIntField(payload, "publishIntervalSeconds"', source)
                self.assertIn("subscribe(cfg::kMqttTopicConfig", source)
                self.assertIn('"publishIntervalSeconds", g_control.publishIntervalSeconds', source)
                self.assertIn('"sampledAt", g_control.timeText', source)
                self.assertIn('"timeSynced", g_control.timeSynced', source)


if __name__ == "__main__":
    unittest.main()
