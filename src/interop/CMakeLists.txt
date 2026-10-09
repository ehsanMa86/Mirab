# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
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
add_library(anahita_interop STATIC src/Gis.cpp src/Wntr.cpp
    src/Shapefile.cpp src/Dxf.cpp src/CsvImport.cpp src/ImportModel.cpp)
add_library(Anahita::interop ALIAS anahita_interop)
target_include_directories(anahita_interop PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(anahita_interop PUBLIC Anahita::core PRIVATE Anahita::warnings)
if(TARGET Anahita::sqlite)
    target_link_libraries(anahita_interop PRIVATE Anahita::sqlite)
    target_compile_definitions(anahita_interop PRIVATE ANAHITA_HAVE_SQLITE)
endif()
