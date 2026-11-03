#!/usr/bin/env python3
"""Validate shared last-weather storage and its producer/consumer integration."""
import os
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
component = project / "components/tab5_weather_snapshot"
with tempfile.TemporaryDirectory(prefix="tab5-weather-snapshot-") as temporary:
    binary = str(Path(temporary) / "check-snapshot")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-g", "-I", str(project / "tests/boot_fakes"),
        "-I", str(component / "include"), str(project / "tests/test_weather_snapshot.c"),
        str(component / "snapshot_model.c"), str(component / "tab5_weather_snapshot.c"),
        "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)

console = (project / "main/tab5_console.cpp").read_text()
weather = (project.parent / "tab5-weather/firmware/main/tab5_weather.cpp").read_text()
assert "INSERT SD LIBRARY" not in console
assert console.count("tab5_weather_snapshot_load(") == 1
assert "tab5_weather_snapshot_store(" not in console
assert "tab5_weather_snapshot_store(" in weather
assert "publish_console_weather(view.settings.places[i], c->forecast)" in weather
store_source = "\n".join(p.read_text() for p in component.glob("*.c"))
for forbidden in ("nvs_flash_erase", "esp_http", "esp_wifi", "https://", "esp_sntp"):
    assert forbidden not in store_source
print("PASS: Console reads last weather once; only Weather publishes; no weather network or clock dependencies")
