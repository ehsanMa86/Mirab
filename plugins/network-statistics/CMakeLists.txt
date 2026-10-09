# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
#
# File:    plugins/network-statistics/CMakeLists.txt
# Purpose: Build definition of the network statistics example plugin.
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
qt_add_plugin(anahita_network_statistics CLASS_NAME NetworkStatisticsPlugin NetworkStatisticsPlugin.cpp)
target_include_directories(anahita_network_statistics PRIVATE "${PROJECT_SOURCE_DIR}/src/app/include")
target_link_libraries(anahita_network_statistics PRIVATE Qt6::Widgets Anahita::core)
set_target_properties(anahita_network_statistics PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/plugins")
if(WIN32)
    set(ANAHITA_PLUGIN_DIR "bin/plugins")
elseif(APPLE)
    set(ANAHITA_PLUGIN_DIR "Anahita.app/Contents/PlugIns/anahita")
else()
    set(ANAHITA_PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/anahita/plugins")
endif()
install(TARGETS anahita_network_statistics
    LIBRARY DESTINATION "${ANAHITA_PLUGIN_DIR}"
    RUNTIME DESTINATION "${ANAHITA_PLUGIN_DIR}")
