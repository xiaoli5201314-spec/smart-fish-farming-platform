from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
POND_PROJECTS = (
    "esp32s3_water_gateway_pond1",
    "esp32s3_water_gateway_pond2",
    "esp32s3_water_gateway_pond3",
)


class GatewayChipTemperatureTests(unittest.TestCase):
    def test_formal_gateways_publish_current_chip_temperature(self):
        mqtt_field = (
            'appendFloatField(json, firstField, "chipTempC", temperatureRead(), 1);'
        )
        local_field = (
            'appendFloatField(json, firstGatewayField, "chipTempC", '
            "temperatureRead(), 1);"
        )

        for project in POND_PROJECTS:
            with self.subTest(project=project):
                source = (ROOT / project / "src" / "main.cpp").read_text(
                    encoding="utf-8"
                )
                self.assertIn(mqtt_field, source)
                self.assertIn(local_field, source)


if __name__ == "__main__":
    unittest.main()
