# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    src/cli/CMakeLists.txt
# Purpose: Build definition of mirab-cli, the command-line tool and solver process.
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
add_executable(mirab-cli main.cpp Calibrate.cpp Calibrate.h Link.cpp Link.h)
target_link_libraries(mirab-cli PRIVATE
    Mirab::engine Mirab::resilience Mirab::interop Mirab::warnings)
if(WIN32)
    target_link_libraries(mirab-cli PRIVATE shell32)
endif()
if(MINGW)
    # Keep the solver process free of MinGW runtime DLLs.
    target_link_options(mirab-cli PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(APPLE)
    set_target_properties(mirab-cli PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
elseif(UNIX)
    set_target_properties(mirab-cli PROPERTIES INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/mirab")
endif()
install(TARGETS mirab-cli RUNTIME DESTINATION "${MIRAB_INSTALL_BINDIR}")
