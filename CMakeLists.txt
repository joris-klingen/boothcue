cmake_minimum_required(VERSION 3.22)

project(BoothCue VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The plugin target (JUCE, VST3 + AU) is not here yet: see TODO.md, step 1.
# What exists is the plain-C++ core and its two regression nets.

find_package(ZLIB REQUIRED)

# ---- als-check ----------------------------------------------------------------
# The .als parser against the synthetic sets in tests/fixtures
# (tools/make_fixtures.py). Also the `--dump` side of tools/alsmuse_crosscheck.py.
add_executable(als-check
    tools/AlsCheck.cpp
    Source/AlsParser.cpp
)
target_include_directories(als-check PRIVATE Source)
target_link_libraries(als-check PRIVATE ZLIB::ZLIB)

# ---- song-check ---------------------------------------------------------------
# Lyric import and linking, cue placement, count-in, bar/beat, loop passes,
# tap / nudge. No JUCE, no clock.
add_executable(song-check
    tools/SongCheck.cpp
    Source/Song.cpp
    Source/LyricImport.cpp
)
target_include_directories(song-check PRIVATE Source)

enable_testing()
add_test(NAME als-parser COMMAND als-check "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME song-logic COMMAND song-check)
