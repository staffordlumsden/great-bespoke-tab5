#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
clang++ -std=c++20 -Wall -Wextra -I../firmware/main test_quicknote.cpp ../firmware/main/note_model.cpp -o test_quicknote
./test_quicknote
rm -f test_quicknote
