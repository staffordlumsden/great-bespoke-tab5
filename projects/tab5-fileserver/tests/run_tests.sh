#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_fileserver.cpp ../firmware/main/fs_core.cpp -o /tmp/test_tab5_fileserver
/tmp/test_tab5_fileserver
rm -f /tmp/test_tab5_fileserver
