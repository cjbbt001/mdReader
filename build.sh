#!/bin/sh
# Build MdReader.exe with the bundled w64devkit toolchain (no MSVC required).
# Usage: sh build.sh
set -e
cd "$(dirname "$0")"

TC="$(pwd)/.tools/w64devkit/w64devkit/bin"
if [ -d "$TC" ]; then PATH="$TC:$PATH"; fi
export PATH

mkdir -p build
obj=$(mktemp -d build/obj.XXXXXX)
trap 'rm -f "$obj/mdreader.res.o" "$obj/md4c.o" "$obj/entity.o"; rmdir "$obj"' EXIT

# md4c is C: build it with gcc so the C99 sources stay valid.
windres -I res res/mdreader.rc -o "$obj/mdreader.res.o"

gcc -std=c99 -O2 -ffunction-sections -fdata-sections -c third_party/md4c/md4c.c -o "$obj/md4c.o"
gcc -std=c99 -O2 -ffunction-sections -fdata-sections -c third_party/md4c/entity.c -o "$obj/entity.o"

g++ -std=c++20 -O2 -mwindows -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -Isrc -Ithird_party/md4c \
    src/main.cpp src/graphics.cpp src/layout.cpp src/doc.cpp src/editor.cpp src/files.cpp \
    src/images.cpp src/highlight.cpp src/chrome.cpp src/source.cpp src/preview.cpp \
    "$obj/md4c.o" "$obj/entity.o" "$obj/mdreader.res.o" \
    -o "${1:-build/MdReader.exe}" \
    -ld2d1 -ldwrite -limm32 -lwindowscodecs -lshlwapi -lole32 -lcomdlg32 -lshell32 -ldwmapi -luxtheme -luuid -lgdi32 -luser32 \
    -static -static-libgcc -Wl,--gc-sections -s

ls -l "${1:-build/MdReader.exe}"
