cmake_minimum_required(VERSION 3.25)

project(PitlaneDanmakuLite LANGUAGES CXX)

add_library(pitlane_lite_core STATIC
    src/BilibiliClient.cpp
    src/LocalObsServer.cpp
    src/MessagePipeline.cpp
    src/SettingsStore.cpp
    src/TextSanitizer.cpp
)

target_include_directories(pitlane_lite_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(pitlane_lite_core PUBLIC cxx_std_20)

option(PITLANE_LITE_REQUIRE_COMPRESSION "Require zlib and brotli for live danmaku decompression" ON)

find_package(unofficial-brotli CONFIG QUIET)
find_package(ZLIB QUIET)

if (unofficial-brotli_FOUND AND ZLIB_FOUND)
    target_compile_definitions(pitlane_lite_core PRIVATE PITLANE_LITE_HAS_COMPRESSION=1)
    target_link_libraries(pitlane_lite_core
        PRIVATE
            unofficial::brotli::brotlidec
            ZLIB::ZLIB
    )
elseif(PITLANE_LITE_REQUIRE_COMPRESSION)
    message(FATAL_ERROR "Pitlane lite requires zlib and brotli. Configure with the vcpkg toolchain, or pass -DPITLANE_LITE_REQUIRE_COMPRESSION=OFF for a receive-limited experimental build.")
endif()

if (MSVC)
    target_compile_options(pitlane_lite_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pitlane_lite_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pitlane_lite WIN32
    src/main.cpp
)

target_link_libraries(pitlane_lite PRIVATE
    pitlane_lite_core
    comctl32
    bcrypt
    gdiplus
    winhttp
    ws2_32
)

target_compile_features(pitlane_lite PRIVATE cxx_std_20)

if (MSVC)
    target_compile_options(pitlane_lite PRIVATE /W4 /permissive- /utf-8)
endif()
