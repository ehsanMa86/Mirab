# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    cmake/MirabPackaging.cmake
# Purpose: Installation of runtime dependencies and CPack configuration for Windows, macOS and Linux packages.
#
# Copyright (C) 2026 Ehsan Madadi
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# --- Qt runtime deployment ---------------------------------------------------
if(MIRAB_BUILD_GUI AND COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET Mirab
        OUTPUT_SCRIPT MIRAB_DEPLOY_SCRIPT
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${MIRAB_DEPLOY_SCRIPT}")
endif()

# --- macOS: re-sign the bundle after install-time rpath edits ---------------
if(APPLE AND MIRAB_BUILD_GUI)
    install(CODE [[
        execute_process(COMMAND codesign --force --deep --sign - "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Mirab.app"
                        RESULT_VARIABLE _sign)
        if(NOT _sign EQUAL 0)
            message(WARNING "ad-hoc code signing of Mirab.app failed")
        endif()
    ]])
endif()

# --- MinGW runtime (the GUI and plugins use the shared C++ runtime) ----------
if(MINGW AND MIRAB_BUILD_GUI)
    get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${_mingw_bin}/${_dll}")
            install(FILES "${_mingw_bin}/${_dll}" DESTINATION "${MIRAB_INSTALL_BINDIR}")
        endif()
    endforeach()
endif()

# --- Linux desktop integration -----------------------------------------------
if(UNIX AND NOT APPLE AND MIRAB_BUILD_GUI)
    # Qt 6.5+ needs libxcb-cursor for X11, which minimal distributions lack.
    # Ship it next to the Qt libraries (their RUNPATH is $ORIGIN).
    find_library(MIRAB_XCB_CURSOR NAMES libxcb-cursor.so.0 xcb-cursor)
    if(MIRAB_XCB_CURSOR)
        get_filename_component(_xcb_real "${MIRAB_XCB_CURSOR}" REALPATH)
        install(FILES "${_xcb_real}" DESTINATION "${CMAKE_INSTALL_LIBDIR}" RENAME libxcb-cursor.so.0)
    else()
        message(STATUS "libxcb-cursor not found; Linux packages will rely on the system copy")
    endif()
    install(FILES packaging/linux/io.github.ehsanma86.Mirab.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES packaging/linux/io.github.ehsanma86.Mirab.metainfo.xml
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
    install(FILES packaging/icons/mirab.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
    foreach(_size 16 32 48 64 128 256 512)
        install(FILES "packaging/icons/mirab-${_size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps"
            RENAME mirab.png)
    endforeach()
endif()

# --- CPack -------------------------------------------------------------------
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_PACKAGE_NAME "Mirab")
set(CPACK_PACKAGE_VENDOR "${MIRAB_VENDOR}")
set(CPACK_PACKAGE_CONTACT "Ehsan Madadi <https://github.com/ehsanMa86>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Mirab")
set(CPACK_PACKAGE_EXECUTABLES "Mirab;Mirab")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|x64)$")
    set(_arch "x64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(_arch "arm64")
else()
    set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    find_program(MIRAB_MAKENSIS makensis)
    if(MIRAB_MAKENSIS)
        list(APPEND CPACK_GENERATOR "NSIS")
    endif()
    set(CPACK_PACKAGE_FILE_NAME "Mirab-${PROJECT_VERSION}-windows-${_arch}")
    set(CPACK_NSIS_DISPLAY_NAME "Mirab ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "Mirab")
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/packaging/windows/mirab.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/packaging/windows/mirab.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\Mirab.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_HELP_LINK "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_MENU_LINKS "doc/README.md" "Read me" "${PROJECT_HOMEPAGE_URL}" "Mirab on GitHub")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\Mirab.lnk' '$INSTDIR\\bin\\Mirab.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\Mirab.lnk'")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "WriteRegStr HKCU 'Software\\Classes\\Applications\\Mirab.exe\\shell\\open\\command' '' '\"$INSTDIR\\bin\\Mirab.exe\" \"%1\"'")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "DeleteRegKey HKCU 'Software\\Classes\\Applications\\Mirab.exe'")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_FILE_NAME "Mirab-${PROJECT_VERSION}-macos-${_arch}")
    set(CPACK_DMG_VOLUME_NAME "Mirab ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_FILE_NAME "Mirab-${PROJECT_VERSION}-linux-${_arch}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
endif()

include(CPack)
