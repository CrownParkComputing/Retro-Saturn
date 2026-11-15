cmake_minimum_required(VERSION 3.28)
project(RetroSaturn VERSION 2.2.0 LANGUAGES C CXX)
set(SATURN_APP_BUILD "6" CACHE STRING "Store build number")
set(SATURN_APPLE_TEAM "88A3T9K9C5" CACHE STRING "Existing Apple development team")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL3 3.2 REQUIRED CONFIG)
set(RETRO_SATURN_CORE_DIR "${CMAKE_SOURCE_DIR}/build/core-linux" CACHE PATH "Built native Saturn bridge")
if(APPLE)
  add_subdirectory(native/core)
else()
  find_library(RETRO_SATURN_CORE NAMES ymircore PATHS "${RETRO_SATURN_CORE_DIR}" NO_DEFAULT_PATH REQUIRED)
  add_library(ymircore SHARED IMPORTED)
  set_target_properties(ymircore PROPERTIES IMPORTED_LOCATION "${RETRO_SATURN_CORE}")
endif()
set(IMGUI "${CMAKE_SOURCE_DIR}/core/vendor/imgui/imgui")
file(GLOB FRONTEND CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/frontend/*.cpp")
add_executable(RetroSaturn MACOSX_BUNDLE ${FRONTEND}
  "${IMGUI}/imgui.cpp" "${IMGUI}/imgui_draw.cpp" "${IMGUI}/imgui_tables.cpp" "${IMGUI}/imgui_widgets.cpp"
  "${IMGUI}/backends/imgui_impl_sdl3.cpp" "${IMGUI}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(RetroSaturn PRIVATE frontend native/core/bridge core/vendor/stb/stb "${IMGUI}" "${IMGUI}/backends")
target_link_libraries(RetroSaturn PRIVATE SDL3::SDL3 ymircore)
target_compile_definitions(RetroSaturn PRIVATE "SATURN_APP_VERSION=\"${PROJECT_VERSION}\"" "SATURN_APP_BUILD=\"${SATURN_APP_BUILD}\"")
if(IOS)
  enable_language(OBJCXX)
  target_sources(RetroSaturn PRIVATE frontend/saturn_apple.mm apple/Assets.xcassets apple/PrivacyInfo.xcprivacy)
  set_source_files_properties(frontend/saturn_apple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(apple/Assets.xcassets apple/PrivacyInfo.xcprivacy PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_compile_definitions(RetroSaturn PRIVATE SATURN_IOS=1)
  target_link_libraries(RetroSaturn PRIVATE "-framework UIKit" "-framework UniformTypeIdentifiers" "-framework Foundation")
  set_target_properties(RetroSaturn PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/apple/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.crownparkcomputing.saturnretro"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.crownparkcomputing.saturnretro"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${SATURN_APPLE_TEAM}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_MARKETING_VERSION "${PROJECT_VERSION}"
    XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${SATURN_APP_BUILD}"
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
    XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING NO)
endif()
file(STRINGS "${CMAKE_SOURCE_DIR}/core/CMakeLists.txt" version_line REGEX "^project\\(Ymir VERSION")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" core_version "${version_line}")
execute_process(COMMAND git describe --tags --always WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/core" OUTPUT_VARIABLE core_desc OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git log -1 --format=%cs WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/core" OUTPUT_VARIABLE core_date OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(RetroSaturn PRIVATE "SATURN_CORE_VERSION=\"${core_version}\"" "SATURN_CORE_DESC=\"${core_desc}\"" "SATURN_CORE_DATE=\"${core_date}\"")
if(NOT APPLE)
  set_target_properties(RetroSaturn PROPERTIES BUILD_RPATH "$ORIGIN;${RETRO_SATURN_CORE_DIR}")
endif()
if(IOS)
  set(asset_dest "$<TARGET_BUNDLE_DIR:RetroSaturn>")
elseif(APPLE)
  set(asset_dest "$<TARGET_BUNDLE_CONTENT_DIR:RetroSaturn>/Resources")
else()
  set(asset_dest "$<TARGET_FILE_DIR:RetroSaturn>")
endif()
add_custom_command(TARGET RetroSaturn POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/frontend/assets" "${asset_dest}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/frontend/demo" "${asset_dest}/demo")

include(CTest)
if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
  add_executable(saturn_library_paths_test frontend/tests/library_paths_test.cpp
    frontend/saturn_config.cpp frontend/saturn_library.cpp frontend/saturn_setup.cpp
    frontend/saturn_artwork.cpp frontend/saturn_saf.cpp)
  target_include_directories(saturn_library_paths_test PRIVATE frontend)
  target_link_libraries(saturn_library_paths_test PRIVATE SDL3::SDL3)
  add_test(NAME saturn_library_paths COMMAND saturn_library_paths_test)
  add_executable(saturn_sdl_audio_test frontend/tests/sdl_audio_test.cpp native/core/bridge/audio_backend_sdl.cpp)
  target_include_directories(saturn_sdl_audio_test PRIVATE native/core/bridge frontend)
  target_link_libraries(saturn_sdl_audio_test PRIVATE SDL3::SDL3)
  add_test(NAME saturn_sdl_audio COMMAND saturn_sdl_audio_test)
  set_tests_properties(saturn_sdl_audio PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy" TIMEOUT 10)
endif()
