# Anahita - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Anahita
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
ANAHITA_DEV="${ANAHITA_DEV:-$LOCALAPPDATA/anahita-dev}"
ANAHITA_DEV="$(cygpath -u "$ANAHITA_DEV" 2>/dev/null || echo "$ANAHITA_DEV")"
export PATH="$ANAHITA_DEV/nsis-3.11:$ANAHITA_DEV/Qt/Tools/mingw1310_64/bin:$ANAHITA_DEV/Qt/6.8.3/mingw_64/bin:$ANAHITA_DEV/venv/Scripts:$PATH"
export CMAKE_PREFIX_PATH="$ANAHITA_DEV/Qt/6.8.3/mingw_64"
