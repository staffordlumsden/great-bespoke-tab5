#!/usr/bin/env python3
"""Contract checks for Tab5 ThinClient Console integration and device safety."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware" / "main"


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    required_files = [
        ROOT / "VERSION",
        ROOT / "LICENSE",
        ROOT / "PROVENANCE.md",
        ROOT / "THIRD_PARTY_NOTICES.txt",
        ROOT / "Tab5ThinClient-LICENSE.txt",
        ROOT / "README.md",
        ROOT / "data" / "tab5-fuse-server.py",
        ROOT / "tools" / "package_sd_app.py",
        ROOT / "firmware" / "CMakeLists.txt",
        ROOT / "firmware" / "partitions.csv",
        ROOT / "firmware" / "sdkconfig.defaults",
    ]
    missing = [str(p.relative_to(ROOT)) for p in required_files if not p.is_file()]
    if missing:
        print(f"FAIL: missing required files: {missing}", file=sys.stderr)
        return 1

    fw_cmake = read_text(ROOT / "firmware" / "CMakeLists.txt")
    if "partition-table-flash" not in fw_cmake or "tab5_thinclient_sd_only" not in fw_cmake:
        print("FAIL: firmware/CMakeLists.txt missing flash-target blocklist", file=sys.stderr)
        return 1

    sdkconfig = read_text(ROOT / "firmware" / "sdkconfig.defaults")
    for required_cfg in (
        "CONFIG_IDF_TARGET=\"esp32p4\"",
        "CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y",
        "CONFIG_ESP32P4_REV_MIN_100=y",
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
        "CONFIG_PARTITION_TABLE_CUSTOM=y",
    ):
        if required_cfg not in sdkconfig:
            print(f"FAIL: sdkconfig.defaults missing {required_cfg}", file=sys.stderr)
            return 1

    all_main_sources = "\n".join(
        read_text(p)
        for p in sorted(MAIN.glob("*"))
        if p.suffix in {".c", ".cpp", ".h", ".hpp", ".txt"}
    )

    required_symbols = [
        "tab5_app_boot_ready",
        "tab5_app_boot_return_to_console",
        "tab5_radio_power_set",
        "tab5_weather_wifi_init",
        "tab5_display_set_rotation",
        "pixelfont_7x5_thin",
        "pixelfont_11x5_thin",
        "lgfx_font_japan_gothic_20",
        "TerminusBitmap",
    ]
    for sym in required_symbols:
        if sym not in all_main_sources:
            print(f"FAIL: required symbol/reference '{sym}' not found in firmware/main", file=sys.stderr)
            return 1

    print("check_thinclient_contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
