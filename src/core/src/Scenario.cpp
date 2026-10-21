/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Scenario.cpp
 * Purpose: Scenarios: row keys, differences between input documents, applying and storing changes.
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
#include "mirab/core/Scenario.h"

#include "mirab/core/Text.h"
#include "mirab/core/Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace mirab::core
{

using text::toUpper;
using text::trim;

bool ScenarioChange::operator==(const ScenarioChange& o) const
{
    return kind == o.kind && section == o.section && key == o.key && ordinal == o.ordinal &&
           column == o.column && from == o.from && to == o.to;
}

namespace
{
// ---------------------------------------------------------------------------
// Row keys
// ---------------------------------------------------------------------------
bool contains(std::initializer_list<const char*> list, std::string_view s)
{
    for (const char* x : list)
    {
        if (s == x)
            return true;
    }
    return false;
}

bool isOptionSection(std::string_view section, Dialect dialect)
{
    if (dialect == Dialect::Swmm)
        return contains({"OPTIONS", "REPORT", "MAP", "BACKDROP", "EVAPORATION", "TEMPERATURE",
                         "ADJUSTMENTS"},
                        section);
    return contains({"OPTIONS", "TIMES", "REPORT", "ENERGY", "REACTIONS", "BACKDROP"}, section);
}

bool isRuleSection(std::string_view section, Dialect dialect)
{
    return section == (dialect == Dialect::Swmm ? "CONTROLS" : "RULES");
}

// Option keys of two words (the value follows them).
bool isTwoWordOption(const std::vector<std::string>& tokens)
{
    static const char* keys[][2] = {
        {"DEMAND", "MODEL"},       {"DEMAND", "MULTIPLIER"},  {"DEMAND", "PATTERN"},
        {"DEMAND", "CHARGE"},      {"SPECIFIC", "GRAVITY"},   {"EMITTER", "EXPONENT"},
        {"EMITTER", "BACKFLOW"},   {"MINIMUM", "PRESSURE"},   {"REQUIRED", "PRESSURE"},
        {"PRESSURE", "EXPONENT"},  {"HYDRAULIC", "TIMESTEP"}, {"QUALITY", "TIMESTEP"},
        {"PATTERN", "TIMESTEP"},   {"PATTERN", "START"},      {"REPORT", "TIMESTEP"},
        {"REPORT", "START"},       {"START", "CLOCKTIME"},    {"RULE", "TIMESTEP"},
        {"STATUS", "REPORT"},      {"GLOBAL", "EFFICIENCY"},  {"GLOBAL", "PRICE"},
        {"GLOBAL", "PATTERN"},     {"FLOW", "PACED"}};
    if (tokens.size() < 3)
        return false;
    const std::string a = toUpper(tokens[0]);
    const std::string b = toUpper(tokens[1]);
    for (const auto& k : keys)
    {
        if (a == k[0] && b == k[1])
            return true;
    }
    return false;
}

std::string joinKey(const std::vector<std::string>& tokens, std::size_t n, bool upper)
{
    n = std::min(n, tokens.size());
    std::string k;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (i)
            k += ' ';
        k += upper ? toUpper(tokens[i]) : tokens[i];
    }
    return k;
}

/// Key of a row in sections where it depends on the row alone.
std::string plainKey(std::string_view section, const std::vector<std::string>& t, Dialect dialect)
{
    if (t.empty())
        return {};
    if (isOptionSection(section, dialect))
    {
        if (dialect == Dialect::Epanet && section == "ENERGY")
        {
            if (toUpper(t[0]) == "PUMP")
                return joinKey(t, t.size() >= 4 ? 3 : t.size() - 1, true);
            return joinKey(t, t.size() >= 3 ? 2 : 1, true);
        }
        if (dialect == Dialect::Epanet && section == "REACTIONS")
            return joinKey(t, t.size() >= 3 ? 2 : 1, true);
        return joinKey(t, isTwoWordOption(t) ? 2 : 1, true);
    }
    if (dialect == Dialect::Epanet && (section == "CONTROLS" || section == "TAGS"))
        return t.size() >= 2 ? toUpper(t[0]) + " " + t[1] : toUpper(t[0]);
    if (dialect == Dialect::Swmm &&
        contains({"INFLOWS", "DWF", "TREATMENT", "BUILDUP", "WASHOFF", "LID_USAGE", "TAGS"}, section))
        return joinKey(t, 2, false);
    if (section == "LABELS" && t.size() >= 3)
        return t[2];
    return t[0];
}
} // namespace

bool isTextSection(std::string_view section)
{
    return section == "TITLE";
}

SectionKeys::SectionKeys(const InpSection& section, Dialect dialect)
{
    build(section.name, section.allRowTokens(), dialect);
}

SectionKeys::SectionKeys(std::string_view section, const std::vector<std::vector<std::string>>& rows,
                         Dialect dialect)
{
    build(section, rows, dialect);
}

void SectionKeys::build(std::string_view section, const std::vector<std::vector<std::string>>& rows,
                        Dialect dialect)
{
    const std::size_t n = rows.size();
    keys_.resize(n);
    ordinals_.resize(n);
    if (isRuleSection(section, dialect))
    {
        std::string current;
        for (std::size_t r = 0; r < n; ++r)
        {
            const auto& t = rows[r];
            if (t.size() >= 2 && toUpper(t[0]) == "RULE")
                current = t[1];
            keys_[r] = current;
        }
    }
    else if (section == "TRANSECTS" && dialect == Dialect::Swmm)
    {
        std::string current;
        for (std::size_t r = 0; r < n; ++r)
        {
            const auto& t = rows[r];
            const std::string head = t.empty() ? std::string() : toUpper(t[0]);
            if (head == "X1" && t.size() >= 2)
                current = t[1];
            if (head == "NC")
            {
                // An NC row belongs to the transect that follows it.
                std::string next;
                for (std::size_t k = r + 1; k < n; ++k)
                {
                    if (!rows[k].empty() && toUpper(rows[k][0]) == "X1")
                    {
                        next = rows[k].size() >= 2 ? rows[k][1] : std::string();
                        break;
                    }
                }
                keys_[r] = next;
                continue;
            }
            keys_[r] = current;
        }
    }
    else
    {
        for (std::size_t r = 0; r < n; ++r)
            keys_[r] = plainKey(section, rows[r], dialect);
    }
    for (std::size_t r = 0; r < n; ++r)
    {
        auto [it, added] = byKey_.try_emplace(keys_[r]);
        if (added)
            order_.push_back(keys_[r]);
        ordinals_[r] = static_cast<int>(it->second.size());
        it->second.push_back(static_cast<int>(r));
    }
}

int SectionKeys::find(const std::string& key, int ordinal) const
{
    const auto it = byKey_.find(key);
    if (it == byKey_.end() || ordinal < 0 || ordinal >= static_cast<int>(it->second.size()))
        return -1;
    return it->second[static_cast<std::size_t>(ordinal)];
}

const std::vector<int>* SectionKeys::rows(const std::string& key) const
{
    const auto it = byKey_.find(key);
    return it == byKey_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Differences
// ---------------------------------------------------------------------------
namespace
{
std::vector<std::string> dataLines(const InpSection* s)
{
    std::vector<std::string> out;
    if (!s)
        return out;
    for (const auto& l : s->lines)
    {
        if (InpSection::isDataLine(l))
            out.push_back(trim(l));
    }
    return out;
}

void diffSection(const std::string& name, const InpSection* parent, const InpSection* child,
                 Dialect dialect, std::vector<ScenarioChange>& out)
{
    if (parent && child && parent->lines == child->lines)
        return;
    if (isTextSection(name))
    {
        auto from = dataLines(parent);
        auto to = dataLines(child);
        if (from != to)
        {
            ScenarioChange c;
            c.kind = ScenarioChange::Kind::Text;
            c.section = name;
            c.from = std::move(from);
            c.to = std::move(to);
            out.push_back(std::move(c));
        }
        return;
    }
    const auto prows = parent ? parent->allRowTokens() : std::vector<std::vector<std::string>>{};
    const auto crows = child ? child->allRowTokens() : std::vector<std::vector<std::string>>{};
    if (prows == crows)
        return;
    const SectionKeys pk(name, prows, dialect);
    const SectionKeys ck(name, crows, dialect);
    static const std::vector<int> none;
    auto handle = [&](const std::string& key) {
        const std::vector<int>& p = pk.rows(key) ? *pk.rows(key) : none;
        const std::vector<int>& c = ck.rows(key) ? *ck.rows(key) : none;
        const std::size_t common = std::min(p.size(), c.size());
        for (std::size_t i = 0; i < common; ++i)
        {
            const auto& a = prows[static_cast<std::size_t>(p[i])];
            const auto& b = crows[static_cast<std::size_t>(c[i])];
            if (a == b)
                continue;
            if (a.size() == b.size())
            {
                for (std::size_t col = 0; col < a.size(); ++col)
                {
                    if (a[col] == b[col])
                        continue;
                    ScenarioChange ch;
                    ch.kind = ScenarioChange::Kind::Set;
                    ch.section = name;
                    ch.key = key;
                    ch.ordinal = static_cast<int>(i);
                    ch.column = static_cast<int>(col);
                    ch.from = {a[col]};
                    ch.to = {b[col]};
                    out.push_back(std::move(ch));
                }
            }
            else
            {
                ScenarioChange ch;
                ch.kind = ScenarioChange::Kind::Row;
                ch.section = name;
                ch.key = key;
                ch.ordinal = static_cast<int>(i);
                ch.from = a;
                ch.to = b;
                out.push_back(std::move(ch));
            }
        }
        for (std::size_t i = common; i < p.size(); ++i)
        {
            ScenarioChange ch;
            ch.kind = ScenarioChange::Kind::Remove;
            ch.section = name;
            ch.key = key;
            ch.ordinal = static_cast<int>(i);
            ch.from = prows[static_cast<std::size_t>(p[i])];
            out.push_back(std::move(ch));
        }
        for (std::size_t i = common; i < c.size(); ++i)
        {
            ScenarioChange ch;
            ch.kind = ScenarioChange::Kind::Add;
            ch.section = name;
            ch.key = key;
            ch.ordinal = static_cast<int>(i);
            ch.to = crows[static_cast<std::size_t>(c[i])];
            out.push_back(std::move(ch));
        }
    };
    for (const auto& key : pk.order())
        handle(key);
    for (const auto& key : ck.order())
    {
        if (!pk.rows(key))
            handle(key);
    }
}
} // namespace

std::vector<ScenarioChange> diffDocuments(const InpDocument& parent, const InpDocument& child,
                                          Dialect dialect)
{
    std::vector<ScenarioChange> out;
    for (const auto& s : parent.sections)
        diffSection(s.name, &s, child.find(s.name), dialect, out);
    for (const auto& s : child.sections)
    {
        if (!parent.find(s.name))
            diffSection(s.name, nullptr, &s, dialect, out);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Applying changes
// ---------------------------------------------------------------------------
namespace
{
std::string describe(const ScenarioChange& c)
{
    std::string where = "[" + c.section + "]";
    if (!c.key.empty())
        where += " " + c.key;
    if (c.ordinal > 0)
        where += " #" + std::to_string(c.ordinal + 1);
    return where;
}

void conflict(std::vector<ScenarioConflict>* out, const std::string& scenario, const ScenarioChange& c,
              bool missing, std::string message)
{
    if (!out)
        return;
    ScenarioConflict k;
    k.scenario = scenario;
    k.section = c.section;
    k.key = c.key;
    k.ordinal = c.ordinal;
    k.column = c.column;
    k.missing = missing;
    k.message = describe(c) + ": " + message;
    out->push_back(std::move(k));
}

/// Section of a dialect whose rows belong to an element defined elsewhere
/// ([DEMANDS], [COORDINATES], ...), with the element's category.
Category dependentCategory(Dialect dialect, std::string_view section)
{
    const SectionDef* def = findSectionDef(dialect, section);
    if (!def || def->keyCategory == Category::None || !def->elementKind.empty())
        return Category::None;
    return def->keyCategory;
}

void applyText(InpDocument& doc, const ScenarioChange& c, std::vector<ScenarioConflict>* conflicts,
               const std::string& scenario)
{
    InpSection* s = doc.find(c.section);
    if (dataLines(s) != c.from)
        conflict(conflicts, scenario, c, false, "the base text has changed since the scenario was edited");
    InpSection& target = s ? *s : doc.ensure(c.section);
    target.lines = c.to;
    target.lines.push_back("");
}

void applySection(InpDocument& doc, Dialect dialect, const std::string& name,
                  const std::vector<const ScenarioChange*>& changes,
                  std::vector<ScenarioConflict>* conflicts, const std::string& scenario,
                  const std::map<Category, std::unordered_set<std::string>>* ids)
{
    InpSection* sec = doc.find(name);
    bool hasAdd = false;
    for (const auto* c : changes)
        hasAdd = hasAdd || c->kind == ScenarioChange::Kind::Add;
    if (!sec)
    {
        if (!hasAdd)
        {
            for (const auto* c : changes)
                conflict(conflicts, scenario, *c, true, "the section is no longer in the model");
            return;
        }
        sec = &doc.ensure(name);
    }
    auto rows = sec->allRowTokens();
    const std::vector<int> lineOf = sec->dataLineIndices();
    const SectionKeys keys(name, rows, dialect);
    std::vector<char> removed(rows.size(), 0), dirty(rows.size(), 0);
    std::map<int, std::vector<std::string>> after; // line index -> new lines after it
    std::vector<std::string> tail;
    const Category dep = dependentCategory(dialect, name);

    for (const auto* c : changes)
    {
        using K = ScenarioChange::Kind;
        if (c->kind == K::Add)
        {
            if (ids && dep != Category::None && !c->to.empty())
            {
                const auto it = ids->find(dep);
                if (it == ids->end() || !it->second.count(c->to.front()))
                {
                    conflict(conflicts, scenario, *c, true,
                             c->to.front() + " is no longer in the model; the row was left out");
                    continue;
                }
            }
            const std::string line = InpSection::formatRow(c->to);
            const std::vector<int>* same = keys.rows(c->key);
            if (same && !same->empty())
                after[lineOf[static_cast<std::size_t>(same->back())]].push_back(line);
            else
                tail.push_back(line);
            continue;
        }
        const int r = keys.find(c->key, c->ordinal);
        if (r < 0)
        {
            conflict(conflicts, scenario, *c, true,
                     c->kind == K::Remove ? "the row was already removed" : "the row is no longer in the model");
            continue;
        }
        auto& tokens = rows[static_cast<std::size_t>(r)];
        if (c->kind == K::Set)
        {
            const auto col = static_cast<std::size_t>(std::max(0, c->column));
            const std::string from = c->from.empty() ? std::string() : c->from.front();
            if (col >= tokens.size() || tokens[col] != from)
                conflict(conflicts, scenario, *c, false,
                         "the base value changed from " + from + " to " +
                             (col < tokens.size() ? tokens[col] : std::string("(none)")));
            if (col >= tokens.size())
                tokens.resize(col + 1, "*");
            tokens[col] = c->to.empty() ? std::string("*") : c->to.front();
            dirty[static_cast<std::size_t>(r)] = 1;
        }
        else if (c->kind == K::Row)
        {
            if (tokens != c->from)
                conflict(conflicts, scenario, *c, false, "the base row has changed since the scenario was edited");
            tokens = c->to;
            dirty[static_cast<std::size_t>(r)] = 1;
        }
        else if (c->kind == K::Remove)
        {
            if (tokens != c->from)
                conflict(conflicts, scenario, *c, false, "the removed row had changed in the base");
            removed[static_cast<std::size_t>(r)] = 1;
        }
    }

    std::vector<std::string> out;
    out.reserve(sec->lines.size() + tail.size());
    std::size_t row = 0;
    for (std::size_t i = 0; i < sec->lines.size(); ++i)
    {
        const std::string& line = sec->lines[i];
        if (row < lineOf.size() && static_cast<std::size_t>(lineOf[row]) == i)
        {
            if (!removed[row])
                out.push_back(dirty[row] ? InpSection::formatRow(rows[row], text::splitInpLine(line).comment)
                                         : line);
            ++row;
        }
        else
        {
            out.push_back(line);
        }
        const auto it = after.find(static_cast<int>(i));
        if (it != after.end())
            out.insert(out.end(), it->second.begin(), it->second.end());
    }
    if (!tail.empty())
    {
        std::size_t at = out.size();
        while (at > 0 && trim(out[at - 1]).empty())
            --at;
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), tail.begin(), tail.end());
        if (at + tail.size() == out.size())
            out.push_back("");
    }
    sec->lines = std::move(out);
}

std::map<Category, std::unordered_set<std::string>> elementIds(const InpDocument& doc, Dialect dialect)
{
    std::map<Category, std::unordered_set<std::string>> ids;
    for (const auto& def : sectionDefs(dialect))
    {
        if (def.keyCategory == Category::None || def.elementKind.empty())
            continue;
        if (const InpSection* s = doc.find(def.name))
        {
            auto& set = ids[def.keyCategory];
            for (const auto& t : s->allRowTokens())
            {
                if (!t.empty())
                    set.insert(t.front());
            }
        }
    }
    return ids;
}
} // namespace

void applyChanges(InpDocument& doc, Dialect dialect, const std::vector<ScenarioChange>& changes,
                  std::vector<ScenarioConflict>* conflicts, const std::string& scenarioName)
{
    // Sections in the order of their first change; element rows before the
    // rows that depend on them, so that a new element's data can be checked.
    std::vector<std::string> order;
    std::map<std::string, std::vector<const ScenarioChange*>> bySection;
    for (const auto& c : changes)
    {
        if (c.kind == ScenarioChange::Kind::Text)
        {
            applyText(doc, c, conflicts, scenarioName);
            continue;
        }
        auto [it, added] = bySection.try_emplace(c.section);
        if (added)
            order.push_back(c.section);
        it->second.push_back(&c);
    }
    std::vector<std::string> dependent;
    for (const auto& name : order)
    {
        if (dependentCategory(dialect, name) != Category::None)
            dependent.push_back(name);
        else
            applySection(doc, dialect, name, bySection[name], conflicts, scenarioName, nullptr);
    }
    if (dependent.empty())
        return;
    const auto ids = elementIds(doc, dialect);
    for (const auto& name : dependent)
        applySection(doc, dialect, name, bySection[name], conflicts, scenarioName, &ids);
}

// ---------------------------------------------------------------------------
// Scenario lists
// ---------------------------------------------------------------------------
const Scenario* findScenario(const std::vector<Scenario>& scenarios, std::string_view name)
{
    for (const auto& s : scenarios)
    {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

Scenario* findScenario(std::vector<Scenario>& scenarios, std::string_view name)
{
    for (auto& s : scenarios)
    {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

std::vector<const Scenario*> scenarioChain(const std::vector<Scenario>& scenarios, std::string_view name,
                                           std::string* error)
{
    std::vector<const Scenario*> chain;
    std::string current(name);
    while (!current.empty())
    {
        const Scenario* s = findScenario(scenarios, current);
        if (!s)
        {
            if (error)
                *error = chain.empty() ? "no scenario named \"" + current + "\""
                                       : "scenario \"" + chain.back()->name + "\" is based on \"" + current +
                                             "\", which does not exist";
            return {};
        }
        if (std::find(chain.begin(), chain.end(), s) != chain.end())
        {
            if (error)
                *error = "the parents of scenario \"" + std::string(name) + "\" form a loop";
            return {};
        }
        chain.push_back(s);
        current = s->parent;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

InpDocument scenarioDocument(const InpDocument& base, Dialect dialect, const std::vector<Scenario>& scenarios,
                             std::string_view name, std::vector<ScenarioConflict>* conflicts)
{
    InpDocument doc = base;
    if (name.empty())
        return doc;
    std::string error;
    const auto chain = scenarioChain(scenarios, name, &error);
    if (chain.empty())
        throw std::runtime_error(error);
    for (const Scenario* s : chain)
        applyChanges(doc, dialect, s->changes, conflicts, s->name);
    return doc;
}

Model scenarioModel(const Model& base, const std::vector<Scenario>& scenarios, std::string_view name,
                    std::vector<ScenarioConflict>* conflicts)
{
    if (name.empty())
        return base;
    return Model(scenarioDocument(base.document(), base.dialect(), scenarios, name, conflicts), base.dialect());
}

std::string checkScenarioName(const std::vector<Scenario>& scenarios, std::string_view name)
{
    const std::string n = trim(name);
    if (n.empty())
        return "the name is empty";
    if (n != name)
        return "the name starts or ends with a blank";
    if (n.size() > 64)
        return "the name is longer than 64 characters";
    if (n.find_first_of(",;\"\t\r\n") != std::string::npos)
        return "the name contains a comma, semicolon, quote or line break";
    if (text::iequals(n, "base") || text::iequals(n, "base model"))
        return "\"" + n + "\" is reserved for the base model";
    const std::string stem = scenarioFileStem(n);
    for (const auto& s : scenarios)
    {
        if (s.name == n)
            return "a scenario named \"" + n + "\" already exists";
        if (text::iequals(scenarioFileStem(s.name), stem))
            return "the results file name would be the same as that of \"" + s.name + "\"";
    }
    return {};
}

std::string scenarioFileStem(std::string_view name)
{
    std::string out;
    for (char ch : name)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) && c < 128)
            out += ch;
        else if (ch == '-' || ch == '_')
            out += ch;
        else if (out.empty() || out.back() != '-')
            out += '-';
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    while (!out.empty() && out.front() == '-')
        out.erase(out.begin());
    return out.empty() ? std::string("scenario") : out;
}

ScenarioEngine scenarioEngine(const std::vector<Scenario>& scenarios, std::string_view name,
                              const ScenarioEngine& base)
{
    ScenarioEngine e = base;
    for (const Scenario* s : scenarioChain(scenarios, name))
    {
        if (!s->engineId.empty())
            e.engineId = s->engineId;
        if (s->useMsx >= 0)
            e.useMsx = s->useMsx == 1;
        if (!s->msxFile.empty())
            e.msxFile = s->msxFile;
    }
    return e;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
namespace
{
const char* kindName(ScenarioChange::Kind k)
{
    switch (k)
    {
    case ScenarioChange::Kind::Set:
        return "set";
    case ScenarioChange::Kind::Row:
        return "row";
    case ScenarioChange::Kind::Add:
        return "add";
    case ScenarioChange::Kind::Remove:
        return "remove";
    case ScenarioChange::Kind::Text:
        return "text";
    }
    return "set";
}

json::Value stringArray(const std::vector<std::string>& v)
{
    json::Value a = json::Value::array();
    for (const auto& s : v)
        a.push(s);
    return a;
}

std::vector<std::string> strings(const json::Value& v)
{
    std::vector<std::string> out;
    if (v.isString())
        out.push_back(v.asString());
    for (const auto& x : v.items())
        out.push_back(x.asString());
    return out;
}
} // namespace

json::Value scenariosToJson(const std::vector<Scenario>& scenarios)
{
    json::Value list = json::Value::array();
    for (const auto& s : scenarios)
    {
        json::Value o = json::Value::object();
        o.set("name", s.name);
        if (!s.description.empty())
            o.set("description", s.description);
        if (!s.parent.empty())
            o.set("parent", s.parent);
        if (!s.engineId.empty())
            o.set("engine", s.engineId);
        if (s.useMsx >= 0)
            o.set("useMsx", s.useMsx == 1);
        if (!s.msxFile.empty())
            o.set("msxFile", s.msxFile);
        json::Value changes = json::Value::array();
        for (const auto& c : s.changes)
        {
            json::Value v = json::Value::object();
            v.set("op", kindName(c.kind));
            v.set("section", c.section);
            if (c.kind != ScenarioChange::Kind::Text)
                v.set("key", c.key);
            if (c.ordinal > 0)
                v.set("row", c.ordinal);
            if (c.kind == ScenarioChange::Kind::Set)
            {
                v.set("column", c.column);
                v.set("from", c.from.empty() ? std::string() : c.from.front());
                v.set("to", c.to.empty() ? std::string() : c.to.front());
            }
            else
            {
                if (c.kind != ScenarioChange::Kind::Add)
                    v.set("from", stringArray(c.from));
                if (c.kind != ScenarioChange::Kind::Remove)
                    v.set("to", stringArray(c.to));
            }
            changes.push(v);
        }
        o.set("changes", changes);
        list.push(o);
    }
    return list;
}

std::vector<Scenario> scenariosFromJson(const json::Value& list)
{
    std::vector<Scenario> out;
    for (const auto& o : list.items())
    {
        if (!o.isObject())
            continue;
        Scenario s;
        s.name = o.get("name").asString();
        if (s.name.empty() || findScenario(out, s.name))
            continue;
        s.description = o.get("description").asString();
        s.parent = o.get("parent").asString();
        s.engineId = o.get("engine").asString();
        if (const json::Value* m = o.find("useMsx"); m && m->isBool())
            s.useMsx = m->asBool() ? 1 : 0;
        s.msxFile = o.get("msxFile").asString();
        for (const auto& v : o.get("changes").items())
        {
            ScenarioChange c;
            const std::string op = v.get("op").asString();
            if (op == "set")
                c.kind = ScenarioChange::Kind::Set;
            else if (op == "row")
                c.kind = ScenarioChange::Kind::Row;
            else if (op == "add")
                c.kind = ScenarioChange::Kind::Add;
            else if (op == "remove")
                c.kind = ScenarioChange::Kind::Remove;
            else if (op == "text")
                c.kind = ScenarioChange::Kind::Text;
            else
                continue;
            c.section = toUpper(v.get("section").asString());
            c.key = v.get("key").asString();
            c.ordinal = static_cast<int>(v.get("row").asNumber(0));
            c.column = static_cast<int>(v.get("column").asNumber(-1));
            c.from = strings(v.get("from"));
            c.to = strings(v.get("to"));
            if (c.section.empty() || (c.kind == ScenarioChange::Kind::Set && c.column < 0))
                continue;
            s.changes.push_back(std::move(c));
        }
        out.push_back(std::move(s));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Result summary
// ---------------------------------------------------------------------------
std::vector<ResultSummaryItem> resultSummary(const ResultSet& results, const Model& model)
{
    std::vector<ResultSummaryItem> out;
    const ProjectUnits units = model.units();
    const int periods = results.periodCount();
    if (periods == 0)
        return out;
    double step = results.reportStep;
    if (step <= 0 && periods > 1)
        step = results.times[1] - results.times[0];
    const double cms = flowToCms(results.flowUnits.empty() ? units.flow : results.flowUnits);
    const double toVolume = cms / units.toSI("volume"); // flow x seconds -> project volume units
    const bool volumes = step > 0 && cms > 0;

    auto kindSet = [&](std::initializer_list<const char*> kinds) {
        std::unordered_set<std::string> ids;
        for (const auto& n : model.nodes())
        {
            for (const char* k : kinds)
            {
                if (n.kind == k)
                    ids.insert(n.id);
            }
        }
        return ids;
    };
    // Visits the values of a variable for the elements in @p only (all when null).
    auto each = [&](const char* table, const char* variable, const std::unordered_set<std::string>* only,
                    const auto& fn) {
        const ResultTable* t = results.table(table);
        const int v = t ? t->variableIndex(variable) : -1;
        if (v < 0)
            return false;
        std::vector<int> elements;
        for (int e = 0; e < t->elementCount(); ++e)
        {
            if (!only || only->count(t->ids[static_cast<std::size_t>(e)]))
                elements.push_back(e);
        }
        if (elements.empty())
            return false;
        for (int p = 0; p < periods; ++p)
        {
            for (int e : elements)
            {
                const double x = t->value(p, v, e);
                if (!std::isnan(x))
                    fn(p, x);
            }
        }
        return true;
    };
    const auto* nodeTable = results.table("node");
    auto unitOf = [&](const char* table, const char* variable) {
        const ResultTable* t = results.table(table);
        const int v = t ? t->variableIndex(variable) : -1;
        return v >= 0 ? t->variables[static_cast<std::size_t>(v)].unit : std::string();
    };

    if (model.dialect() != Dialect::Swmm && nodeTable && nodeTable->variableIndex("Pressure") >= 0)
    {
        const auto junctions = kindSet({"Junction"});
        double lo = std::numeric_limits<double>::max(), sum = 0;
        long long count = 0;
        if (each("node", "Pressure", &junctions, [&](int, double x) {
                lo = std::min(lo, x);
                sum += x;
                ++count;
            }))
        {
            out.push_back({"Minimum junction pressure", unitOf("node", "Pressure"), lo});
            out.push_back({"Mean junction pressure", unitOf("node", "Pressure"), count ? sum / static_cast<double>(count) : 0});
        }
        double vmax = 0;
        if (each("link", "Velocity", nullptr, [&](int, double x) { vmax = std::max(vmax, std::fabs(x)); }))
            out.push_back({"Maximum velocity", unitOf("link", "Velocity"), vmax});
        double demand = 0;
        if (volumes && each("node", "Demand", &junctions, [&](int, double x) { demand += x; }))
            out.push_back({"Demand volume", units.label("volume"), demand * step * toVolume});
        double kw = 0;
        if (step > 0 && each("link", "Pump Power", nullptr, [&](int, double x) { kw += std::fabs(x); }))
            out.push_back({"Pump energy", "kWh", kw * step / 3600.0});
        return out;
    }

    // SWMM
    std::vector<double> perPeriod(static_cast<std::size_t>(periods), 0.0);
    double peak = 0, total = 0;
    if (each("node", "Flooding", nullptr, [&](int, double x) {
            peak = std::max(peak, x);
            total += x;
        }))
    {
        out.push_back({"Peak node flooding", unitOf("node", "Flooding"), peak});
        if (volumes)
            out.push_back({"Flooding volume", units.label("volume"), total * step * toVolume});
    }
    const auto outfalls = kindSet({"Outfall"});
    double outflow = 0, peakOut = 0;
    std::fill(perPeriod.begin(), perPeriod.end(), 0.0);
    if (!outfalls.empty() && each("node", "Total Inflow", &outfalls, [&](int p, double x) {
            outflow += x;
            perPeriod[static_cast<std::size_t>(p)] += x;
        }))
    {
        for (double x : perPeriod)
            peakOut = std::max(peakOut, x);
        out.push_back({"Peak outfall flow", unitOf("node", "Total Inflow"), peakOut});
        if (volumes)
            out.push_back({"Outfall volume", units.label("volume"), outflow * step * toVolume});
    }
    std::fill(perPeriod.begin(), perPeriod.end(), 0.0);
    double peakRunoff = 0;
    if (each("subcatchment", "Runoff", nullptr, [&](int p, double x) { perPeriod[static_cast<std::size_t>(p)] += x; }))
    {
        for (double x : perPeriod)
            peakRunoff = std::max(peakRunoff, x);
        out.push_back({"Peak runoff", unitOf("subcatchment", "Runoff"), peakRunoff});
    }
    double capacity = 0;
    if (each("link", "Capacity", nullptr, [&](int, double x) { capacity = std::max(capacity, x); }))
        out.push_back({"Maximum link capacity used", "", capacity});
    return out;
}

} // namespace mirab::core
