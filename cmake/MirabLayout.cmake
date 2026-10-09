# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    cmake/MirabLayout.cmake
# Purpose: Install layout for Windows, macOS and Linux packages.
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
# Windows : <prefix>/bin/Mirab.exe, mirab-cli.exe, bin/engines/*.dll
# Linux   : <prefix>/bin/mirab, mirab-cli, lib/mirab/engines/*.so
# macOS   : Mirab.app/Contents/MacOS/{Mirab,mirab-cli},
#           Mirab.app/Contents/Frameworks/engines/*.dylib

if(WIN32)
    set(MIRAB_INSTALL_BINDIR     "bin")
    set(MIRAB_INSTALL_ENGINEDIR  "bin/engines")
    set(MIRAB_INSTALL_DOCDIR     "doc")
    set(MIRAB_INSTALL_EXAMPLEDIR "examples")
elseif(APPLE)
    set(MIRAB_INSTALL_BINDIR     "Mirab.app/Contents/MacOS")
    set(MIRAB_INSTALL_ENGINEDIR  "Mirab.app/Contents/Frameworks/engines")
    set(MIRAB_INSTALL_DOCDIR     "Mirab.app/Contents/Resources/doc")
    set(MIRAB_INSTALL_EXAMPLEDIR "Mirab.app/Contents/Resources/examples")
else()
    set(MIRAB_INSTALL_BINDIR     "${CMAKE_INSTALL_BINDIR}")
    # Fixed "lib" (not the multiarch libdir) so the executable finds ../lib/mirab.
    set(MIRAB_INSTALL_ENGINEDIR  "lib/mirab/engines")
    set(MIRAB_INSTALL_DOCDIR     "${CMAKE_INSTALL_DOCDIR}")
    set(MIRAB_INSTALL_EXAMPLEDIR "${CMAKE_INSTALL_DATADIR}/mirab/examples")
endif()
