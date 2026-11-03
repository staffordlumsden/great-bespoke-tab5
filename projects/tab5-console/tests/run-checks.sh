#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-native
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I main tests/check_power_layout.cpp -o build-native/check_power_layout
build-native/check_power_layout
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I main tests/check_matrix_model.cpp -o build-native/check_matrix_model
build-native/check_matrix_model
python3 tests/check_app_boot.py
python3 tests/check_weather_snapshot.py
python3 tests/check_installer_runtime.py
python3 tests/check_installer.py
python3 tests/check_installer_ui.py
echo "PASS: all Console host checks"
