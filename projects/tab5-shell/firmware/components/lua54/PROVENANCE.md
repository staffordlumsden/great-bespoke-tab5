# Lua 5.4.8

Source: https://www.lua.org/ftp/lua-5.4.8.tar.gz

Archive SHA-256: `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`,
matched to the hash published at https://www.lua.org/ftp/ on 1 November 2026.

Unmodified `src/*.c` and `src/*.h`, excluding standalone executables `lua.c`,
`luac.c`. `LICENSE.html` is the upstream `doc/readme.html` with the MIT notice.
The component CMakeLists is local integration. No system/dynamic loader libraries
are opened by Shell. See the project README for resource and trust boundaries.

Local CMake sets `LUAI_MAXCCALLS=24` (upstream-supported build macro) to bound
nested C-library callbacks; the host suite uses the same setting. Shell's C++
adapter supplies missing newlib C++ long-long limits macros from compiler
constants, preserving the upstream 64-bit Lua integer ABI.
