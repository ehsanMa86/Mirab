/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/src/Model.cpp
 * Purpose: Builds the network graph from an input document and applies schema-aware edits.
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
#include "anahita/core/Model.h"

#include "anahita/core/Text.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace anahita::core
{

using text::iequals;
using text::toUpper;

void Bounds::include(const Point& p)
{
    if (!valid)
    {
        minX = maxX = p.x;
        minY = maxY = p.y;
        valid = true;
        return;
    }
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
}

long long daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

bool parseSwmmDate(std::string_view textValue, long long& days)
{
    const std::string t = text::trim(textValue);
    int parts[3] = {0, 0, 0};
    int n = 0;
    std::string cur;
    for (char c : t + "/")
    {
        if (c == '/' || c == '-')
        {
            if (n >= 3)
                return false;
            auto v = text::toInt(cur);
            if (!v)
                return false;
            parts[n++] = static_cast<int>(*v);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (n != 3)
        return false;
    int month = parts[0], day = parts[1], year = parts[2];
    if (parts[0] > 31) // ISO style YYYY-MM-DD
    {
        year = parts[0];
        month = parts[1];
        day = parts[2];
    }
    if (month < 1 || month > 12 || day < 1 || day > 31)
        return false;
    days = daysFromCivil(year, month, day);
    return true;
}

Model::Model() = default;

Model::Model(InpDocument document, Dialect dialect) : dialect_(dialect), doc_(std::move(document))
{
    rebuild();
}

Model Model::fromFile(const std::filesystem::path& path)
{
    InpDocument doc = InpDocument::load(path);
    const Dialect d = detectDialect(doc);
    return Model(std::move(doc), d);
}

Model Model::fromText(std::string_view textValue)
{
    InpDocument doc = InpDocument::parse(textValue);
    const Dialect d = detectDialect(doc);
    return Model(std::move(doc), d);
}

Dialect Model::detectDialect(const InpDocument& doc)
{
    static const char* swmmOnly[] = {"SUBCATCHMENTS", "CONDUITS", "OUTFALLS", "RAINGAGES",
                                     "XSECTIONS",     "STORAGE",  "DIVIDERS", "ORIFICES",
                                     "WEIRS",         "OUTLETS",  "SUBAREAS", "POLYGONS"};
    static const char* epanetOnly[] = {"PIPES", "RESERVOIRS", "TANKS", "VALVES", "EMITTERS",
                                       "DEMANDS", "LEAKAGE", "MIXING", "ENERGY", "REACTIONS"};
    if (doc.has("SPECIES") && (doc.has("COEFFICIENTS") || doc.has("PIPES") || doc.has("TERMS")) &&
        !doc.has("JUNCTIONS"))
        return Dialect::Msx;
    int swmm = 0, epanet = 0;
    for (const char* s : swmmOnly)
        swmm += doc.has(s) ? 1 : 0;
    for (const char* s : epanetOnly)
        epanet += doc.has(s) ? 1 : 0;
    if (doc.keyValue("OPTIONS", "FLOW_UNITS"))
        swmm += 2;
    if (doc.keyValue("OPTIONS", "UNITS"))
        epanet += 2;
    return swmm > epanet ? Dialect::Swmm : Dialect::Epanet;
}

Model Model::newProject(Dialect dialect, std::string_view flowUnits)
{
    InpDocument doc;
    if (dialect == Dialect::Swmm)
    {
        const std::string fu = flowUnits.empty() ? "CMS" : toUpper(flowUnits);
        doc.ensure("TITLE").appendRow({"New", "Anahita", "SWMM", "project"});
        auto& o = doc.ensure("OPTIONS");
        o.appendRow({"FLOW_UNITS", fu});
        o.appendRow({"INFILTRATION", "HORTON"});
        o.appendRow({"FLOW_ROUTING", "DYNWAVE"});
        o.appendRow({"LINK_OFFSETS", "DEPTH"});
        o.appendRow({"START_DATE", "01/01/2026"});
        o.appendRow({"START_TIME", "00:00:00"});
        o.appendRow({"REPORT_START_DATE", "01/01/2026"});
        o.appendRow({"REPORT_START_TIME", "00:00:00"});
        o.appendRow({"END_DATE", "01/01/2026"});
        o.appendRow({"END_TIME", "12:00:00"});
        o.appendRow({"DRY_DAYS", "5"});
        o.appendRow({"REPORT_STEP", "00:05:00"});
        o.appendRow({"WET_STEP", "00:05:00"});
        o.appendRow({"DRY_STEP", "01:00:00"});
        o.appendRow({"ROUTING_STEP", "0:00:05"});
        o.appendRow({"ALLOW_PONDING", "NO"});
        o.appendRow({"INERTIAL_DAMPING", "PARTIAL"});
        o.appendRow({"NORMAL_FLOW_LIMITED", "BOTH"});
        o.appendRow({"MIN_SURFAREA", "1.167"});
        auto& r = doc.ensure("REPORT");
        r.appendRow({"INPUT", "NO"});
        r.appendRow({"CONTROLS", "NO"});
        r.appendRow({"SUBCATCHMENTS", "ALL"});
        r.appendRow({"NODES", "ALL"});
        r.appendRow({"LINKS", "ALL"});
        doc.ensure("MAP").appendRow({"DIMENSIONS", "0", "0", "10000", "10000"});
    }
    else
    {
        const std::string fu = flowUnits.empty() ? "LPS" : toUpper(flowUnits);
        doc.ensure("TITLE").appendRow({"New", "Anahita", "EPANET", "project"});
        auto& t = doc.ensure("TIMES");
        t.appendRow({"DURATION", "24:00"});
        t.appendRow({"HYDRAULIC", "TIMESTEP", "1:00"});
        t.appendRow({"QUALITY", "TIMESTEP", "0:05"});
        t.appendRow({"PATTERN", "TIMESTEP", "1:00"});
        t.appendRow({"REPORT", "TIMESTEP", "1:00"});
        t.appendRow({"REPORT", "START", "0:00"});
        t.appendRow({"START", "CLOCKTIME", "12", "AM"});
        auto& r = doc.ensure("REPORT");
        r.appendRow({"STATUS", "NO"});
        r.appendRow({"SUMMARY", "NO"});
        auto& o = doc.ensure("OPTIONS");
        o.appendRow({"UNITS", fu});
        o.appendRow({"HEADLOSS", isSiFlowUnit(dialect, fu) ? "D-W" : "H-W"});
        o.appendRow({"SPECIFIC", "GRAVITY", "1.0"});
        o.appendRow({"VISCOSITY", "1.0"});
        o.appendRow({"TRIALS", "200"});
        o.appendRow({"ACCURACY", "0.001"});
        o.appendRow({"UNBALANCED", "CONTINUE", "10"});
        o.appendRow({"PATTERN", "1"});
        o.appendRow({"DEMAND", "MULTIPLIER", "1.0"});
        o.appendRow({"DEMAND", "MODEL", "DDA"});
        o.appendRow({"QUALITY", "NONE", "mg/L"});
        o.appendRow({"DIFFUSIVITY", "1.0"});
        o.appendRow({"TOLERANCE", "0.01"});
    }
    for (const auto& name : sectionOrder(dialect))
    {
        if (name == "END")
            continue;
        if (findSectionDef(dialect, name) && findSectionDef(dialect, name)->keyCategory != Category::None)
            doc.ensure(name);
    }
    if (dialect == Dialect::Epanet)
        doc.ensure("END");
    return Model(std::move(doc), dialect);
}

void Model::rebuild()
{
    nodes_.clear();
    links_.clear();
    areas_.clear();
    gages_.clear();

    for (const auto& def : sectionDefs(dialect_))
    {
        if (def.elementKind.empty())
            continue;
        const InpSection* s = doc_.find(def.name);
        if (!s)
            continue;
        const int rows = s->rowCount();
        for (int r = 0; r < rows; ++r)
        {
            const auto tokens = s->rowTokens(r);
            if (tokens.empty())
                continue;
            switch (def.keyCategory)
            {
            case Category::Node:
            {
                NodeItem n;
                n.id = tokens[0];
                n.kind = def.elementKind;
                n.section = def.name;
                n.row = r;
                nodes_.push_back(std::move(n));
                break;
            }
            case Category::Link:
            {
                LinkItem l;
                l.id = tokens[0];
                l.kind = def.elementKind;
                l.section = def.name;
                l.row = r;
                if (def.fromColumn >= 0 && static_cast<int>(tokens.size()) > def.fromColumn)
                    l.from = tokens[static_cast<std::size_t>(def.fromColumn)];
                if (def.toColumn >= 0 && static_cast<int>(tokens.size()) > def.toColumn)
                    l.to = tokens[static_cast<std::size_t>(def.toColumn)];
                links_.push_back(std::move(l));
                break;
            }
            case Category::Area:
            {
                AreaItem a;
                a.id = tokens[0];
                a.section = def.name;
                a.row = r;
                if (tokens.size() > 1)
                    a.gage = tokens[1];
                if (tokens.size() > 2)
                    a.outlet = tokens[2];
                areas_.push_back(std::move(a));
                break;
            }
            case Category::Gage:
            {
                GageItem g;
                g.id = tokens[0];
                g.section = def.name;
                g.row = r;
                gages_.push_back(std::move(g));
                break;
            }
            case Category::None:
                break;
            }
        }
    }

    std::unordered_map<std::string, std::size_t> nodeIdx, linkIdx, areaIdx, gageIdx;
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        nodeIdx.emplace(nodes_[i].id, i);
    for (std::size_t i = 0; i < links_.size(); ++i)
        linkIdx.emplace(links_[i].id, i);
    for (std::size_t i = 0; i < areas_.size(); ++i)
        areaIdx.emplace(areas_[i].id, i);
    for (std::size_t i = 0; i < gages_.size(); ++i)
        gageIdx.emplace(gages_[i].id, i);

    auto forRows = [&](const char* section, auto&& fn) {
        const InpSection* s = doc_.find(section);
        if (!s)
            return;
        const int rows = s->rowCount();
        for (int r = 0; r < rows; ++r)
        {
            const auto tokens = s->rowTokens(r);
            if (tokens.size() < 3)
                continue;
            const auto x = text::toDouble(tokens[1]);
            const auto y = text::toDouble(tokens[2]);
            if (x && y)
                fn(tokens[0], Point{*x, *y});
        }
    };
    forRows("COORDINATES", [&](const std::string& id, Point p) {
        auto it = nodeIdx.find(id);
        if (it != nodeIdx.end())
        {
            nodes_[it->second].pos = p;
            nodes_[it->second].placed = true;
        }
    });
    forRows("VERTICES", [&](const std::string& id, Point p) {
        auto it = linkIdx.find(id);
        if (it != linkIdx.end())
            links_[it->second].vertices.push_back(p);
    });
    forRows("POLYGONS", [&](const std::string& id, Point p) {
        auto it = areaIdx.find(id);
        if (it != areaIdx.end())
            areas_[it->second].polygon.push_back(p);
    });
    forRows("SYMBOLS", [&](const std::string& id, Point p) {
        auto it = gageIdx.find(id);
        if (it != gageIdx.end())
        {
            gages_[it->second].pos = p;
            gages_[it->second].placed = true;
        }
    });
}

const NodeItem* Model::findNode(std::string_view id) const
{
    for (const auto& n : nodes_)
    {
        if (n.id == id)
            return &n;
    }
    return nullptr;
}

const LinkItem* Model::findLink(std::string_view id) const
{
    for (const auto& l : links_)
    {
        if (l.id == id)
            return &l;
    }
    return nullptr;
}

const AreaItem* Model::findArea(std::string_view id) const
{
    for (const auto& a : areas_)
    {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

const GageItem* Model::findGage(std::string_view id) const
{
    for (const auto& g : gages_)
    {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

int Model::nodeIndex(std::string_view id) const
{
    for (std::size_t i = 0; i < nodes_.size(); ++i)
    {
        if (nodes_[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

int Model::linkIndex(std::string_view id) const
{
    for (std::size_t i = 0; i < links_.size(); ++i)
    {
        if (links_[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

Bounds Model::bounds() const
{
    Bounds b;
    for (const auto& n : nodes_)
    {
        if (n.placed)
            b.include(n.pos);
    }
    for (const auto& l : links_)
    {
        for (const auto& v : l.vertices)
            b.include(v);
    }
    for (const auto& a : areas_)
    {
        for (const auto& v : a.polygon)
            b.include(v);
    }
    for (const auto& g : gages_)
    {
        if (g.placed)
            b.include(g.pos);
    }
    return b;
}

std::string Model::option(std::string_view key) const
{
    return doc_.keyValue("OPTIONS", key).value_or(std::string());
}

void Model::setOption(std::string_view key, const std::string& value)
{
    doc_.setKeyValue("OPTIONS", key, value);
}

ProjectUnits Model::units() const
{
    if (dialect_ == Dialect::Swmm)
        return ProjectUnits::fromFlowUnits(dialect_, option("FLOW_UNITS"));
    std::string pressure = option("PRESSURE");
    // "PRESSURE EXPONENT" is a different option and is filtered by keyValue().
    return ProjectUnits::fromFlowUnits(dialect_, text::splitInpLine(option("UNITS")).tokens.empty()
                                                     ? std::string()
                                                     : text::splitInpLine(option("UNITS")).tokens[0],
                                       pressure);
}

std::string Model::title() const
{
    const InpSection* s = doc_.find("TITLE");
    if (!s)
        return {};
    for (const auto& l : s->lines)
    {
        const std::string t = text::trim(l);
        if (!t.empty() && t.front() != ';')
            return t;
    }
    return {};
}

namespace
{
double epanetTime(const InpDocument& doc, std::string_view key, double fallback)
{
    auto v = doc.keyValue("TIMES", key);
    if (!v)
        return fallback;
    const auto tokens = text::splitInpLine(*v).tokens;
    if (tokens.empty())
        return fallback;
    auto s = text::parseClockOrDuration(tokens[0], tokens.size() > 1 ? tokens[1] : std::string());
    return s.value_or(fallback);
}

double swmmDateTime(const InpDocument& doc, const char* dateKey, const char* timeKey, bool& ok)
{
    long long days = 0;
    auto d = doc.keyValue("OPTIONS", dateKey);
    ok = d && parseSwmmDate(*d, days);
    double seconds = static_cast<double>(days) * 86400.0;
    if (auto t = doc.keyValue("OPTIONS", timeKey))
        seconds += text::parseClockOrDuration(*t).value_or(0.0);
    return seconds;
}
} // namespace

double Model::durationSeconds() const
{
    if (dialect_ == Dialect::Swmm)
    {
        bool okStart = false, okEnd = false;
        const double start = swmmDateTime(doc_, "START_DATE", "START_TIME", okStart);
        const double end = swmmDateTime(doc_, "END_DATE", "END_TIME", okEnd);
        if (!okStart || !okEnd)
            return 0.0;
        return std::max(0.0, end - start);
    }
    return epanetTime(doc_, "DURATION", 0.0);
}

double Model::reportStepSeconds() const
{
    if (dialect_ == Dialect::Swmm)
    {
        auto v = doc_.keyValue("OPTIONS", "REPORT_STEP");
        return v ? text::parseClockOrDuration(*v).value_or(900.0) : 900.0;
    }
    return epanetTime(doc_, "REPORT TIMESTEP", epanetTime(doc_, "HYDRAULIC TIMESTEP", 3600.0));
}

double Model::hydraulicStepSeconds() const
{
    if (dialect_ == Dialect::Swmm)
    {
        auto v = doc_.keyValue("OPTIONS", "ROUTING_STEP");
        return v ? text::parseClockOrDuration(*v, "SEC").value_or(20.0) : 20.0;
    }
    return epanetTime(doc_, "HYDRAULIC TIMESTEP", 3600.0);
}

std::string Model::demandModel() const
{
    const auto tokens = text::splitInpLine(option("DEMAND MODEL")).tokens;
    if (tokens.empty())
        return "DDA";
    return toUpper(tokens[0]);
}

std::vector<ElementRow> Model::rowsFor(Category category, std::string_view id) const
{
    std::vector<ElementRow> out;
    for (const auto& def : sectionDefs(dialect_))
    {
        if (def.keyCategory != category)
            continue;
        if (def.name == "COORDINATES" || def.name == "VERTICES" || def.name == "POLYGONS" ||
            def.name == "SYMBOLS")
            continue;
        const InpSection* s = doc_.find(def.name);
        if (!s)
            continue;
        const int rows = s->rowCount();
        for (int r = 0; r < rows; ++r)
        {
            auto tokens = s->rowTokens(r);
            if (!tokens.empty() && tokens[0] == id)
                out.push_back(ElementRow{def.name, r, &def, std::move(tokens)});
        }
    }
    return out;
}

bool Model::setToken(std::string_view section, int row, int column, const std::string& value)
{
    InpSection* s = doc_.find(section);
    if (!s || row < 0 || row >= s->rowCount() || column < 0)
        return false;
    auto tokens = s->rowTokens(row);
    const std::string v = text::trim(value);
    if (column >= static_cast<int>(tokens.size()))
    {
        if (v.empty())
            return true;
        while (static_cast<int>(tokens.size()) < column)
            tokens.push_back("*");
        tokens.push_back(v);
    }
    else if (v.empty() && column == static_cast<int>(tokens.size()) - 1 && column > 0)
    {
        tokens.pop_back();
    }
    else
    {
        tokens[static_cast<std::size_t>(column)] = v.empty() ? "*" : text::quoteIfNeeded(text::unquote(v));
    }
    s->setRowTokens(row, tokens);
    rebuild();
    return true;
}

bool Model::idInUse(Category category, std::string_view id) const
{
    switch (category)
    {
    case Category::Node:
        return findNode(id) != nullptr;
    case Category::Link:
        return findLink(id) != nullptr;
    case Category::Area:
        return findArea(id) != nullptr;
    case Category::Gage:
        return findGage(id) != nullptr;
    case Category::None:
        break;
    }
    return false;
}

std::string Model::uniqueId(Category category, std::string_view prefix) const
{
    for (int i = 1;; ++i)
    {
        std::string candidate = std::string(prefix) + std::to_string(i);
        if (!idInUse(category, candidate))
            return candidate;
    }
}

namespace
{
bool validId(std::string_view id, std::string* error)
{
    if (id.empty() || id.size() > 31)
    {
        if (error)
            *error = "IDs must have 1 to 31 characters";
        return false;
    }
    for (char c : id)
    {
        if (c == ' ' || c == '\t' || c == ';' || c == '"')
        {
            if (error)
                *error = "IDs cannot contain blanks, quotes or semicolons";
            return false;
        }
    }
    return true;
}

std::vector<std::string> defaultNodeRow(Dialect d, std::string_view kind, const std::string& id)
{
    const std::string k = toUpper(kind);
    if (d == Dialect::Epanet)
    {
        if (k == "JUNCTION")
            return {id, "0", "0", ""};
        if (k == "RESERVOIR")
            return {id, "100"};
        if (k == "TANK")
            return {id, "100", "5", "0", "10", "20", "0", "", ""};
    }
    else
    {
        if (k == "JUNCTION")
            return {id, "0", "3", "0", "0", "0"};
        if (k == "OUTFALL")
            return {id, "0", "FREE", "NO"};
        if (k == "STORAGE")
            return {id, "0", "5", "0", "FUNCTIONAL", "1000", "0", "0", "0", "0"};
        if (k == "DIVIDER")
            return {id, "0", "*", "CUTOFF", "0", "0", "0", "0", "0"};
    }
    return {id};
}

std::vector<std::string> trimTrailingEmpty(std::vector<std::string> v)
{
    while (!v.empty() && v.back().empty())
        v.pop_back();
    return v;
}
} // namespace

std::string Model::coordinateSection(Category category) const
{
    switch (category)
    {
    case Category::Node:
        return "COORDINATES";
    case Category::Link:
        return "VERTICES";
    case Category::Area:
        return "POLYGONS";
    case Category::Gage:
        return "SYMBOLS";
    case Category::None:
        break;
    }
    return {};
}

bool Model::addNode(std::string_view kind, std::string_view id, Point pos, std::string* error)
{
    const SectionDef* def = sectionForKind(dialect_, kind);
    if (!def || def->keyCategory != Category::Node)
    {
        if (error)
            *error = "unknown node type " + std::string(kind);
        return false;
    }
    if (!validId(id, error))
        return false;
    if (idInUse(Category::Node, id))
    {
        if (error)
            *error = "a node named " + std::string(id) + " already exists";
        return false;
    }
    const std::string sid(id);
    doc_.ensure(def->name).appendRow(trimTrailingEmpty(defaultNodeRow(dialect_, kind, sid)));
    doc_.ensure("COORDINATES").appendRow({sid, text::formatDouble(pos.x), text::formatDouble(pos.y)});
    rebuild();
    return true;
}

bool Model::addLink(std::string_view kind, std::string_view id, std::string_view from,
                    std::string_view to, std::string* error)
{
    const SectionDef* def = sectionForKind(dialect_, kind);
    if (!def || def->keyCategory != Category::Link)
    {
        if (error)
            *error = "unknown link type " + std::string(kind);
        return false;
    }
    if (!validId(id, error))
        return false;
    if (idInUse(Category::Link, id))
    {
        if (error)
            *error = "a link named " + std::string(id) + " already exists";
        return false;
    }
    if (!findNode(from) || !findNode(to) || from == to)
    {
        if (error)
            *error = "a link needs two different existing nodes";
        return false;
    }
    const std::string sid(id), a(from), b(to);
    const std::string k = toUpper(kind);
    std::vector<std::string> row;
    if (dialect_ == Dialect::Epanet)
    {
        const bool si = units().system == UnitSystem::SI;
        if (k == "PIPE")
            row = {sid, a, b, si ? "100" : "330", si ? "150" : "6", si ? "0.1" : "100", "0", "Open"};
        else if (k == "PUMP")
            row = {sid, a, b, "POWER", "10"};
        else if (k == "VALVE")
            row = {sid, a, b, si ? "150" : "6", "PRV", si ? "30" : "40", "0"};
        // D-W roughness is in mm (SI) or 1e-3 ft (US); H-W uses C.
        if (k == "PIPE" && toUpper(option("HEADLOSS")).rfind("H-W", 0) == 0)
            row[5] = "100";
        doc_.ensure(def->name).appendRow(row);
    }
    else
    {
        if (k == "CONDUIT")
        {
            doc_.ensure(def->name).appendRow({sid, a, b, "100", "0.013", "0", "0", "0", "0"});
            doc_.ensure("XSECTIONS").appendRow({sid, "CIRCULAR", "1", "0", "0", "0", "1"});
        }
        else if (k == "PUMP")
        {
            doc_.ensure(def->name).appendRow({sid, a, b, "*", "ON", "0", "0"});
        }
        else if (k == "ORIFICE")
        {
            doc_.ensure(def->name).appendRow({sid, a, b, "SIDE", "0", "0.65", "NO", "0"});
            doc_.ensure("XSECTIONS").appendRow({sid, "CIRCULAR", "0.5", "0", "0", "0"});
        }
        else if (k == "WEIR")
        {
            doc_.ensure(def->name).appendRow({sid, a, b, "TRANSVERSE", "0", "3.33", "NO", "0", "0", "YES"});
            doc_.ensure("XSECTIONS").appendRow({sid, "RECT_OPEN", "1", "1", "0", "0"});
        }
        else if (k == "OUTLET")
        {
            doc_.ensure(def->name).appendRow({sid, a, b, "0", "FUNCTIONAL/DEPTH", "10", "0.5", "NO"});
        }
    }
    rebuild();
    return true;
}

bool Model::addArea(std::string_view id, const std::vector<Point>& polygon, std::string_view outlet,
                    std::string* error)
{
    if (dialect_ != Dialect::Swmm)
    {
        if (error)
            *error = "subcatchments exist only in SWMM projects";
        return false;
    }
    if (!validId(id, error))
        return false;
    if (idInUse(Category::Area, id))
    {
        if (error)
            *error = "a subcatchment named " + std::string(id) + " already exists";
        return false;
    }
    const std::string sid(id);
    const std::string gage = gages_.empty() ? std::string("*") : gages_.front().id;
    const std::string out = outlet.empty() ? std::string("*") : std::string(outlet);
    doc_.ensure("SUBCATCHMENTS").appendRow({sid, gage, out, "1", "25", "100", "0.5", "0"});
    doc_.ensure("SUBAREAS").appendRow({sid, "0.01", "0.1", "0.05", "0.05", "25", "OUTLET"});
    doc_.ensure("INFILTRATION").appendRow({sid, "3.0", "0.5", "4", "7", "0"});
    auto& poly = doc_.ensure("POLYGONS");
    for (const auto& p : polygon)
        poly.appendRow({sid, text::formatDouble(p.x), text::formatDouble(p.y)});
    rebuild();
    return true;
}

bool Model::addGage(std::string_view id, Point pos, std::string* error)
{
    if (dialect_ != Dialect::Swmm)
    {
        if (error)
            *error = "rain gages exist only in SWMM projects";
        return false;
    }
    if (!validId(id, error))
        return false;
    if (idInUse(Category::Gage, id))
    {
        if (error)
            *error = "a rain gage named " + std::string(id) + " already exists";
        return false;
    }
    const std::string sid(id);
    doc_.ensure("RAINGAGES").appendRow({sid, "INTENSITY", "1:00", "1.0", "TIMESERIES", "*"});
    doc_.ensure("SYMBOLS").appendRow({sid, text::formatDouble(pos.x), text::formatDouble(pos.y)});
    rebuild();
    return true;
}

namespace
{
bool movePointRow(InpDocument& doc, const char* section, std::string_view id, Point pos)
{
    InpSection& s = doc.ensure(section);
    const int rows = s.rowCount();
    for (int r = 0; r < rows; ++r)
    {
        auto tokens = s.rowTokens(r);
        if (!tokens.empty() && tokens[0] == id)
        {
            s.setRowTokens(r, {tokens[0], text::formatDouble(pos.x), text::formatDouble(pos.y)});
            return true;
        }
    }
    s.appendRow({std::string(id), text::formatDouble(pos.x), text::formatDouble(pos.y)});
    return true;
}
} // namespace

bool Model::moveNode(std::string_view id, Point pos)
{
    if (!findNode(id))
        return false;
    movePointRow(doc_, "COORDINATES", id, pos);
    rebuild();
    return true;
}

bool Model::moveGage(std::string_view id, Point pos)
{
    if (!findGage(id))
        return false;
    movePointRow(doc_, "SYMBOLS", id, pos);
    rebuild();
    return true;
}

bool Model::setVertices(std::string_view linkId, const std::vector<Point>& vertices)
{
    if (!findLink(linkId))
        return false;
    InpSection& s = doc_.ensure("VERTICES");
    for (int r = s.rowCount() - 1; r >= 0; --r)
    {
        auto tokens = s.rowTokens(r);
        if (!tokens.empty() && tokens[0] == linkId)
            s.removeRow(r);
    }
    for (const auto& p : vertices)
        s.appendRow({std::string(linkId), text::formatDouble(p.x), text::formatDouble(p.y)});
    rebuild();
    return true;
}

namespace
{
bool tagMatches(const std::vector<std::string>& tokens, Category category, std::string_view id)
{
    if (tokens.size() < 2 || tokens[1] != id)
        return false;
    const std::string kind = toUpper(tokens[0]);
    switch (category)
    {
    case Category::Node:
        return kind == "NODE";
    case Category::Link:
        return kind == "LINK";
    case Category::Area:
        return kind == "SUBCATCH";
    default:
        return false;
    }
}

const std::set<std::string>& controlKeywords(Category category)
{
    static const std::set<std::string> node = {"NODE", "JUNCTION", "RESERVOIR", "TANK",
                                                "STORAGE", "OUTFALL", "DIVIDER"};
    static const std::set<std::string> link = {"LINK",    "PIPE", "PUMP",   "VALVE",
                                                "CONDUIT", "ORIFICE", "WEIR", "OUTLET"};
    static const std::set<std::string> gage = {"GAGE"};
    static const std::set<std::string> none;
    switch (category)
    {
    case Category::Node:
        return node;
    case Category::Link:
        return link;
    case Category::Gage:
        return gage;
    default:
        return none;
    }
}
} // namespace

void Model::removeKeyedRows(Category category, std::string_view id)
{
    for (const auto& def : sectionDefs(dialect_))
    {
        InpSection* s = doc_.find(def.name);
        if (!s)
            continue;
        if (def.name == "TAGS")
        {
            for (int r = s->rowCount() - 1; r >= 0; --r)
            {
                if (tagMatches(s->rowTokens(r), category, id))
                    s->removeRow(r);
            }
            continue;
        }
        if (def.keyCategory != category)
            continue;
        for (int r = s->rowCount() - 1; r >= 0; --r)
        {
            auto tokens = s->rowTokens(r);
            if (!tokens.empty() && tokens[0] == id)
                s->removeRow(r);
        }
    }
}

bool Model::removeElement(Category category, std::string_view id)
{
    if (!idInUse(category, id))
        return false;
    const std::string sid(id);
    if (category == Category::Node)
    {
        std::vector<std::string> attached;
        for (const auto& l : links_)
        {
            if (l.from == sid || l.to == sid)
                attached.push_back(l.id);
        }
        for (const auto& l : attached)
            removeKeyedRows(Category::Link, l);
    }
    removeKeyedRows(category, sid);
    rebuild();
    return true;
}

bool Model::renameElement(Category category, std::string_view oldId, std::string_view newId,
                          std::string* error)
{
    if (!idInUse(category, oldId))
    {
        if (error)
            *error = "no element named " + std::string(oldId);
        return false;
    }
    if (oldId == newId)
        return true;
    if (!validId(newId, error))
        return false;
    if (idInUse(category, newId))
    {
        if (error)
            *error = std::string(newId) + " is already used";
        return false;
    }
    const std::string from(oldId), to(newId);
    const auto& keywords = controlKeywords(category);

    for (auto& section : doc_.sections)
    {
        const SectionDef* def = findSectionDef(dialect_, section.name);
        const int rows = section.rowCount();
        for (int r = 0; r < rows; ++r)
        {
            auto tokens = section.rowTokens(r);
            bool changed = false;
            if (section.name == "TAGS")
            {
                if (tagMatches(tokens, category, from))
                {
                    tokens[1] = to;
                    changed = true;
                }
            }
            else if (section.name == "CONTROLS" || section.name == "RULES")
            {
                for (std::size_t i = 1; i < tokens.size(); ++i)
                {
                    if (tokens[i] == from && keywords.count(toUpper(tokens[i - 1])))
                    {
                        tokens[i] = to;
                        changed = true;
                    }
                }
            }
            else if (def)
            {
                if (def->keyCategory == category && !tokens.empty() && tokens[0] == from)
                {
                    tokens[0] = to;
                    changed = true;
                }
                for (std::size_t c = 1; c < def->columns.size() && c < tokens.size(); ++c)
                {
                    const ColumnType t = def->columns[c].type;
                    const bool refers =
                        (category == Category::Node &&
                         (t == ColumnType::NodeRef || t == ColumnType::NodeOrAreaRef)) ||
                        (category == Category::Link && t == ColumnType::LinkRef) ||
                        (category == Category::Area &&
                         (t == ColumnType::AreaRef || t == ColumnType::NodeOrAreaRef)) ||
                        (category == Category::Gage && t == ColumnType::GageRef);
                    if (refers && tokens[c] == from)
                    {
                        tokens[c] = to;
                        changed = true;
                    }
                }
            }
            if (changed)
                section.setRowTokens(r, tokens);
        }
    }
    rebuild();
    return true;
}

std::vector<ModelIssue> Model::validate() const
{
    std::vector<ModelIssue> issues;
    std::set<std::string> seen;
    for (const auto& n : nodes_)
    {
        if (!seen.insert(n.id).second)
            issues.push_back({ModelIssue::Severity::Error, "duplicate node ID " + n.id, n.section, n.row});
        if (!n.placed)
            issues.push_back({ModelIssue::Severity::Warning, "node " + n.id + " has no coordinates",
                              n.section, n.row});
    }
    seen.clear();
    for (const auto& l : links_)
    {
        if (!seen.insert(l.id).second)
            issues.push_back({ModelIssue::Severity::Error, "duplicate link ID " + l.id, l.section, l.row});
        if (!findNode(l.from))
            issues.push_back({ModelIssue::Severity::Error,
                              "link " + l.id + " starts at unknown node " + l.from, l.section, l.row});
        if (!findNode(l.to))
            issues.push_back({ModelIssue::Severity::Error,
                              "link " + l.id + " ends at unknown node " + l.to, l.section, l.row});
    }
    for (const auto& a : areas_)
    {
        if (a.outlet != "*" && !a.outlet.empty() && !findNode(a.outlet) && !findArea(a.outlet))
            issues.push_back({ModelIssue::Severity::Error,
                              "subcatchment " + a.id + " drains to unknown outlet " + a.outlet,
                              a.section, a.row});
        if (a.gage != "*" && !a.gage.empty() && !findGage(a.gage))
            issues.push_back({ModelIssue::Severity::Error,
                              "subcatchment " + a.id + " uses unknown rain gage " + a.gage,
                              a.section, a.row});
    }
    if (nodes_.empty())
        issues.push_back({ModelIssue::Severity::Warning, "the network has no nodes", {}, -1});
    return issues;
}

} // namespace anahita::core
