#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_BIN="/tmp/test_tab5_htmlviewer"

clang++ -std=c++20 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
    -I"${PROJECT_DIR}/firmware/main" \
    -I"${PROJECT_DIR}/firmware/main/fonts" \
    "${SCRIPT_DIR}/test_htmlviewer.cpp" \
    "${PROJECT_DIR}/firmware/main/html_core.cpp" \
    "${PROJECT_DIR}/firmware/main/html_fonts.cpp" \
    -o "${OUT_BIN}"

"${OUT_BIN}"
rm -f "${OUT_BIN}"
