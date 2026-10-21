/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Objects.cpp
 * Purpose: Named model objects (SWMM pollutants, land uses, aquifers, ...) and keyed data rows.
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
#include "mirab/core/Model.h"

#include "mirab/core/Text.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace mirab::core
{

using text::iequals;
using text::toUpper;

namespace
{
/// Name defined by a row of an object section, or empty.
std::string definedName(const SectionDef& def, const std::vector<std::string>& tokens)
{
    if (tokens.empty())
        return {};
    if (def.name == "TRANSECTS")
        return tokens.size() >= 2 && iequals(tokens[0], "X1") ? tokens[1] : std::string();
    if (def.name == "CONTROLS")
        return tokens.size() >= 2 && iequals(tokens[0], "RULE") ? tokens[1] : std::string();
    return tokens[0];
}

/// Column of the defining row that holds the object name.
std::size_t nameColumn(const SectionDef& def)
{
    return def.name == "TRANSECTS" || def.name == "CONTROLS" ? 1 : 0;
}

bool validObjectName(std::string_view id, std::string* error)
{
    if (id.empty() || id.size() > 31)
    {
        if (error)
            *error = "names must have 1 to 31 characters";
        return false;
    }
    for (char c : id)
    {
        if (c == ' ' || c == '\t' || c == ';' || c == '"' || c == '[' || c == ']')
        {
            if (error)
                *error = "names cannot contain blanks, quotes, brackets or semicolons";
            return false;
        }
    }
    return true;
}

/// Object references in sections that Mirab keeps as free text.
struct FreeReference
{
    const char* section;
    std::size_t column;
    const char* kind;
};
const FreeReference kFreeReferences[] = {{"LID_USAGE", 1, "LID"}};

bool isIdentifierChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/// Replaces the identifiers @p from and R_@p from in a math expression token.
bool renameInExpression(std::string& token, const std::string& from, const std::string& to)
{
    std::string out;
    bool changed = false;
    std::size_t i = 0;
    while (i < token.size())
    {
        if (!isIdentifierChar(token[i]))
        {
            out += token[i++];
            continue;
        }
        std::size_t j = i;
        while (j < token.size() && isIdentifierChar(token[j]))
            ++j;
        const std::string word = token.substr(i, j - i);
        if (word == from)
        {
            out += to;
            changed = true;
        }
        else if (word == "R_" + from || word == "r_" + from)
        {
            out += word.substr(0, 2) + to;
            changed = true;
        }
        else
        {
            out += word;
        }
        i = j;
    }
    if (changed)
        token = out;
    return changed;
}

/// Layout cache: one layout per section unless rows select variants.
class Layouts
{
public:
    explicit Layouts(const SectionDef& def) : def_(def), fixed_(def.layout({})) {}
    const RowLayout& operator()(const std::vector<std::string>& tokens)
    {
        if (def_.variantColumn < 0)
            return fixed_;
        row_ = def_.layout(tokens);
        return row_;
    }

private:
    const SectionDef& def_;
    RowLayout fixed_;
    RowLayout row_;
};

bool isTail(const RowLayout& l, std::size_t c)
{
    return l.variadic && !l.columns.empty() && c + 1 >= l.columns.size();
}
} // namespace

std::vector<std::string> Model::objectNames(std::string_view kind) const
{
    std::vector<std::string> names;
    const SectionDef* def = sectionForObject(dialect_, kind);
    if (!def)
        return names;
    const InpSection* s = doc_.find(def->name);
    if (!s)
        return names;
    std::set<std::string> seen;
    for (const auto& tokens : s->allRowTokens())
    {
        std::string n = definedName(*def, tokens);
        if (!n.empty() && seen.insert(n).second)
            names.push_back(std::move(n));
    }
    return names;
}

bool Model::objectExists(std::string_view kind, std::string_view name) const
{
    const auto names = objectNames(kind);
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::string Model::uniqueObjectName(std::string_view kind, std::string_view prefix) const
{
    const auto names = objectNames(kind);
    for (int i = 1;; ++i)
    {
        std::string candidate = std::string(prefix) + std::to_string(i);
        if (std::find(names.begin(), names.end(), candidate) == names.end())
            return candidate;
    }
}

std::vector<int> Model::rowsOfKey(std::string_view section, std::string_view key) const
{
    std::vector<int> out;
    const InpSection* s = doc_.find(section);
    if (!s)
        return out;
    const auto rows = s->allRowTokens();
    const int n = static_cast<int>(rows.size());
    auto first = [&](int r) { return rows[static_cast<std::size_t>(r)].empty() ? std::string() : toUpper(rows[static_cast<std::size_t>(r)][0]); };
    if (section == "TRANSECTS" || section == "CONTROLS")
    {
        const char* head = section == "TRANSECTS" ? "X1" : "RULE";
        for (int r = 0; r < n; ++r)
        {
            const auto& t = rows[static_cast<std::size_t>(r)];
            if (t.size() < 2 || first(r) != head || t[1] != key)
                continue;
            if (section == "TRANSECTS" && r > 0 && first(r - 1) == "NC")
                out.push_back(r - 1);
            out.push_back(r);
            for (int k = r + 1; k < n; ++k)
            {
                const std::string f = first(k);
                if (f == head || (section == "TRANSECTS" && f == "NC"))
                    break;
                out.push_back(k);
            }
            break;
        }
        return out;
    }
    for (int r = 0; r < n; ++r)
    {
        const auto& t = rows[static_cast<std::size_t>(r)];
        if (!t.empty() && t[0] == key)
            out.push_back(r);
    }
    return out;
}

void Model::replaceRows(std::string_view section, const std::vector<int>& rowIndices,
                        const std::vector<std::vector<std::string>>& rows)
{
    InpSection& s = doc_.ensure(section);
    std::vector<int> idx;
    for (int r : rowIndices)
    {
        if (r >= 0 && r < s.rowCount())
            idx.push_back(r);
    }
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    const std::size_t common = std::min(idx.size(), rows.size());
    for (std::size_t i = 0; i < common; ++i)
    {
        if (s.rowTokens(idx[i]) != rows[i])
            s.setRowTokens(idx[i], rows[i]);
    }
    if (rows.size() > idx.size())
    {
        if (idx.empty())
        {
            for (const auto& r : rows)
                s.appendRow(r);
        }
        else
        {
            const auto lines = s.dataLineIndices();
            auto at = s.lines.begin() + lines[static_cast<std::size_t>(idx.back())] + 1;
            for (std::size_t k = idx.size(); k < rows.size(); ++k)
                at = s.lines.insert(at, InpSection::formatRow(rows[k])) + 1;
        }
    }
    else
    {
        for (std::size_t i = idx.size(); i-- > common;)
            s.removeRow(idx[i]);
    }
    rebuild();
}

std::vector<ElementRow> Model::objectRows(std::string_view kind, std::string_view name) const
{
    std::vector<ElementRow> out;
    const SectionDef* def = sectionForObject(dialect_, kind);
    if (!def)
        return out;
    auto collect = [&](const SectionDef& d) {
        const InpSection* s = doc_.find(d.name);
        if (!s)
            return;
        for (int r : rowsOfKey(d.name, name))
            out.push_back(ElementRow{d.name, r, &d, s->rowTokens(r)});
    };
    collect(*def);
    for (const auto& d : sectionDefs(dialect_))
    {
        if (&d == def || d.keyCategory != Category::None || d.columns.empty())
            continue;
        const ColumnDef& key = d.columns.front();
        if (key.type == ColumnType::ObjectRef && iequals(key.object, kind))
            collect(d);
    }
    return out;
}

bool Model::addObject(std::string_view kind, std::string_view name, std::string* error)
{
    const SectionDef* def = sectionForObject(dialect_, kind);
    if (!def)
    {
        if (error)
            *error = "unknown object type " + std::string(kind);
        return false;
    }
    if (!validObjectName(name, error))
        return false;
    if (objectExists(kind, name))
    {
        if (error)
            *error = std::string(name) + " is already used";
        return false;
    }
    const std::string n(name);
    const bool si = units().system == UnitSystem::SI;
    const std::string k = toUpper(kind);
    std::vector<std::vector<std::string>> rows;
    if (k == "POLLUTANT")
        rows = {{n, "MG/L", "0", "0", "0", "0", "NO", "*", "0", "0", "0"}};
    else if (k == "LANDUSE")
        rows = {{n, "0", "0", "0"}};
    else if (k == "AQUIFER")
        rows = {{n, "0.5", "0.15", "0.3", si ? "125" : "5", "10", "15", "0.35", si ? "4" : "14",
                 si ? "0.05" : "0.002", "0", si ? "3" : "10", "0.3"}};
    else if (k == "SNOWPACK")
    {
        const std::string tb = si ? "0" : "32";
        const std::string sd = si ? "25" : "1";
        rows = {{n, "PLOWABLE", "0.001", "0.001", tb, "0.10", "0", "0", "0"},
                {n, "IMPERVIOUS", "0.001", "0.001", tb, "0.10", "0", "0", sd},
                {n, "PERVIOUS", "0.001", "0.001", tb, "0.10", "0", "0", sd},
                {n, "REMOVAL", sd, "0", "0", "0", "0", "0"}};
    }
    else if (k == "HYDROGRAPH")
    {
        if (gages_.empty())
        {
            if (error)
                *error = "a unit hydrograph needs a rain gage";
            return false;
        }
        rows = {{n, gages_.front().id}, {n, "ALL", "SHORT", "0.05", "1", "2"}};
    }
    else if (k == "TRANSECT")
    {
        const std::string h = si ? "3" : "10";
        const std::string w = si ? "3" : "10";
        rows = {{"NC", "0.05", "0.05", "0.03"},
                {"X1", n, "4", w, si ? "6" : "20", "0", "0", "0", "1", "1", "0"},
                {"GR", h, "0", "0", w, "0", si ? "6" : "20", h, si ? "9" : "30"}};
    }
    else if (k == "STREET")
        rows = {{n, si ? "6" : "20", si ? "0.15" : "0.5", "4", "0.016", "0", "0", "2", "0", "0", "0"}};
    else if (k == "INLET")
        rows = {{n, "GRATE", si ? "0.6" : "2", si ? "0.6" : "2", "P_BAR-50"}};
    else if (k == "LID")
        rows = {{n, "RG"},
                {n, "SURFACE", si ? "150" : "6", "0", "0.1", "1", "5"},
                {n, "SOIL", si ? "300" : "12", "0.5", "0.2", "0.1", si ? "12.7" : "0.5", "10", si ? "88.9" : "3.5"}};
    else if (k == "RULE")
    {
        const LinkItem* target = nullptr;
        for (const auto& l : links_)
        {
            if (l.kind == "Pump" || l.kind == "Orifice" || l.kind == "Weir" || l.kind == "Outlet")
            {
                target = &l;
                break;
            }
        }
        if (!target)
        {
            if (error)
                *error = "a control rule needs a pump, orifice, weir or outlet to act on";
            return false;
        }
        const bool pump = target->kind == "Pump";
        rows = {{"RULE", n},
                {"IF", "SIMULATION", "TIME", ">", "0"},
                {"THEN", toUpper(target->kind), target->id, pump ? "STATUS" : "SETTING", "=", pump ? "ON" : "1"}};
        InpSection& s = doc_.ensure(def->name);
        if (s.rowCount() > 0)
        {
            // Rules are separated by a blank line.
            const auto lines = s.dataLineIndices();
            s.lines.insert(s.lines.begin() + lines.back() + 1, std::string());
        }
    }
    replaceRows(def->name, {}, rows);
    return true;
}

bool Model::renameObject(std::string_view kind, std::string_view oldName, std::string_view newName,
                         std::string* error)
{
    const SectionDef* def = sectionForObject(dialect_, kind);
    if (!def || !objectExists(kind, oldName))
    {
        if (error)
            *error = "no object named " + std::string(oldName);
        return false;
    }
    if (oldName == newName)
        return true;
    if (!validObjectName(newName, error))
        return false;
    if (objectExists(kind, newName))
    {
        if (error)
            *error = std::string(newName) + " is already used";
        return false;
    }
    const std::string from(oldName), to(newName);
    for (auto& section : doc_.sections)
    {
        const SectionDef* d = findSectionDef(dialect_, section.name);
        if (!d)
            continue;
        const auto rows = section.allRowTokens();
        Layouts layouts(*d);
        for (std::size_t r = 0; r < rows.size(); ++r)
        {
            auto tokens = rows[r];
            bool changed = false;
            if (d == def)
            {
                const std::size_t c = nameColumn(*d);
                if (definedName(*d, tokens) == from)
                {
                    tokens[c] = to;
                    changed = true;
                }
            }
            if (d->columns.empty())
            {
                for (const auto& f : kFreeReferences)
                {
                    if (section.name == f.section && iequals(f.kind, kind) && tokens.size() > f.column &&
                        tokens[f.column] == from)
                    {
                        tokens[f.column] = to;
                        changed = true;
                    }
                }
            }
            else
            {
                const RowLayout& l = layouts(tokens);
                for (std::size_t c = 0; c < tokens.size(); ++c)
                {
                    const ColumnDef* col = l.column(c);
                    if (!col)
                        break;
                    if (col->type == ColumnType::ObjectRef && iequals(col->object, kind) && tokens[c] == from)
                    {
                        tokens[c] = to;
                        changed = true;
                    }
                    else if (section.name == "TREATMENT" && iequals(kind, "Pollutant") && isTail(l, c))
                    {
                        changed = renameInExpression(tokens[c], from, to) || changed;
                    }
                }
            }
            if (changed)
                section.setRowTokens(static_cast<int>(r), tokens);
        }
    }
    rebuild();
    return true;
}

bool Model::removeObject(std::string_view kind, std::string_view name)
{
    const SectionDef* def = sectionForObject(dialect_, kind);
    if (!def || !objectExists(kind, name))
        return false;
    const std::string n(name);
    {
        auto rows = rowsOfKey(def->name, n);
        InpSection* s = doc_.find(def->name);
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
            s->removeRow(*it);
    }
    for (auto& section : doc_.sections)
    {
        const SectionDef* d = findSectionDef(dialect_, section.name);
        if (!d)
            continue;
        const auto rows = section.allRowTokens();
        Layouts layouts(*d);
        for (int r = static_cast<int>(rows.size()) - 1; r >= 0; --r)
        {
            auto tokens = rows[static_cast<std::size_t>(r)];
            bool changed = false;
            bool drop = false;
            if (d->columns.empty())
            {
                for (const auto& f : kFreeReferences)
                {
                    if (section.name == f.section && iequals(f.kind, kind) && tokens.size() > f.column &&
                        tokens[f.column] == n)
                        drop = true;
                }
            }
            else
            {
                const RowLayout& l = layouts(tokens);
                for (std::size_t c = 0; c < tokens.size() && !drop; ++c)
                {
                    const ColumnDef* col = l.column(c);
                    if (!col)
                        break;
                    if (col->type != ColumnType::ObjectRef || !iequals(col->object, kind) || tokens[c] != n)
                        continue;
                    if (section.name == "XSECTIONS")
                        continue; // keep the cross section; validate() reports the reference
                    if (l.repeatFrom >= 0 && c >= static_cast<std::size_t>(l.repeatFrom))
                    {
                        // Remove the whole group (land use and percent, ...).
                        const std::size_t period = l.columns.size() - static_cast<std::size_t>(l.repeatFrom);
                        const std::size_t start = c - (c - static_cast<std::size_t>(l.repeatFrom)) % period;
                        const std::size_t end = std::min(tokens.size(), start + period);
                        tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(start),
                                     tokens.begin() + static_cast<std::ptrdiff_t>(end));
                        changed = true;
                        drop = tokens.size() <= static_cast<std::size_t>(l.repeatFrom);
                        c = start - 1;
                    }
                    else if (col->optional || !d->elementKind.empty())
                    {
                        if (c + 1 == tokens.size())
                            tokens.pop_back();
                        else
                            tokens[c] = col->blank;
                        changed = true;
                    }
                    else
                    {
                        drop = true;
                    }
                }
            }
            if (drop)
                section.removeRow(r);
            else if (changed)
                section.setRowTokens(r, tokens);
        }
    }
    rebuild();
    return true;
}

std::vector<std::string> Model::newDataRow(const SectionDef& def, std::string_view key,
                                           std::string* missing) const
{
    const std::string k(key);
    auto firstObject = [&](const std::string& kind, std::string& out) {
        const auto names = objectNames(kind);
        if (names.empty())
        {
            if (missing)
                *missing = kind;
            return false;
        }
        out = names.front();
        return true;
    };
    std::vector<std::string> row = {k};
    const std::string& s = def.name;
    if (dialect_ == Dialect::Swmm)
    {
        if (s == "INFLOWS")
            return {k, "FLOW", "\"\"", "FLOW", "1.0", "1.0", "0"};
        if (s == "DWF")
            return {k, "FLOW", "0"};
        if (s == "COVERAGES")
        {
            std::string lu;
            if (!firstObject("Landuse", lu))
                return {};
            return {k, lu, "100"};
        }
        if (s == "GWF")
        {
            bool lateral = false;
            for (int r : rowsOfKey(s, k))
            {
                const auto t = doc_.find(s)->rowTokens(r);
                lateral = lateral || (t.size() > 1 && iequals(t[1], "LATERAL"));
            }
            return {k, lateral ? "DEEP" : "LATERAL", "0"};
        }
        if (s == "TREATMENT")
        {
            std::string p;
            if (!firstObject("Pollutant", p))
                return {};
            return {k, p, "R", "=", "0"};
        }
        if (s == "XSECTIONS")
            return {k, "CIRCULAR", "1", "0", "0", "0", "1"};
        if (s == "BUILDUP" || s == "WASHOFF")
        {
            std::string p;
            if (!firstObject("Pollutant", p))
                return {};
            if (s == "BUILDUP")
                return {k, p, "NONE", "0", "0", "0", "AREA"};
            return {k, p, "EMC", "0", "0", "0", "0"};
        }
        if (s == "GROUNDWATER" || s == "INLET_USAGE")
        {
            std::string object;
            if (!firstObject(s == "GROUNDWATER" ? "Aquifer" : "Inlet", object))
                return {};
            std::string node;
            if (s == "GROUNDWATER")
            {
                const AreaItem* a = findArea(k);
                node = a && findNode(a->outlet) ? a->outlet : (nodes_.empty() ? "*" : nodes_.front().id);
                return {k, object, node, "0", "0", "0", "0", "0", "0", "0"};
            }
            const LinkItem* l = findLink(k);
            node = l ? l->from : (nodes_.empty() ? "*" : nodes_.front().id);
            return {k, object, node};
        }
    }
    else if (s == "STATUS")
    {
        return {k, "OPEN"};
    }
    for (std::size_t c = 1; c < def.columns.size(); ++c)
    {
        const ColumnDef& col = def.columns[c];
        if (col.optional)
            break;
        std::string v = "0";
        switch (col.type)
        {
        case ColumnType::Choice:
            v = col.choices.empty() ? "0" : col.choices.front();
            break;
        case ColumnType::ObjectRef:
            if (!firstObject(col.object, v))
                return {};
            break;
        case ColumnType::NodeRef:
        case ColumnType::NodeOrAreaRef:
            v = nodes_.empty() ? "*" : nodes_.front().id;
            break;
        case ColumnType::LinkRef:
            v = links_.empty() ? "*" : links_.front().id;
            break;
        case ColumnType::AreaRef:
            v = areas_.empty() ? "*" : areas_.front().id;
            break;
        case ColumnType::GageRef:
            v = gages_.empty() ? "*" : gages_.front().id;
            break;
        case ColumnType::PatternRef:
        case ColumnType::CurveRef:
        case ColumnType::SeriesRef:
            v = col.blank;
            break;
        default:
            break;
        }
        row.push_back(v);
    }
    return row;
}

} // namespace mirab::core
