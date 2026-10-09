# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tools/env-mingw.sh
# Purpose: Puts the private Windows toolchain (CMake, Ninja, Qt 6, MinGW) on PATH for a Git Bash session.
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
# Usage:  source tools/env-mingw.sh
MIRAB_DEV="${MIRAB_DEV:-$LOCALAPPDATA/mirab-dev}"
MIRAB_DEV="$(cygpath -u "$MIRAB_DEV" 2>/dev/null || echo "$MIRAB_DEV")"
export PATH="$MIRAB_DEV/nsis-3.11:$MIRAB_DEV/Qt/Tools/mingw1310_64/bin:$MIRAB_DEV/Qt/6.8.3/mingw_64/bin:$MIRAB_DEV/venv/Scripts:$PATH"
export CMAKE_PREFIX_PATH="$MIRAB_DEV/Qt/6.8.3/mingw_64"
