# Android NDK build for the Ymir multiplatform bridge.
#
# Produces flutter_app/android/app/src/main/jniLibs/arm64-v8a/libymircore.so
# which Flutter's DynamicLibrary.open('libymircore.so') resolves at runtime.
#
# Mirrors the ViceMultiplatform android/CMakeLists.txt shape; the
# differences are ymir-core (no VICE-style --wrap=, just a clean C++
# subdirectory) and the bridge sources (ymir_bridge.cpp +
# audio_backend_android.cpp instead of alsa).
cmake_minimum_required(VERSION 3.22.1)
project(ymircore_android CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT ANDROID)
    message(FATAL_ERROR "This CMakeLists is for Android only. "
        "Use the host one at native/ymir_core/linux/CMakeLists.txt instead.")
endif()

find_package(Threads REQUIRED)

# Pull in ymir-core as a subproject. Ymir_LIBRARY_ONLY restricts the
# upstream build to libs/ymir-core — no apps/ymir-sdl3 GUI code.
set(YMIR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(Ymir_LIBRARY_ONLY        ON  CACHE BOOL "" FORCE)
set(Ymir_ENABLE_IPO          OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_TESTS        OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_SANDBOX      OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_YMDASM       OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_YMIR_HEADLESS OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_YMIR_DBG     OFF CACHE BOOL "" FORCE)
set(Ymir_DEV_BUILD OFF CACHE BOOL "" FORCE)
set(Ymir_FF_VIRTUA_GUN ON CACHE BOOL "" FORCE)
add_subdirectory("${YMIR_ROOT}" "${CMAKE_BINARY_DIR}/ymir-core-build")

add_library(ymircore SHARED
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/ymir_bridge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/savestate_io.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/audio_backend_android.cpp")

target_compile_features(ymircore PRIVATE cxx_std_17)
target_include_directories(ymircore PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../bridge"
    "${YMIR_ROOT}/libs/ymir-core/include")

find_library(AAUDIO_LIB aaudio)
find_library(ANDROID_LIB android)
find_library(LOG_LIB log)

target_link_libraries(ymircore PRIVATE
    ymir-core
    Threads::Threads
    ${AAUDIO_LIB}
    ${ANDROID_LIB}
    ${LOG_LIB})

set_target_properties(ymircore PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")