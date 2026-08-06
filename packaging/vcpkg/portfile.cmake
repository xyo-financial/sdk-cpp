vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO xyo-financial/sdk-cpp
    REF "v${VERSION}"
    SHA512 <SHA512_PLACEHOLDER> # Populated automatically by release workflow
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
