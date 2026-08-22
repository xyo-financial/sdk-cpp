cmake_minimum_required(VERSION 3.15)
project(vcpkg_sample CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(XYOSDK CONFIG REQUIRED)

add_executable(vcpkg_sample main.cpp)
target_link_libraries(vcpkg_sample PRIVATE XYO::SDK)
