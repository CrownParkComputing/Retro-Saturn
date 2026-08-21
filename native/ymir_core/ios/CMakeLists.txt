# iOS device build for the Ymir multiplatform bridge.
#
# The counterpart to native/ymir_core/linux/CMakeLists.txt, and deliberately
# the same shape: pull in the repo-root CMakeLists (which builds
# libs/ymir-core under Ymir_LIBRARY_ONLY), then link ONE library from the
# bridge sources plus the platform's audio backend.
#
# This replaces the mobaiapp/iosbox Docker cross-build. That path needed a
# container AND an iOS SDK volume populated by hand from an Xcode extraction,
# which no CI runner has: the macOS runners have no Docker, and the Linux ones
# have no SDK. A Mac already carries everything needed, so the toolchain is
# just Xcode's, found through CMake's own iOS support.
cmake_minimum_required(VERSION 3.22)
project(ymircore_ios CXX C OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(YMIR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(Ymir_LIBRARY_ONLY         ON  CACHE BOOL "" FORCE)
set(Ymir_ENABLE_IPO           OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_TESTS         OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_SANDBOX       OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_YMDASM        OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_YMIR_HEADLESS OFF CACHE BOOL "" FORCE)
set(Ymir_ENABLE_YMIR_DBG      OFF CACHE BOOL "" FORCE)
set(Ymir_DEV_BUILD            OFF CACHE BOOL "" FORCE)
set(Ymir_FF_VIRTUA_GUN        ON  CACHE BOOL "" FORCE)
add_subdirectory("${YMIR_ROOT}" "${CMAKE_BINARY_DIR}/ymir-core-build")

# Built AS a framework rather than a bare dylib wrapped in one afterwards.
#
# An App Store bundle's Frameworks/ directory is expected to hold .framework
# wrappers; a loose .dylib there is what the old Swift runtime looked like,
# and Apple's validator then demands a SwiftSupport folder Xcode will not
# generate (rejection 90426). Letting CMake produce the framework means the
# Info.plist, the layout and the install_name are all consistent by
# construction instead of assembled by a shell script afterwards.
# audio_backend_ios.mm ONLY -- not audio_backend.cpp. Despite the plain name,
# that file is the ALSA backend for Linux; the Android build likewise takes
# only audio_backend_android.cpp. Compiling both would want alsa headers that
# do not exist here and define the same entry points twice.
add_library(YmirCore SHARED
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/ymir_bridge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../bridge/audio_backend_ios.mm")

target_compile_features(YmirCore PRIVATE cxx_std_17)
target_include_directories(YmirCore PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../bridge"
    "${YMIR_ROOT}/libs/ymir-core/include")
target_link_libraries(YmirCore PRIVATE ymir-core)

# CoreAudio for the backend; the rest are what AVAudioSession pulls in.
target_link_libraries(YmirCore PRIVATE
    "-framework AudioToolbox"
    "-framework AVFoundation"
    "-framework Foundation"
    "-framework CoreFoundation")

set_target_properties(YmirCore PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER com.crownpark.retrosaturn.YmirCore
    MACOSX_FRAMEWORK_BUNDLE_VERSION 1
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
    # Matches the Runner's own floor. Validation 90068 rejects an upload whose
    # embedded binaries declare a MinimumOSVersion below Apple's.
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    # Found via the app's @rpath at load time, out of Frameworks/.
    INSTALL_NAME_DIR "@rpath"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
