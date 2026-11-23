#!/bin/sh
# Build mdreader.exe with the bundled w64devkit toolchain (no MSVC required).
# Usage: sh build.sh
set -e
cd "$(dirname "$0")"

TC="$(pwd)/.tools/w64devkit/w64devkit/bin"
if [ -d "$TC" ]; then PATH="$TC:$PATH"; fi
export PATH

mkdir -p build

# md4c is C: build it with gcc so the C99 sources stay valid.
windres -I res res/mdreader.rc -o build/mdreader.res.o

gcc -std=c99 -O2 -c third_party/md4c/md4c.c -o build/md4c.o
gcc -std=c99 -O2 -c third_party/md4c/entity.c -o build/entity.o

g++ -std=c++20 -O2 -mwindows -fno-exceptions -fno-rtti \
    -Isrc -Ithird_party/md4c \
    src/main.cpp src/graphics.cpp src/layout.cpp src/doc.cpp \
    build/md4c.o build/entity.o build/mdreader.res.o \
    -o build/mdreader.exe \
    -ld2d1 -ldwrite -lole32 -lcomdlg32 -lshell32 -ldwmapi -luxtheme -luuid -lgdi32 -luser32 \
    -static -static-libgcc -s

ls -l build/mdreader.exe
