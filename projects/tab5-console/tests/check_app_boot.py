#!/usr/bin/env python3
"""Exercise the production boot lifecycle with fault-injecting ESP-IDF fakes."""
import os
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="tab5-app-boot-") as temporary:
    binary = str(Path(temporary) / "check-boot")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-g",
        "-I", str(project / "tests/boot_fakes"),
        "-I", str(project / "components/tab5_app_boot/include"),
        str(project / "tests/test_app_boot.c"),
        str(project / "components/tab5_app_boot/tab5_app_boot.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
