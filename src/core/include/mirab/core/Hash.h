/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Hash.h
 * Purpose: SHA-256 digests used to verify downloaded engine files.
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

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace mirab::core
{

/// Lower-case hexadecimal SHA-256 of a byte string.
std::string sha256Hex(std::string_view data);
/// Lower-case hexadecimal SHA-256 of a file. Throws std::runtime_error on read errors.
std::string sha256File(const std::filesystem::path& path);

} // namespace mirab::core
