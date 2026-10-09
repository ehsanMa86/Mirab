/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/include/anahita/core/Results.h
 * Purpose: Engine-independent simulation results and the Anahita results file (.anr).
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
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace anahita::core
{

struct Variable
{
    std::string name; ///< e.g. "Pressure", "Flow", "CL2"
    std::string unit; ///< display unit, e.g. "psi", "LPS", "mg/L"
};

/// Time series for one class of elements (nodes, links, subcatchments or system).
/// Values are stored period-major: data[(period * vars + var) * elements + element].
class ResultTable
{
public:
    std::string kind; ///< "node", "link", "subcatchment" or "system"
    std::vector<std::string> ids;
    std::vector<Variable> variables;
    std::vector<float> data;

    int elementCount() const { return static_cast<int>(ids.size()); }
    int variableCount() const { return static_cast<int>(variables.size()); }
    int periodCount() const;

    int variableIndex(std::string_view name) const;
    int elementIndex(std::string_view id) const;

    float value(int period, int variable, int element) const;
    void set(int period, int variable, int element, float value);
    std::vector<float> series(int variable, int element) const;
    /// All element values of one variable at one period.
    std::vector<float> snapshot(int period, int variable) const;

    /// Grows the data array to hold @p periods periods (new values are zero).
    void resizePeriods(int periods);
    /// Adds variables after the existing ones, keeping stored values.
    void appendVariables(const std::vector<Variable>& more);

private:
    // Lookup table for elementIndex(), rebuilt when the ID list changes size.
    mutable std::unordered_map<std::string, int> index_;
    mutable std::size_t indexed_ = static_cast<std::size_t>(-1);
};

class ResultSet
{
public:
    std::string engineId;
    std::string engineName;
    std::string title;
    std::string flowUnits;
    std::string unitSystem; ///< "US" or "SI"
    double reportStart = 0.0; ///< seconds from simulation start of the first period
    double reportStep = 0.0;  ///< seconds between periods
    std::vector<double> times; ///< seconds from simulation start of each period
    std::map<std::string, std::string> metadata;
    std::vector<std::string> messages;
    std::vector<ResultTable> tables;

    int periodCount() const { return static_cast<int>(times.size()); }
    bool empty() const { return times.empty(); }

    ResultTable* table(std::string_view kind);
    const ResultTable* table(std::string_view kind) const;
    /// Creates (or replaces) a table with the given ids and variables.
    ResultTable& defineTable(const std::string& kind, std::vector<std::string> ids,
                             std::vector<Variable> variables);
    /// Appends a period at @p seconds and returns its index.
    int addPeriod(double seconds);

    /// Writes the Anahita results format. Throws std::runtime_error on failure.
    void save(const std::filesystem::path& path) const;
    /// Reads the Anahita results format. Throws std::runtime_error on failure.
    static ResultSet load(const std::filesystem::path& path);

    /// Reads a binary output file written by EPANET 2.x.
    static ResultSet readEpanetOutput(const std::filesystem::path& path);
    /// Reads a binary output file written by SWMM 5.x.
    static ResultSet readSwmmOutput(const std::filesystem::path& path);
    /// Chooses the reader from the file's magic number.
    static ResultSet loadAny(const std::filesystem::path& path);
};

} // namespace anahita::core
