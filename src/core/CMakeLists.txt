# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    src/core/CMakeLists.txt
# Purpose: Build definition of the UI-free core library.
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
add_library(mirab_core STATIC
    src/ControlRules.cpp
    src/Calibration.cpp
    src/Crs.cpp
    src/Editing.cpp
    src/Elevation.cpp
    src/Features.cpp
    src/Gif.cpp
    src/Geometry.cpp
    src/Hash.cpp
    src/Inflate.cpp
    src/InpDocument.cpp
    src/Json.cpp
    src/Linkage.cpp
    src/LiveResults.cpp
    src/Model.cpp
    src/ModelJson.cpp
    src/Network.cpp
    src/Objects.cpp
    src/Optimizer.cpp
    src/PrintLayout.cpp
    src/ProjectSettings.cpp
    src/Query.cpp
    src/Raster.cpp
    src/Results.cpp
    src/Schema.cpp
    src/Scenario.cpp
    src/SwmmObjects.cpp
    src/Text.cpp
    src/Units.cpp)
add_library(Mirab::core ALIAS mirab_core)
target_include_directories(mirab_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(mirab_core PRIVATE Mirab::warnings)
if(MINGW)
    target_link_libraries(mirab_core PUBLIC stdc++fs)
endif()
