/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/ModelJson.cpp
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
#include "mirab/core/ModelJson.h"

#include "mirab/core/Text.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace mirab::core
{

using json::Value;

namespace
{

const char* const kMultiWordKeys[] = {
    "DEMAND MODEL",      "DEMAND MULTIPLIER",  "SPECIFIC GRAVITY",  "EMITTER EXPONENT",
    "EMITTER BACKFLOW",  "MINIMUM PRESSURE",   "REQUIRED PRESSURE", "PRESSURE EXPONENT",
    "HYDRAULIC TIMESTEP", "QUALITY TIMESTEP",  "PATTERN TIMESTEP",  "PATTERN START",
    "REPORT TIMESTEP",   "REPORT START",       "START CLOCKTIME",   "RULE TIMESTEP",
    "STATUS REPORT",     "DEMAND CHARGE",      "GLOBAL EFFICIENCY", "GLOBAL PRICE",
    "GLOBAL PATTERN",    "GLOBAL BULK",        "GLOBAL WALL",       "ORDER BULK",
    "ORDER WALL",        "ORDER TANK",         "LIMITING POTENTIAL", "ROUGHNESS CORRELATION"};

bool isGeometrySection(std::string_view name)
{
    return name == "COORDINATES" || name == "VERTICES" || name == "POLYGONS" || name == "SYMBOLS";
}

const char* categoryName(Category c)
{
    switch (c)
    {
    case Category::Node:
        return "node";
    case Category::Link:
        return "link";
    case Category::Area:
        return "subcatchment";
    case Category::Gage:
        return "gage";
    case Category::None:
        break;
    }
    return "";
}

Value tokenValue(const ColumnDef* c, const std::string& token)
{
    if (token.empty() || token == "*" || token == "\"\"" || (c && token == c->blank))
        return Value();
    // Free text columns often hold numbers too (SWMM cross-section sizes).
    if (c && (c->type == ColumnType::Number || c->type == ColumnType::Text))
    {
        if (auto v = text::toDouble(token))
            return Value(*v);
    }
    return Value(text::unquote(token));
}

/// Named values of a row, without its key column. With @p fill, columns the
/// row leaves out are reported as null.
Value rowValues(const SectionDef& def, const std::vector<std::string>& tokens, bool fill)
{
    Value v = Value::object();
    const RowLayout l = def.layout(tokens);
    const std::size_t n = tokens.size();
    std::size_t limit = n;
    if (fill && l.repeatFrom < 0)
        limit = std::max(n, l.columns.size());
    for (std::size_t i = 1; i < limit; ++i)
    {
        const ColumnDef* c = l.column(i);
        if (l.variadic && !l.columns.empty() && i + 1 >= l.columns.size())
        {
            if (i >= n)
            {
                v.set(l.columnName(i), Value());
                break;
            }
            std::vector<std::string> rest(tokens.begin() + static_cast<std::ptrdiff_t>(i),
                                          tokens.end());
            v.set(l.columnName(i),
                  rest.size() == 1 ? tokenValue(c, rest[0]) : Value(text::join(rest, " ")));
            break;
        }
        v.set(l.columnName(i), i < n ? tokenValue(c, tokens[i]) : Value());
    }
    return v;
}

Value tokenArray(const std::vector<std::string>& tokens)
{
    Value a = Value::array();
    for (const auto& t : tokens)
        a.push(t);
    return a;
}

Value point(const Point& p)
{
    Value a = Value::array();
    a.push(p.x);
    a.push(p.y);
    return a;
}

/// All row tokens of the sections read so far.
class RowCache
{
public:
    explicit RowCache(const InpDocument& doc) : doc_(doc) {}
    const std::vector<std::vector<std::string>>& rows(const std::string& section)
    {
        auto it = rows_.find(section);
        if (it != rows_.end())
            return it->second;
        const InpSection* s = doc_.find(section);
        auto rows = s ? s->allRowTokens() : std::vector<std::vector<std::string>>{};
        return rows_.emplace(section, std::move(rows)).first->second;
    }

private:
    const InpDocument& doc_;
    std::map<std::string, std::vector<std::vector<std::string>>> rows_;
};

std::string valueText(const Value& v, const std::string& what)
{
    switch (v.type())
    {
    case Value::Type::Null:
        return {};
    case Value::Type::Bool:
        return v.asBool() ? "YES" : "NO";
    case Value::Type::Number:
        return text::formatDouble(v.asNumber(), 15);
    case Value::Type::String:
        return v.asString();
    default:
        throw std::runtime_error(what + " must be a number, a string or null");
    }
}

} // namespace

std::string columnKey(std::string_view name)
{
    std::string out;
    for (char ch : name)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c))
            out += static_cast<char>(std::tolower(c));
    }
    return out;
}

const std::vector<std::string>& optionSections(Dialect dialect)
{
    static const std::vector<std::string> epanet = {"OPTIONS", "TIMES", "REPORT", "REACTIONS",
                                                    "ENERGY"};
    static const std::vector<std::string> swmm = {"OPTIONS", "REPORT", "EVAPORATION", "TEMPERATURE",
                                                  "ADJUSTMENTS"};
    static const std::vector<std::string> msx = {"OPTIONS", "REPORT"};
    switch (dialect)
    {
    case Dialect::Epanet:
        return epanet;
    case Dialect::Swmm:
        return swmm;
    case Dialect::Msx:
        return msx;
    }
    return epanet;
}

std::vector<std::pair<std::string, std::string>> optionEntries(const InpDocument& doc,
                                                               std::string_view section)
{
    std::vector<std::pair<std::string, std::string>> out;
    const InpSection* s = doc.find(section);
    if (!s)
        return out;
    std::vector<std::vector<std::string>> rows;
    for (const auto& tokens : s->allRowTokens())
    {
        if (tokens.empty())
            continue;
        std::vector<std::string> upper;
        for (const auto& t : tokens)
            upper.push_back(text::toUpper(t));
        rows.push_back(std::move(upper));
    }
    auto sharesPrefix = [&](std::size_t row, std::size_t n) {
        for (std::size_t o = 0; o < rows.size(); ++o)
        {
            if (o == row || rows[o].size() < n)
                continue;
            const auto end = rows[o].begin() + static_cast<std::ptrdiff_t>(n);
            if (std::equal(rows[o].begin(), end, rows[row].begin()))
                return true;
        }
        return false;
    };
    std::set<std::string> used;
    const auto all = s->allRowTokens();
    std::size_t index = 0;
    for (const auto& tokens : all)
    {
        if (tokens.empty())
            continue;
        const std::size_t row = index++;
        std::size_t k = 1;
        if (tokens.size() >= 3)
        {
            const std::string two = rows[row][0] + " " + rows[row][1];
            for (const char* m : kMultiWordKeys)
                k = two == m ? 2 : k;
        }
        // Rows that start alike ("PUMP PU1 PRICE", "PUMP PU1 PATTERN") get
        // enough words for InpDocument::keyValue() to find each of them.
        while (k + 1 < tokens.size() && sharesPrefix(row, k))
            ++k;
        const auto split = static_cast<std::ptrdiff_t>(k);
        std::vector<std::string> words(rows[row].begin(), rows[row].begin() + split);
        const std::string key = text::join(words, " ");
        if (!used.insert(key).second)
            continue;
        std::vector<std::string> rest(tokens.begin() + split, tokens.end());
        out.emplace_back(key, text::join(rest, " "));
    }
    return out;
}

json::Value modelToJson(const Model& model)
{
    const Dialect dialect = model.dialect();
    const InpDocument& doc = model.document();
    RowCache cache(doc);

    Value out = Value::object();
    out.set("format", kModelJsonFormat);
    out.set("dialect", dialectName(dialect));
    out.set("title", model.title());
    const auto units = model.units();
    Value u = Value::object();
    u.set("flow", units.flow);
    u.set("system", units.system == UnitSystem::SI ? "SI" : "US");
    if (dialect == Dialect::Epanet)
        u.set("pressure", units.pressure);
    out.set("units", u);
    Value times = Value::object();
    times.set("duration", model.durationSeconds());
    times.set("hydraulicStep", model.hydraulicStepSeconds());
    times.set("reportStep", model.reportStepSeconds());
    times.set("reportStart", model.reportStartSeconds());
    out.set("times", times);

    Value options = Value::object();
    for (const auto& name : optionSections(dialect))
    {
        if (!doc.has(name))
            continue;
        Value o = Value::object();
        for (const auto& [k, v] : optionEntries(doc, name))
            o.set(k, v);
        options.set(name, o);
    }
    out.set("options", options);

    // Elements: the defining row as "properties", rows of other sections keyed
    // by the element under "data".
    struct Group
    {
        Category category;
        const char* member;
        std::vector<Value> items;
        std::unordered_map<std::string, std::size_t> index;
    };
    std::vector<Group> groups = {{Category::Node, "nodes", {}, {}},
                                 {Category::Link, "links", {}, {}},
                                 {Category::Area, "subcatchments", {}, {}},
                                 {Category::Gage, "gages", {}, {}}};
    auto start = [&](Group& g, const std::string& id, const std::string& kind,
                     const std::string& section, int row) -> Value& {
        Value e = Value::object();
        e.set("id", id);
        e.set("kind", kind);
        e.set("section", section);
        const SectionDef* def = findSectionDef(dialect, section);
        const auto& rows = cache.rows(section);
        if (def && row >= 0 && static_cast<std::size_t>(row) < rows.size())
            e.set("properties", rowValues(*def, rows[static_cast<std::size_t>(row)], true));
        else
            e.set("properties", Value::object());
        g.index[id] = g.items.size();
        g.items.push_back(std::move(e));
        return g.items.back();
    };
    for (const auto& n : model.nodes())
    {
        Value& e = start(groups[0], n.id, n.kind, n.section, n.row);
        e.set("x", n.placed ? Value(n.pos.x) : Value());
        e.set("y", n.placed ? Value(n.pos.y) : Value());
    }
    for (const auto& l : model.links())
    {
        Value& e = start(groups[1], l.id, l.kind, l.section, l.row);
        e.set("from", l.from);
        e.set("to", l.to);
        Value v = Value::array();
        for (const auto& p : l.vertices)
            v.push(point(p));
        e.set("vertices", v);
    }
    for (const auto& a : model.areas())
    {
        Value& e = start(groups[2], a.id, a.kind, a.section, a.row);
        e.set("outlet", a.outlet);
        e.set("gage", a.gage);
        Value v = Value::array();
        for (const auto& p : a.polygon)
            v.push(point(p));
        e.set("polygon", v);
    }
    for (const auto& gage : model.gages())
    {
        Value& e = start(groups[3], gage.id, "RainGage", gage.section, gage.row);
        e.set("x", gage.placed ? Value(gage.pos.x) : Value());
        e.set("y", gage.placed ? Value(gage.pos.y) : Value());
    }
    for (auto& g : groups)
    {
        for (const auto& def : sectionDefs(dialect))
        {
            if (def.keyCategory != g.category || !def.elementKind.empty() ||
                isGeometrySection(def.name) || !doc.has(def.name))
                continue;
            for (const auto& tokens : cache.rows(def.name))
            {
                if (tokens.empty())
                    continue;
                auto it = g.index.find(tokens[0]);
                if (it == g.index.end())
                    continue;
                g.items[it->second]["data"][def.name].push(rowValues(def, tokens, false));
            }
        }
        Value list = Value::array();
        for (auto& e : g.items)
        {
            if (!e.contains("data"))
                e.set("data", Value::object());
            list.push(std::move(e));
        }
        out.set(g.member, list);
    }

    // Named objects (SWMM pollutants, land uses, ...).
    std::set<std::string> objectSectionNames;
    Value objects = Value::object();
    for (const auto& kind : objectKinds(dialect))
    {
        const SectionDef* def = sectionForObject(dialect, kind);
        if (def)
            objectSectionNames.insert(def->name);
        for (const auto& d : sectionDefs(dialect))
        {
            if (d.keyCategory == Category::None && !d.columns.empty() &&
                d.columns.front().type == ColumnType::ObjectRef &&
                text::iequals(d.columns.front().object, kind))
                objectSectionNames.insert(d.name);
        }
        Value list = Value::array();
        for (const auto& name : model.objectNames(kind))
        {
            Value o = Value::object();
            o.set("id", name);
            o.set("section", def ? def->name : std::string());
            Value rows = Value::array();
            for (const auto& r : model.objectRows(kind, name))
            {
                Value row = Value::object();
                row.set("section", r.section);
                row.set("tokens", tokenArray(r.tokens));
                if (r.def && !r.def->columns.empty())
                    row.set("values", rowValues(*r.def, r.tokens, false));
                rows.push(row);
            }
            o.set("rows", rows);
            list.push(o);
        }
        objects.set(kind, list);
    }
    out.set("objects", objects);

    // Everything else as rows of tokens: patterns, curves, time series, controls, ...
    const auto& opts = optionSections(dialect);
    Value sections = Value::object();
    for (const auto& s : doc.sections)
    {
        const bool option = std::find(opts.begin(), opts.end(), s.name) != opts.end();
        if (s.name == "TITLE" || s.name == "END" || isGeometrySection(s.name) ||
            objectSectionNames.count(s.name) || option)
            continue;
        const SectionDef* def = findSectionDef(dialect, s.name);
        if (def && def->keyCategory != Category::None)
            continue;
        Value rows = Value::array();
        for (const auto& tokens : cache.rows(s.name))
            rows.push(tokenArray(tokens));
        sections.set(s.name, rows);
    }
    out.set("sections", sections);
    return out;
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------
namespace
{

class Editor
{
public:
    explicit Editor(Model& model) : model_(model) {}

    bool apply(const Value& e)
    {
        if (!e.isObject())
            throw std::runtime_error("an edit must be a JSON object");
        if (e.contains("option"))
            return setOption(e);
        if (e.contains("rows"))
            return replaceRows(e);
        return setValue(e);
    }

    void finish()
    {
        if (dirty_)
            model_.rebuild();
        dirty_ = false;
    }

private:
    struct Target
    {
        std::string section;
        int row = -1;
    };

    bool setOption(const Value& e)
    {
        const std::string section = text::toUpper(e.get("section").asString("OPTIONS"));
        const std::string key = text::trim(e.get("option").asString());
        if (key.empty())
            throw std::runtime_error("\"option\" needs a key");
        if (!e.contains("value"))
            throw std::runtime_error("the edit needs a \"value\" (null removes the option)");
        InpDocument& doc = model_.document();
        const auto before = doc.keyValue(section, key);
        const Value& value = e.get("value");
        if (value.isNull())
        {
            if (!before)
                return false;
            doc.removeKeyValue(section, key);
        }
        else
        {
            const std::string v = text::trim(valueText(value, "\"value\""));
            if (before && text::join(text::splitInpLine(*before).tokens, " ") ==
                              text::join(text::splitInpLine(v).tokens, " "))
                return false;
            const InpSection* s = doc.find(section);
            const auto old = s ? s->allRowTokens() : std::vector<std::vector<std::string>>{};
            doc.setKeyValue(section, key, v);
            // Keep the key as the file spells it ("Duration", not "DURATION").
            InpSection* changed = doc.find(section);
            const std::size_t words = text::splitInpLine(key).tokens.size();
            for (std::size_t r = 0; r < old.size(); ++r)
            {
                auto tokens = changed->rowTokens(static_cast<int>(r));
                if (tokens == old[r] || tokens.size() < words || old[r].size() < words)
                    continue;
                std::copy(old[r].begin(), old[r].begin() + static_cast<std::ptrdiff_t>(words),
                          tokens.begin());
                changed->setRowTokens(static_cast<int>(r), tokens);
                break;
            }
        }
        rows_.erase(section);
        dirty_ = true;
        return true;
    }

    bool replaceRows(const Value& e)
    {
        const std::string section = text::toUpper(e.get("section").asString());
        if (section.empty())
            throw std::runtime_error("\"rows\" needs a \"section\"");
        const Value& rowsValue = e.get("rows");
        if (!rowsValue.isArray())
            throw std::runtime_error("\"rows\" must be an array of rows");
        const SectionDef* def = findSectionDef(model_.dialect(), section);
        if (def && (!def->elementKind.empty() || def->keyCategory != Category::None))
            throw std::runtime_error("[" + section +
                                     "] holds element data; set its values one by one");
        const std::string key = e.get("id").asString();
        const bool keyed = !key.empty() && def && !def->columns.empty();
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : rowsValue.items())
        {
            std::vector<std::string> tokens;
            if (keyed)
                tokens.push_back(text::quoteIfNeeded(key));
            if (r.isArray())
            {
                for (const auto& t : r.items())
                    tokens.push_back(text::quoteIfNeeded(valueText(t, "a row value")));
            }
            else
            {
                for (auto& t : text::splitInpLine(valueText(r, "a row")).tokens)
                    tokens.push_back(t);
            }
            if (!tokens.empty())
                rows.push_back(std::move(tokens));
        }
        finish();
        std::vector<int> indices;
        const InpSection* s = model_.document().find(section);
        if (!key.empty())
            indices = model_.rowsOfKey(section, key);
        else if (s)
            for (int r = 0; r < s->rowCount(); ++r)
                indices.push_back(r);
        std::vector<std::vector<std::string>> old;
        for (int r : indices)
            old.push_back(s->rowTokens(r));
        if (old == rows)
            return false;
        model_.replaceRows(section, indices, rows);
        rows_.erase(section);
        return true;
    }

    bool setValue(const Value& e)
    {
        const std::string id = text::trim(valueText(e.get("id"), "\"id\""));
        if (id.empty())
            throw std::runtime_error("the edit needs an \"id\", an \"option\" or \"rows\"");
        const Value& column = e.get("column");
        if (column.isNull())
            throw std::runtime_error("the edit needs a \"column\"");
        if (!e.contains("value"))
            throw std::runtime_error("the edit needs a \"value\"");
        const int rowNumber = static_cast<int>(e.get("row").asNumber(0));
        const std::string value = valueText(e.get("value"), "\"value\"");
        const std::string section = text::toUpper(e.get("section").asString());

        std::vector<Target> candidates;
        std::string owner;
        std::string objectKind;
        Category category = Category::None;
        if (!section.empty())
        {
            candidates.push_back(targetIn(section, id, rowNumber));
        }
        else
        {
            resolveOwner(id, e.get("category").asString(), category, objectKind);
            if (category != Category::None)
            {
                Target t = definingRow(category, id);
                candidates.push_back(t);
                for (const auto& def : sectionDefs(model_.dialect()))
                {
                    if (def.keyCategory != category || !def.elementKind.empty() ||
                        isGeometrySection(def.name))
                        continue;
                    const auto& rows = keyRows(def.name, id);
                    if (static_cast<std::size_t>(rowNumber) < rows.size())
                        candidates.push_back({def.name, rows[static_cast<std::size_t>(rowNumber)]});
                }
            }
            else
            {
                for (const auto& r : model_.objectRows(objectKind, id))
                    candidates.push_back({r.section, r.row});
            }
        }

        for (const auto& t : candidates)
        {
            InpSection* s = model_.document().find(t.section);
            if (!s || t.row < 0 || t.row >= s->rowCount())
                continue;
            const auto tokens = s->rowTokens(t.row);
            const SectionDef* def = findSectionDef(model_.dialect(), t.section);
            int index = -1;
            bool tail = false;
            if (column.isNumber())
            {
                index = static_cast<int>(column.asNumber());
            }
            else if (def)
            {
                const RowLayout l = def->layout(tokens);
                const std::string want = columnKey(column.asString());
                std::size_t limit = std::max(tokens.size(), l.columns.size());
                if (l.repeatFrom >= 0)
                    limit += l.columns.size() - static_cast<std::size_t>(l.repeatFrom);
                for (std::size_t i = 0; i < limit && index < 0; ++i)
                {
                    if (columnKey(l.columnName(i)) == want)
                    {
                        index = static_cast<int>(i);
                        tail = l.variadic && !l.columns.empty() && i + 1 >= l.columns.size();
                    }
                }
            }
            if (index < 0)
                continue;
            if (index == 0)
                return rename(def, tokens, value);
            return setToken(*s, t.row, index, tail, def, tokens, value);
        }
        if (candidates.empty())
            throw std::runtime_error("no rows for " + id +
                                     (section.empty() ? "" : " in [" + section + "]"));
        throw std::runtime_error(id + " has no column \"" + column.asString() + "\"" +
                                 (section.empty() ? "" : " in [" + section + "]"));
    }

    /// Finds what @p id names: an element (category) or a named object (kind).
    void resolveOwner(const std::string& id, const std::string& hint, Category& category,
                      std::string& kind)
    {
        const std::string h = text::toLower(hint);
        struct Option
        {
            Category c;
            const char* name;
            bool found;
        };
        const std::vector<Option> elements = {
            {Category::Node, "node", model_.findNode(id) != nullptr},
            {Category::Link, "link", model_.findLink(id) != nullptr},
            {Category::Area, "subcatchment", model_.findArea(id) != nullptr},
            {Category::Gage, "gage", model_.findGage(id) != nullptr}};
        int matches = 0;
        for (const auto& o : elements)
        {
            const bool wanted = h.empty() || h == o.name || (o.c == Category::Area && h == "area");
            if (wanted && o.found)
            {
                category = o.c;
                ++matches;
            }
        }
        if (matches > 1)
            throw std::runtime_error(id + " names several elements; add \"category\"");
        if (matches == 1)
            return;
        for (const auto& k : objectKinds(model_.dialect()))
        {
            if ((h.empty() || text::iequals(h, k)) && model_.objectExists(k, id))
            {
                kind = k;
                return;
            }
        }
        throw std::runtime_error("no element or object named " + id);
    }

    Target definingRow(Category c, const std::string& id) const
    {
        switch (c)
        {
        case Category::Node:
            return {model_.findNode(id)->section, model_.findNode(id)->row};
        case Category::Link:
            return {model_.findLink(id)->section, model_.findLink(id)->row};
        case Category::Area:
            return {model_.findArea(id)->section, model_.findArea(id)->row};
        case Category::Gage:
            return {model_.findGage(id)->section, model_.findGage(id)->row};
        case Category::None:
            break;
        }
        return {};
    }

    /// Row @p n of @p id in @p section; adds a row with default values when
    /// the section has none (n == row count).
    Target targetIn(const std::string& section, const std::string& id, int n)
    {
        const SectionDef* def = findSectionDef(model_.dialect(), section);
        if (!def)
            throw std::runtime_error("unknown section [" + section + "]");
        std::vector<int> rows;
        if (def->keyCategory == Category::None && !def->objectKind.empty())
            rows = model_.rowsOfKey(section, id);
        else
            rows = keyRows(section, id);
        if (n >= 0 && static_cast<std::size_t>(n) < rows.size())
            return {section, rows[static_cast<std::size_t>(n)]};
        if (n != static_cast<int>(rows.size()))
            throw std::runtime_error(id + " has " + std::to_string(rows.size()) + " row(s) in [" +
                                     section + "]");
        if (!def->elementKind.empty() || !def->objectKind.empty())
        {
            const std::string& kind = def->elementKind.empty() ? def->objectKind : def->elementKind;
            throw std::runtime_error("no " + kind + " named " + id);
        }
        if (def->keyCategory != Category::None)
        {
            Category found = Category::None;
            std::string kind;
            resolveOwner(id, categoryName(def->keyCategory), found, kind);
        }
        std::string missing;
        auto tokens = model_.newDataRow(*def, id, &missing);
        if (tokens.empty())
            throw std::runtime_error("[" + section + "] needs a " + missing + " first");
        const int row = model_.document().ensure(section).appendRow(tokens);
        rows_.erase(section);
        dirty_ = true;
        return {section, row};
    }

    const std::vector<int>& keyRows(const std::string& section, const std::string& key)
    {
        auto it = rows_.find(section);
        if (it == rows_.end())
        {
            std::unordered_map<std::string, std::vector<int>> map;
            if (const InpSection* s = model_.document().find(section))
            {
                const auto all = s->allRowTokens();
                for (std::size_t r = 0; r < all.size(); ++r)
                {
                    if (!all[r].empty())
                        map[all[r][0]].push_back(static_cast<int>(r));
                }
            }
            it = rows_.emplace(section, std::move(map)).first;
        }
        static const std::vector<int> none;
        auto k = it->second.find(key);
        return k == it->second.end() ? none : k->second;
    }

    bool rename(const SectionDef* def, const std::vector<std::string>& tokens,
                const std::string& value)
    {
        const std::string newId = text::trim(value);
        if (newId.empty() || newId == tokens[0])
            return false;
        finish();
        std::string err;
        bool ok = false;
        if (def && !def->elementKind.empty())
            ok = model_.renameElement(def->keyCategory, tokens[0], newId, &err);
        else if (def && !def->objectKind.empty())
            ok = model_.renameObject(def->objectKind, tokens[0], newId, &err);
        else
            throw std::runtime_error("rename " + tokens[0] + " in its defining section");
        if (!ok)
            throw std::runtime_error(err.empty() ? "cannot rename " + tokens[0] : err);
        rows_.clear();
        return true;
    }

    bool setToken(InpSection& s, int row, int column, bool tail, const SectionDef* def,
                  std::vector<std::string> tokens, const std::string& value)
    {
        const auto before = tokens;
        const std::string v = text::trim(value);
        if (tail)
        {
            tokens.resize(std::min(tokens.size(), static_cast<std::size_t>(column)));
            while (tokens.size() < static_cast<std::size_t>(column))
                tokens.push_back("*");
            for (const auto& t : text::splitInpLine(v).tokens)
                tokens.push_back(t);
        }
        else
        {
            std::string blank = "*";
            if (def)
            {
                const RowLayout l = def->layout(tokens);
                if (const ColumnDef* c = l.column(static_cast<std::size_t>(column)))
                    blank = c->blank;
            }
            const auto col = static_cast<std::size_t>(column);
            if (col >= tokens.size())
            {
                if (v.empty())
                    return false;
                while (tokens.size() < col)
                    tokens.push_back(blank);
                tokens.push_back(text::quoteIfNeeded(text::unquote(v)));
            }
            else if (v.empty() && col == tokens.size() - 1 && col > 0)
            {
                tokens.pop_back();
            }
            else
            {
                tokens[col] = v.empty() ? blank : text::quoteIfNeeded(text::unquote(v));
            }
        }
        if (tokens == before)
            return false;
        s.setRowTokens(row, tokens);
        dirty_ = true;
        // Rows keep their place, so only a changed key invalidates the index.
        if (column == 0)
            rows_.erase(s.name);
        return true;
    }

    Model& model_;
    bool dirty_ = false;
    std::map<std::string, std::unordered_map<std::string, std::vector<int>>> rows_;
};

} // namespace

EditSummary applyEdits(Model& model, const json::Value& edits)
{
    const Value* list = &edits;
    if (edits.isObject())
        list = edits.find("edits");
    if (!list || !list->isArray())
        throw std::runtime_error("edits must be a JSON array or an object with an \"edits\" array");
    EditSummary summary;
    Editor editor(model);
    for (std::size_t i = 0; i < list->size(); ++i)
    {
        try
        {
            if (editor.apply((*list)[i]))
                ++summary.changed;
            ++summary.applied;
        }
        catch (const std::exception& e)
        {
            editor.finish();
            throw std::runtime_error("edit " + std::to_string(i + 1) + ": " + e.what());
        }
    }
    editor.finish();
    return summary;
}

} // namespace mirab::core
