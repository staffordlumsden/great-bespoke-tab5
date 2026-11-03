#!/usr/bin/env bash
set -euo pipefail
firmware="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd -- "$firmware/../../.." && pwd)"
for action in "$@"; do
    case "$action" in
        flash|app-flash|bootloader-flash|partition-table-flash|erase-flash|erase_flash)
            echo "Ollama Analytics is an SD app: package it and launch it from Console." >&2
            exit 2
            ;;
    esac
done
cd "$firmware"
exec "$root/idf.py" "$@"
