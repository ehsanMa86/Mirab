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
    SeriesRef
};

struct ColumnDef
{
    std::string name;
    ColumnType type = ColumnType::Text;
    std::string quantity; ///< unit family: length, diameter, flow, elevation, ...
    std::vector<std::string> choices;
    bool optional = false;
    std::string help;
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
};

const std::vector<SectionDef>& sectionDefs(Dialect dialect);
const SectionDef* findSectionDef(Dialect dialect, std::string_view name);
/// Section that defines elements of @p kind, e.g. "Junction" -> [JUNCTIONS].
const SectionDef* sectionForKind(Dialect dialect, std::string_view kind);
/// Element kinds of a category in display order.
std::vector<std::string> elementKinds(Dialect dialect, Category category);

/// Canonical section order used when creating new files.
const std::vector<std::string>& sectionOrder(Dialect dialect);

} // namespace mirab::core
