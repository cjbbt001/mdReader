cmake_minimum_required(VERSION 3.20)
project(mdreader LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(md4c STATIC
    third_party/md4c/md4c.c
    third_party/md4c/entity.c)
target_include_directories(md4c PUBLIC third_party/md4c)

add_executable(MdReader WIN32
    src/main.cpp
    src/graphics.cpp
    src/layout.cpp
    src/doc.cpp
    src/editor.cpp
    src/files.cpp
    src/chrome.cpp
    src/source.cpp
    src/preview.cpp
    src/images.cpp
    src/highlight.cpp
    res/mdreader.rc)
target_include_directories(MdReader PRIVATE res)
target_link_libraries(MdReader PRIVATE md4c d2d1 dwrite ole32 comdlg32 shell32 dwmapi uxtheme imm32 windowscodecs shlwapi uuid gdi32 user32)
if(MINGW)
    target_link_options(MdReader PRIVATE -static -static-libgcc)
endif()

if(MSVC)
    set_property(TARGET MdReader md4c PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(MdReader PRIVATE /utf-8)
    target_compile_options(md4c PRIVATE /utf-8)
    target_link_options(MdReader PRIVATE /MANIFEST:NO)
endif()
