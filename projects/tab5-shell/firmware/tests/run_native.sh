#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CXX=${CXX:-clang++}
OUTPUT=${TMPDIR:-/tmp}/tab5-shell-tests

"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$HERE/../main" \
    "$HERE/test_shell.cpp" \
    "$HERE/../main/shell_core.cpp" \
    "$HERE/../main/file_store.cpp" \
    "$HERE/../main/line_editor.cpp" \
    -o "$OUTPUT"

"$OUTPUT"

"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$HERE/../main" "$HERE/test_documents.cpp" \
    "$HERE/../main/shell_core.cpp" "$HERE/../main/file_store.cpp" \
    "$HERE/../main/document_mode.cpp" -o "$OUTPUT-documents"
"$OUTPUT-documents"

"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$HERE/../main" "$HERE/test_session.cpp" \
    "$HERE/../main/shell_core.cpp" "$HERE/../main/file_store.cpp" \
    "$HERE/../main/session.cpp" -o "$OUTPUT-session"
"$OUTPUT-session"

LUA_DIR="$HERE/../components/lua54/src"
LUA_BUILD=$(mktemp -d "${TMPDIR:-/tmp}/tab5-lua-tests.XXXXXX")
trap 'rm -rf "$LUA_BUILD"' EXIT INT TERM
for source in "$LUA_DIR"/*.c; do
    "${CC:-clang}" -std=c99 -O1 -g -fsanitize=address,undefined \
        -fno-omit-frame-pointer -DLUAI_MAXCCALLS=24 -I"$LUA_DIR" -c "$source" \
        -o "$LUA_BUILD/$(basename "$source" .c).o"
done
"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$HERE/../main" -I"$LUA_DIR" "$HERE/test_packages.cpp" \
    "$HERE/../main/shell_core.cpp" "$HERE/../main/file_store.cpp" \
    "$HERE/../main/package_runtime.cpp" "$HERE/../main/lua_runtime.cpp" \
    "$LUA_BUILD"/*.o -o "$OUTPUT-packages"
"$OUTPUT-packages"

TERM_DIR="$HERE/../../../tab5-terminal/firmware/components/term_core"
"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$HERE/../main" -I"$TERM_DIR/include" "$HERE/test_viewport.cpp" \
    "$HERE/../main/shell_core.cpp" "$HERE/../main/file_store.cpp" \
    "$HERE/../main/document_mode.cpp" "$TERM_DIR/src/terminal.cpp" \
    "$TERM_DIR/src/screen.cpp" "$TERM_DIR/src/parser.cpp" -o "$OUTPUT-viewport"
"$OUTPUT-viewport"

"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$HERE/fakes" -I"$HERE/../main" \
    -I"$HERE/../../../tab5-quicknote/firmware/main/fonts" -I"$TERM_DIR/include" \
    "$HERE/test_display_startup.cpp" "$HERE/../main/shell_display.cpp" \
    "$TERM_DIR/src/terminal.cpp" "$TERM_DIR/src/screen.cpp" \
    "$TERM_DIR/src/parser.cpp" -o "$OUTPUT-display-startup"
"$OUTPUT-display-startup"

"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$HERE/../main" -I"$TERM_DIR/include" "$HERE/test_scrollback.cpp" \
    "$HERE/../main/scrollback.cpp" "$HERE/../main/shell_core.cpp" \
    "$HERE/../main/file_store.cpp" "$TERM_DIR/src/terminal.cpp" \
    "$TERM_DIR/src/screen.cpp" "$TERM_DIR/src/parser.cpp" -o "$OUTPUT-scrollback"
"$OUTPUT-scrollback"
