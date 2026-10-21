/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Query.h
 * Purpose: Attribute queries over model elements and their results, and element tables as CSV.
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

#include "mirab/core/Model.h"
#include "mirab/core/Results.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mirab::core
{

/// A filter expression such as "Pressure < 20", "Diameter >= 300 AND
/// Roughness < 100", "Type = Pump OR (Kind = Valve AND NOT Setting = 0)" or
/// "ID contains P1". Operators: = (or ==), != (or <>), <, <=, >, >= and
/// "contains"; conditions combine with AND, OR, NOT and parentheses. Field
/// names may contain spaces; text values with spaces are quoted. Numbers
/// compare as numbers, text compares without regard to case.
class Query
{
public:
    Query();
    ~Query();
    Query(const Query& other);
    Query& operator=(const Query& other);

    /// Parses an expression; returns nullopt and sets @p error when it is not
    /// valid. An empty text gives a query that matches everything.
    static std::optional<Query> parse(std::string_view text, std::string* error = nullptr);

    bool empty() const;
    /// Field names in the order they appear.
    std::vector<std::string> fields() const;
    /// Evaluates the query. @p field returns a field's value, or nullopt when
    /// the element does not have it (the condition is then false).
    bool matches(const std::function<std::optional<std::string>(const std::string& name)>& field) const;

    struct Node;

private:
    std::shared_ptr<const Node> root_;
};

/// Named values of one element: ID, Type, the columns of its rows and,
/// when results are given, the result variables at a period.
using ElementFields = std::vector<std::pair<std::string, std::string>>;

/// Looks up a field by name without regard to case. "Kind" is a synonym of
/// "Type", and a "result." prefix reads the result variable of that name
/// even when a property has the same name.
std::optional<std::string> fieldValue(const ElementFields& fields, std::string_view name);

/// Builds the fields of many elements of one category at once.
class FieldTable
{
public:
    FieldTable(const Model& model, Category category, const ResultSet* results = nullptr, int period = 0);
    /// Fields of one element (empty when the element does not exist).
    ElementFields fields(const std::string& id) const;
    /// Names of every property and result field of the category, for help
    /// lists and for checking a query.
    std::vector<std::string> fieldNames() const;

private:
    const Model& model_;
    Category category_;
    const ResultSet* results_;
    int period_;
    std::unordered_map<std::string, std::vector<ElementRow>> rows_;
};

/// Elements of @p category (and, when given, of element type @p kind) that
/// match the query. Category::None searches every category.
std::vector<ElementRef> selectByQuery(const Model& model, const Query& query, Category category,
                                      const std::string& kind = {}, const ResultSet* results = nullptr,
                                      int period = 0);

/// CSV table of elements: Category, Type, ID, then every property field and
/// result variable (at @p period) that any of them has.
std::string elementsCsv(const Model& model, const std::vector<ElementRef>& elements,
                        const ResultSet* results = nullptr, int period = 0);

} // namespace mirab::core
