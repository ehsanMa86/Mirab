/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/ModelJson.h
 * Purpose: JSON view of a model and batch edits for scripting (mirab-cli model get/set).
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
#include "mirab/core/Model.h"

#include <string>
#include <string_view>
#include <vector>

namespace mirab::core
{

/// Format tag of modelToJson() documents.
inline constexpr const char* kModelJsonFormat = "mirab-model/1";

/// Every element with its properties named by the schema, the options, the
/// named objects and the remaining sections of a model (see docs/cli.md).
json::Value modelToJson(const Model& model);

/// Options of a key/value section ([OPTIONS], [TIMES], ...) in file order as
/// (KEY, value) pairs. Keys are upper case and may have several words
/// ("DEMAND MODEL"); a key that repeats gets the next word added so that
/// every key is unique and can be passed back to InpDocument::setKeyValue().
std::vector<std::pair<std::string, std::string>> optionEntries(const InpDocument& doc,
                                                               std::string_view section);

/// Sections whose rows modelToJson() reports as options.
const std::vector<std::string>& optionSections(Dialect dialect);

/// Outcome of applyEdits().
struct EditSummary
{
    int applied = 0; ///< edits processed
    int changed = 0; ///< edits that changed the file
};

/// Applies a list of edits (a JSON array, or an object with an "edits" array)
/// through the model so that untouched lines keep their text and comments:
///
///  - {"id", "column", "value"} sets one value of an element or object;
///    optional "section", "row" (n-th row of the key in that section) and
///    "category" (node, link, subcatchment, gage or an object kind),
///  - {"option", "value"} sets a key/value option ("section" defaults to
///    OPTIONS; a null value removes the option),
///  - {"section", "id", "rows"} replaces the rows of a pattern, curve, time
///    series or other keyed data (without "id": every row of the section).
///
/// Throws std::runtime_error naming the edit that cannot be applied; edits
/// before it remain applied.
EditSummary applyEdits(Model& model, const json::Value& edits);

/// Normalised column name used to match names from scripts: lower case
/// letters and digits only ("Base demand", "base_demand" -> "basedemand").
std::string columnKey(std::string_view name);

} // namespace mirab::core
