#!/usr/bin/env python3
"""Exercise the real installer with real SHA-256 and fault-injecting SDK fakes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

project = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="tab5-installer-") as temporary:
    binary = str(Path(temporary) / "check-installer")
    command = [os.environ.get("CC", "cc"), "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra",
               "-Werror", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-g",
               "-I", str(project / "tests/boot_fakes"),
               "-I", str(project / "components/tab5_app_boot/include"),
               "-I", str(project / "components/tab5_installer/include"),
               str(project / "tests/test_installer.c"),
               str(project / "components/tab5_installer/tab5_installer.c"), "-o", binary]
    if sys.platform != "darwin": command += ["-lcrypto"]
    subprocess.run(command, check=True)
    subprocess.run([binary, temporary], check=True)
