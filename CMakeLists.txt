cmake_minimum_required(VERSION 3.16)
project(XYOSDK VERSION 2.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Dependencies – cpprestsdk (casablanca) provides HTTP + JSON + pplx.
# libcurl is no longer used directly by the wrapper; cpprestsdk bundles its
# own transport layer. Boost is required by cpprestsdk (boost::optional etc.).
# ---------------------------------------------------------------------------
find_package(cpprestsdk REQUIRED)
find_package(Boost REQUIRED)
find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# OpenAPI-generated sources (output of: npx @openapitools/openapi-generator-cli
#   generate -i specs/openapi.yml -g cpp-restsdk -o cpp/openapi)
# ---------------------------------------------------------------------------
set(GENERATED_SOURCES
  openapi/src/ModelBase.cpp
  openapi/src/Object.cpp
  openapi/src/AnyType.cpp
  openapi/src/HttpContent.cpp
  openapi/src/JsonBody.cpp
  openapi/src/MultipartFormData.cpp
  openapi/src/ApiClient.cpp
  openapi/src/ApiConfiguration.cpp
  openapi/src/ApiException.cpp
  openapi/src/api/EnrichmentApi.cpp
  openapi/src/model/APIError.cpp
  openapi/src/model/EnrichTransactionCollectionResponse.cpp
  openapi/src/model/EnrichTransactions_request_inner.cpp
  openapi/src/model/EnrichmentCollectionStatusResponse.cpp
  openapi/src/model/EnrichmentRequest.cpp
  openapi/src/model/EnrichmentResponse.cpp
  openapi/src/model/ErrorResponse.cpp
)

# ---------------------------------------------------------------------------
# SDK library
# ---------------------------------------------------------------------------
add_library(xyo_sdk src/client.cpp ${GENERATED_SOURCES})
add_library(XYO::SDK ALIAS xyo_sdk)

target_compile_features(xyo_sdk PUBLIC cxx_std_17)

target_include_directories(xyo_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/openapi/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/openapi/include/XYOSDK>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(xyo_sdk
  PUBLIC
    cpprestsdk::cpprest
    Boost::boost
    ZLIB::ZLIB
)

set_target_properties(xyo_sdk PROPERTIES
  VERSION     ${PROJECT_VERSION}
  SOVERSION   ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME SDK
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(XYO_BUILD_TESTS "Build the XYO SDK tests" ON)
if(XYO_BUILD_TESTS)
  enable_testing()
  add_executable(xyo_sdk_tests tests/client_test.cpp)
  target_link_libraries(xyo_sdk_tests PRIVATE XYO::SDK)
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
