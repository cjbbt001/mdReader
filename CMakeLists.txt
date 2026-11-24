cmake_minimum_required(VERSION 3.20)
project(mdreader LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_RELEASE "/O2")
set(CMAKE_CXX_FLAGS_RELEASE "/O2")

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
    src/chrome.cpp
    src/source.cpp
    src/images.cpp
    src/highlight.cpp)
target_link_libraries(MdReader PRIVATE md4c d2d1 dwrite ole32 comdlg32 shell32 dwmapi uxtheme imm32 windowscodecs shlwapi uuid)

if(MSVC)
    set_property(TARGET MdReader PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_compile_options(MdReader PRIVATE /utf-8)
    target_compile_options(md4c PRIVATE /utf-8)
endif()
