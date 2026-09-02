cmake_minimum_required(VERSION 3.16)
project(XYOSDK VERSION 3.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FetchContent)

if(MSVC)
  add_compile_definitions(
    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
    _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS
  )
endif()

# ---------------------------------------------------------------------------
# Dependencies – cpr (C++ Requests / libcurl), nlohmann_json, ZLIB, OpenSSL
# ---------------------------------------------------------------------------
set(CPR_FORCE_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_USE_SYSTEM_GTEST OFF CACHE BOOL "" FORCE)

find_package(cpr CONFIG QUIET)
if(NOT cpr_FOUND)
  FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG        1.10.5
  )
  FetchContent_MakeAvailable(cpr)
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# SDK library
# ---------------------------------------------------------------------------
add_library(xyo_sdk src/client.cpp)
add_library(XYO::SDK ALIAS xyo_sdk)

target_compile_features(xyo_sdk PUBLIC cxx_std_17)

target_include_directories(xyo_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(xyo_sdk
  PRIVATE
    cpr::cpr
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
    OpenSSL::Crypto
)

if(MSVC)
  target_compile_options(xyo_sdk PRIVATE /W4 /permissive-)
else()
  target_compile_options(xyo_sdk PRIVATE -Wall -Wextra)
endif()

target_compile_definitions(xyo_sdk INTERFACE $<$<BOOL:${BUILD_SHARED_LIBS}>:XYO_SDK_SHARED>)

set_target_properties(xyo_sdk PROPERTIES
  VERSION     ${PROJECT_VERSION}
  SOVERSION   ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME SDK
  DEFINE_SYMBOL "XYO_SDK_EXPORTS"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(XYO_BUILD_TESTS "Build the XYO SDK tests" ON)
if(XYO_BUILD_TESTS)
  enable_testing()
  add_executable(xyo_sdk_tests tests/client_test.cpp)
  target_link_libraries(xyo_sdk_tests PRIVATE XYO::SDK nlohmann_json::nlohmann_json ZLIB::ZLIB)
  if(WIN32)
    target_link_libraries(xyo_sdk_tests PRIVATE ws2_32)
  endif()
  if(MSVC)
    target_compile_options(xyo_sdk_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(xyo_sdk_tests PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME xyo_sdk_tests COMMAND xyo_sdk_tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS xyo_sdk EXPORT XYOSDKTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XYOSDKTargets
  FILE      XYOSDKTargets.cmake
  NAMESPACE XYO::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/XYOSDK
)
configure_package_config_file(cmake/XYOSDKConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/XYOSDKConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/XYOSDK
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/XYOSDKConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/XYOSDKConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/XYOSDKConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/XYOSDK
)
