# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    src/CMakeLists.txt
# Purpose: Source tree of Mirab: libraries, command-line tool and desktop application.
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
add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(resilience)
add_subdirectory(interop)
add_subdirectory(cli)
if(MIRAB_BUILD_GUI)
    add_subdirectory(app)
    add_subdirectory(${PROJECT_SOURCE_DIR}/plugins ${PROJECT_BINARY_DIR}/plugins)
endif()
