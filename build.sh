#!/bin/sh
# LineCursor - Windows x64 build with llvm-mingw (same toolchain as keypad-timer)
set -eu
cd "$(dirname "$0")"
mkdir -p build
MINGW="${MINGW:-/home/box/tools/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64}"
export PATH="$MINGW/bin:$PATH"
CC=x86_64-w64-mingw32-gcc
WINDRES=x86_64-w64-mingw32-windres
CFLAGS="-Os -Wall -Wextra -Werror -municode -mwindows -s -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
$WINDRES -O coff -I src -i src/app.rc -o build/app.res.o
$CC $CFLAGS -Wl,--gc-sections -static-libgcc -o build/LineCursor.exe src/main.c build/app.res.o -luser32 -lgdi32 -lcomctl32
rm -f build/app.res.o
ls -l build/LineCursor.exe
