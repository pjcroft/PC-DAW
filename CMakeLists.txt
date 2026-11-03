cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# macOS: build a universal binary (Apple Silicon + Intel) by default.
# Must be set before project().
# ---------------------------------------------------------------------------
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

project(CroftDAW VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies: JUCE + Tracktion Engine, pinned to known-good commits.
# To use local checkouts instead (offline builds), configure with e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#   -DFETCHCONTENT_SOURCE_DIR_TRACKTION=/path/to/tracktion_engine
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7ff644f65d7daa1428c34b9738ebb5c449dc9172   # commit pinned by Tracktion Engine below
    GIT_SHALLOW    FALSE)

FetchContent_Declare(tracktion
    GIT_REPOSITORY https://github.com/Tracktion/tracktion_engine.git
    GIT_TAG        a5e44996f5f08dfa6af5f5d02d900fb3cfa37422   # develop, 2026-10-24 (v3.5.0)
    GIT_SHALLOW    FALSE
    GIT_SUBMODULES ""                                         # we supply JUCE ourselves
    SOURCE_SUBDIR  modules)                                   # only register the engine modules

FetchContent_MakeAvailable(juce tracktion)

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
# App icon (rendered from Source/Logo.h with: "Croft DAW" --render-icon assets/icon.png)
set(CROFT_ICON_ARGS "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
    set(CROFT_ICON_ARGS ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
endif()

juce_add_gui_app(CroftDAW
    ${CROFT_ICON_ARGS}
    PRODUCT_NAME                "Croft DAW"
    COMPANY_NAME                "Croft"
    BUNDLE_ID                   "com.croft.daw"
    VERSION                     ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "Croft DAW needs audio input access to record your instruments."
    NEEDS_CURL                  FALSE
    NEEDS_WEB_BROWSER           FALSE
    PLUGINHOST_AU               TRUE)

juce_generate_juce_header(CroftDAW)

target_sources(CroftDAW PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/TrackComponents.cpp
    Source/Session.cpp
    Source/MixerComponents.cpp)

target_include_directories(CroftDAW PRIVATE Source)

target_compile_definitions(CroftDAW PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(CroftDAW PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

set_target_properties(CroftDAW PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if(APPLE)
    target_compile_options(CroftDAW PRIVATE -fno-aligned-allocation)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(CroftDAW PRIVATE atomic)
    target_compile_definitions(CroftDAW PRIVATE JUCE_JACK=1)   # JACK / PipeWire-JACK support
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(CroftDAW PRIVATE -Wno-unknown-pragmas)
endif()
