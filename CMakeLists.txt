cmake_minimum_required(VERSION 3.16)
project(vdj-ui VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Network)
    message(STATUS "Using Qt5 (install qt6-base-dev for Qt6)")
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Man page
# ---------------------------------------------------------------------------
install(DIRECTORY data/devices DESTINATION share/vdj-ui FILES_MATCHING PATTERN "*.device")
install(FILES data/default.adjmm data/xdj700.adjmm DESTINATION share/vdj-ui)
install(FILES doc/vdj-ui.1 DESTINATION share/man/man1)

# ---------------------------------------------------------------------------
# Packaging (deb)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
set(CPACK_PACKAGE_NAME "vdj-ui")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "VDJ")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt GUI for Virtual DJ - renders ProLink beat grids")
set(CPACK_PACKAGE_CONTACT "vdj <vdj@localhost>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qtbase5-dev, libcdj, libvdj")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "VDJ")
set(CPACK_GENERATOR "DEB")
set(CPACK_STRIP_FILES ON)
include(CPack)

# 'make deb' convenience target -> cpack -G DEB
add_custom_target(deb
    COMMAND ${CMAKE_CPACK_COMMAND} -G DEB -C ${CMAKE_BUILD_TYPE}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building deb package"
)
