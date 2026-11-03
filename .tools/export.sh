#!/bin/zsh
# Portable Tab5 workspace ESP-IDF environment (zsh).
# Put machine-specific SDK paths in the ignored export.local.sh, or export
# IDF_PATH and IDF_TOOLS_PATH before calling the workspace wrapper.
TAB5_WORKSPACE_ROOT="${${(%):-%x}:A:h:h}"
if [[ -f "$TAB5_WORKSPACE_ROOT/.tools/export.local.sh" ]]; then
    . "$TAB5_WORKSPACE_ROOT/.tools/export.local.sh" || return 1
fi
export IDF_PATH="${IDF_PATH:-$TAB5_WORKSPACE_ROOT/.tools/esp-idf}"
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$TAB5_WORKSPACE_ROOT/.tools/espressif}"

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "error: ESP-IDF export script not found at $IDF_PATH/export.sh" >&2
    echo "Set IDF_PATH or copy .tools/export.local.sh.example to .tools/export.local.sh." >&2
    return 1
fi

. "$IDF_PATH/export.sh"
