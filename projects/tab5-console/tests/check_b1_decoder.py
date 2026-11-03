#!/usr/bin/env python3
"""Run the copied CYD B1 mapping/descriptor suites under ASan and UBSan."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="tab5-b1-tests-") as directory:
    for case in ("keyboard_mapping", "keyboard_report"):
        executable = Path(directory) / case
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(root / "components/tab5_radio/cyd_keyboard"),
            str(root / "components/tab5_radio/cyd_keyboard" / f"{case}.cpp"),
            str(root / "tests" / f"test_{case}.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
