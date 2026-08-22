cmake_minimum_required(VERSION 3.15)
project(vcpkg_sample CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC AND VCPKG_TARGET_TRIPLET MATCHES "static" AND NOT VCPKG_TARGET_TRIPLET MATCHES "static-md")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(XYOSDK CONFIG REQUIRED)

add_executable(vcpkg_sample main.cpp)
target_link_libraries(vcpkg_sample PRIVATE XYO::SDK)
