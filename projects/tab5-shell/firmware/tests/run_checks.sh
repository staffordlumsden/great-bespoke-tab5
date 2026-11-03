#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
"$HERE/run_native.sh"
python3 "$HERE/check_contract.py"
python3 -m py_compile "$HERE/../../tools/package_sd_app.py" "$HERE/../../tools/validate_release.py"
printf '%s\n' 'PASS tab5-shell checks'
