# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    CMakeLists.txt
# Purpose: Top-level build definition for Mirab.
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
cmake_minimum_required(VERSION 3.21)

project(Mirab
    VERSION 0.2.1
    DESCRIPTION "Unified modeling environment for water distribution and stormwater systems"
    HOMEPAGE_URL "https://github.com/ehsanMa86/Mirab"
    LANGUAGES C CXX)

set(MIRAB_VENDOR "Ehsan Madadi")
set(MIRAB_COPYRIGHT "Copyright (C) 2026 Ehsan Madadi")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(MIRAB_BUILD_GUI        "Build the Qt 6 desktop application" ON)
option(MIRAB_BUILD_TESTS      "Build the unit and integration tests" ON)
option(MIRAB_BUILD_ENGINES    "Fetch and build the bundled simulation engines" ON)
option(MIRAB_WITH_GEOPACKAGE  "Enable GeoPackage export (builds the bundled SQLite)" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

include(GNUInstallDirs)
include(MirabLayout)
include(MirabWarnings)

# Everything that runs lands in one folder of the build tree, so the
# application, the command-line tool and the engines find each other
# without installation.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(MIRAB_ENGINE_BUILD_DIR "${PROJECT_BINARY_DIR}/bin/engines")

configure_file(cmake/MirabConfig.h.in
    "${PROJECT_BINARY_DIR}/generated/mirab/MirabConfig.h" @ONLY)

if(MIRAB_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network PrintSupport OPTIONAL_COMPONENTS LinguistTools)
    qt_standard_project_setup()
endif()

add_subdirectory(third_party)
if(MIRAB_BUILD_ENGINES)
    add_subdirectory(engines)
endif()
add_subdirectory(src)

if(MIRAB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md CHANGELOG.md AUTHORS
    DESTINATION "${MIRAB_INSTALL_DOCDIR}")
install(DIRECTORY examples/ DESTINATION "${MIRAB_INSTALL_EXAMPLEDIR}")
install(DIRECTORY docs/ DESTINATION "${MIRAB_INSTALL_DOCDIR}")

include(MirabPackaging)
