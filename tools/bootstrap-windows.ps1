# Mirab - Unified Water Infrastructure Modeling Environment
# https://github.com/ehsanMa86/Mirab
#
# File:    tools/bootstrap-windows.ps1
# Purpose: Installs a private Windows build toolchain (CMake, Ninja, Qt 6.8 with MinGW 13.1, NSIS) without administrator rights.
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
# Usage (PowerShell):  powershell -ExecutionPolicy Bypass -File tools\bootstrap-windows.ps1
# Then, in Git Bash:   source tools/env-mingw.sh
param(
    [string]$Root = "$env:LOCALAPPDATA\mirab-dev",
    [string]$QtVersion = "6.8.3"
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Root | Out-Null

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw "Python 3 is needed once to install the toolchain (https://www.python.org)." }

if (-not (Test-Path "$Root\venv")) { & $python -m venv "$Root\venv" }
& "$Root\venv\Scripts\python.exe" -m pip install --upgrade pip | Out-Null
& "$Root\venv\Scripts\python.exe" -m pip install cmake ninja aqtinstall

if (-not (Test-Path "$Root\Qt\$QtVersion\mingw_64")) {
    & "$Root\venv\Scripts\aqt.exe" install-qt windows desktop $QtVersion win64_mingw --outputdir "$Root\Qt"
}
if (-not (Test-Path "$Root\Qt\Tools\mingw1310_64")) {
    & "$Root\venv\Scripts\aqt.exe" install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 --outputdir "$Root\Qt"
}
if (-not (Test-Path "$Root\nsis-3.11\makensis.exe")) {
    $zip = "$Root\nsis.zip"
    Invoke-WebRequest -Uri "https://sourceforge.net/projects/nsis/files/NSIS%203/3.11/nsis-3.11.zip/download" -OutFile $zip -UserAgent "curl"
    Expand-Archive -Path $zip -DestinationPath $Root -Force
}
Write-Host "Toolchain ready in $Root"
