#!/usr/bin/env bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
output="$project/artifacts/native"
mkdir -p "$output"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/tab5-weather-native.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT

"${CC:-cc}" -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -c "$project/tests/third_party/cjson/cJSON.c" -o "$temporary/json.o"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g \
  -I "$project/firmware/main" -I "$project/firmware/main/fonts" \
  -I "$project/tests/third_party/cjson" \
  "$project/tests/test_weather.cpp" "$project/firmware/main/weather_model.cpp" \
  "$project/firmware/main/ui.cpp" "$temporary/json.o" -o "$output/check"
"$output/check" "$project/tests/fixtures" "$output"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g \
  -I "$project/tests/fakes" -I "$project/firmware/main" \
  -I "$project/tests/third_party/cjson" \
  "$project/tests/test_http.cpp" "$project/firmware/main/weather_model.cpp" \
  "$temporary/json.o" -o "$output/check-http"
"$output/check-http" "$project/tests/fixtures"

python3 "$project/tests/test_sd_contract.py"
