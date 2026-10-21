/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/LiveResults.h
 * Purpose: Results streamed while a simulation runs: the layout and frame events of the solver protocol.
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

#include "mirab/core/Json.h"
#include "mirab/core/Results.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::core::live
{

/// Base64 (RFC 4648, with padding, no line breaks).
std::string base64Encode(const void* data, std::size_t size);
/// Returns false when @p text is not valid base64.
bool base64Decode(std::string_view text, std::vector<unsigned char>& out);

/// True when @p a and @p b have the same tables, element IDs and variables.
bool sameLayout(const ResultSet& a, const ResultSet& b);

/// Payload of the "layout" event: engine, units and the tables of @p results
/// (kinds, element IDs, variables), without values.
json::Value layoutPayload(const ResultSet& results, int layout);

/// One complete JSON line (without the line break) of the "frame" event:
/// every value of period @p slot of @p results as little-endian float32,
/// table by table, variable by variable, element by element, in base64.
std::string frameLine(const ResultSet& results, int slot, int layout, int period, double time);

struct Frame
{
    int layout = 0;
    int period = 0;    ///< index of the reporting period in the final results
    double time = 0.0; ///< seconds from the simulation start
    std::vector<float> values;
};

/// Reads a layout payload into a result set without periods.
/// Throws std::runtime_error when it is malformed.
ResultSet parseLayout(const json::Value& payload, int* layout = nullptr);

/// Reads a "frame" event line. Throws std::runtime_error when it is malformed.
Frame parseFrameLine(std::string_view line);

/// Number of values one frame of @p layout carries.
std::size_t frameSize(const ResultSet& layout);

/// Appends @p frame to @p results as a new period, or replaces the latest
/// period when the frame has the same time. Returns the period written, or
/// -1 (and changes nothing) when the frame does not fit the tables.
int appendFrame(ResultSet& results, const Frame& frame);

} // namespace mirab::core::live
