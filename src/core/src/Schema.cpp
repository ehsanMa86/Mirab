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

ColumnDef ref(std::string name, std::string kind, bool optional = false, std::string help = {},
              std::vector<std::string> keywords = {})
{
    ColumnDef c = col(std::move(name), CT::ObjectRef, {}, optional, std::move(help));
    c.object = std::move(kind);
    c.choices = std::move(keywords);
    return c;
}

ColumnDef blankAsEmpty(ColumnDef c)
{
    c.blank = "\"\"";
    return c;
}

SectionDef objectSect(std::string name, std::string kind, std::vector<ColumnDef> cols,
                      std::string description)
{
    SectionDef s = sect(std::move(name), Category::None, {}, std::move(cols), std::move(description));
    s.objectKind = std::move(kind);
    return s;
}

/// Water quality, groundwater, RDII, snow, streets and inlets (SWMM 5.2 input format).
void appendSwmmData(std::vector<SectionDef>& v)
{
    v.push_back(objectSect(
        "POLLUTANTS", "Pollutant",
        {col("Name", CT::Id), choice("Units", {"MG/L", "UG/L", "#/L"}, false, "concentration units"),
         col("Crain", CT::Number, {}, false, "concentration in rainfall"),
         col("Cgw", CT::Number, {}, false, "concentration in groundwater"),
         col("Crdii", CT::Number, {}, false, "concentration in RDII inflow"),
         col("Kdecay", CT::Number, "decay", false, "first-order decay coefficient"),
         choice("SnowOnly", {"NO", "YES"}, true, "buildup occurs only when there is snow"),
         ref("CoPollutant", "Pollutant", true, "pollutant whose runoff concentration adds to this one"),
         col("CoFraction", CT::Number, {}, true, "fraction of the co-pollutant concentration"),
         col("Cdwf", CT::Number, {}, true, "concentration in dry weather flow"),
         col("Cinit", CT::Number, {}, true, "initial concentration throughout the system")},
        "Pollutants"));
    v.push_back(objectSect(
        "LANDUSES", "Landuse",
        {col("Name", CT::Id),
         col("SweepInterval", CT::Number, "days", true, "days between street sweeping"),
         col("Availability", CT::Number, {}, true, "fraction of buildup available for sweeping"),
         col("LastSweep", CT::Number, "days", true, "days since last swept at the start")},
        "Land uses"));
    {
        auto s = sect("BUILDUP", Category::None, {},
                      {ref("Landuse", "Landuse"), ref("Pollutant", "Pollutant"),
                       choice("Function", {"NONE", "POW", "EXP", "SAT", "EXT"}, false,
                              "buildup function: power, exponential, saturation or external time series"),
                       col("C1", CT::Number, {}, false, "maximum buildup (mass per unit of area or curb length)"),
                       col("C2", CT::Number, {}, false, "rate constant (or scaling factor for EXT)"),
                       col("C3", CT::Text, {}, false, "power or half-saturation constant (time series for EXT)"),
                       choice("PerUnit", {"AREA", "CURB"}, false, "buildup is per unit of area or of curb length")},
                      "Pollutant buildup");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    {
        auto s = sect("WASHOFF", Category::None, {},
                      {ref("Landuse", "Landuse"), ref("Pollutant", "Pollutant"),
                       choice("Function", {"NONE", "EXP", "RC", "EMC"}, false,
                              "washoff function: exponential, rating curve or event mean concentration"),
                       col("C1", CT::Number, {}, false, "washoff coefficient or event mean concentration"),
                       col("C2", CT::Number, {}, false, "washoff exponent"),
                       col("SweepRemoval", CT::Number, "percent", false, "street sweeping removal efficiency"),
                       col("BmpRemoval", CT::Number, "percent", false, "BMP removal efficiency")},
                      "Pollutant washoff");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    {
        auto s = sect("COVERAGES", Category::Area, {},
                      {col("Subcatchment", CT::AreaRef), ref("Landuse", "Landuse"),
                       col("Percent", CT::Number, "percent", false, "percent of the subcatchment area")},
                      "Land use coverage");
        s.multiRowPerKey = true;
        s.repeatFrom = 1;
        v.push_back(s);
    }
    {
        auto s = sect("LOADINGS", Category::Area, {},
                      {col("Subcatchment", CT::AreaRef), ref("Pollutant", "Pollutant"),
                       col("InitBuildup", CT::Number, "loading", false, "initial buildup")},
                      "Initial pollutant buildup");
        s.multiRowPerKey = true;
        s.repeatFrom = 1;
        v.push_back(s);
    }
    {
        auto s = sect("INFLOWS", Category::Node, {},
                      {col("Node", CT::NodeRef),
                       ref("Constituent", "Pollutant", false, "FLOW or a pollutant", {"FLOW"}),
                       blankAsEmpty(col("TimeSeries", CT::SeriesRef, {}, true,
                                        "time series of the inflow (blank for a constant baseline)")),
                       choice("Type", {"FLOW", "CONCEN", "MASS"}, true,
                              "FLOW for flow, CONCEN or MASS for a pollutant"),
                       col("Mfactor", CT::Number, {}, true, "factor that converts mass inflow to mass/s (1.0 for flow)"),
                       col("Sfactor", CT::Number, {}, true, "scaling factor of the time series values"),
                       col("Baseline", CT::Number, {}, true, "constant baseline value"),
                       blankAsEmpty(col("Pattern", CT::PatternRef, {}, true, "time pattern of the baseline"))},
                      "External inflow");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    {
        auto s = sect("DWF", Category::Node, {},
                      {col("Node", CT::NodeRef),
                       ref("Constituent", "Pollutant", false, "FLOW or a pollutant", {"FLOW"}),
                       col("Baseline", CT::Number, {}, false, "average dry weather flow or concentration"),
                       blankAsEmpty(col("MonthlyPattern", CT::PatternRef, {}, true)),
                       blankAsEmpty(col("DailyPattern", CT::PatternRef, {}, true)),
                       blankAsEmpty(col("HourlyPattern", CT::PatternRef, {}, true)),
                       blankAsEmpty(col("WeekendPattern", CT::PatternRef, {}, true))},
                      "Dry weather inflow");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    v.push_back(sect("RDII", Category::Node, {},
                     {col("Node", CT::NodeRef),
                      ref("UnitHydrograph", "Hydrograph", false, "unit hydrograph group"),
                      col("SewerArea", CT::Number, "area", false, "area of the sewershed that contributes RDII")},
                     "Rainfall-dependent infiltration/inflow"));
    {
        auto s = sect("TREATMENT", Category::Node, {},
                      {col("Node", CT::NodeRef), ref("Pollutant", "Pollutant"),
                       col("Expression", CT::Text, {}, false,
                           "R = removal or C = concentration, e.g. R = 0.75 * R_TSS")},
                      "Treatment");
        s.multiRowPerKey = true;
        s.variadic = true;
        v.push_back(s);
    }
    v.push_back(objectSect(
        "AQUIFERS", "Aquifer",
        {col("Name", CT::Id), col("Porosity", CT::Number, {}, false, "volume of voids / total soil volume"),
         col("WiltingPoint", CT::Number, {}, false, "soil moisture content at wilting point"),
         col("FieldCapacity", CT::Number, {}, false, "soil moisture content at field capacity"),
         col("Ksat", CT::Number, "rate", false, "saturated hydraulic conductivity"),
         col("Kslope", CT::Number, {}, false, "slope of the conductivity versus moisture content curve"),
         col("Tslope", CT::Number, {}, false, "slope of the soil tension versus moisture content curve"),
         col("ETupper", CT::Number, {}, false, "fraction of evaporation available in the upper zone"),
         col("ETlower", CT::Number, "depth", false, "depth into the lower zone over which evaporation can occur"),
         col("Seepage", CT::Number, "rate", false, "seepage rate to deep groundwater"),
         col("Ebottom", CT::Number, "elevation", false, "elevation of the bottom of the aquifer"),
         col("WaterTable", CT::Number, "elevation", false, "initial water table elevation"),
         col("UnsatMoisture", CT::Number, {}, false, "initial moisture content of the unsaturated zone"),
         col("ETpattern", CT::PatternRef, {}, true, "monthly pattern of upper zone evaporation")},
        "Aquifers"));
    v.push_back(sect("GROUNDWATER", Category::Area, {},
                     {col("Subcatchment", CT::AreaRef), ref("Aquifer", "Aquifer"),
                      col("Node", CT::NodeRef, {}, false, "node that receives groundwater flow"),
                      col("SurfaceElev", CT::Number, "elevation", false, "ground surface elevation"),
                      col("A1", CT::Number, {}, false, "groundwater flow coefficient"),
                      col("B1", CT::Number, {}, false, "groundwater flow exponent"),
                      col("A2", CT::Number, {}, false, "surface water flow coefficient"),
                      col("B2", CT::Number, {}, false, "surface water flow exponent"),
                      col("A3", CT::Number, {}, false, "surface-groundwater interaction coefficient"),
                      col("SurfaceDepth", CT::Number, "depth", false, "fixed depth of surface water at the node"),
                      col("ThresholdElev", CT::Number, "elevation", true,
                          "groundwater elevation that must be reached before flow occurs (* for the node invert)"),
                      col("BottomElev", CT::Number, "elevation", true, "aquifer bottom elevation (overrides the aquifer)"),
                      col("InitialElev", CT::Number, "elevation", true, "initial water table elevation (overrides the aquifer)"),
                      col("InitialMoisture", CT::Number, {}, true, "initial upper zone moisture (overrides the aquifer)")},
                     "Groundwater"));
    {
        auto s = sect("GWF", Category::Area, {},
                      {col("Subcatchment", CT::AreaRef),
                       choice("Flow", {"LATERAL", "DEEP"}, false, "lateral flow to the node or deep percolation"),
                       col("Expression", CT::Text, {}, false, "flow per unit area as a math expression")},
                      "Groundwater flow expression");
        s.multiRowPerKey = true;
        s.variadic = true;
        v.push_back(s);
    }
    {
        auto s = objectSect("SNOWPACKS", "Snowpack",
                            {col("Name", CT::Id),
                             choice("Surface", {"PLOWABLE", "IMPERVIOUS", "PERVIOUS", "REMOVAL"}),
                             col("Parameters", CT::Text)},
                            "Snow packs");
        s.multiRowPerKey = true;
        s.variadic = true;
        s.variantColumn = 1;
        auto melt = [](const char* last, const char* lastHelp) {
            return std::vector<ColumnDef>{
                col("Cmin", CT::Number, "meltrate", false, "minimum melt coefficient (December 21)"),
                col("Cmax", CT::Number, "meltrate", false, "maximum melt coefficient (June 21)"),
                col("Tbase", CT::Number, "temperature", false, "snow melt base temperature"),
                col("FWF", CT::Number, {}, false, "free water capacity (fraction of snow depth)"),
                col("SD0", CT::Number, "smalldepth", false, "initial snow depth (water equivalent)"),
                col("FW0", CT::Number, "smalldepth", false, "initial free water in the pack"),
                col(last, CT::Number, {}, false, lastHelp)};
        };
        s.variants = {
            {"PLOWABLE", melt("SNN0", "fraction of impervious area that can be plowed"), false},
            {"IMPERVIOUS", melt("SD100", "snow depth above which there is 100% cover"), false},
            {"PERVIOUS", melt("SD100", "snow depth above which there is 100% cover"), false},
            {"REMOVAL",
             {col("Dplow", CT::Number, "smalldepth", false, "depth at which snow removal begins"),
              col("Fout", CT::Number, {}, false, "fraction moved out of the watershed"),
              col("Fimp", CT::Number, {}, false, "fraction moved to the impervious area"),
              col("Fperv", CT::Number, {}, false, "fraction moved to the pervious area"),
              col("Fimelt", CT::Number, {}, false, "fraction converted into immediate melt"),
              col("Fsub", CT::Number, {}, true, "fraction moved to another subcatchment"),
              col("Nsub", CT::AreaRef, {}, true, "subcatchment that receives that snow")},
             false}};
        v.push_back(s);
    }
    {
        auto s = freeSect("HYDROGRAPHS", "Unit hydrographs");
        s.objectKind = "Hydrograph";
        v.push_back(s);
    }
    {
        auto s = freeSect("TRANSECTS", "Transects");
        s.objectKind = "Transect";
        v.push_back(s);
    }
    v.push_back(objectSect(
        "STREETS", "Street",
        {col("Name", CT::Id), col("Tcrown", CT::Number, "length", false, "distance from curb to street crown"),
         col("Hcurb", CT::Number, "length", false, "curb height"),
         col("Sx", CT::Number, "percent", false, "street cross slope"),
         col("nRoad", CT::Number, {}, false, "Manning's n of the road surface"),
         col("a", CT::Number, "smalldepth", true, "gutter depression height"),
         col("W", CT::Number, "length", true, "depressed section width"),
         choice("Sides", {"1", "2"}, true, "1 for a single-sided street, 2 for two sides"),
         col("Tback", CT::Number, "length", true, "street backing width"),
         col("Sback", CT::Number, "percent", true, "street backing slope"),
         col("nBack", CT::Number, {}, true, "Manning's n of the street backing")},
        "Streets"));
    {
        auto s = objectSect("INLETS", "Inlet",
                            {col("Name", CT::Id),
                             choice("Type", {"GRATE", "CURB", "SLOTTED", "DROP_GRATE", "DROP_CURB", "CUSTOM"}),
                             col("Parameters", CT::Text)},
                            "Street inlets");
        s.multiRowPerKey = true;
        s.variadic = true;
        s.variantColumn = 1;
        const std::vector<std::string> grates = {"P_BAR-50",    "P_BAR-50x100", "P_BAR-30",
                                                 "CURVED_VANE", "TILT_BAR-45",  "TILT_BAR-30",
                                                 "RETICULINE",  "GENERIC"};
        const std::vector<ColumnDef> grate = {
            col("Length", CT::Number, "length", false, "length of the grate"),
            col("Width", CT::Number, "length", false, "width of the grate"),
            choice("GrateType", grates),
            col("OpenArea", CT::Number, {}, true, "fraction of the grate area that is open (GENERIC grates)"),
            col("SplashVelocity", CT::Number, "velocity", true, "splash over velocity (GENERIC grates)")};
        const std::vector<ColumnDef> curb = {col("Length", CT::Number, "length", false, "length of the curb opening"),
                                             col("Height", CT::Number, "length", false, "height of the curb opening")};
        auto curbThroat = curb;
        curbThroat.push_back(choice("Throat", {"HORIZONTAL", "INCLINED", "VERTICAL"}, true, "throat angle"));
        s.variants = {
            {"GRATE", grate, false},
            {"CURB", curbThroat, false},
            {"SLOTTED",
             {col("Length", CT::Number, "length", false, "length of the slot"),
              col("Width", CT::Number, "length", false, "width of the slot")},
             false},
            {"DROP_GRATE", grate, false},
            {"DROP_CURB", curb, false},
            {"CUSTOM", {col("Curve", CT::CurveRef, {}, false, "diversion or rating curve of captured flow")}, false}};
        v.push_back(s);
    }
    v.push_back(sect("INLET_USAGE", Category::Link, {},
                     {col("Conduit", CT::LinkRef), ref("Inlet", "Inlet"),
                      col("Node", CT::NodeRef, {}, false, "node that receives the captured flow"),
                      col("Number", CT::Number, {}, true, "number of inlets on each side of the street"),
                      col("PctClogged", CT::Number, "percent", true, "degree to which the inlet is clogged"),
                      col("Qmax", CT::Number, "flow", true, "largest flow the inlet can capture (0 for no limit)"),
                      col("aLocal", CT::Number, "smalldepth", true, "height of the local gutter depression"),
                      col("wLocal", CT::Number, "length", true, "width of the local gutter depression"),
                      choice("Placement", {"AUTOMATIC", "ON_GRADE", "ON_SAG"}, true)},
                     "Street inlet"));
    {
        auto s = freeSect("LID_CONTROLS", "LID controls");
        s.objectKind = "LID";
        v.push_back(s);
    }
    {
        auto s = freeSect("CONTROLS", "Control rules");
        s.objectKind = "Rule";
        v.push_back(s);
    }
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
                       ref("SnowPack", "Snowpack", true, "snow pack of the subcatchment")},
                      "Subcatchments");
        v.push_back(s);
    }
    v.push_back(sect("SUBAREAS", Category::Area, {},
                     {col("Subcatchment", CT::AreaRef), col("N-Imperv", CT::Number),
                      col("N-Perv", CT::Number), col("S-Imperv", CT::Number, "smalldepth"),
                      col("S-Perv", CT::Number, "smalldepth"), col("PctZero", CT::Number),
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
                      col("Aponded", CT::Number, "surfacearea", true)},
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
        s.variantColumn = 1;
        s.variants = {
            {"IRREGULAR", {ref("Transect", "Transect", false, "transect of the channel")}, false},
            {"STREET", {ref("Street", "Street", false, "street cross section")}, false},
            {"CUSTOM",
             {col("Geom1", CT::Number, "depth", false, "full height"),
              col("Curve", CT::CurveRef, {}, false, "shape curve"),
              col("Barrels", CT::Number, {}, true)},
             false}};
        v.push_back(s);
    }
    v.push_back(sect("LOSSES", Category::Link, {},
                     {col("Link", CT::LinkRef), col("Kentry", CT::Number), col("Kexit", CT::Number),
                      col("Kavg", CT::Number), choice("FlapGate", {"NO", "YES"}, true),
                      col("Seepage", CT::Number, {}, true)},
                     "Conduit losses"));
    appendSwmmData(v);
    {
        auto s = sect("LID_USAGE", Category::Area, {},
                      {col("Subcatchment", CT::AreaRef), ref("LID", "LID", false, "LID control"),
                       col("Number", CT::Number, {}, false, "number of replicate units"),
                       col("Area", CT::Number, "surfacearea", false, "area of each unit"),
                       col("Width", CT::Number, "length", false, "outflow edge width of each unit"),
                       col("InitSat", CT::Number, "percent", false, "initial saturation"),
                       col("FromImp", CT::Number, "percent", false, "percent of impervious area treated"),
                       choice("ToPerv", {"0", "1"}, true, "1 sends outflow to the pervious area"),
                       col("RptFile", CT::Text, {}, true, "detailed report file (\"*\" for none)"),
                       col("DrainTo", CT::NodeOrAreaRef, {}, true, "node or subcatchment receiving drain flow"),
                       col("FromPerv", CT::Number, "percent", true, "percent of pervious area treated")},
                      "LID units in subcatchments");
        s.multiRowPerKey = true;
        v.push_back(s);
    }
    for (const char* n : {"TEMPERATURE", "ADJUSTMENTS", "EVENTS", "FILES", "PROFILES",
                          "REPORT", "TAGS", "MAP", "LABELS", "BACKDROP"})
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

const ColumnDef* RowLayout::column(std::size_t index) const
{
    if (columns.empty())
        return nullptr;
    if (index < columns.size())
        return &columns[index];
    if (repeatFrom >= 0 && static_cast<std::size_t>(repeatFrom) < columns.size())
    {
        const std::size_t period = columns.size() - static_cast<std::size_t>(repeatFrom);
        return &columns[static_cast<std::size_t>(repeatFrom) + (index - static_cast<std::size_t>(repeatFrom)) % period];
    }
    return variadic ? &columns.back() : nullptr;
}

std::string RowLayout::columnName(std::size_t index) const
{
    const ColumnDef* c = column(index);
    if (!c)
        return "Value " + std::to_string(index + 1);
    if (repeatFrom >= 0 && index >= columns.size())
    {
        const std::size_t period = columns.size() - static_cast<std::size_t>(repeatFrom);
        return c->name + " " + std::to_string((index - static_cast<std::size_t>(repeatFrom)) / period + 1);
    }
    return c->name;
}

const SectionVariant* SectionDef::variant(std::string_view key) const
{
    for (const auto& v : variants)
    {
        if (text::iequals(v.key, key))
            return &v;
    }
    return nullptr;
}

RowLayout SectionDef::layout(const std::vector<std::string>& tokens) const
{
    RowLayout l;
    l.columns = columns;
    l.variadic = variadic;
    l.repeatFrom = repeatFrom;
    if (variantColumn >= 0 && tokens.size() > static_cast<std::size_t>(variantColumn))
    {
        if (const SectionVariant* v = variant(tokens[static_cast<std::size_t>(variantColumn)]))
        {
            l.columns.resize(static_cast<std::size_t>(variantColumn) + 1);
            l.columns.insert(l.columns.end(), v->columns.begin(), v->columns.end());
            l.variadic = v->variadic;
            l.repeatFrom = -1;
        }
    }
    return l;
}

std::vector<std::string> objectKinds(Dialect dialect)
{
    if (dialect != Dialect::Swmm)
        return {};
    return {"Pollutant", "Landuse",  "Aquifer", "Snowpack", "Hydrograph",
            "Transect",  "Street",   "Inlet",   "LID",      "Rule"};
}

const SectionDef* sectionForObject(Dialect dialect, std::string_view kind)
{
    for (const auto& s : sectionDefs(dialect))
    {
        if (!s.objectKind.empty() && text::iequals(s.objectKind, kind))
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
