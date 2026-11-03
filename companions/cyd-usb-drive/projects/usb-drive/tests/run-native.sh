#!/bin/bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$project/../.." && pwd)"
output="$project/artifacts/native"
mkdir -p "$output"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I "$project/firmware/main" -I "$root/projects/launcher/firmware/main" \
  "$project/tests/test_ui_path.cpp" "$project/firmware/main/drive_path.cpp" \
  "$project/firmware/main/ui.cpp" -o "$output/check"
"$output/check" "$output"
