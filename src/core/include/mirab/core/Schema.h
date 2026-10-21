/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Schema.h
 * Purpose: Column schemas for EPANET and SWMM input file sections.
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

#include <string>
#include <string_view>
#include <vector>

namespace mirab::core
{

enum class Dialect
{
    Epanet,
    Swmm,
    Msx
};

const char* dialectName(Dialect d);

/// What the first column of a section refers to.
enum class Category
{
    None, ///< free-form or option section
    Node,
    Link,
    Area, ///< SWMM subcatchment
    Gage  ///< SWMM rain gage
};

enum class ColumnType
{
    Id,
    NodeRef,
    LinkRef,
    AreaRef,
    NodeOrAreaRef,
    GageRef,
    Number,
    Text,
    Choice,
    PatternRef,
    CurveRef,
    SeriesRef,
    ObjectRef ///< named object (pollutant, land use, ...) of the kind in ColumnDef::object
};

struct ColumnDef
{
    std::string name;
    ColumnType type = ColumnType::Text;
    std::string quantity; ///< unit family: length, diameter, flow, elevation, ...
    std::vector<std::string> choices;
    bool optional = false;
    std::string help;
    std::string object; ///< ObjectRef: referenced object kind, e.g. "Pollutant"
    /// Token written for an empty value inside a row ("*", or "\"\"" where the
    /// engine expects an empty string).
    std::string blank = "*";
};

/// Layout of the columns that follow a type column, e.g. the parameters of a
/// GRATE inlet or of the REMOVAL row of a snow pack.
struct SectionVariant
{
    std::string key; ///< value of the type column
    std::vector<ColumnDef> columns;
    bool variadic = false;
};

/// Columns that describe one particular row of a section.
struct RowLayout
{
    std::vector<ColumnDef> columns;
    bool variadic = false; ///< the last column takes the rest of the row
    int repeatFrom = -1;   ///< columns from this one on repeat for longer rows
    /// Column that describes token @p index, or nullptr when there is none.
    const ColumnDef* column(std::size_t index) const;
    /// Display name of token @p index ("Percent 2" for a repeated column).
    std::string columnName(std::size_t index) const;
};

struct SectionDef
{
    std::string name;
    Category keyCategory = Category::None;
    std::string elementKind; ///< set when the section defines elements ("Junction", "Pipe", ...)
    std::vector<ColumnDef> columns;
    int fromColumn = -1;
    int toColumn = -1;
    bool variadic = false;        ///< rows may carry more values than listed columns
    bool multiRowPerKey = false;  ///< several rows can share the same key (patterns, curves, ...)
    std::string description;
    /// Set when the section defines named objects that are not drawn on the
    /// map ("Pollutant", "Landuse", ...).
    std::string objectKind;
    int repeatFrom = -1;    ///< columns from this one on repeat (land use/percent pairs, ...)
    int variantColumn = -1; ///< column whose value selects one of @c variants
    std::vector<SectionVariant> variants;

    /// Columns of a row with the given tokens (resolves variants).
    RowLayout layout(const std::vector<std::string>& tokens) const;
    /// Variant for a type keyword, or nullptr.
    const SectionVariant* variant(std::string_view key) const;
};

const std::vector<SectionDef>& sectionDefs(Dialect dialect);
const SectionDef* findSectionDef(Dialect dialect, std::string_view name);
/// Section that defines elements of @p kind, e.g. "Junction" -> [JUNCTIONS].
const SectionDef* sectionForKind(Dialect dialect, std::string_view kind);
/// Element kinds of a category in display order.
std::vector<std::string> elementKinds(Dialect dialect, Category category);

/// Kinds of named objects of a dialect in display order (SWMM: "Pollutant",
/// "Landuse", "Aquifer", "Snowpack", "Hydrograph", "Transect", "Street",
/// "Inlet", "LID", "Rule").
std::vector<std::string> objectKinds(Dialect dialect);
/// Section that defines objects of @p kind, e.g. "Pollutant" -> [POLLUTANTS].
const SectionDef* sectionForObject(Dialect dialect, std::string_view kind);

/// Canonical section order used when creating new files.
const std::vector<std::string>& sectionOrder(Dialect dialect);

} // namespace mirab::core
