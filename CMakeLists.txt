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
    Source/Prompter.cpp
    Source/Chords.cpp
    Source/SetChords.cpp
    Source/ChordChart.cpp
    Source/QrCode.cpp
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

    # An audio EFFECT that passes its input through untouched, so it can sit on
    # any track (the vocal track itself, typically). It only reads the host's
    # playhead. VST3 only (no AU); the Standalone runs the display without a
    # host (no transport: it shows "no host position").
    juce_add_plugin(BoothCue
        COMPANY_NAME                "Joris Klingen"
        BUNDLE_ID                   "com.jorisklingen.boothcue"
        PLUGIN_MANUFACTURER_CODE    Jokl
        PLUGIN_CODE                 Bcue
        FORMATS                     VST3 Standalone
        PRODUCT_NAME                "BoothCue"
        DESCRIPTION                 "Lyrics and song position for the singer while tracking"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${BOOTHCUE_COPY_PLUGIN}
        VST3_CATEGORIES             Fx Tools
    )

    # JUCE signs the bundles ad hoc before its later post-build steps (the VST3's
    # moduleinfo.json is written after signing), so the built bundles fail
    # `codesign --verify`. Run JUCE's own check once more as the last step: it
    # re-signs ad hoc only when the signature is invalid. (The optional
    # BOOTHCUE_COPY_PLUGIN copy runs before this step; install.command does not.)
    # boothcue-chart goes into the app, so the songbook finds the detector
    # wherever BoothCue is installed. Before the signing check below: the copy
    # changes the bundle.
    if(APPLE)
        add_dependencies(BoothCue_Standalone boothcue-chart)
        add_custom_command(TARGET BoothCue_Standalone POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:boothcue-chart>"
                    "$<TARGET_BUNDLE_DIR:BoothCue_Standalone>/Contents/Resources/boothcue-chart"
            VERBATIM)
    endif()

    if(APPLE)
        foreach(bundle_target BoothCue_VST3 BoothCue_Standalone)
            add_custom_command(TARGET ${bundle_target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" "-Dsrc=$<TARGET_BUNDLE_DIR:${bundle_target}>"
                        -P "${JUCE_CMAKE_UTILS_DIR}/checkBundleSigning.cmake"
                VERBATIM)
        endforeach()
    endif()

    target_sources(BoothCue PRIVATE
        Source/LiveFeed.cpp
        Source/PhonesPanel.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PrompterPainter.cpp
        Source/PrompterView.cpp
        Source/DisplayWindow.cpp
        Source/SetWatcher.cpp
        Source/SongLibrary.cpp
        Source/ChordMidi.cpp
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

    # ---- prompter-render --------------------------------------------------------
    # Draws the singer's display for a set of scenes through the REAL
    # PrompterPainter into a PNG (default previews/prompter-sheet.png): review
    # the display without a host. Also the `prompter-render` CTest.
    juce_add_console_app(prompter-render PRODUCT_NAME "prompter-render")
    target_sources(prompter-render PRIVATE
        tools/PrompterRender.cpp
        Source/PrompterPainter.cpp
        Source/ChordMidi.cpp
    )
    target_include_directories(prompter-render PRIVATE Source)
    target_compile_definitions(prompter-render PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(prompter-render PRIVATE
        boothcue-core
        juce::juce_audio_basics
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    # ---- watch-check ------------------------------------------------------------
    # The real SetWatcher on copies of the fixtures: link, save, damaged save,
    # delete / reappear, re-point. The `set-watcher` CTest.
    juce_add_console_app(watch-check PRODUCT_NAME "watch-check")
    target_sources(watch-check PRIVATE
        tools/WatchCheck.cpp
        Source/SetWatcher.cpp
    )
    target_include_directories(watch-check PRIVATE Source)
    target_link_libraries(watch-check PRIVATE
        boothcue-core
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    # ---- plugin-smoke -----------------------------------------------------------
    # The real processor without a host: pass-through, playhead, display window,
    # editor, state. Needs a desktop session (Linux: Xvfb + a window manager).
    # The `plugin-smoke` CTest.
    add_executable(plugin-smoke tools/PluginSmoke.cpp)
    target_include_directories(plugin-smoke PRIVATE Source)
    target_compile_definitions(plugin-smoke PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(plugin-smoke PRIVATE
        BoothCue
        juce::juce_audio_processors
        juce::juce_gui_basics
    )
endif()

# ---- checks ---------------------------------------------------------------------
# als-check: the .als parser against the synthetic sets in tests/fixtures
# (tools/make_fixtures.py). Also the `--dump` side of tools/alsmuse_crosscheck.py.
add_executable(als-check tools/AlsCheck.cpp)
target_link_libraries(als-check PRIVATE boothcue-core)

# boothcue-chart: a set's parts and detected chords as JSON, for the songbook
# (bovenburen_manager runs it). On the Mac it is also copied into BoothCue.app.
add_executable(boothcue-chart tools/ChartTool.cpp)
target_link_libraries(boothcue-chart PRIVATE boothcue-core)

# qr-check: the QR encoder, read back by a decoder written from the standard
# (format / version BCH, zig-zag, Reed-Solomon syndromes, byte mode).
add_executable(qr-check tools/QrCheck.cpp)
target_link_libraries(qr-check PRIVATE boothcue-core)

# song-check: lyric import and linking, cue placement, count-in, bar/beat, loop
# passes, tap / nudge, the transport hand-off. No JUCE, no clock.
add_executable(song-check tools/SongCheck.cpp)
find_package(Threads REQUIRED)   # the transport hand-off is checked across two threads
target_link_libraries(song-check PRIVATE boothcue-core Threads::Threads)

# chord-check: the chord detector on the Bovenburen chord charts played as
# synthetic MIDI (arpeggios, passing / grace notes, suspensions, a melody on
# top, a walking bass, inversions, humanised timing). --explain prints per beat.
add_executable(chord-check tools/ChordCheck.cpp)
target_link_libraries(chord-check PRIVATE boothcue-core)

enable_testing()
add_test(NAME als-parser COMMAND als-check "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME chord-detect COMMAND chord-check)
add_test(NAME qr-code COMMAND qr-check)
add_test(NAME chart-tool COMMAND boothcue-chart "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/notes.als")
set_tests_properties(chart-tool PROPERTIES PASS_REGULAR_EXPRESSION "\"parts\": \\[\\{\"name\".*\"chart\": \"# BoothCue chords")
add_test(NAME song-logic COMMAND song-check "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lyrics")
if(BOOTHCUE_BUILD_PLUGIN)
    add_test(NAME prompter-render
             COMMAND prompter-render "${CMAKE_CURRENT_BINARY_DIR}/prompter-sheet.png")
    add_test(NAME set-watcher COMMAND watch-check "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME plugin-smoke COMMAND plugin-smoke "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
endif()
