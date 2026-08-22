vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO xyo-financial/sdk-cpp
    REF "v${VERSION}"
    SHA512 6e178bbc8699aa7ec3602c5ff096ffa4b059d9184d14c0dc9c4b4fd3d5c6d17d9d53123b377b1bf3df57fb8afe0565e71636543b58a37703c4df70e2b424bdec
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DXYO_BUILD_TESTS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME XYOSDK CONFIG_PATH lib/cmake/XYOSDK)

# Remove redundant duplicate files in debug build
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# Install license and usage
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
