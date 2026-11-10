cmake_minimum_required(VERSION 3.22)

project(BoothCue VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

option(BOOTHCUE_BUILD_PLUGIN "Build the JUCE plugin (off: only the plain-C++ core and its checks)" ON)
option(BOOTHCUE_COPY_PLUGIN  "Install the built plugins into the system plugin folders" OFF)

# The .als parser inflates with the system zlib. JUCE carries its own copy, but
# inside a C++ namespace, so the two never clash.
find_package(ZLIB REQUIRED)

# ---- the plain-C++ core -----------------------------------------------------
# No JUCE: the parser, the song model and the lyric import are shared by the
# plugin and the checks below.
add_library(boothcue-core STATIC
    Source/AlsParser.cpp
    Source/Song.cpp
    Source/LyricImport.cpp
)
target_include_directories(boothcue-core PUBLIC Source)
target_link_libraries(boothcue-core PUBLIC ZLIB::ZLIB)
set_target_properties(boothcue-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- the plugin ---------------------------------------------------------------
if(BOOTHCUE_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    # AU only exists on macOS. The Standalone runs the display without a host
    # (no transport: it shows "no host position").
    set(BOOTHCUE_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND BOOTHCUE_FORMATS AU)
    endif()

    # An audio EFFECT that passes its input through untouched, so it can sit on
    # any track (the vocal track itself, typically). It only reads the host's
    # playhead.
    juce_add_plugin(BoothCue
        COMPANY_NAME                "Joris Klingen"
        BUNDLE_ID                   "com.jorisklingen.boothcue"
        PLUGIN_MANUFACTURER_CODE    Jokl
        PLUGIN_CODE                 Bcue
        FORMATS                     ${BOOTHCUE_FORMATS}
        PRODUCT_NAME                "BoothCue"
        DESCRIPTION                 "Lyrics and song position for the singer while tracking"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${BOOTHCUE_COPY_PLUGIN}
        VST3_CATEGORIES             Fx Tools
        AU_MAIN_TYPE                kAudioUnitType_Effect
    )

    target_sources(BoothCue PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    )

    target_include_directories(BoothCue PRIVATE Source)

    target_compile_definitions(BoothCue PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(BoothCue
        PRIVATE
            boothcue-core
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endif()

# ---- checks ---------------------------------------------------------------------
# als-check: the .als parser against the synthetic sets in tests/fixtures
# (tools/make_fixtures.py). Also the `--dump` side of tools/alsmuse_crosscheck.py.
add_executable(als-check tools/AlsCheck.cpp)
target_link_libraries(als-check PRIVATE boothcue-core)

# song-check: lyric import and linking, cue placement, count-in, bar/beat, loop
# passes, tap / nudge, the transport hand-off. No JUCE, no clock.
add_executable(song-check tools/SongCheck.cpp)
target_link_libraries(song-check PRIVATE boothcue-core)

enable_testing()
add_test(NAME als-parser COMMAND als-check "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME song-logic COMMAND song-check)
