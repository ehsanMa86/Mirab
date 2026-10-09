# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
#
# File:    cmake/AnahitaLayout.cmake
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
# Windows : <prefix>/bin/Anahita.exe, anahita-cli.exe, bin/engines/*.dll
# Linux   : <prefix>/bin/anahita, anahita-cli, lib/anahita/engines/*.so
# macOS   : Anahita.app/Contents/MacOS/{Anahita,anahita-cli},
#           Anahita.app/Contents/Frameworks/engines/*.dylib

if(WIN32)
    set(ANAHITA_INSTALL_BINDIR     "bin")
    set(ANAHITA_INSTALL_ENGINEDIR  "bin/engines")
    set(ANAHITA_INSTALL_DOCDIR     "doc")
    set(ANAHITA_INSTALL_EXAMPLEDIR "examples")
elseif(APPLE)
    set(ANAHITA_INSTALL_BINDIR     "Anahita.app/Contents/MacOS")
    set(ANAHITA_INSTALL_ENGINEDIR  "Anahita.app/Contents/Frameworks/engines")
    set(ANAHITA_INSTALL_DOCDIR     "Anahita.app/Contents/Resources/doc")
    set(ANAHITA_INSTALL_EXAMPLEDIR "Anahita.app/Contents/Resources/examples")
else()
    set(ANAHITA_INSTALL_BINDIR     "${CMAKE_INSTALL_BINDIR}")
    set(ANAHITA_INSTALL_ENGINEDIR  "${CMAKE_INSTALL_LIBDIR}/anahita/engines")
    set(ANAHITA_INSTALL_DOCDIR     "${CMAKE_INSTALL_DOCDIR}")
    set(ANAHITA_INSTALL_EXAMPLEDIR "${CMAKE_INSTALL_DATADIR}/anahita/examples")
endif()
