cmake_minimum_required(VERSION 3.16)
# The single source of the version number is ./version (VERSION=x.y.z, also sourced by shell scripts like make-deb.sh).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version" VDJ_VERSION_LINE REGEX "^VERSION=")
string(REGEX REPLACE "^VERSION=" "" VDJ_VERSION "${VDJ_VERSION_LINE}")
if(NOT VDJ_VERSION MATCHES "^[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
    message(FATAL_ERROR "./version must contain VERSION=<major>.<minor>[.<patch>], found '${VDJ_VERSION_LINE}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/version")
project(vdj-ui VERSION ${VDJ_VERSION} LANGUAGES C CXX)
add_compile_definitions(VDJ_UI_VERSION="${VDJ_VERSION}")

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
add_subdirectory(plugins/xone-ladspa)

enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Man page
# ---------------------------------------------------------------------------
install(DIRECTORY data/devices DESTINATION share/vdj-ui FILES_MATCHING PATTERN "*.device")
install(FILES data/default.adjmm data/xdj700.adjmm DESTINATION share/vdj-ui)
configure_file(doc/vdj-ui.1.in ${CMAKE_CURRENT_BINARY_DIR}/vdj-ui.1 @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vdj-ui.1 DESTINATION share/man/man1)

# Logo: the About box loads share/vdj-ui/vdj-64x64.png; the hicolor icons (named after data/vdj-ui.desktop's Icon=)
# are for the desktop menu and window icon.
install(FILES img/vdj-64x64.png DESTINATION share/vdj-ui)
# The icons and the desktop file are their own component: the deb installs them with everything else, and `sudo make install-icons` installs
# only these (no binary, no dependencies), which is what a taskbar needs to show the right icon for a binary run from build/src.
foreach(size 16x16 22x22 24x24 32x32 64x64 128x128 256x256 512x512)
    install(FILES img/vdj-${size}.png DESTINATION share/icons/hicolor/${size}/apps RENAME vdj-ui.png COMPONENT icons)
endforeach()
install(FILES img/vdj.svg DESTINATION share/icons/hicolor/scalable/apps RENAME vdj-ui.svg COMPONENT icons)
install(FILES data/vdj-ui.desktop DESTINATION share/applications COMPONENT icons)
# trakcrate-ui: the trakcrate-only window. The same binary started through a symlink of that name (main.cpp looks at argv[0]); its own
# desktop file and icon give it its own place in the taskbar.
foreach(size 22x22 32x32 48x48 64x64 128x128)
    install(FILES img/trakcrate-${size}.png DESTINATION share/icons/hicolor/${size}/apps RENAME trakcrate-ui.png COMPONENT icons)
endforeach()
install(FILES data/trakcrate-ui.desktop DESTINATION share/applications COMPONENT icons)
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
    file(CREATE_LINK vdj-ui \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/trakcrate-ui\" SYMBOLIC)
")
install(CODE "
    if(NOT DEFINED ENV{DESTDIR} OR \"\$ENV{DESTDIR}\" STREQUAL \"\")
        find_program(_icon_cache gtk-update-icon-cache)
        if(_icon_cache)
            execute_process(COMMAND \${_icon_cache} -q -t -f \"\${CMAKE_INSTALL_PREFIX}/share/icons/hicolor\" OUTPUT_QUIET ERROR_QUIET)
        endif()
        find_program(_desktop_db update-desktop-database)
        if(_desktop_db)
            execute_process(COMMAND \${_desktop_db} -q \"\${CMAKE_INSTALL_PREFIX}/share/applications\" OUTPUT_QUIET ERROR_QUIET)
        endif()
    endif()
" COMPONENT icons)
add_custom_target(install-icons
    COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_COMPONENT=icons -P ${CMAKE_BINARY_DIR}/cmake_install.cmake
    COMMENT "Installing the hicolor icons and vdj-ui.desktop under ${CMAKE_INSTALL_PREFIX} (run as: sudo make install-icons)"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Packaging (deb)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
set(CPACK_PACKAGE_NAME "vdj-ui")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "VDJ")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Virtual DJ - for syncing midi drum machines to Pioneer decks")
set(CPACK_PACKAGE_CONTACT "vdj <vdj@localhost>")
# libvdj.so is shipped by the libcdj package; there is no separate libvdj package to depend on
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qtbase5-dev, libcdj, fonts-liberation, libcap2-bin")
# postinst: setcap cap_net_bind_service on the installed binary (UDP port 111)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/data/debian/postinst")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "VDJ")
set(CPACK_GENERATOR "DEB")
set(CPACK_STRIP_FILES ON)
include(CPack)

# 'make deb' convenience target -> cpack -G DEB
# -C needs a value: an unset CMAKE_BUILD_TYPE (single config generators without -DCMAKE_BUILD_TYPE) must leave it out
if(CMAKE_BUILD_TYPE)
    set(VDJ_CPACK_CONFIG -C ${CMAKE_BUILD_TYPE})
endif()
add_custom_target(deb
    COMMAND ${CMAKE_CPACK_COMMAND} -G DEB ${VDJ_CPACK_CONFIG}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building deb package"
)
