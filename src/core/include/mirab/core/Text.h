/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Text.h
 * Purpose: Locale-independent string helpers shared by every Mirab library.
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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::text
{

std::string trim(std::string_view s);
std::string toUpper(std::string_view s);
std::string toLower(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool istartsWith(std::string_view s, std::string_view prefix);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);

/// A line of an EPANET/SWMM input file split into tokens and trailing comment.
/// Tokens keep their double quotes so that a row can be written back unchanged.
struct SplitLine
{
    std::vector<std::string> tokens;
    std::string comment; ///< text after ';' without the ';'
    bool hasComment = false;
};

/// Splits on blanks and tabs, keeps "quoted strings" together and stops at a
/// ';' that is outside quotes.
SplitLine splitInpLine(std::string_view line);

/// Removes surrounding double quotes, if any.
std::string unquote(std::string_view token);

/// Adds double quotes when the value contains blanks.
std::string quoteIfNeeded(std::string_view value);

/// Parses a number using the "C" locale regardless of the process locale.
std::optional<double> toDouble(std::string_view s);
std::optional<long long> toInt(std::string_view s);

/// Formats a number with the "C" locale: integers without a decimal point,
/// other values with up to @p precision significant digits.
std::string formatDouble(double value, int precision = 10);

/// Parses "hh:mm", "hh:mm:ss", plain hours ("1.5") or a value with a unit
/// keyword ("30 MIN", "2 DAYS") into seconds. Returns nullopt on failure.
std::optional<double> parseClockOrDuration(std::string_view value, std::string_view unit = {});

/// Formats seconds as "h:mm" (or "d h:mm" when longer than a day).
std::string formatDuration(double seconds);

} // namespace mirab::text
