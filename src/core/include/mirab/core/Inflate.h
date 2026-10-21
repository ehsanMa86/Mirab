/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Inflate.h
 * Purpose: Decompression of zlib and raw deflate streams (RFC 1950, RFC 1951).
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mirab::core
{

/// Decompresses a raw deflate stream (RFC 1951) and appends the bytes to @p out.
/// Returns false, with a message in @p error, on corrupt or truncated input.
/// @p sizeHint reserves memory when the output size is known.
bool inflateRaw(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                std::string* error = nullptr, std::size_t sizeHint = 0);

/// Decompresses a zlib stream (RFC 1950: header, deflate data, Adler-32) and
/// checks the checksum. Preset dictionaries are not supported.
bool zlibDecompress(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                    std::string* error = nullptr, std::size_t sizeHint = 0);

/// Adler-32 checksum (RFC 1950), continuing from @p adler (1 for a new checksum).
std::uint32_t adler32(const std::uint8_t* data, std::size_t size, std::uint32_t adler = 1);

} // namespace mirab::core
