#!/bin/bash
set -euo pipefail
firmware="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
project="$(cd -- "$firmware/../../.." && pwd)"
export IDF_PATH="$project/.tools/esp-idf"
export IDF_TOOLS_PATH="$project/.tools/espressif"
export PATH="$IDF_TOOLS_PATH/python_env/idf5.5_py3.13_env/bin:$PATH"
for arg in "$@"; do
 if [[ "$arg" == flash || "$arg" == app-flash ]]; then
  printf '%s\n' 'Install USBDrive through CYD Console; its factory slot is reserved.' >&2
  exit 1
 fi
done
source "$IDF_PATH/export.sh"
cd "$firmware"
exec idf.py "$@"
