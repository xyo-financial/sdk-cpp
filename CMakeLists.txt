cmake_minimum_required(VERSION 3.16)
project(xyo_sdk_cpp VERSION 1.1.1 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(CURL REQUIRED)

add_library(xyo_sdk src/client.cpp)
add_library(XYO::SDK ALIAS xyo_sdk)
target_compile_features(xyo_sdk PUBLIC cxx_std_17)
target_include_directories(xyo_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(xyo_sdk PUBLIC CURL::libcurl)
set_target_properties(xyo_sdk PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME SDK
)

option(XYO_BUILD_TESTS "Build the XYO SDK tests" ON)
if(XYO_BUILD_TESTS)
  enable_testing()
  add_executable(xyo_sdk_tests tests/client_test.cpp)
  target_link_libraries(xyo_sdk_tests PRIVATE XYO::SDK)
  add_test(NAME xyo_sdk_tests COMMAND xyo_sdk_tests)
endif()

install(TARGETS xyo_sdk EXPORT XYOSDKTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT XYOSDKTargets
  FILE XYOSDKTargets.cmake
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
