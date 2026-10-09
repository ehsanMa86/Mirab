/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Schema.cpp
 * Purpose: Definitions of the EPANET 2.2/2.3 and SWMM 5.2 section layouts used by the editor.
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
#include "mirab/core/Schema.h"

#include "mirab/core/Text.h"

namespace mirab::core
{

const char* dialectName(Dialect d)
{
    switch (d)
    {
    case Dialect::Epanet:
        return "EPANET";
    case Dialect::Swmm:
        return "SWMM";
    case Dialect::Msx:
        return "EPANET-MSX";
    }
    return "?";
}

namespace
{
using CT = ColumnType;

ColumnDef col(std::string name, CT type, std::string quantity = {}, bool optional = false,
              std::string help = {})
{
    ColumnDef c;
    c.name = std::move(name);
    c.type = type;
    c.quantity = std::move(quantity);
    c.optional = optional;
    c.help = std::move(help);
    return c;
}

ColumnDef choice(std::string name, std::vector<std::string> choices, bool optional = false,
                 std::string help = {})
{
    ColumnDef c;
    c.name = std::move(name);
    c.type = CT::Choice;
    c.choices = std::move(choices);
    c.optional = optional;
    c.help = std::move(help);
    return c;
}

SectionDef sect(std::string name, Category key, std::string kind, std::vector<ColumnDef> cols,
                std::string description, int from = -1, int to = -1)
{
    SectionDef s;
    s.name = std::move(name);
    s.keyCategory = key;
    s.elementKind = std::move(kind);
    s.columns = std::move(cols);
    s.description = std::move(description);
    s.fromColumn = from;
    s.toColumn = to;
    return s;
}

SectionDef freeSect(std::string name, std::string description)
{
    return sect(std::move(name), Category::None, {}, {}, std::move(description));
}

std::vector<SectionDef> buildEpanet()
{
    std::vector<SectionDef> v;
    v.push_back(freeSect("TITLE", "Descriptive title"));
    v.push_back(sect("JUNCTIONS", Category::Node, "Junction",
                     {col("ID", CT::Id), col("Elevation", CT::Number, "elevation"),
                      col("Demand", CT::Number, "flow", true),
                      col("Pattern", CT::PatternRef, {}, true)},
                     "Junction nodes"));
    v.push_back(sect("RESERVOIRS", Category::Node, "Reservoir",
                     {col("ID", CT::Id), col("Head", CT::Number, "elevation"),
                      col("Pattern", CT::PatternRef, {}, true)},
                     "Reservoir nodes"));
    v.push_back(sect("TANKS", Category::Node, "Tank",
                     {col("ID", CT::Id), col("Elevation", CT::Number, "elevation"),
                      col("InitLevel", CT::Number, "length"), col("MinLevel", CT::Number, "length"),
                      col("MaxLevel", CT::Number, "length"), col("Diameter", CT::Number, "length"),
                      col("MinVol", CT::Number, "volume", true),
                      col("VolCurve", CT::CurveRef, {}, true),
                      choice("Overflow", {"YES", "NO"}, true)},
                     "Storage tanks"));
    {
        auto s = sect("PIPES", Category::Link, "Pipe",
                      {col("ID", CT::Id), col("Node1", CT::NodeRef), col("Node2", CT::NodeRef),
                       col("Length", CT::Number, "length"), col("Diameter", CT::Number, "diameter"),
                       col("Roughness", CT::Number, "roughness"),
                       col("MinorLoss", CT::Number, {}, true),
                       choice("Status", {"OPEN", "CLOSED", "CV"}, true)},
                      "Pipes", 1, 2);
        v.push_back(s);
    }
    {
        auto s = sect("PUMPS", Category::Link, "Pump",
                      {col("ID", CT::Id), col("Node1", CT::NodeRef), col("Node2", CT::NodeRef),
                       col("Parameters", CT::Text, {}, true,
                           "HEAD curve, POWER kw, SPEED s, PATTERN p")},
                      "Pumps", 1, 2);
        s.variadic = true;
        v.push_back(s);
    }
    {
        auto s = sect("VALVES", Category::Link, "Valve",
                      {col("ID", CT::Id), col("Node1", CT::NodeRef), col("Node2", CT::NodeRef),
                       col("Diameter", CT::Number, "diameter"),
                       choice("Type", {"PRV", "PSV", "PBV", "FCV", "TCV", "GPV", "PCV"}),
                       col("Setting", CT::Text), col("MinorLoss", CT::Number, {}, true),
                       col("Curve", CT::CurveRef, {}, true, "PCV loss curve (EPANET 2.3)")},
                      "Valves", 1, 2);
        v.push_back(s);
    }
    {
        auto s = sect("DEMANDS", Category::Node, {},
                      {col("Junction", CT::NodeRef), col("Demand", CT::Number, "flow"),
                       col("Pattern", CT::PatternRef, {}, true), col("Category", CT::Text, {}, true)},
                      "Multiple demand categories");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    v.push_back(sect("EMITTERS", Category::Node, {},
                     {col("Junction", CT::NodeRef), col("Coefficient", CT::Number)},
                     "Emitter (sprinkler) coefficients"));
    v.push_back(sect("LEAKAGE", Category::Link, {},
                     {col("Pipe", CT::LinkRef), col("LeakArea", CT::Number, {}, false,
                                                    "leak area per 100 length units (EPANET 2.3)"),
                      col("LeakExpansion", CT::Number, {}, false,
                          "leak area expansion per unit of pressure head (EPANET 2.3)")},
                     "FAVAD pipe leakage (EPANET 2.3)"));
    v.push_back(sect("STATUS", Category::Link, {},
                     {col("Link", CT::LinkRef), col("Status/Setting", CT::Text)},
                     "Initial link status"));
    {
        auto s = sect("PATTERNS", Category::None, {},
                      {col("ID", CT::Text), col("Multipliers", CT::Text)}, "Time patterns");
        s.variadic = true;
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    {
        auto s = sect("CURVES", Category::None, {},
                      {col("ID", CT::Text), col("X", CT::Number), col("Y", CT::Number)},
                      "Data curves");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    v.push_back(freeSect("CONTROLS", "Simple controls"));
    v.push_back(freeSect("RULES", "Rule-based controls"));
    v.push_back(freeSect("ENERGY", "Energy analysis options"));
    v.push_back(sect("QUALITY", Category::Node, {},
                     {col("Node", CT::NodeRef), col("InitQual", CT::Number)},
                     "Initial water quality"));
    v.push_back(sect("SOURCES", Category::Node, {},
                     {col("Node", CT::NodeRef),
                      choice("Type", {"CONCEN", "MASS", "FLOWPACED", "SETPOINT"}),
                      col("Quality", CT::Number), col("Pattern", CT::PatternRef, {}, true)},
                     "Water quality sources"));
    v.push_back(freeSect("REACTIONS", "Reaction coefficients"));
    v.push_back(sect("MIXING", Category::Node, {},
                     {col("Tank", CT::NodeRef), choice("Model", {"MIXED", "2COMP", "FIFO", "LIFO"}),
                      col("Fraction", CT::Number, {}, true)},
                     "Tank mixing models"));
    v.push_back(freeSect("TIMES", "Time step options"));
    v.push_back(freeSect("REPORT", "Report options"));
    v.push_back(freeSect("OPTIONS", "Analysis options"));
    v.push_back(sect("COORDINATES", Category::Node, {},
                     {col("Node", CT::NodeRef), col("X", CT::Number), col("Y", CT::Number)},
                     "Node coordinates"));
    {
        auto s = sect("VERTICES", Category::Link, {},
                      {col("Link", CT::LinkRef), col("X", CT::Number), col("Y", CT::Number)},
                      "Link vertices");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    v.push_back(freeSect("LABELS", "Map labels"));
    v.push_back(freeSect("BACKDROP", "Map backdrop"));
    v.push_back(freeSect("TAGS", "Object tags"));
    v.push_back(freeSect("END", "End of file"));
    return v;
}

std::vector<SectionDef> buildSwmm()
{
    std::vector<SectionDef> v;
    v.push_back(freeSect("TITLE", "Descriptive title"));
    v.push_back(freeSect("OPTIONS", "Analysis options"));
    v.push_back(freeSect("EVAPORATION", "Evaporation data"));
    {
        auto s = sect("RAINGAGES", Category::Gage, "RainGage",
                      {col("Name", CT::Id),
                       choice("Format", {"INTENSITY", "VOLUME", "CUMULATIVE"}),
                       col("Interval", CT::Text), col("SCF", CT::Number),
                       col("Source", CT::Text, {}, false, "TIMESERIES name | FILE path station units")},
                      "Rain gages");
        s.variadic = true;
        v.push_back(s);
    }
    {
        auto s = sect("SUBCATCHMENTS", Category::Area, "Subcatchment",
                      {col("Name", CT::Id), col("RainGage", CT::GageRef),
                       col("Outlet", CT::NodeOrAreaRef), col("Area", CT::Number, "area"),
                       col("PctImperv", CT::Number), col("Width", CT::Number, "length"),
                       col("PctSlope", CT::Number), col("CurbLen", CT::Number, "length", true),
                       col("SnowPack", CT::Text, {}, true)},
                      "Subcatchments");
        v.push_back(s);
    }
    v.push_back(sect("SUBAREAS", Category::Area, {},
                     {col("Subcatchment", CT::AreaRef), col("N-Imperv", CT::Number),
                      col("N-Perv", CT::Number), col("S-Imperv", CT::Number, "depth"),
                      col("S-Perv", CT::Number, "depth"), col("PctZero", CT::Number),
                      choice("RouteTo", {"OUTLET", "IMPERVIOUS", "PERVIOUS"}),
                      col("PctRouted", CT::Number, {}, true)},
                     "Subcatchment subareas"));
    {
        auto s = sect("INFILTRATION", Category::Area, {},
                      {col("Subcatchment", CT::AreaRef), col("Param1", CT::Number),
                       col("Param2", CT::Number), col("Param3", CT::Number),
                       col("Param4", CT::Number, {}, true), col("Param5", CT::Number, {}, true)},
                      "Infiltration parameters");
        s.variadic = true;
        v.push_back(s);
    }
    v.push_back(sect("JUNCTIONS", Category::Node, "Junction",
                     {col("Name", CT::Id), col("Elevation", CT::Number, "elevation"),
                      col("MaxDepth", CT::Number, "depth"), col("InitDepth", CT::Number, "depth", true),
                      col("SurDepth", CT::Number, "depth", true),
                      col("Aponded", CT::Number, "area", true)},
                     "Junction nodes"));
    {
        auto s = sect("OUTFALLS", Category::Node, "Outfall",
                      {col("Name", CT::Id), col("Elevation", CT::Number, "elevation"),
                       choice("Type", {"FREE", "NORMAL", "FIXED", "TIDAL", "TIMESERIES"}),
                       col("StageData", CT::Text, {}, true), choice("Gated", {"NO", "YES"}, true),
                       col("RouteTo", CT::AreaRef, {}, true)},
                      "Outfall nodes");
        s.variadic = true;
        v.push_back(s);
    }
    {
        auto s = sect("DIVIDERS", Category::Node, "Divider",
                      {col("Name", CT::Id), col("Elevation", CT::Number, "elevation"),
                       col("DivertedLink", CT::LinkRef),
                       choice("Type", {"OVERFLOW", "CUTOFF", "TABULAR", "WEIR"}),
                       col("Parameters", CT::Text, {}, true)},
                      "Flow divider nodes");
        s.variadic = true;
        v.push_back(s);
    }
    {
        auto s = sect("STORAGE", Category::Node, "Storage",
                      {col("Name", CT::Id), col("Elevation", CT::Number, "elevation"),
                       col("MaxDepth", CT::Number, "depth"), col("InitDepth", CT::Number, "depth"),
                       choice("Shape", {"FUNCTIONAL", "TABULAR", "CYLINDRICAL", "CONICAL",
                                        "PARABOLOID", "PYRAMIDAL"}),
                       col("Parameters", CT::Text, {}, true)},
                      "Storage unit nodes");
        s.variadic = true;
        v.push_back(s);
    }
    v.push_back(sect("CONDUITS", Category::Link, "Conduit",
                     {col("Name", CT::Id), col("FromNode", CT::NodeRef), col("ToNode", CT::NodeRef),
                      col("Length", CT::Number, "length"), col("Roughness", CT::Number),
                      col("InOffset", CT::Number, "depth"), col("OutOffset", CT::Number, "depth"),
                      col("InitFlow", CT::Number, "flow", true),
                      col("MaxFlow", CT::Number, "flow", true)},
                     "Conduit links", 1, 2));
    v.push_back(sect("PUMPS", Category::Link, "Pump",
                     {col("Name", CT::Id), col("FromNode", CT::NodeRef), col("ToNode", CT::NodeRef),
                      col("PumpCurve", CT::CurveRef), choice("Status", {"ON", "OFF"}, true),
                      col("Startup", CT::Number, "depth", true),
                      col("Shutoff", CT::Number, "depth", true)},
                     "Pump links", 1, 2));
    v.push_back(sect("ORIFICES", Category::Link, "Orifice",
                     {col("Name", CT::Id), col("FromNode", CT::NodeRef), col("ToNode", CT::NodeRef),
                      choice("Type", {"SIDE", "BOTTOM"}), col("Offset", CT::Number, "depth"),
                      col("Qcoeff", CT::Number), choice("Gated", {"NO", "YES"}, true),
                      col("CloseTime", CT::Number, {}, true)},
                     "Orifice links", 1, 2));
    {
        auto s = sect("WEIRS", Category::Link, "Weir",
                      {col("Name", CT::Id), col("FromNode", CT::NodeRef), col("ToNode", CT::NodeRef),
                       choice("Type", {"TRANSVERSE", "SIDEFLOW", "V-NOTCH", "TRAPEZOIDAL", "ROADWAY"}),
                       col("CrestHt", CT::Number, "depth"), col("Qcoeff", CT::Number),
                       choice("Gated", {"NO", "YES"}, true), col("EndCon", CT::Number, {}, true),
                       col("EndCoeff", CT::Number, {}, true),
                       choice("Surcharge", {"YES", "NO"}, true)},
                      "Weir links", 1, 2);
        s.variadic = true;
        v.push_back(s);
    }
    {
        auto s = sect("OUTLETS", Category::Link, "Outlet",
                      {col("Name", CT::Id), col("FromNode", CT::NodeRef), col("ToNode", CT::NodeRef),
                       col("Offset", CT::Number, "depth"),
                       choice("Type", {"TABULAR/DEPTH", "TABULAR/HEAD", "FUNCTIONAL/DEPTH",
                                       "FUNCTIONAL/HEAD"}),
                       col("Parameters", CT::Text, {}, true)},
                      "Outlet links", 1, 2);
        s.variadic = true;
        v.push_back(s);
    }
    {
        auto s = sect("XSECTIONS", Category::Link, {},
                      {col("Link", CT::LinkRef), col("Shape", CT::Text),
                       col("Geom1", CT::Text, "depth"), col("Geom2", CT::Number, "length", true),
                       col("Geom3", CT::Number, {}, true), col("Geom4", CT::Number, {}, true),
                       col("Barrels", CT::Number, {}, true), col("Culvert", CT::Number, {}, true)},
                      "Link cross sections");
        s.variadic = true;
        v.push_back(s);
    }
    v.push_back(sect("LOSSES", Category::Link, {},
                     {col("Link", CT::LinkRef), col("Kentry", CT::Number), col("Kexit", CT::Number),
                      col("Kavg", CT::Number), choice("FlapGate", {"NO", "YES"}, true),
                      col("Seepage", CT::Number, {}, true)},
                     "Conduit losses"));
    for (const char* n : {"TRANSECTS", "STREETS", "INLETS", "INLET_USAGE", "CONTROLS",
                          "POLLUTANTS", "LANDUSES", "COVERAGES", "LOADINGS", "BUILDUP", "WASHOFF",
                          "TREATMENT", "INFLOWS", "DWF", "RDII", "HYDROGRAPHS", "LID_CONTROLS",
                          "LID_USAGE", "AQUIFERS", "GROUNDWATER", "GWF", "SNOWPACKS",
                          "TEMPERATURE", "ADJUSTMENTS", "EVENTS", "FILES", "PROFILES", "REPORT",
                          "TAGS", "MAP", "LABELS", "BACKDROP"})
        v.push_back(freeSect(n, {}));
    {
        auto s = sect("CURVES", Category::None, {},
                      {col("Name", CT::Text), col("Type/X", CT::Text), col("X/Y", CT::Text)},
                      "Data curves");
        s.variadic = true;
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    {
        auto s = sect("TIMESERIES", Category::None, {},
                      {col("Name", CT::Text), col("Date/Time", CT::Text), col("Value", CT::Text)},
                      "Time series");
        s.variadic = true;
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    {
        auto s = sect("PATTERNS", Category::None, {},
                      {col("Name", CT::Text), col("Type/Factors", CT::Text)}, "Time patterns");
        s.variadic = true;
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    v.push_back(sect("COORDINATES", Category::Node, {},
                     {col("Node", CT::NodeRef), col("X", CT::Number), col("Y", CT::Number)},
                     "Node coordinates"));
    {
        auto s = sect("VERTICES", Category::Link, {},
                      {col("Link", CT::LinkRef), col("X", CT::Number), col("Y", CT::Number)},
                      "Link vertices");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    {
        auto s = sect("POLYGONS", Category::Area, {},
                      {col("Subcatchment", CT::AreaRef), col("X", CT::Number), col("Y", CT::Number)},
                      "Subcatchment outlines");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    v.push_back(sect("SYMBOLS", Category::Gage, {},
                     {col("Gage", CT::GageRef), col("X", CT::Number), col("Y", CT::Number)},
                     "Rain gage positions"));
    return v;
}

std::vector<SectionDef> buildMsx()
{
    std::vector<SectionDef> v;
    for (const char* n : {"TITLE", "OPTIONS", "SPECIES", "COEFFICIENTS", "TERMS", "PIPES", "TANKS",
                          "SOURCES", "QUALITY", "PARAMETERS", "PATTERNS", "DIFFUSIVITY", "REPORT"})
        v.push_back(freeSect(n, {}));
    return v;
}
} // namespace

const std::vector<SectionDef>& sectionDefs(Dialect dialect)
{
    static const std::vector<SectionDef> epanet = buildEpanet();
    static const std::vector<SectionDef> swmm = buildSwmm();
    static const std::vector<SectionDef> msx = buildMsx();
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

const SectionDef* findSectionDef(Dialect dialect, std::string_view name)
{
    const std::string n = text::toUpper(name);
    for (const auto& s : sectionDefs(dialect))
    {
        if (s.name == n)
            return &s;
    }
    return nullptr;
}

const SectionDef* sectionForKind(Dialect dialect, std::string_view kind)
{
    for (const auto& s : sectionDefs(dialect))
    {
        if (!s.elementKind.empty() && text::iequals(s.elementKind, kind))
            return &s;
    }
    return nullptr;
}

std::vector<std::string> elementKinds(Dialect dialect, Category category)
{
    std::vector<std::string> kinds;
    for (const auto& s : sectionDefs(dialect))
    {
        if (!s.elementKind.empty() && s.keyCategory == category)
            kinds.push_back(s.elementKind);
    }
    return kinds;
}

const std::vector<std::string>& sectionOrder(Dialect dialect)
{
    static const std::vector<std::string> epanet = [] {
        std::vector<std::string> v;
        for (const auto& s : sectionDefs(Dialect::Epanet))
            v.push_back(s.name);
        return v;
    }();
    static const std::vector<std::string> swmm = {
        "TITLE",       "OPTIONS",  "EVAPORATION", "RAINGAGES", "SUBCATCHMENTS", "SUBAREAS",
        "INFILTRATION", "JUNCTIONS", "OUTFALLS",   "DIVIDERS",  "STORAGE",       "CONDUITS",
        "PUMPS",       "ORIFICES", "WEIRS",       "OUTLETS",   "XSECTIONS",     "LOSSES",
        "CURVES",      "TIMESERIES", "PATTERNS",  "REPORT",    "TAGS",          "MAP",
        "COORDINATES", "VERTICES", "POLYGONS",    "SYMBOLS",   "LABELS"};
    static const std::vector<std::string> msx = {"TITLE", "OPTIONS", "SPECIES", "COEFFICIENTS",
                                                 "TERMS", "PIPES",   "TANKS",   "SOURCES",
                                                 "QUALITY", "PARAMETERS", "PATTERNS", "REPORT"};
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

} // namespace mirab::core
