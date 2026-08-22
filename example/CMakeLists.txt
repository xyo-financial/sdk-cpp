cmake_minimum_required(VERSION 3.16)
project(xyo_sdk_example LANGUAGES CXX)

# We expect the package to be made available via CMake Config (e.g. from Conan or vcpkg)
find_package(XYOSDK CONFIG REQUIRED)

add_executable(xyo_example main.cpp)
target_link_libraries(xyo_example PRIVATE XYO::SDK)
