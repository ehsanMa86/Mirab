/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/interop/src/ImportDetail.h
 * Purpose: Internal helpers shared by the shapefile, DXF and CSV importers.
 *
 * Copyright (C) 2026 Ehsan Madadi
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace anahita::interop::detail
{

/// Reads a whole file as bytes. Throws std::runtime_error when it cannot be read.
std::string readFileBytes(const std::filesystem::path& path);

/// True when @p s is well-formed UTF-8.
bool isValidUtf8(std::string_view s);

/// Converts Windows-1252 (a superset of the printable ISO-8859-1 range) to UTF-8.
std::string cp1252ToUtf8(std::string_view s);

/// Returns @p s unchanged when it is valid UTF-8, else converts it from Windows-1252.
std::string toUtf8Auto(std::string_view s);

/// Lower-case letters and digits only: "Max Depth" -> "maxdepth".
std::string normalizedName(std::string_view s);

} // namespace anahita::interop::detail
