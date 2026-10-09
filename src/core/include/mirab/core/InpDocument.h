/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/InpDocument.h
 * Purpose: Section-preserving document model for EPANET, EPANET-MSX and SWMM input files.
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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::core
{

/// One "[SECTION]" of an input file. Every line after the header is kept
/// verbatim (comments and blank lines included) so that a file read and
/// written by Mirab changes only where the user edited it.
class InpSection
{
public:
    std::string name;   ///< canonical name, upper case, without brackets
    std::string header; ///< header line exactly as written, e.g. "[JUNCTIONS]"
    std::vector<std::string> lines;

    /// True for lines that carry data (not blank, not a pure comment).
    static bool isDataLine(std::string_view line);

    /// Indices into @c lines of the data lines, in order.
    std::vector<int> dataLineIndices() const;
    int rowCount() const;

    std::vector<std::string> rowTokens(int row) const;
    /// Tokens of every data row in one pass (rowTokens(r) scans from the start).
    std::vector<std::vector<std::string>> allRowTokens() const;
    std::string rowComment(int row) const;

    /// Replaces the tokens of a row and keeps its trailing comment.
    void setRowTokens(int row, const std::vector<std::string>& tokens);
    /// Appends a data row (before trailing blank lines) and returns its row index.
    int appendRow(const std::vector<std::string>& tokens, const std::string& comment = {});
    void removeRow(int row);

    /// Formats tokens as an aligned row.
    static std::string formatRow(const std::vector<std::string>& tokens,
                                 const std::string& comment = {});

private:
    int lineIndexOfRow(int row) const;
};

class InpDocument
{
public:
    std::vector<std::string> preamble; ///< lines before the first section
    std::vector<InpSection> sections;

    static InpDocument parse(std::string_view text);
    /// Throws std::runtime_error when the file cannot be read.
    static InpDocument load(const std::filesystem::path& path);

    std::string toString() const;
    /// Throws std::runtime_error when the file cannot be written.
    void save(const std::filesystem::path& path) const;

    InpSection* find(std::string_view name);
    const InpSection* find(std::string_view name) const;
    bool has(std::string_view name) const { return find(name) != nullptr; }
    /// Returns the section, creating it (before [END] when present) if needed.
    InpSection& ensure(std::string_view name);
    bool remove(std::string_view name);

    /// Looks up a key in an option-style section ("KEY VALUE" rows). Keys may
    /// have several words, e.g. "DEMAND MODEL"; the value is the rest of the row.
    std::optional<std::string> keyValue(std::string_view section, std::string_view key) const;
    /// Sets or adds a key/value row in an option-style section.
    void setKeyValue(std::string_view section, std::string_view key, const std::string& value);
    /// Removes key/value rows whose key matches.
    bool removeKeyValue(std::string_view section, std::string_view key);

    static std::string canonicalSectionName(std::string_view headerLine);
};

} // namespace mirab::core
