# Linux host build for the Ymir multiplatform bridge.
#
# Adds the repo-root CMakeLists (which builds libs/ymir-core via
# Ymir_LIBRARY_ONLY=ON), then links a single libymircore.so from the
# bridge sources + the ALSA audio backend. Native test executables
# prove the C ABI in isolation; the Flutter app loads the .so via
# DynamicLibrary.open on the absolute path.
cmake_minimum_required(VERSION 3.22)
project(ymircore_linux CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Threads REQUIRED)

# Pull in the upstream Ymir project as a subdirectory. Ymir_LIBRARY_ONLY
# restricts its build to libs/ymir-core — no apps/ymir-sdl3 GUI code.
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

# The shared library the Flutter FE will dlopen.
add_library(ymircore SHARED
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/ymir_bridge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/audio_backend.cpp")

target_compile_features(ymircore PRIVATE cxx_std_17)
target_include_directories(ymircore PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../bridge"
    "${YMIR_ROOT}/libs/ymir-core/include")
target_link_libraries(ymircore PRIVATE
    ymir-core
    Threads::Threads)
target_link_libraries(ymircore PRIVATE ${ALSA_LIBRARIES})
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
target_include_directories(ymircore PRIVATE ${ALSA_INCLUDE_DIRS})

set_target_properties(ymircore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ============================================================
#  Native test executables (host-side proof of the C ABI)
# ============================================================
# Each test links against the bridge's static equivalent so we don't
# have to dlopen our own .so. We do that by building a convenience
# static lib with the same sources.
add_library(ymircore_static STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/ymir_bridge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/audio_backend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/audio_backend_stub.cpp")
target_include_directories(ymircore_static PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../bridge"
    "${YMIR_ROOT}/libs/ymir-core/include")
target_compile_features(ymircore_static PRIVATE cxx_std_17)
target_link_libraries(ymircore_static PUBLIC ymir-core Threads::Threads fmt fmt-c)
target_link_libraries(ymircore_static PRIVATE ${ALSA_LIBRARIES})
target_include_directories(ymircore_static PRIVATE ${ALSA_INCLUDE_DIRS})

function(add_ymir_test NAME)
    add_executable(${NAME} "test/${NAME}.cpp")
    target_link_libraries(${NAME} PRIVATE ymircore_static Threads::Threads)
    target_include_directories(${NAME} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../bridge"
        "${YMIR_ROOT}/libs/ymir-core/include")
endfunction()

add_ymir_test(ymir_smoke_test)
add_ymir_test(ymir_peripheral_test)
add_ymir_test(ymir_swap_snapshot_test)
add_ymir_test(ymir_audio_tone_test)

set_target_properties(ymir_smoke_test ymir_peripheral_test
                      ymir_swap_snapshot_test ymir_audio_tone_test
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")