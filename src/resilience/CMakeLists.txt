# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    src/resilience/CMakeLists.txt
# Purpose: Build definition of the native resilience metrics library.
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
add_library(mirab_resilience STATIC src/Resilience.cpp)
add_library(Mirab::resilience ALIAS mirab_resilience)
target_include_directories(mirab_resilience PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(mirab_resilience PUBLIC Mirab::engine PRIVATE Mirab::warnings)
