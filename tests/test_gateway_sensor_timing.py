from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
POND_PROJECTS = (
    "esp32s3_water_gateway_pond1",
    "esp32s3_water_gateway_pond2",
    "esp32s3_water_gateway_pond3",
)


class GatewaySensorTimingTests(unittest.TestCase):
    def test_pond_gateways_wait_between_different_rs485_sensors(self):
        poll_sequence = re.compile(
            r"refreshPh\(verbose\);\s*"
            r"delay\(cfg::kInterSensorGapMs\);\s*"
            r"refreshDo\(verbose\);"
        )

        for project in POND_PROJECTS:
            with self.subTest(project=project):
                config = (ROOT / project / "include" / "pond_config.h").read_text(
                    encoding="utf-8"
                )
                source = (ROOT / project / "src" / "main.cpp").read_text(
                    encoding="utf-8"
                )

                self.assertIn("constexpr uint32_t kRequestGapMs = 80;", config)
                self.assertIn("constexpr uint32_t kInterSensorGapMs = 500;", config)
                self.assertRegex(source, poll_sequence)


if __name__ == "__main__":
    unittest.main()
