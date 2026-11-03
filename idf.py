#!/bin/zsh
# Workspace wrapper for ESP-IDF idf.py.
# Sources the shared environment and delegates to the real idf.py.

SCRIPT_DIR="${0:A:h}"
. "$SCRIPT_DIR/.tools/export.sh" || exit 1
exec "$IDF_PATH/tools/idf.py" "$@"
