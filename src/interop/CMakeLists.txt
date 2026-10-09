# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    src/interop/CMakeLists.txt
# Purpose: Build definition of the GIS and WNTR interchange library.
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
add_library(mirab_interop STATIC src/Gis.cpp src/Wntr.cpp
    src/Shapefile.cpp src/Dxf.cpp src/CsvImport.cpp src/ImportModel.cpp)
add_library(Mirab::interop ALIAS mirab_interop)
target_include_directories(mirab_interop PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(mirab_interop PUBLIC Mirab::core PRIVATE Mirab::warnings)
if(TARGET Mirab::sqlite)
    target_link_libraries(mirab_interop PRIVATE Mirab::sqlite)
    target_compile_definitions(mirab_interop PRIVATE MIRAB_HAVE_SQLITE)
endif()
