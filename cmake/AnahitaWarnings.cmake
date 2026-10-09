# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
#
# File:    cmake/AnahitaWarnings.cmake
# Purpose: Compiler warning settings applied to Anahita's own targets only.
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

add_library(anahita_warnings INTERFACE)
add_library(Anahita::warnings ALIAS anahita_warnings)

if(MSVC)
    target_compile_options(anahita_warnings INTERFACE /W4 /permissive- /utf-8)
    target_compile_definitions(anahita_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(anahita_warnings INTERFACE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()
