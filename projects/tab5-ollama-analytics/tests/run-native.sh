#!/usr/bin/env bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$project/../.." && pwd)"
output="$project/artifacts/native"
mkdir -p "$output"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/tab5-ollama-native.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT
"${CC:-cc}" -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined -g \
    -c "$project/tests/third_party/cjson/cJSON.c" -o "$temporary/json.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g \
    -I "$project/firmware/main" -I "$root/projects/tab5-console/main/fonts" \
    -I "$project/tests/third_party/cjson" \
    "$project/tests/test_models.cpp" "$project/firmware/main/ollama_analytics.cpp" \
    "$project/firmware/main/cyd_usage/usage_model.cpp" "$project/firmware/main/ui.cpp" \
    "$temporary/json.o" -o "$output/check"
"$output/check" "$project/tests/fixtures" "$output"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I "$project/tests/fakes" \
    -I "$project/firmware/main" -I "$project/tests/third_party/cjson" \
    "$project/tests/test_http.cpp" "$project/firmware/main/ollama_http.cpp" \
    "$project/firmware/main/ollama_analytics.cpp" "$project/firmware/main/cyd_usage/usage_model.cpp" \
    "$temporary/json.o" -o "$output/check-http"
"$output/check-http" "$project/tests/fixtures"
