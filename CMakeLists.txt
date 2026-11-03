cmake_minimum_required(VERSION 3.24)

# These must be set before project(): CMake creates CMAKE_OSX_* as (empty) cache entries
# while configuring the compiler, and a later set(... CACHE ...) without FORCE is then a
# no-op. Left empty, clap-wrapper decides macOS is older than 10.15 and downloads a
# std::filesystem replacement -- which also makes the build need the network.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

project(bh-lab VERSION 0.1.0 LANGUAGES C CXX)

# The AUv2 wrapper compiles Objective-C++ sources.
if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All deps are git submodules pinned in .gitmodules:
#   deps/clap         -> free-audio/clap @ 1.2.10
#   deps/clap-wrapper -> free-audio/clap-wrapper @ v0.17.0
#   deps/AudioUnitSDK -> apple/AudioUnitSDK @ AudioUnitSDK-1.1.0
# (new clones need: git clone --recursive ...)
#
# Point the wrapper at the pinned SDKs instead of letting it fetch its own.
set(CLAP_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/clap" CACHE STRING "")
set(AUDIOUNIT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/AudioUnitSDK" CACHE STRING "")
add_subdirectory(deps/clap-wrapper EXCLUDE_FROM_ALL)

# All the plugin logic lives in a static library; the AU format links it and adds only
# its own entry-point translation unit.
add_library(bh-lab-impl STATIC
        src/chord_engine.cpp
        src/bhl_plugin.cpp)
target_include_directories(bh-lab-impl PUBLIC src)
target_link_libraries(bh-lab-impl PUBLIC clap clap-wrapper-extensions)

if(APPLE)
    target_sources(bh-lab-impl PRIVATE src/bhl_gui.mm)
    target_link_libraries(bh-lab-impl PUBLIC "-framework Cocoa")
endif()

# AUv2 only -- no other plugin format is built.
# The wrapper always builds the CLAP the AU embeds; only the AU is installed.
make_clapfirst_plugins(
        TARGET_NAME bh-lab
        IMPL_TARGET bh-lab-impl

        OUTPUT_NAME "bh-lab"
        ENTRY_SOURCE "src/bhl_entry.cpp"

        BUNDLE_IDENTIFIER "com.mayreh.bhlab"
        BUNDLE_VERSION ${PROJECT_VERSION}

        PLUGIN_FORMATS CLAP AUV2

        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bh-lab_assets

        # 'aumi' is kAudioUnitType_MIDIProcessor -- Logic's MIDI FX slot. The codes
        # below form the AU identity.
        AUV2_MANUFACTURER_NAME "mayreh"
        AUV2_MANUFACTURER_CODE "Mayh"
        AUV2_SUBTYPE_CODE "BhLb"
        AUV2_INSTRUMENT_TYPE "aumi"
)

# Installs the .component into ~/Library/Audio/Plug-Ins/Components after each build.
add_custom_command(TARGET bh-lab_auv2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/bh-lab_assets/bh-lab.component
                $ENV{HOME}/Library/Audio/Plug-Ins/Components/bh-lab.component)

enable_testing()
add_executable(chord-engine-test tests/chord_engine_test.cpp src/chord_engine.cpp)
target_include_directories(chord-engine-test PRIVATE src)
add_test(NAME chord-engine-test COMMAND chord-engine-test)

if(APPLE)
    # Renders the plugin view to a PNG so the UI can be checked without a host.
    add_executable(gui-preview tools/gui_preview.mm src/bhl_gui.mm src/chord_engine.cpp)
    target_include_directories(gui-preview PRIVATE src)
    target_link_libraries(gui-preview PRIVATE "-framework Cocoa")

    # Runs the plugin core against a synthetic host: the basics of chord triggering.
    add_executable(basics-test tools/basics_test.cpp)
    target_link_libraries(basics-test PRIVATE bh-lab-impl)
    add_test(NAME basics-test COMMAND basics-test)
endif()
