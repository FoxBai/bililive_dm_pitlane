cmake_minimum_required(VERSION 3.25)

project(PitlaneDanmakuLite LANGUAGES CXX)

add_library(pitlane_lite_core STATIC
    src/MessagePipeline.cpp
    src/TextSanitizer.cpp
)

target_include_directories(pitlane_lite_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(pitlane_lite_core PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(pitlane_lite_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(pitlane_lite_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
