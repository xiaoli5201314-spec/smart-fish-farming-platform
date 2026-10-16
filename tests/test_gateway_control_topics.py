from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
POND_PROJECTS = (
    "esp32s3_water_gateway_pond1",
    "esp32s3_water_gateway_pond2",
    "esp32s3_water_gateway_pond3",
)


class GatewayControlTopicTests(unittest.TestCase):
    def test_formal_gateways_define_control_topics_and_hooks(self):
        required_config_snippets = (
            "kMqttTopicControlCmd",
            "kMqttTopicControlStatus",
        )
        required_source_snippets = (
            "subscribe(cfg::kMqttTopicControlCmd",
            "publishControlStatus(",
            "shouldCollectData(",
        )

        for project in POND_PROJECTS:
            with self.subTest(project=project):
                config = (ROOT / project / "include" / "pond_config.h").read_text(encoding="utf-8")
                source = (ROOT / project / "src" / "main.cpp").read_text(encoding="utf-8")
                for snippet in required_config_snippets:
                    self.assertIn(snippet, config)
                for snippet in required_source_snippets:
                    self.assertIn(snippet, source)


if __name__ == "__main__":
    unittest.main()
