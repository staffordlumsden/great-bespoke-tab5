#!/usr/bin/env bash
set -euo pipefail
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

python3 "$project/tests/check_terminal_contract.py"
python3 -m py_compile \
    "$project/tests/check_terminal_contract.py" \
    "$project/tools/package_sd_app.py"

mkdir -p "$project/tests/build-native"
"${CXX:-c++}" -std=c++23 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$project/tests/fakes" -I"$project/firmware/main" \
    "$project/firmware/main/hid_translate.cpp" \
    "$project/tests/hid_translate_test.cpp" \
    -o "$project/tests/build-native/hid_translate_test"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    "$project/tests/build-native/hid_translate_test"

"${CXX:-c++}" -std=c++23 -Wall -Wextra \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$project/firmware/components/term_core/include" \
    "$project/firmware/components/term_core/src/parser.cpp" \
    "$project/firmware/components/term_core/src/screen.cpp" \
    "$project/tests/scrollback_test.cpp" \
    -o "$project/tests/build-native/scrollback_test"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    "$project/tests/build-native/scrollback_test"

cmake -S "$project/upstream/host_test" -B "$project/upstream/build-host" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$project/upstream/build-host" --parallel
ctest --test-dir "$project/upstream/build-host" --output-on-failure
