cmake_minimum_required(VERSION 3.28)
project(LiveZoomWin LANGUAGES CXX)
enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(poc)
add_subdirectory(tests)
