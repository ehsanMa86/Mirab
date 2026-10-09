# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    engines/patches/msx-fix-compiler-typo.cmake
# Purpose: Portability fix applied to the downloaded EPANET-MSX 2.0 sources: a typo breaks non-Windows builds.
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
#
# msxcompiler.c refers to 'wiMSX.Compiler' in its non-Windows branch; the
# variable is 'MSX'. The upstream file is otherwise left unchanged.
file(READ "${FILE}" _src)
string(REPLACE "wiMSX.Compiler" "MSX.Compiler" _fixed "${_src}")
if(NOT _fixed STREQUAL _src)
    file(WRITE "${FILE}" "${_fixed}")
    message(STATUS "Patched ${FILE} (wiMSX typo)")
endif()
