/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Scenario.h
 * Purpose: Scenarios: alternatives of a base model stored as changes, applied on demand.
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

#include "mirab/core/InpDocument.h"
#include "mirab/core/Json.h"
#include "mirab/core/Model.h"
#include "mirab/core/Results.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mirab::core
{

/// One difference between a scenario and its parent. Rows are found by
/// their key (element or object ID, option name, rule name) and the ordinal
/// among the rows of the section that share that key, never by position, so
/// that a scenario still applies after rows were added to the base model.
struct ScenarioChange
{
    enum class Kind
    {
        Set,    ///< one value of a row: @c column, from[0] -> to[0]
        Row,    ///< all values of a row (the number of values changed)
        Add,    ///< a new row, inserted after the last row with the same key
        Remove, ///< a row removed
        Text    ///< the whole text of a free-text section such as [TITLE]
    };
    Kind kind = Kind::Set;
    std::string section;
    std::string key;
    int ordinal = 0;
    int column = -1;
    std::vector<std::string> from; ///< parent values (tokens, or lines for Text)
    std::vector<std::string> to;   ///< scenario values

    bool operator==(const ScenarioChange& o) const;
};

/// An alternative of the base model: changes relative to the base model or
/// to a parent scenario, and optionally another engine or MSX file.
struct Scenario
{
    std::string name;
    std::string description;
    std::string parent; ///< empty: the base model
    std::string engineId; ///< empty: as the parent
    int useMsx = -1;      ///< -1: as the parent, 0: off, 1: on
    std::string msxFile;  ///< empty: as the parent
    std::vector<ScenarioChange> changes;
};

/// A change that did not apply cleanly because the model it was recorded on
/// has changed since.
struct ScenarioConflict
{
    std::string scenario;
    std::string section;
    std::string key;
    int ordinal = 0;
    int column = -1;
    bool missing = false; ///< the row is gone (otherwise its value changed)
    std::string message;
};

/// Keys of the data rows of one section (see ScenarioChange).
class SectionKeys
{
public:
    SectionKeys() = default;
    SectionKeys(const InpSection& section, Dialect dialect);
    /// Builds the keys from rows already split into tokens.
    SectionKeys(std::string_view section, const std::vector<std::vector<std::string>>& rows,
                Dialect dialect);

    int rowCount() const { return static_cast<int>(keys_.size()); }
    const std::string& key(int row) const { return keys_[static_cast<std::size_t>(row)]; }
    int ordinal(int row) const { return ordinals_[static_cast<std::size_t>(row)]; }
    /// Data row with the key and ordinal, or -1.
    int find(const std::string& key, int ordinal) const;
    /// Data rows with the key, in order.
    const std::vector<int>* rows(const std::string& key) const;
    /// Keys in order of first appearance.
    const std::vector<std::string>& order() const { return order_; }

private:
    void build(std::string_view section, const std::vector<std::vector<std::string>>& rows,
               Dialect dialect);
    std::vector<std::string> keys_;
    std::vector<int> ordinals_;
    std::vector<std::string> order_;
    std::unordered_map<std::string, std::vector<int>> byKey_;
};

/// True for sections compared as whole text ([TITLE]).
bool isTextSection(std::string_view section);

/// Changes that turn @p parent into @p child. Comments and spacing are ignored.
std::vector<ScenarioChange> diffDocuments(const InpDocument& parent, const InpDocument& child,
                                          Dialect dialect);
/// Applies changes to @p doc. Changes whose row is missing are skipped; they
/// and changes whose recorded parent value differs are reported in @p conflicts.
void applyChanges(InpDocument& doc, Dialect dialect, const std::vector<ScenarioChange>& changes,
                  std::vector<ScenarioConflict>* conflicts = nullptr,
                  const std::string& scenarioName = {});

const Scenario* findScenario(const std::vector<Scenario>& scenarios, std::string_view name);
Scenario* findScenario(std::vector<Scenario>& scenarios, std::string_view name);
/// The scenario and its ancestors, the oldest first. Empty with @p error set
/// when the scenario or a parent does not exist or the parents form a loop.
std::vector<const Scenario*> scenarioChain(const std::vector<Scenario>& scenarios,
                                           std::string_view name, std::string* error = nullptr);
/// The input document of a scenario: the base with the changes of each
/// scenario of the chain applied. An empty name returns the base. Throws
/// std::runtime_error for an unknown scenario.
InpDocument scenarioDocument(const InpDocument& base, Dialect dialect,
                             const std::vector<Scenario>& scenarios, std::string_view name,
                             std::vector<ScenarioConflict>* conflicts = nullptr);
Model scenarioModel(const Model& base, const std::vector<Scenario>& scenarios,
                    std::string_view name, std::vector<ScenarioConflict>* conflicts = nullptr);
/// Problems of a scenario name (empty, reserved, already used), empty when valid.
std::string checkScenarioName(const std::vector<Scenario>& scenarios, std::string_view name);
/// File name part for a scenario's results: "<project>.<stem>.anr".
std::string scenarioFileStem(std::string_view name);

/// Engine settings of a scenario after inheritance.
struct ScenarioEngine
{
    std::string engineId;
    bool useMsx = false;
    std::string msxFile;
};
ScenarioEngine scenarioEngine(const std::vector<Scenario>& scenarios, std::string_view name,
                              const ScenarioEngine& base);

json::Value scenariosToJson(const std::vector<Scenario>& scenarios);
std::vector<Scenario> scenariosFromJson(const json::Value& list);

/// One figure of the scenario comparison summary.
struct ResultSummaryItem
{
    std::string name;
    std::string unit;
    double value = 0.0;
};
/// Key figures of a run for comparing scenarios: EPANET minimum and mean
/// junction pressure, maximum velocity, demand volume and pump energy; SWMM
/// peak flooding, flooding volume, outfall volume and peak runoff. Volumes
/// and energy add up the reporting periods.
std::vector<ResultSummaryItem> resultSummary(const ResultSet& results, const Model& model);

} // namespace mirab::core
