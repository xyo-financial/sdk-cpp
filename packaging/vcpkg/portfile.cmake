vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO xyo-financial/sdk-cpp
    REF "v${VERSION}"
    SHA512 56b8c7fb253469fd5ac35caf063c368525406e0749c577cdece2beafb91fd59d12f71950b1f56df19261e6fac0406baf15d2653152cd6ef330e4abaab0b66a3f
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

# Install license
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
