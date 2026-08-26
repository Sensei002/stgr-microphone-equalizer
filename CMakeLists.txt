cmake_minimum_required(VERSION 3.21)

project(STGRMicrophoneEqualizer
        VERSION 1.0.0
        DESCRIPTION "STGR Microphone Equalizer - system-level microphone audio processing"
        LANGUAGES CXX)

set(STGR_VERSION_STRING "${PROJECT_VERSION}")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "STGR Microphone Equalizer supports x64 (64-bit) builds only.")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "STGR Microphone Equalizer is a native Windows application (Windows 10/11 x64).")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "STGR Microphone Equalizer must be built with MSVC (Visual Studio 2019/2022, x64).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(STGR_BUILD_APO       "Build the STGR audio processing object (SFX APO DLL)" ON)
option(STGR_BUILD_GUI       "Build the STGR configuration GUI" ON)
option(STGR_BUILD_TRAY      "Build the STGR system tray application" ON)
option(STGR_BUILD_SERVER    "Build the STGR audio server (plugin bridge host)" ON)
option(STGR_BUILD_TESTS     "Build unit tests" ON)
option(STGR_BUILD_BENCH     "Build the performance benchmark" ON)
option(STGR_ENABLE_VST2     "Enable VST2 (VST 2.4 ABI) plugin hosting" ON)
option(STGR_ENABLE_VST3     "Enable VST3 plugin hosting (requires the VST3 SDK)" ON)
option(STGR_VST3_FETCH      "Fetch the VST3 SDK automatically when not found" ON)
set(STGR_VST3_SDK_DIR "" CACHE PATH "Path to an existing VST3 SDK checkout (overrides fetching)")

# ---------------------------------------------------------------------------
# Common flags
# ---------------------------------------------------------------------------
add_compile_options(/W3 /EHsc /bigobj /utf-8)
add_compile_options($<$<CONFIG:Release>:/O2>)
add_compile_options($<$<CONFIG:Release>:/Ob2>)
add_compile_options($<$<CONFIG:Release>:/Oi>)
add_compile_options($<$<CONFIG:Release>:/GL>)
add_compile_options($<$<CONFIG:Debug>:/Od>)
add_compile_options($<$<CONFIG:Debug>:/Zi>)
add_link_options($<$<CONFIG:Release>:/LTCG>)

# Strict floating-point semantics are not needed; allow fast math where safe.
add_compile_options($<$<CONFIG:Release>:/fp:fast>)
# Debug builds use /fp:precise to keep results deterministic for tests.
add_compile_options($<$<CONFIG:Debug>:/fp:precise>)

set(STGR_DEFINES
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/$<CONFIG>)

# ---------------------------------------------------------------------------
# VST3 SDK detection (used by the plugin host / audio server)
# ---------------------------------------------------------------------------
if(STGR_ENABLE_VST3)
    if(NOT STGR_VST3_SDK_DIR)
        find_path(STGR_VST3_SDK_DIR
            NAMES pluginterfaces/vst/ivstaudioprocessor.h
            HINTS "$ENV{VST3_SDK_DIR}" "${CMAKE_SOURCE_DIR}/third_party/vst3"
            DOC "Path to the VST3 SDK")
    endif()
    if(NOT STGR_VST3_SDK_DIR AND STGR_VST3_FETCH)
        message(STATUS "VST3 SDK not found - fetching (shallow clone of steinbergmedia/vst3sdk)")
        include(FetchContent)
        FetchContent_Declare(vst3sdk
            GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
            GIT_TAG        master
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE
            SOURCE_DIR     ${CMAKE_BINARY_DIR}/vst3sdk)
        FetchContent_MakeAvailable(vst3sdk)
        set(STGR_VST3_SDK_DIR ${vst3sdk_SOURCE_DIR})
    endif()
    if(STGR_VST3_SDK_DIR)
        message(STATUS "VST3 SDK: ${STGR_VST3_SDK_DIR}")
    else()
        message(WARNING "VST3 SDK not found - VST3 hosting will be compiled as a stub. "
                        "Set -DSTGR_VST3_SDK_DIR=... or enable STGR_VST3_FETCH.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src/common)
add_subdirectory(src/dsp)
add_subdirectory(src/config)
add_subdirectory(src/bridge)
add_subdirectory(src/devices)
add_subdirectory(src/plugins)
if(STGR_BUILD_APO)
    add_subdirectory(src/apo)
endif()
if(STGR_BUILD_SERVER)
    add_subdirectory(src/server)
endif()
if(STGR_BUILD_GUI)
    add_subdirectory(src/gui)
endif()
if(STGR_BUILD_TRAY)
    add_subdirectory(src/tray)
endif()
if(STGR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(STGR_BUILD_BENCH)
    add_subdirectory(tests/bench)
endif()
add_subdirectory(src/tools)

# ---------------------------------------------------------------------------
# Install rules (files land in the installer staging dir)
# ---------------------------------------------------------------------------
install(DIRECTORY assets/ DESTINATION assets)
