/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/SwmmObjects.cpp
 * Purpose: SWMM named objects: LID controls and usage, transects and unit hydrographs.
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
#include "mirab/core/SwmmObjects.h"

#include "mirab/core/Model.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace mirab::core
{

namespace
{
using Row = std::vector<std::string>;

std::string fmt(double v)
{
    return text::formatDouble(v, 8);
}

std::optional<double> num(const std::vector<std::string>& tokens, std::size_t i)
{
    return i < tokens.size() ? text::toDouble(tokens[i]) : std::nullopt;
}

double numOr(const std::vector<std::string>& tokens, std::size_t i, double fallback = 0.0)
{
    return num(tokens, i).value_or(fallback);
}

void addIssue(std::vector<ObjectIssue>& out, ObjectIssue::Severity sev, std::string field,
              std::string message)
{
    out.push_back({sev, std::move(field), std::move(message)});
}

void error(std::vector<ObjectIssue>& out, std::string field, std::string message)
{
    addIssue(out, ObjectIssue::Severity::Error, std::move(field), std::move(message));
}

void warning(std::vector<ObjectIssue>& out, std::string field, std::string message)
{
    addIssue(out, ObjectIssue::Severity::Warning, std::move(field), std::move(message));
}

/// Index just after the last non-blank line of a section (where new rows go).
std::size_t appendPosition(const InpSection& s)
{
    std::size_t at = s.lines.size();
    while (at > 0 && text::trim(s.lines[at - 1]).empty())
        --at;
    return at;
}

/// Rewrites the rows of one object of a keyed section. A new row whose key
/// matches an old row replaces it in place and keeps its comment; unmatched
/// old rows are removed; unmatched new rows are placed next to their
/// neighbours in @p rows. Lines of other objects and comment lines stay.
void replaceObjectRows(InpSection& s, std::string_view oldName, const std::vector<Row>& rows,
                       const std::function<std::string(const Row&)>& keyOf)
{
    struct OldRow
    {
        std::size_t line;
        std::string key;
        std::string comment;
        bool used = false;
    };
    std::vector<OldRow> old;
    std::map<std::string, int> seen;
    for (std::size_t i = 0; i < s.lines.size(); ++i)
    {
        if (!InpSection::isDataLine(s.lines[i]))
            continue;
        const auto split = text::splitInpLine(s.lines[i]);
        if (split.tokens.empty() || split.tokens[0] != oldName)
            continue;
        std::string key = keyOf(split.tokens);
        key += "#" + std::to_string(seen[key]++);
        old.push_back({i, key, split.comment});
    }
    seen.clear();
    std::vector<int> match(rows.size(), -1);
    for (std::size_t k = 0; k < rows.size(); ++k)
    {
        std::string key = keyOf(rows[k]);
        key += "#" + std::to_string(seen[key]++);
        for (std::size_t j = 0; j < old.size(); ++j)
        {
            if (!old[j].used && old[j].key == key)
            {
                old[j].used = true;
                match[k] = static_cast<int>(j);
                break;
            }
        }
    }
    if (old.empty())
    {
        auto at = s.lines.begin() + static_cast<std::ptrdiff_t>(appendPosition(s));
        for (const auto& r : rows)
            at = s.lines.insert(at, InpSection::formatRow(r)) + 1;
        return;
    }
    // Where each unmatched new row goes: after the previous matched row, or
    // before the next matched one, or at the first old row.
    std::map<std::size_t, std::vector<std::string>> before, after;
    std::vector<std::string> anchor;
    for (std::size_t k = 0; k < rows.size(); ++k)
    {
        if (match[k] >= 0)
            continue;
        const std::string line = InpSection::formatRow(rows[k]);
        int prev = -1, next = -1;
        for (std::size_t p = k; p-- > 0;)
        {
            if (match[p] >= 0)
            {
                prev = match[p];
                break;
            }
        }
        for (std::size_t n = k + 1; n < rows.size() && prev < 0; ++n)
        {
            if (match[n] >= 0)
            {
                next = match[n];
                break;
            }
        }
        if (prev >= 0)
            after[old[static_cast<std::size_t>(prev)].line].push_back(line);
        else if (next >= 0)
            before[old[static_cast<std::size_t>(next)].line].push_back(line);
        else
            anchor.push_back(line);
    }
    std::map<std::size_t, std::size_t> rowOfLine; // old line -> new row index
    for (std::size_t k = 0; k < rows.size(); ++k)
    {
        if (match[k] >= 0)
            rowOfLine[old[static_cast<std::size_t>(match[k])].line] = k;
    }
    std::set<std::size_t> oldLines;
    for (const auto& o : old)
        oldLines.insert(o.line);
    std::map<std::size_t, std::string> commentOf;
    for (const auto& o : old)
        commentOf[o.line] = o.comment;

    std::vector<std::string> out;
    out.reserve(s.lines.size() + rows.size());
    for (std::size_t i = 0; i < s.lines.size(); ++i)
    {
        if (i == old.front().line)
            out.insert(out.end(), anchor.begin(), anchor.end());
        if (!oldLines.count(i))
        {
            out.push_back(s.lines[i]);
            continue;
        }
        auto it = rowOfLine.find(i);
        if (it == rowOfLine.end())
            continue; // removed row
        if (auto b = before.find(i); b != before.end())
            out.insert(out.end(), b->second.begin(), b->second.end());
        out.push_back(InpSection::formatRow(rows[it->second], commentOf[i]));
        if (auto a = after.find(i); a != after.end())
            out.insert(out.end(), a->second.begin(), a->second.end());
    }
    s.lines = std::move(out);
}

bool sameName(const std::string& token, std::string_view name)
{
    return token == name;
}

} // namespace

bool hasErrors(const std::vector<ObjectIssue>& issues)
{
    return std::any_of(issues.begin(), issues.end(), [](const ObjectIssue& i) {
        return i.severity == ObjectIssue::Severity::Error;
    });
}

std::vector<std::string> keyedObjectNames(const InpDocument& doc, std::string_view section)
{
    std::vector<std::string> names;
    std::set<std::string> seen;
    if (const auto* s = doc.find(section))
    {
        for (const auto& tokens : s->allRowTokens())
        {
            if (!tokens.empty() && seen.insert(tokens[0]).second)
                names.push_back(tokens[0]);
        }
    }
    return names;
}

int renameSwmmObjectReferences(InpDocument& doc, std::string_view section, std::string_view oldName,
                               std::string_view newName)
{
    std::string target;
    std::size_t column = 1;
    if (text::iequals(section, "LID_CONTROLS"))
        target = "LID_USAGE";
    else if (text::iequals(section, "HYDROGRAPHS"))
        target = "RDII";
    else if (text::iequals(section, "TRANSECTS"))
    {
        target = "XSECTIONS";
        column = 2;
    }
    InpSection* s = target.empty() || oldName == newName ? nullptr : doc.find(target);
    if (!s)
        return 0;
    int changed = 0;
    const auto rows = s->allRowTokens();
    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        auto t = rows[r];
        if (t.size() <= column || t[column] != oldName)
            continue;
        if (column == 2 && !text::iequals(t[1], "IRREGULAR"))
            continue;
        t[column] = std::string(newName);
        s->setRowTokens(static_cast<int>(r), t);
        ++changed;
    }
    return changed;
}

UnitSystem swmmUnitSystem(const InpDocument& doc)
{
    const auto flow = doc.keyValue("OPTIONS", "FLOW_UNITS");
    return flow && isSiFlowUnit(Dialect::Swmm, *flow) ? UnitSystem::SI : UnitSystem::US;
}

// ---------------------------------------------------------------------------
// LID controls

const char* lidLayerKeyword(LidLayer layer)
{
    switch (layer)
    {
    case LidLayer::Surface:
        return "SURFACE";
    case LidLayer::Soil:
        return "SOIL";
    case LidLayer::Pavement:
        return "PAVEMENT";
    case LidLayer::Storage:
        return "STORAGE";
    case LidLayer::Drain:
        return "DRAIN";
    case LidLayer::DrainMat:
        return "DRAINMAT";
    case LidLayer::Removals:
        return "REMOVALS";
    }
    return "";
}

const char* lidLayerName(LidLayer layer)
{
    switch (layer)
    {
    case LidLayer::Surface:
        return "Surface";
    case LidLayer::Soil:
        return "Soil";
    case LidLayer::Pavement:
        return "Pavement";
    case LidLayer::Storage:
        return "Storage";
    case LidLayer::Drain:
        return "Drain";
    case LidLayer::DrainMat:
        return "Drainage mat";
    case LidLayer::Removals:
        return "Pollutant removals";
    }
    return "";
}

std::optional<LidLayer> lidLayerFromKeyword(std::string_view keyword)
{
    for (LidLayer l : {LidLayer::Surface, LidLayer::Soil, LidLayer::Pavement, LidLayer::Storage,
                       LidLayer::Drain, LidLayer::DrainMat, LidLayer::Removals})
    {
        if (text::iequals(keyword, lidLayerKeyword(l)))
            return l;
    }
    return std::nullopt;
}

const std::vector<LidTypeInfo>& lidTypes()
{
    using L = LidLayer;
    static const std::vector<LidTypeInfo> types = {
        {"BC", "Bio-retention cell", {L::Surface, L::Soil, L::Storage, L::Drain, L::Removals},
         {L::Surface, L::Soil, L::Storage},
         "Depression with vegetation grown in an engineered soil mixture above a gravel "
         "storage bed, with an optional underdrain."},
        {"RG", "Rain garden", {L::Surface, L::Soil}, {L::Surface, L::Soil},
         "Bio-retention cell with only the engineered soil layer and no gravel bed."},
        {"GR", "Green roof", {L::Surface, L::Soil, L::DrainMat},
         {L::Surface, L::Soil, L::DrainMat},
         "Bio-retention cell on a roof: a soil layer over a drainage mat that conveys "
         "percolated water off the roof."},
        {"IT", "Infiltration trench", {L::Surface, L::Storage, L::Drain, L::Removals},
         {L::Surface, L::Storage},
         "Narrow ditch filled with gravel that intercepts runoff and lets it infiltrate, with "
         "an optional underdrain."},
        {"PP", "Permeable pavement",
         {L::Surface, L::Pavement, L::Soil, L::Storage, L::Drain, L::Removals},
         {L::Surface, L::Pavement, L::Storage},
         "Pavement or pavers over a gravel storage layer; rain passes through the pavement "
         "into storage. The soil layer is optional."},
        {"RB", "Rain barrel", {L::Storage, L::Drain, L::Removals}, {L::Storage},
         "Container that collects roof runoff and releases it through a drain valve."},
        {"RD", "Rooftop disconnection", {L::Surface, L::Drain, L::Removals}, {L::Surface},
         "Roof downspouts discharge onto pervious land instead of the sewer; the drain is the "
         "gutter and downspout capacity."},
        {"VS", "Vegetative swale", {L::Surface}, {L::Surface},
         "Grassed channel with a trapezoidal cross-section that slows and infiltrates runoff."},
    };
    return types;
}

const LidTypeInfo* findLidType(std::string_view code)
{
    for (const auto& t : lidTypes())
    {
        if (text::iequals(t.code, code))
            return &t;
    }
    return nullptr;
}

const std::vector<LidParamDef>& lidLayerParams(LidLayer layer)
{
    constexpr double big = 1e30;
    static const std::vector<LidParamDef> surface = {
        {"StorHt", "Berm height", "depth", 0, big, false, false, "6", "150", false,
         "Maximum depth water can pond above the surface before it overflows. For a swale it "
         "is the depth of the trapezoidal channel."},
        {"VegFrac", "Vegetation volume fraction", "fraction", 0, 1, false, true, "0", "0", false,
         "Fraction of the ponding volume filled with vegetation stems and leaves (0 to less "
         "than 1). Usually 0 to 0.2."},
        {"Rough", "Surface roughness", "roughness", 0, big, false, false, "0", "0", false,
         "Manning's n for overland flow over the surface, for example 0.1 for permeable "
         "pavement, 0.24 for a grassed swale; 0 when there is no surface outflow."},
        {"Slope", "Surface slope", "slope", 0, big, false, false, "0", "0", false,
         "Slope of the surface (percent); 0 when there is no surface outflow."},
        {"Xslope", "Swale side slope", "sideslope", 0, big, false, false, "0", "0", false,
         "Slope of the side walls of a swale's trapezoidal cross-section (run over rise). "
         "Ignored by the other types."},
    };
    static const std::vector<LidParamDef> soil = {
        {"Thick", "Thickness", "depth", 0, big, true, false, "18", "450", false,
         "Thickness of the soil layer, typically 18 to 36 in (450 to 900 mm) for bio-retention "
         "and 3 to 6 in (75 to 150 mm) for green roofs."},
        {"Por", "Porosity", "fraction", 0, 1, true, false, "0.5", "0.5", false,
         "Volume of pore space relative to total volume (fraction)."},
        {"FC", "Field capacity", "fraction", 0, 1, false, false, "0.2", "0.2", false,
         "Volume of pore water relative to total volume after the soil has been allowed to "
         "drain fully; must be below the porosity."},
        {"WP", "Wilting point", "fraction", 0, 1, false, false, "0.1", "0.1", false,
         "Volume of pore water relative to total volume for a well dried soil where only bound "
         "water remains; must be below the field capacity."},
        {"Ksat", "Conductivity", "rate", 0, big, true, false, "0.5", "12.7", false,
         "Hydraulic conductivity of the fully saturated soil."},
        {"Kcoeff", "Conductivity slope", "coefficient", 0, big, false, false, "10", "10", false,
         "Slope of the log of conductivity against moisture content (dimensionless), "
         "typically 5 for sands to 15 for silt loams."},
        {"Suct", "Suction head", "depth", 0, big, false, false, "3.5", "88.9", false,
         "Average soil capillary suction along the wetting front (Green-Ampt)."},
    };
    static const std::vector<LidParamDef> pavement = {
        {"Thick", "Thickness", "depth", 0, big, true, false, "6", "150", false,
         "Thickness of the pavement layer, typically 4 to 6 in (100 to 150 mm)."},
        {"Vratio", "Void ratio", "ratio", 0, big, true, false, "0.15", "0.15", false,
         "Volume of void space relative to the volume of solids, typically 0.12 to 0.21. "
         "Use 0 for block pavers whose infill is described by the impervious fraction."},
        {"FracImp", "Impervious surface fraction", "fraction", 0, 1, false, false, "0", "0",
         false, "Ratio of impervious paver material to total area for modular systems; 0 for "
         "continuous porous pavement."},
        {"Perm", "Permeability", "rate", 0, big, true, false, "100", "2540", false,
         "Permeability of the concrete or asphalt, or of the paver joint material."},
        {"Vclog", "Clogging factor", "coefficient", 0, big, false, false, "0", "0", false,
         "Number of pavement void volumes of runoff that clog the pavement completely; 0 "
         "ignores clogging."},
        {"Treg", "Regeneration interval", "days", 0, big, false, false, "0", "0", true,
         "Days between pavement cleanings that restore its permeability; 0 for none."},
        {"Freg", "Regeneration fraction", "fraction", 0, 1, false, false, "0", "0", true,
         "Fraction of the clogging removed by a cleaning (0 to 1)."},
    };
    static const std::vector<LidParamDef> storage = {
        {"Height", "Thickness", "depth", 0, big, false, false, "12", "300", false,
         "Thickness of the gravel storage layer, typically 6 to 18 in (150 to 450 mm); for a "
         "rain barrel its height."},
        {"Vratio", "Void ratio", "ratio", 0, big, true, false, "0.75", "0.75", false,
         "Volume of void space relative to the volume of solids, typically 0.5 to 0.75. "
         "Ignored for rain barrels."},
        {"Seepage", "Seepage rate", "rate", 0, big, false, false, "0.5", "12.7", false,
         "Rate at which water seeps into the native soil below the layer; 0 with an "
         "impermeable liner. Ignored for rain barrels."},
        {"Vclog", "Clogging factor", "coefficient", 0, big, false, false, "0", "0", false,
         "Number of storage void volumes of runoff that clog the bottom of the layer; 0 "
         "ignores clogging."},
        {"Covrd", "Covered", "yesno", 0, 0, false, false, "YES", "YES", true,
         "Rain barrels only: YES when the barrel is covered and receives no direct rain."},
    };
    static const std::vector<LidParamDef> drain = {
        {"Coeff", "Flow coefficient", "coefficient", 0, big, false, false, "0", "0", false,
         "Coefficient C of the drain flow q = C (h - Hd)^n, in in/hr or mm/hr. Use 0 for no "
         "drain. For rooftop disconnection it is the roof drain capacity."},
        {"Expon", "Flow exponent", "exponent", 0, big, false, false, "0.5", "0.5", false,
         "Exponent n of the drain flow equation, 0.5 for an orifice."},
        {"Offset", "Offset height", "depth", 0, big, false, false, "6", "150", false,
         "Height of the drain above the bottom of the storage layer or rain barrel."},
        {"Delay", "Drain delay", "hours", 0, big, false, false, "6", "6", false,
         "Rain barrels only: hours after rain stops before the drain opens; 0 keeps it always "
         "open."},
        {"Hopen", "Open level", "depth", 0, big, false, false, "0", "0", true,
         "Water level in the storage layer at which the drain opens; 0 to ignore."},
        {"Hclose", "Closed level", "depth", 0, big, false, false, "0", "0", true,
         "Water level at which the drain closes; must be below the open level."},
        {"Qcurve", "Control curve", "curve", 0, 0, false, false, "", "", true,
         "Optional control curve that adjusts the drain flow with the water level."},
    };
    static const std::vector<LidParamDef> drainMat = {
        {"Thick", "Thickness", "depth", 0, big, true, false, "3", "75", false,
         "Thickness of the drainage mat, typically 1 to 2 in (25 to 50 mm)."},
        {"Vratio", "Void fraction", "fraction", 0, 1, true, false, "0.5", "0.5", false,
         "Ratio of void volume to total volume of the mat, typically 0.5 to 0.6."},
        {"Rough", "Roughness", "roughness", 0, big, false, false, "0.1", "0.1", false,
         "Manning's n of flow through the mat, typically 0.1 to 0.4."},
    };
    static const std::vector<LidParamDef> none;
    switch (layer)
    {
    case LidLayer::Surface:
        return surface;
    case LidLayer::Soil:
        return soil;
    case LidLayer::Pavement:
        return pavement;
    case LidLayer::Storage:
        return storage;
    case LidLayer::Drain:
        return drain;
    case LidLayer::DrainMat:
        return drainMat;
    case LidLayer::Removals:
        return none;
    }
    return none;
}

std::string lidUnitLabel(std::string_view quantity, UnitSystem system)
{
    const bool si = system == UnitSystem::SI;
    if (quantity == "depth")
        return si ? "mm" : "in";
    if (quantity == "rate")
        return si ? "mm/hr" : "in/hr";
    if (quantity == "recovery")
        return si ? "mm/day" : "in/day";
    if (quantity == "percent" || quantity == "slope")
        return "%";
    if (quantity == "hours")
        return "hr";
    if (quantity == "days")
        return "days";
    if (quantity == "sideslope")
        return "run/rise";
    return {};
}

std::string LidControl::value(LidLayer layer, int index) const
{
    auto it = layers.find(layer);
    if (it == layers.end() || index < 0 || static_cast<std::size_t>(index) >= it->second.size())
        return {};
    return it->second[static_cast<std::size_t>(index)];
}

std::optional<double> LidControl::number(LidLayer layer, int index) const
{
    return text::toDouble(value(layer, index));
}

std::vector<std::vector<std::string>> LidControl::toRows() const
{
    std::vector<Row> rows;
    rows.push_back({name, type});
    for (const auto& [layer, values] : layers)
    {
        Row r = {name, lidLayerKeyword(layer)};
        // Optional trailing values that are empty are left out.
        std::vector<std::string> v = values;
        while (!v.empty() && text::trim(v.back()).empty())
            v.pop_back();
        for (auto& x : v)
            r.push_back(text::trim(x).empty() ? "0" : text::quoteIfNeeded(text::trim(x)));
        rows.push_back(std::move(r));
    }
    return rows;
}

std::vector<std::string> lidControlNames(const InpDocument& doc)
{
    return keyedObjectNames(doc, "LID_CONTROLS");
}

std::optional<LidControl> readLidControl(const InpDocument& doc, std::string_view name)
{
    const auto* s = doc.find("LID_CONTROLS");
    if (!s)
        return std::nullopt;
    LidControl c;
    bool found = false;
    for (const auto& tokens : s->allRowTokens())
    {
        if (tokens.size() < 2 || !sameName(tokens[0], name))
            continue;
        found = true;
        c.name = tokens[0];
        if (findLidType(tokens[1]))
        {
            c.type = text::toUpper(tokens[1]);
            continue;
        }
        if (auto layer = lidLayerFromKeyword(tokens[1]))
        {
            std::vector<std::string> values(tokens.begin() + 2, tokens.end());
            for (auto& v : values)
                v = text::unquote(v);
            if (*layer == LidLayer::Removals)
            {
                auto& r = c.layers[*layer];
                r.insert(r.end(), values.begin(), values.end());
            }
            else
            {
                c.layers[*layer] = std::move(values);
            }
        }
    }
    if (!found)
        return std::nullopt;
    return c;
}

std::vector<std::string> defaultLidLayer(LidLayer layer, std::string_view type, UnitSystem system)
{
    std::vector<std::string> v;
    const bool si = system == UnitSystem::SI;
    for (const auto& p : lidLayerParams(layer))
    {
        if (p.optional && p.key != "Covrd")
            break;
        v.push_back(si ? p.defaultSI : p.defaultUS);
    }
    const std::string t = text::toUpper(type);
    auto set = [&](std::size_t i, const char* us, const char* siv) {
        if (i < v.size())
            v[i] = si ? siv : us;
    };
    switch (layer)
    {
    case LidLayer::Surface:
        if (t == "VS")
        {
            set(0, "12", "300");
            set(2, "0.24", "0.24");
            set(3, "1", "1");
            set(4, "5", "5");
        }
        else if (t == "PP" || t == "GR" || t == "RD")
        {
            set(0, "0", "0");
            set(2, t == "RD" ? "0.015" : "0.1", t == "RD" ? "0.015" : "0.1");
            set(3, "1", "1");
        }
        else if (t == "IT")
        {
            set(0, "0", "0");
        }
        break;
    case LidLayer::Soil:
        if (t == "GR")
            set(0, "3", "75");
        break;
    case LidLayer::Storage:
        if (t == "RB")
        {
            set(0, "36", "900");
            set(2, "0", "0");
        }
        if (t != "RB" && v.size() > 4)
            v.resize(4);
        break;
    case LidLayer::Drain:
        if (t == "RD")
        {
            set(0, "1", "25");
            set(1, "0", "0");
            set(2, "0", "0");
            set(3, "0", "0");
        }
        else if (t != "RB")
        {
            set(3, "0", "0");
        }
        break;
    default:
        break;
    }
    return v;
}

LidControl defaultLidControl(std::string_view name, std::string_view type, UnitSystem system)
{
    LidControl c;
    c.name = std::string(name);
    c.type = text::toUpper(type);
    if (const auto* info = findLidType(type))
    {
        for (LidLayer l : info->layers)
        {
            if (l != LidLayer::Removals)
                c.layers[l] = defaultLidLayer(l, type, system);
        }
    }
    return c;
}

void writeLidControl(InpDocument& doc, const LidControl& control, std::string_view oldName)
{
    InpSection& s = doc.ensure("LID_CONTROLS");
    auto rows = control.toRows();
    replaceObjectRows(s, oldName.empty() ? std::string_view(control.name) : oldName, rows,
                      [](const Row& r) {
                          if (r.size() < 2)
                              return std::string();
                          if (findLidType(r[1]))
                              return std::string("TYPE");
                          return text::toUpper(r[1]);
                      });
}

std::vector<ObjectIssue> validateLidControl(const LidControl& control, const InpDocument* doc)
{
    std::vector<ObjectIssue> out;
    if (text::trim(control.name).empty())
        error(out, "name", "The LID control needs a name.");
    else if (control.name.find_first_of(" \t;\"") != std::string::npos)
        error(out, "name", "The name cannot contain blanks, quotes or semicolons.");
    const LidTypeInfo* info = findLidType(control.type);
    if (!info)
    {
        error(out, "type", "Unknown LID type \"" + control.type + "\".");
        return out;
    }
    for (LidLayer l : info->required)
    {
        if (!control.hasLayer(l))
            error(out, lidLayerKeyword(l),
                  std::string("A ") + text::toLower(info->name) + " needs a " +
                      text::toLower(lidLayerName(l)) + " layer.");
    }
    std::set<std::string> curves, pollutants;
    if (doc)
    {
        for (const auto& n : keyedObjectNames(*doc, "CURVES"))
            curves.insert(text::toUpper(n));
        for (const auto& n : keyedObjectNames(*doc, "POLLUTANTS"))
            pollutants.insert(text::toUpper(n));
    }
    for (const auto& [layer, values] : control.layers)
    {
        const std::string layerName = lidLayerName(layer);
        if (std::find(info->layers.begin(), info->layers.end(), layer) == info->layers.end())
        {
            warning(out, lidLayerKeyword(layer),
                    layerName + " layer is not used by a " + text::toLower(info->name) +
                        " and is ignored.");
            continue;
        }
        if (layer == LidLayer::Removals)
        {
            if (values.size() % 2 != 0)
                error(out, "REMOVALS", "Pollutant removals must be pollutant and percent pairs.");
            for (std::size_t i = 0; i + 1 < values.size(); i += 2)
            {
                const auto pct = text::toDouble(values[i + 1]);
                if (!pct || *pct < 0 || *pct > 100)
                    error(out, "REMOVALS",
                          "Removal of " + values[i] + " must be a percent from 0 to 100.");
                if (doc && !pollutants.count(text::toUpper(values[i])))
                    warning(out, "REMOVALS", "Pollutant " + values[i] + " is not defined.");
            }
            continue;
        }
        const auto& params = lidLayerParams(layer);
        std::size_t required = 0;
        for (const auto& p : params)
        {
            if (!p.optional)
                ++required;
        }
        std::size_t given = values.size();
        while (given > 0 && text::trim(values[given - 1]).empty())
            --given;
        if (given < required)
            error(out, lidLayerKeyword(layer),
                  layerName + " layer: " + std::to_string(required) + " values expected, " +
                      std::to_string(given) + " given.");
        if (given > params.size())
            warning(out, lidLayerKeyword(layer),
                    layerName + " layer: extra values after the last parameter are ignored.");
        for (std::size_t i = 0; i < params.size() && i < values.size(); ++i)
        {
            const auto& p = params[i];
            const std::string field = std::string(lidLayerKeyword(layer)) + "/" + p.key;
            const std::string v = text::trim(values[i]);
            const std::string label = layerName + " " + text::toLower(p.label);
            if (v.empty())
            {
                if (!p.optional)
                    error(out, field, label + " is missing.");
                continue;
            }
            if (p.quantity == "yesno")
            {
                if (!text::iequals(v, "YES") && !text::iequals(v, "NO"))
                    error(out, field, label + " must be YES or NO.");
                continue;
            }
            if (p.quantity == "curve")
            {
                if (doc && !curves.count(text::toUpper(v)))
                    error(out, field, "Curve " + v + " is not defined.");
                continue;
            }
            const auto x = text::toDouble(v);
            if (!x)
            {
                error(out, field, label + ": \"" + v + "\" is not a number.");
                continue;
            }
            const bool low = p.minExclusive ? *x <= p.min : *x < p.min;
            const bool high = p.maxExclusive ? *x >= p.max : *x > p.max;
            if (low || high)
            {
                std::string range;
                if (p.max >= 1e29)
                    range = (p.minExclusive ? "above " : "at least ") + fmt(p.min);
                else if (!p.minExclusive && !p.maxExclusive)
                    range = "from " + fmt(p.min) + " to " + fmt(p.max);
                else
                    range = (p.minExclusive ? "above " : "at least ") + fmt(p.min) +
                            (p.maxExclusive ? " and below " : " and at most ") + fmt(p.max);
                error(out, field, label + " must be " + range + ".");
            }
        }
    }
    // Relations between parameters, as checked by SWMM.
    if (control.hasLayer(LidLayer::Soil))
    {
        const auto por = control.number(LidLayer::Soil, 1);
        const auto fc = control.number(LidLayer::Soil, 2);
        const auto wp = control.number(LidLayer::Soil, 3);
        if (por && fc && *fc >= *por)
            error(out, "SOIL/FC", "Soil field capacity must be less than the porosity.");
        if (fc && wp && *wp >= *fc)
            error(out, "SOIL/WP", "Soil wilting point must be less than the field capacity.");
    }
    if (control.hasLayer(LidLayer::Drain))
    {
        const double open = control.number(LidLayer::Drain, 4).value_or(0.0);
        const double close = control.number(LidLayer::Drain, 5).value_or(0.0);
        if (open > 0.0 && open <= close)
            error(out, "DRAIN/Hopen", "Drain open level must be above the closed level.");
    }
    if (control.type == "VS")
    {
        const double h = control.number(LidLayer::Surface, 0).value_or(0.0);
        const double n = control.number(LidLayer::Surface, 2).value_or(0.0);
        const double sl = control.number(LidLayer::Surface, 3).value_or(0.0);
        if (h <= 0.0)
            error(out, "SURFACE/StorHt", "A vegetative swale needs a berm height above 0.");
        if (n * sl <= 0.0)
            error(out, "SURFACE/Rough",
                  "A vegetative swale needs a surface roughness and slope above 0.");
    }
    auto needPositive = [&](LidLayer l, int index, const char* what) {
        if (control.hasLayer(l) && control.number(l, index).value_or(0.0) <= 0.0)
            error(out, std::string(lidLayerKeyword(l)) + "/" + lidLayerParams(l)[index].key,
                  std::string("A ") + text::toLower(info->name) + " needs a " + what +
                      " above 0.");
    };
    if (control.type == "IT" || control.type == "RB" || control.type == "PP")
        needPositive(LidLayer::Storage, 0, "storage thickness");
    if (control.type == "BC" || control.type == "RG" || control.type == "GR")
        needPositive(LidLayer::Soil, 0, "soil thickness");
    if (control.type == "GR")
        needPositive(LidLayer::DrainMat, 0, "drainage mat thickness");
    if (control.type == "PP")
        needPositive(LidLayer::Pavement, 0, "pavement thickness");
    return out;
}

// ---------------------------------------------------------------------------
// LID usage

std::vector<LidUsage> readLidUsage(const InpDocument& doc, std::string_view subcatchment)
{
    std::vector<LidUsage> out;
    const auto* s = doc.find("LID_USAGE");
    if (!s)
        return out;
    for (const auto& t : s->allRowTokens())
    {
        if (t.size() < 2 || !sameName(t[0], subcatchment))
            continue;
        LidUsage u;
        u.lid = t[1];
        u.number = static_cast<int>(text::toInt(t.size() > 2 ? t[2] : "1").value_or(1));
        u.area = numOr(t, 3);
        u.width = numOr(t, 4);
        u.initSat = numOr(t, 5);
        u.fromImp = numOr(t, 6);
        u.toPervious = numOr(t, 7) != 0.0;
        if (t.size() > 8 && t[8] != "*")
            u.reportFile = text::unquote(t[8]);
        if (t.size() > 9 && t[9] != "*")
            u.drainTo = t[9];
        u.fromPerv = numOr(t, 10);
        out.push_back(std::move(u));
    }
    return out;
}

std::vector<std::string> lidUsageRow(std::string_view subcatchment, const LidUsage& u)
{
    Row r = {std::string(subcatchment), u.lid,         std::to_string(u.number),
             fmt(u.area),               fmt(u.width),  fmt(u.initSat),
             fmt(u.fromImp),            u.toPervious ? "1" : "0"};
    if (!u.reportFile.empty() || !u.drainTo.empty() || u.fromPerv > 0.0)
    {
        r.push_back(u.reportFile.empty() ? "*" : "\"" + u.reportFile + "\"");
        r.push_back(u.drainTo.empty() ? "*" : u.drainTo);
        if (u.fromPerv > 0.0)
            r.push_back(fmt(u.fromPerv));
    }
    return r;
}

void writeLidUsage(InpDocument& doc, std::string_view subcatchment,
                   const std::vector<LidUsage>& units)
{
    if (units.empty() && !doc.has("LID_USAGE"))
        return;
    InpSection& s = doc.ensure("LID_USAGE");
    std::vector<Row> rows;
    for (const auto& u : units)
        rows.push_back(lidUsageRow(subcatchment, u));
    replaceObjectRows(s, subcatchment, rows,
                      [](const Row& r) { return r.size() > 1 ? text::toUpper(r[1]) : ""; });
}

double lidAreaPerSubcatchmentUnit(UnitSystem system)
{
    return system == UnitSystem::SI ? 10000.0 : 43560.0;
}

LidAreaSummary lidAreaSummary(const Model& model, std::string_view subcatchment,
                              const std::vector<LidUsage>& units)
{
    LidAreaSummary s;
    const double per = lidAreaPerSubcatchmentUnit(model.units().system);
    for (const auto& u : units)
    {
        s.lidArea += std::max(0, u.number) * u.area / per;
        s.impervTreated += u.fromImp;
        s.pervTreated += u.fromPerv;
    }
    if (const auto* a = model.findArea(subcatchment))
    {
        if (const auto* sec = model.document().find(a->section))
            s.subcatchmentArea = numOr(sec->rowTokens(a->row), 3);
    }
    if (s.subcatchmentArea > 0.0)
        s.percent = s.lidArea / s.subcatchmentArea * 100.0;
    return s;
}

std::vector<ObjectIssue> validateLidUsage(const Model& model, std::string_view subcatchment,
                                          const std::vector<LidUsage>& units)
{
    std::vector<ObjectIssue> out;
    if (!model.findArea(subcatchment))
        error(out, {}, "Subcatchment " + std::string(subcatchment) + " does not exist.");
    std::set<std::string> lids;
    for (const auto& n : lidControlNames(model.document()))
        lids.insert(n);
    for (std::size_t i = 0; i < units.size(); ++i)
    {
        const auto& u = units[i];
        const std::string field = std::to_string(i);
        const std::string who =
            "Unit " + std::to_string(i + 1) + (u.lid.empty() ? "" : " (" + u.lid + ")");
        if (u.lid.empty())
            error(out, field, who + ": choose a LID control.");
        else if (!lids.count(u.lid))
            error(out, field, who + ": LID control " + u.lid + " is not defined.");
        if (u.number < 0)
            error(out, field, who + ": the number of units cannot be negative.");
        else if (u.number == 0)
            warning(out, field, who + ": zero units; SWMM ignores this row.");
        if (u.area <= 0.0)
            error(out, field, who + ": the area of each unit must be above 0.");
        if (u.width < 0.0)
            error(out, field, who + ": the width cannot be negative.");
        for (auto [v, what] : {std::pair<double, const char*>{u.initSat, "initial saturation"},
                               {u.fromImp, "% impervious area treated"},
                               {u.fromPerv, "% pervious area treated"}})
        {
            if (v < 0.0 || v > 100.0)
                error(out, field, who + ": " + what + " must be from 0 to 100.");
        }
        if (!u.drainTo.empty() && !model.findNode(u.drainTo) && !model.findArea(u.drainTo))
            error(out, field, who + ": drain outlet " + u.drainTo +
                                  " is not a node or subcatchment.");
    }
    const auto sum = lidAreaSummary(model, subcatchment, units);
    if (sum.subcatchmentArea > 0.0 && sum.percent > 100.0 + 1e-9)
        warning(out, "area", "The LID units cover " + text::formatDouble(sum.percent, 4) +
                                 "% of the subcatchment; SWMM stops when they exceed 100%.");
    if (sum.impervTreated > 100.0 + 1e-9)
        error(out, "area", "The units treat more than 100% of the impervious area.");
    if (sum.pervTreated > 100.0 + 1e-9)
        error(out, "area", "The units treat more than 100% of the pervious area.");
    return out;
}

// ---------------------------------------------------------------------------
// Transects

std::vector<std::pair<double, double>> Transect::adjustedPoints() const
{
    const double f = stationFactor == 0.0 ? 1.0 : stationFactor;
    std::vector<std::pair<double, double>> out;
    out.reserve(points.size());
    for (const auto& [x, z] : points)
        out.emplace_back(x * f, z + elevationOffset);
    return out;
}

double Transect::adjustedLeftBank() const
{
    return leftBank * (stationFactor == 0.0 ? 1.0 : stationFactor);
}

double Transect::adjustedRightBank() const
{
    return rightBank * (stationFactor == 0.0 ? 1.0 : stationFactor);
}

std::vector<Transect> readTransects(const InpDocument& doc)
{
    std::vector<Transect> out;
    const auto* s = doc.find("TRANSECTS");
    if (!s)
        return out;
    double nl = 0, nr = 0, nc = 0;
    int pendingNc = -1;
    Transect* cur = nullptr;
    for (std::size_t i = 0; i < s->lines.size(); ++i)
    {
        if (!InpSection::isDataLine(s->lines[i]))
            continue;
        const auto t = text::splitInpLine(s->lines[i]).tokens;
        if (t.empty())
            continue;
        const std::string key = text::toUpper(t[0]);
        if (key == "NC")
        {
            // Same rules as SWMM: 0 keeps the previous value, overbanks
            // default to the channel value.
            const double l = numOr(t, 1), r = numOr(t, 2), c = numOr(t, 3);
            if (l > 0)
                nl = l;
            if (r > 0)
                nr = r;
            if (c > 0)
                nc = c;
            if (nl == 0)
                nl = nc;
            if (nr == 0)
                nr = nc;
            pendingNc = static_cast<int>(i);
        }
        else if (key == "X1")
        {
            Transect tr;
            tr.name = t.size() > 1 ? t[1] : std::string();
            tr.leftBank = numOr(t, 3);
            tr.rightBank = numOr(t, 4);
            tr.meander = numOr(t, 7);
            tr.stationFactor = numOr(t, 8);
            tr.elevationOffset = numOr(t, 9);
            tr.nLeft = nl;
            tr.nRight = nr;
            tr.nChannel = nc;
            tr.ownRoughness = pendingNc >= 0;
            tr.firstLine = pendingNc >= 0 ? pendingNc : static_cast<int>(i);
            tr.lastLine = static_cast<int>(i);
            pendingNc = -1;
            out.push_back(std::move(tr));
            cur = &out.back();
        }
        else if (key == "GR" && cur)
        {
            for (std::size_t k = 1; k + 1 < t.size(); k += 2)
                cur->points.emplace_back(numOr(t, k + 1), numOr(t, k));
            cur->lastLine = static_cast<int>(i);
        }
    }
    return out;
}

std::optional<Transect> readTransect(const InpDocument& doc, std::string_view name)
{
    for (auto& t : readTransects(doc))
    {
        if (sameName(t.name, name))
            return t;
    }
    return std::nullopt;
}

std::vector<std::string> transectNames(const InpDocument& doc)
{
    std::vector<std::string> names;
    for (const auto& t : readTransects(doc))
        names.push_back(t.name);
    return names;
}

namespace
{
std::string padded(const std::vector<std::string>& tokens, std::size_t width)
{
    // Keyword, one blank, then values in columns (the HEC-2 look of SWMM files).
    std::string out = tokens.empty() ? std::string() : tokens[0];
    for (std::size_t i = 1; i < tokens.size(); ++i)
    {
        out += i == 1 ? " " : "";
        out += tokens[i];
        if (i + 1 < tokens.size())
            out.append(tokens[i].size() < width ? width - tokens[i].size() : 1, ' ');
    }
    return out;
}

std::string ncLine(double l, double r, double c)
{
    return padded({"NC", fmt(l), fmt(r), fmt(c)}, 9);
}

bool sameN(double a, double b)
{
    return std::fabs(a - b) <= 1e-12 * std::max(1.0, std::fabs(a));
}
} // namespace

std::vector<std::string> formatTransect(const Transect& t, bool withRoughness)
{
    std::vector<std::string> lines;
    if (withRoughness)
        lines.push_back(ncLine(t.nLeft, t.nRight, t.nChannel));
    std::string name = t.name;
    if (name.size() < 16)
        name.append(16 - name.size(), ' ');
    lines.push_back(padded({"X1", name, std::to_string(t.points.size()), fmt(t.leftBank),
                            fmt(t.rightBank), "0.0", "0.0", fmt(t.meander),
                            fmt(t.stationFactor), fmt(t.elevationOffset)},
                           9));
    for (std::size_t i = 0; i < t.points.size(); i += 5)
    {
        std::vector<std::string> gr = {"GR"};
        for (std::size_t k = i; k < std::min(t.points.size(), i + 5); ++k)
        {
            gr.push_back(fmt(t.points[k].second));
            gr.push_back(fmt(t.points[k].first));
        }
        lines.push_back(padded(gr, 9));
    }
    return lines;
}

void writeTransect(InpDocument& doc, const Transect& transect, std::string_view oldName)
{
    InpSection& s = doc.ensure("TRANSECTS");
    const auto all = readTransects(doc);
    const std::string_view key = oldName.empty() ? std::string_view(transect.name) : oldName;
    std::size_t idx = all.size();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (sameName(all[i].name, key))
        {
            idx = i;
            break;
        }
    }
    if (idx == all.size())
    {
        auto lines = formatTransect(transect, true);
        std::size_t at = appendPosition(s);
        if (!all.empty())
        {
            lines.insert(lines.begin(), "");
        }
        s.lines.insert(s.lines.begin() + static_cast<std::ptrdiff_t>(at), lines.begin(),
                       lines.end());
        return;
    }
    const Transect& old = all[idx];
    const bool inherits = idx > 0 && !old.ownRoughness &&
                          sameN(all[idx - 1].nLeft, transect.nLeft) &&
                          sameN(all[idx - 1].nRight, transect.nRight) &&
                          sameN(all[idx - 1].nChannel, transect.nChannel);
    // The next transect keeps its n values if it inherited them from this one.
    if (idx + 1 < all.size())
    {
        const Transect& next = all[idx + 1];
        if (!next.ownRoughness &&
            (!sameN(next.nLeft, transect.nLeft) || !sameN(next.nRight, transect.nRight) ||
             !sameN(next.nChannel, transect.nChannel)))
        {
            s.lines.insert(s.lines.begin() + next.firstLine,
                           ncLine(next.nLeft, next.nRight, next.nChannel));
        }
    }
    const auto lines = formatTransect(transect, !inherits);
    s.lines.erase(s.lines.begin() + old.firstLine, s.lines.begin() + old.lastLine + 1);
    s.lines.insert(s.lines.begin() + old.firstLine, lines.begin(), lines.end());
}

std::vector<ObjectIssue> validateTransect(const Transect& t)
{
    std::vector<ObjectIssue> out;
    if (text::trim(t.name).empty())
        error(out, "name", "The transect needs a name.");
    else if (t.name.find_first_of(" \t;\"") != std::string::npos)
        error(out, "name", "The name cannot contain blanks, quotes or semicolons.");
    if (t.points.size() < 2)
        error(out, "points", "A transect needs at least two stations.");
    if (t.points.size() >= 1500)
        error(out, "points", "SWMM accepts fewer than 1500 stations per transect.");
    for (std::size_t i = 1; i < t.points.size(); ++i)
    {
        if (t.points[i].first < t.points[i - 1].first)
        {
            error(out, "points", "Station " + fmt(t.points[i].first) + " (row " +
                                     std::to_string(i + 1) +
                                     ") is left of the previous station; stations must "
                                     "increase from left to right.");
            break;
        }
    }
    if (t.nChannel <= 0.0)
        error(out, "nChannel", "The channel Manning's n must be above 0.");
    if (t.nLeft < 0.0 || t.nRight < 0.0)
        error(out, "nLeft", "Manning's n values cannot be negative.");
    if (t.leftBank > t.rightBank)
        error(out, "leftBank", "The left bank station must not be right of the right bank.");
    if (!t.points.empty())
    {
        const double lo = t.points.front().first, hi = t.points.back().first;
        if (t.leftBank < lo || t.leftBank > hi || t.rightBank < lo || t.rightBank > hi)
            warning(out, "leftBank", "A bank station is outside the range of the stations.");
        auto [mn, mx] = std::minmax_element(t.points.begin(), t.points.end(),
                                            [](const auto& a, const auto& b) {
                                                return a.second < b.second;
                                            });
        if (t.points.size() >= 2 && mn->second >= mx->second)
            error(out, "points", "The transect is flat; it needs some depth.");
    }
    if (t.meander < 0.0)
        error(out, "meander", "The meander modifier cannot be negative.");
    if (t.stationFactor < 0.0)
        error(out, "stationFactor", "The stations modifier cannot be negative.");
    return out;
}

TransectGeometry transectGeometry(const Transect& transect, double y)
{
    TransectGeometry g;
    const auto p = transect.adjustedPoints();
    if (p.size() < 2)
        return g;
    for (std::size_t i = 1; i < p.size(); ++i)
    {
        double x1 = p[i - 1].first, z1 = p[i - 1].second;
        double x2 = p[i].first, z2 = p[i].second;
        if (z1 >= y && z2 >= y)
            continue;
        if (z1 < y && z2 < y)
        {
            g.area += (x2 - x1) * (y - 0.5 * (z1 + z2));
            g.topWidth += x2 - x1;
            g.wettedPerimeter += std::hypot(x2 - x1, z2 - z1);
            continue;
        }
        // Partly submerged: clip at the water surface.
        const double xi = x1 + (y - z1) * (x2 - x1) / (z2 - z1);
        const double xb = z1 < y ? x1 : x2;
        const double zb = z1 < y ? z1 : z2;
        g.area += 0.5 * std::fabs(xi - xb) * (y - zb);
        g.topWidth += std::fabs(xi - xb);
        g.wettedPerimeter += std::hypot(xi - xb, y - zb);
    }
    // Vertical walls at both ends, as SWMM extends the section.
    g.wettedPerimeter += std::max(0.0, y - p.front().second) + std::max(0.0, y - p.back().second);
    if (g.wettedPerimeter > 0.0)
        g.hydraulicRadius = g.area / g.wettedPerimeter;
    return g;
}

// ---------------------------------------------------------------------------
// Unit hydrographs

const char* uhMonthKeyword(int month)
{
    static const char* names[] = {"ALL", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    return month >= 0 && month < 13 ? names[month] : "";
}

const char* uhResponseKeyword(int response)
{
    static const char* names[] = {"SHORT", "MEDIUM", "LONG"};
    return response >= 0 && response < 3 ? names[response] : "";
}

bool UnitHydrographGroup::monthDefined(int month) const
{
    if (month < 0 || month >= kMonths)
        return false;
    const auto& m = params[static_cast<std::size_t>(month)];
    return std::any_of(m.begin(), m.end(), [](const auto& r) { return r.defined; });
}

std::vector<std::string> unitHydrographNames(const InpDocument& doc)
{
    return keyedObjectNames(doc, "HYDROGRAPHS");
}

namespace
{
int monthIndex(std::string_view s)
{
    for (int m = 0; m < 13; ++m)
    {
        if (text::iequals(s, uhMonthKeyword(m)))
            return m;
    }
    return -1;
}

int responseIndex(std::string_view s)
{
    for (int r = 0; r < 3; ++r)
    {
        if (text::iequals(s, uhResponseKeyword(r)))
            return r;
    }
    return -1;
}
} // namespace

std::optional<UnitHydrographGroup> readUnitHydrograph(const InpDocument& doc, std::string_view name)
{
    const auto* s = doc.find("HYDROGRAPHS");
    if (!s)
        return std::nullopt;
    UnitHydrographGroup g;
    bool found = false;
    for (const auto& t : s->allRowTokens())
    {
        if (t.empty() || !sameName(t[0], name))
            continue;
        found = true;
        g.name = t[0];
        if (t.size() == 2)
        {
            g.gage = t[1];
            continue;
        }
        if (t.size() < 6)
            continue;
        const int m = monthIndex(t[1]);
        if (m < 0)
            continue;
        auto& month = g.params[static_cast<std::size_t>(m)];
        const int r = responseIndex(t[2]);
        auto readIa = [&](UnitHydrographResponse& p, std::size_t first) {
            p.iaMax = num(t, first);
            p.iaRecovery = num(t, first + 1);
            p.iaInitial = num(t, first + 2);
        };
        if (r >= 0)
        {
            auto& p = month[static_cast<std::size_t>(r)];
            p.defined = true;
            p.r = numOr(t, 3);
            p.t = numOr(t, 4);
            p.k = numOr(t, 5);
            readIa(p, 6);
        }
        else if (t.size() >= 11)
        {
            // Older format: three R-T-K sets on one line, shared IA values.
            for (std::size_t k = 0; k < 3; ++k)
            {
                auto& p = month[k];
                p.defined = true;
                p.r = numOr(t, 2 + 3 * k);
                p.t = numOr(t, 3 + 3 * k);
                p.k = numOr(t, 4 + 3 * k);
                readIa(p, 11);
            }
        }
    }
    if (!found)
        return std::nullopt;
    return g;
}

std::vector<std::vector<std::string>> unitHydrographRows(const UnitHydrographGroup& g)
{
    std::vector<Row> rows;
    rows.push_back({g.name, g.gage});
    for (int m = 0; m < UnitHydrographGroup::kMonths; ++m)
    {
        for (int r = 0; r < UnitHydrographGroup::kResponses; ++r)
        {
            const auto& p = g.params[static_cast<std::size_t>(m)][static_cast<std::size_t>(r)];
            if (!p.defined)
                continue;
            Row row = {g.name, uhMonthKeyword(m), uhResponseKeyword(r), fmt(p.r), fmt(p.t),
                       fmt(p.k)};
            if (p.iaMax || p.iaRecovery || p.iaInitial)
            {
                row.push_back(fmt(p.iaMax.value_or(0.0)));
                row.push_back(fmt(p.iaRecovery.value_or(0.0)));
                row.push_back(fmt(p.iaInitial.value_or(0.0)));
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

void writeUnitHydrograph(InpDocument& doc, const UnitHydrographGroup& group,
                         std::string_view oldName)
{
    InpSection& s = doc.ensure("HYDROGRAPHS");
    replaceObjectRows(s, oldName.empty() ? std::string_view(group.name) : oldName,
                      unitHydrographRows(group), [](const Row& r) {
                          if (r.size() == 2)
                              return std::string("GAGE");
                          if (r.size() < 3)
                              return std::string();
                          const int resp = responseIndex(r[2]);
                          return text::toUpper(r[1]) + "/" +
                                 (resp >= 0 ? std::string(uhResponseKeyword(resp)) : "OLD");
                      });
}

std::vector<ObjectIssue> validateUnitHydrograph(const UnitHydrographGroup& g, const Model* model)
{
    std::vector<ObjectIssue> out;
    if (text::trim(g.name).empty())
        error(out, "name", "The unit hydrograph group needs a name.");
    else if (g.name.find_first_of(" \t;\"") != std::string::npos)
        error(out, "name", "The name cannot contain blanks, quotes or semicolons.");
    if (text::trim(g.gage).empty())
        error(out, "gage", "Choose the rain gage that supplies rainfall to the hydrographs.");
    else if (model && !model->findGage(g.gage))
        error(out, "gage", "Rain gage " + g.gage + " does not exist.");
    bool any = false;
    for (int m = 0; m < UnitHydrographGroup::kMonths; ++m)
    {
        double sum = 0.0;
        const std::string month = uhMonthKeyword(m);
        for (int r = 0; r < UnitHydrographGroup::kResponses; ++r)
        {
            const auto& p = g.params[static_cast<std::size_t>(m)][static_cast<std::size_t>(r)];
            if (!p.defined)
                continue;
            any = true;
            const std::string who = month + " " + text::toLower(uhResponseKeyword(r)) + " term";
            const std::string field = month + "/" + uhResponseKeyword(r);
            sum += p.r;
            if (p.r < 0.0 || p.r > 1.0)
                error(out, field, who + ": R must be a fraction from 0 to 1.");
            if (p.r > 0.0 && p.t <= 0.0)
                error(out, field, who + ": T (time to peak) must be above 0.");
            if (p.r > 0.0 && p.k <= 0.0)
                error(out, field, who + ": K (recession ratio) must be above 0.");
            if (p.t < 0.0 || p.k < 0.0)
                error(out, field, who + ": T and K cannot be negative.");
            for (const auto& ia : {p.iaMax, p.iaRecovery, p.iaInitial})
            {
                if (ia && *ia < 0.0)
                    error(out, field, who + ": initial abstraction values cannot be negative.");
            }
            if (p.iaMax && p.iaInitial && *p.iaInitial > *p.iaMax)
                warning(out, field, who + ": initial abstraction used exceeds the maximum.");
        }
        if (sum > 1.0 + 1e-9)
            warning(out, month, month + ": the three R values add up to more than 1.");
    }
    if (!any)
        warning(out, {}, "No R-T-K parameters are defined; the group produces no inflow.");
    return out;
}

std::vector<std::pair<double, double>> triangularHydrograph(const UnitHydrographResponse& p)
{
    if (!p.defined || p.t <= 0.0 || p.r <= 0.0)
        return {};
    const double base = p.t * (1.0 + p.k);
    const double peak = 2.0 * p.r / base;
    return {{0.0, 0.0}, {p.t, peak}, {base, 0.0}};
}

} // namespace mirab::core
