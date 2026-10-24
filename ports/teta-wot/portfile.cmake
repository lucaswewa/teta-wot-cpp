# teta-wot-cpp as a vcpkg overlay port: it builds the checkout it lives in,
#
#     vcpkg install teta-wot --overlay-ports=<checkout>/ports
#
# or, in a manifest, `"dependencies": ["teta-wot"]` with the overlay in
# vcpkg-configuration.json's `overlay-ports`. Check out the release tag
# (`v0.1.0`) to build a release.
set(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../..")

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        docs-offline TETA_WOT_DOCS_OFFLINE
        mdns TETA_WOT_MDNS
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DTETA_WOT_BUILD_TESTS=OFF
        -DTETA_WOT_BUILD_EXAMPLES=OFF
        -DTETA_WOT_BUILD_FUZZ=OFF
        -DTETA_WOT_BUILD_TOOLS=OFF
        -DTETA_WOT_TD_VALIDATION=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME teta-wot CONFIG_PATH lib/cmake/teta-wot)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/THIRD_PARTY_NOTICES.md")
