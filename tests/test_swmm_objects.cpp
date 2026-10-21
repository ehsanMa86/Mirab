/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_swmm_objects.cpp
 * Purpose: Tests of the SWMM object editors' parsing, validation and writing.
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
#include "TestFramework.h"

#include "mirab/core/ControlRules.h"
#include "mirab/core/Model.h"
#include "mirab/core/SwmmObjects.h"
#include "mirab/core/Text.h"

#include <algorithm>

using namespace mirab;
using namespace mirab::core;

namespace
{
// A small SWMM 5.2 model with every section the editors work on.
const char* kModel = R"([TITLE]
LID, transect, RDII and control rule test model

[OPTIONS]
FLOW_UNITS           CFS
INFILTRATION         HORTON
FLOW_ROUTING         DYNWAVE

[RAINGAGES]
;;Name  Format    Interval SCF Source
RG1     INTENSITY 0:15     1.0 TIMESERIES Storm1

[SUBCATCHMENTS]
;;Name  Gage  Outlet  Area  %Imperv  Width  %Slope  CurbLen
S1      RG1   J1      4     50       400    0.5     0
S2      RG1   J2      0.5   40       500    0.5     0

[JUNCTIONS]
J1  100  10  0  0  0
J2  98   10  0  0  0

[OUTFALLS]
Out1  90  FREE  NO

[STORAGE]
WW1  95  12  0  FUNCTIONAL  1000  0  0  0  0

[CONDUITS]
C1  J1  J2   400  0.015  0  0  0  0
C2  J2  WW1  400  0.015  0  0  0  0

[PUMPS]
P1  WW1  Out1  Pump1  OFF  0  0

[ORIFICES]
OR1  J2  Out1  SIDE  0  0.65  NO  0

[WEIRS]
W1  J1  Out1  TRANSVERSE  0  3.33  NO  0  0  YES

[XSECTIONS]
C1  IRREGULAR  Main  0  0  0  1
C2  CIRCULAR   2     0  0  0  1

[POLLUTANTS]
TSS  MG/L  0  0  0  0  0  0  0

[LID_CONTROLS]
;;Name  Type/Layer  Parameters
BioCell  BC
BioCell  SURFACE  6  0.1  0  0  0     ;ponding
BioCell  SOIL     18  0.5  0.2  0.1  0.5  10  3.5
BioCell  STORAGE  12  0.75  0.5  0
BioCell  DRAIN    0.5  0.5  6  6
BioCell  REMOVALS TSS 80
;green roof
Roof1  GR
Roof1  SURFACE  0  0  0.1  1  0
Roof1  SOIL     3  0.5  0.2  0.1  0.5  10  3.5
Roof1  DRAINMAT 3  0.5  0.1

[LID_USAGE]
;;Subcatch  LID  Number  Area  Width  InitSat  FromImp  ToPerv  RptFile  DrainTo  FromPerv
S1  BioCell  4  500  10  0  20  0      ;east side
S1  Roof1    1  2000 20  0  30  1  *  J2  10
S2  BioCell  2  300  10  0  15  0

[TRANSECTS]
;Main channel
NC 0.05     0.05     0.03
X1 Main             6        20       80       0.0      0.0      0.0      0.0      0.0
GR 10       0        6        20       2        40       2        60       6        80
GR 10       100
;Same roughness as Main
X1 Side             4        5        15       0.0      0.0      1.2      2        0.5
GR 4        0        1        5        1        15       4        20

[HYDROGRAPHS]
;;Name  Month/Gage  Response  R  T  K  Dmax  Drecov  Dinit
UH1  RG1
UH1  ALL  SHORT   0.02  1  2
UH1  ALL  MEDIUM  0.03  3  3     ;medium term
UH1  ALL  LONG    0.04  6  4   1  0.1  0
UH1  JUL  SHORT   0.05  1  2
UH2  RG1
UH2  MAR  0.01 1 2 0.02 4 3 0.03 8 4

[CURVES]
Pump1  PUMP2  0  0  5  10
Ctl1   CONTROL  0  0  10  1

[TIMESERIES]
Storm1  0:00  0
Storm1  1:00  1

[CONTROLS]
VARIABLE Wet = NODE WW1 DEPTH
EXPRESSION WetFt = Wet * 3.281

;Start the pump
RULE PumpOn
IF NODE WW1 DEPTH > 5
THEN PUMP P1 STATUS = ON
PRIORITY 1

RULE PumpOff
IF Wet < 1
THEN PUMP P1 STATUS = OFF

RULE Gate
IF SIMULATION CLOCKTIME >= 08:00
AND SIMULATION CLOCKTIME < 18:00
OR GAGE RG1 6HR_PRECIP > 0.5
THEN ORIFICE OR1 SETTING = CURVE Ctl1
ELSE ORIFICE OR1 SETTING = PID 0.5 0.1 0
AND WEIR W1 SETTING = 0.5
PRIORITY 2

[END]
)";

Model model()
{
    return Model::fromText(kModel);
}

std::vector<std::string> sectionLines(const Model& m, const char* name)
{
    const auto* s = m.document().find(name);
    return s ? s->lines : std::vector<std::string>{};
}

bool hasMessage(const std::vector<RuleMessage>& msgs, int line, const std::string& part)
{
    return std::any_of(msgs.begin(), msgs.end(), [&](const RuleMessage& m) {
        return m.line == line && m.message.find(part) != std::string::npos;
    });
}

bool hasIssue(const std::vector<ObjectIssue>& issues, const std::string& part)
{
    return std::any_of(issues.begin(), issues.end(), [&](const ObjectIssue& i) {
        return i.message.find(part) != std::string::npos;
    });
}
} // namespace

// ---------------------------------------------------------------------------
// LID controls

TEST_CASE("lid: reads controls with their layers")
{
    const auto m = model();
    CHECK_EQ(lidControlNames(m.document()).size(), std::size_t(2));
    const auto bc = readLidControl(m.document(), "BioCell");
    CHECK(bc.has_value());
    CHECK_EQ(bc->type, std::string("BC"));
    CHECK_EQ(bc->layers.size(), std::size_t(5));
    CHECK_NEAR(*bc->number(LidLayer::Soil, 1), 0.5, 1e-12);
    CHECK_EQ(bc->value(LidLayer::Removals, 0), std::string("TSS"));
    CHECK(validateLidControl(*bc, &m.document()).empty());
    const auto gr = readLidControl(m.document(), "Roof1");
    CHECK(gr && gr->hasLayer(LidLayer::DrainMat));
    CHECK(validateLidControl(*gr, &m.document()).empty());
    CHECK(!readLidControl(m.document(), "Nope"));
}

TEST_CASE("lid: unchanged control writes back the same text")
{
    auto m = model();
    const std::string before = m.toText();
    const auto bc = readLidControl(m.document(), "BioCell");
    writeLidControl(m.document(), *bc);
    CHECK_EQ(sectionLines(m, "LID_CONTROLS").size(), sectionLines(model(), "LID_CONTROLS").size());
    // Values are re-aligned, but comments and other controls stay.
    const auto lines = sectionLines(m, "LID_CONTROLS");
    CHECK(std::find(lines.begin(), lines.end(), ";green roof") != lines.end());
    CHECK(lines[2].find(";ponding") != std::string::npos);
    const auto again = readLidControl(m.document(), "BioCell");
    CHECK(again->layers == bc->layers);
    CHECK(m.toText().find("Roof1  DRAINMAT 3  0.5  0.1") != std::string::npos);
}

TEST_CASE("lid: editing a layer, removing one and adding one")
{
    auto m = model();
    auto bc = *readLidControl(m.document(), "BioCell");
    bc.layers[LidLayer::Soil][0] = "24";
    bc.layers.erase(LidLayer::Drain);
    writeLidControl(m.document(), bc);
    auto r = *readLidControl(m.document(), "BioCell");
    CHECK_EQ(r.value(LidLayer::Soil, 0), std::string("24"));
    CHECK(!r.hasLayer(LidLayer::Drain));
    // Add the drain back: it goes after the storage row, before REMOVALS.
    bc.layers[LidLayer::Drain] = {"1", "0.5", "0", "0"};
    writeLidControl(m.document(), bc);
    const auto lines = sectionLines(m, "LID_CONTROLS");
    int storage = -1, drain = -1, removals = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        const auto t = text::splitInpLine(lines[static_cast<std::size_t>(i)]).tokens;
        if (t.size() > 1 && t[0] == "BioCell")
        {
            if (t[1] == "STORAGE")
                storage = i;
            if (t[1] == "DRAIN")
                drain = i;
            if (t[1] == "REMOVALS")
                removals = i;
        }
    }
    CHECK(storage >= 0 && drain == storage + 1 && removals == drain + 1);
}

TEST_CASE("lid: rename and new control")
{
    auto m = model();
    auto bc = *readLidControl(m.document(), "BioCell");
    bc.name = "Cell2";
    writeLidControl(m.document(), bc, "BioCell");
    CHECK(!readLidControl(m.document(), "BioCell"));
    CHECK(readLidControl(m.document(), "Cell2"));
    auto pp = defaultLidControl("Pave", "PP", UnitSystem::SI);
    CHECK(pp.hasLayer(LidLayer::Pavement) && pp.hasLayer(LidLayer::Storage));
    CHECK(!pp.hasLayer(LidLayer::DrainMat));
    CHECK(validateLidControl(pp, &m.document()).empty());
    writeLidControl(m.document(), pp);
    const auto names = lidControlNames(m.document());
    CHECK_EQ(names.back(), std::string("Pave"));
    for (const auto& t : lidTypes())
    {
        const auto c = defaultLidControl("X", t.code);
        const auto issues = validateLidControl(c);
        if (hasErrors(issues))
            throw test::Failure("default " + t.code + " control: " + issues[0].message);
    }
}

TEST_CASE("lid: validation of ranges and layer rules")
{
    auto c = defaultLidControl("B", "BC");
    c.layers[LidLayer::Soil] = {"18", "0.3", "0.4", "0.1", "0.5", "10", "3.5"};
    auto issues = validateLidControl(c);
    CHECK(hasIssue(issues, "field capacity must be less than the porosity"));
    c.layers[LidLayer::Surface][1] = "1";
    CHECK(hasIssue(validateLidControl(c), "vegetation volume fraction must be"));
    c = defaultLidControl("B", "BC");
    c.layers.erase(LidLayer::Soil);
    CHECK(hasIssue(validateLidControl(c), "needs a soil layer"));
    c = defaultLidControl("S", "VS");
    c.layers[LidLayer::Surface][3] = "0";
    CHECK(hasIssue(validateLidControl(c), "roughness and slope"));
    c = defaultLidControl("R", "RB");
    c.layers[LidLayer::Drain] = {"1", "0.5", "0", "6", "2", "4"};
    CHECK(hasIssue(validateLidControl(c), "open level must be above"));
    c.layers[LidLayer::Drain] = {"1", "0.5", "0", "6", "0", "0", "NoCurve"};
    auto doc = model().document();
    CHECK(hasIssue(validateLidControl(c, &doc), "Curve NoCurve is not defined"));
    c.layers[LidLayer::Drain] = {"1", "x"};
    issues = validateLidControl(c);
    CHECK(hasIssue(issues, "not a number"));
    CHECK(hasIssue(issues, "4 values expected"));
    c.layers[LidLayer::Pavement] = {"6", "0.15", "0", "100", "0"};
    CHECK(hasIssue(validateLidControl(c), "not used by a rain barrel"));
    CHECK_EQ(lidUnitLabel("rate", UnitSystem::SI), std::string("mm/hr"));
    CHECK_EQ(findLidType("rg")->layers.size(), std::size_t(2));
}

// ---------------------------------------------------------------------------
// LID usage

TEST_CASE("lidusage: read, summary and validation")
{
    const auto m = model();
    auto units = readLidUsage(m.document(), "S1");
    CHECK_EQ(units.size(), std::size_t(2));
    CHECK_EQ(units[1].drainTo, std::string("J2"));
    CHECK(units[1].toPervious);
    CHECK_NEAR(units[1].fromPerv, 10.0, 1e-12);
    const auto sum = lidAreaSummary(m, "S1", units);
    CHECK_NEAR(sum.lidArea, (4 * 500 + 2000) / 43560.0, 1e-12);
    CHECK_NEAR(sum.subcatchmentArea, 4.0, 1e-12);
    CHECK_NEAR(sum.impervTreated, 50.0, 1e-12);
    CHECK(validateLidUsage(m, "S1", units).empty());
    // 0.5 ac subcatchment with 30000 ft2 of LID is above 100 %.
    units = {LidUsage{"BioCell", 3, 10000, 10, 0, 60, false, {}, {}, 0},
             LidUsage{"Roof1", 1, 1000, 10, 0, 50, false, {}, "Nowhere", 0}};
    const auto issues = validateLidUsage(m, "S2", units);
    CHECK(hasIssue(issues, "% of the subcatchment"));
    CHECK(hasIssue(issues, "more than 100% of the impervious area"));
    CHECK(hasIssue(issues, "Nowhere is not a node or subcatchment"));
    units = {LidUsage{"Ghost", 1, 0, 10, 0, 0, false, {}, {}, 0}};
    const auto more = validateLidUsage(m, "S2", units);
    CHECK(hasIssue(more, "Ghost is not defined"));
    CHECK(hasIssue(more, "must be above 0"));
}

TEST_CASE("lidusage: writing keeps other subcatchments and comments")
{
    auto m = model();
    auto units = readLidUsage(m.document(), "S1");
    const auto before = sectionLines(m, "LID_USAGE");
    writeLidUsage(m.document(), "S1", units);
    const auto after = sectionLines(m, "LID_USAGE");
    CHECK_EQ(after.size(), before.size());
    CHECK(after[1].find(";east side") != std::string::npos);
    CHECK(readLidUsage(m.document(), "S1").size() == 2);
    units[0].number = 6;
    units.erase(units.begin() + 1);
    units.push_back(LidUsage{"Roof1", 2, 100, 5, 10, 5, false, "roof report.txt", {}, 0});
    writeLidUsage(m.document(), "S1", units);
    const auto r = readLidUsage(m.document(), "S1");
    CHECK_EQ(r.size(), std::size_t(2));
    CHECK_EQ(r[0].number, 6);
    CHECK_EQ(r[1].reportFile, std::string("roof report.txt"));
    CHECK_EQ(readLidUsage(m.document(), "S2").size(), std::size_t(1));
    const auto row = lidUsageRow("S9", LidUsage{"L", 1, 10, 1, 0, 0, false, {}, {}, 25});
    CHECK_EQ(row.size(), std::size_t(11));
    CHECK_EQ(row[8], std::string("*"));
    writeLidUsage(m.document(), "S1", {});
    CHECK(readLidUsage(m.document(), "S1").empty());
    CHECK_EQ(readLidUsage(m.document(), "S2").size(), std::size_t(1));
}

// ---------------------------------------------------------------------------
// Transects

TEST_CASE("transects: reads blocks and inherited roughness")
{
    const auto m = model();
    const auto all = readTransects(m.document());
    CHECK_EQ(all.size(), std::size_t(2));
    CHECK_EQ(all[0].name, std::string("Main"));
    CHECK_EQ(all[0].points.size(), std::size_t(6));
    CHECK_NEAR(all[0].points[1].first, 20.0, 1e-12);
    CHECK_NEAR(all[0].points[1].second, 6.0, 1e-12);
    CHECK(all[0].ownRoughness);
    CHECK(!all[1].ownRoughness);
    CHECK_NEAR(all[1].nChannel, 0.03, 1e-12);
    CHECK_NEAR(all[1].nLeft, 0.05, 1e-12);
    CHECK_NEAR(all[1].meander, 1.2, 1e-12);
    CHECK_NEAR(all[1].stationFactor, 2.0, 1e-12);
    CHECK_NEAR(all[1].adjustedLeftBank(), 10.0, 1e-12);
    CHECK_NEAR(all[1].adjustedPoints()[1].second, 1.5, 1e-12);
    CHECK(validateTransect(all[0]).empty());
    CHECK(validateTransect(all[1]).empty());
    CHECK_EQ(transectNames(m.document()).size(), std::size_t(2));
}

TEST_CASE("transects: geometry below a water surface")
{
    Transect t;
    t.name = "Box";
    t.nChannel = 0.03;
    t.points = {{0, 4}, {0, 0}, {10, 0}, {10, 4}};
    auto g = transectGeometry(t, 2.0);
    CHECK_NEAR(g.area, 20.0, 1e-9);
    CHECK_NEAR(g.topWidth, 10.0, 1e-9);
    CHECK_NEAR(g.wettedPerimeter, 14.0, 1e-9);
    // V-shape: area of a triangle.
    t.points = {{0, 2}, {2, 0}, {4, 2}};
    g = transectGeometry(t, 1.0);
    CHECK_NEAR(g.area, 1.0, 1e-9);
    CHECK_NEAR(g.topWidth, 2.0, 1e-9);
    // Above the ends the section is extended with vertical walls.
    g = transectGeometry(t, 3.0);
    CHECK_NEAR(g.area, 4.0 + 4.0, 1e-9);
    CHECK_NEAR(g.wettedPerimeter, 2 * std::hypot(2.0, 2.0) + 2.0, 1e-9);
}

TEST_CASE("transects: round trip, editing and inheritance")
{
    auto m = model();
    const std::string before = m.toText();
    for (const auto& t : readTransects(m.document()))
        writeTransect(m.document(), t);
    // The rewritten blocks parse to the same transects; comments stay.
    const auto again = readTransects(m.document());
    CHECK_EQ(again.size(), std::size_t(2));
    CHECK(again[1].points == readTransects(model().document())[1].points);
    CHECK(!again[1].ownRoughness);
    const auto lines = sectionLines(m, "TRANSECTS");
    CHECK(std::find(lines.begin(), lines.end(), ";Same roughness as Main") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), ";Main channel") != lines.end());

    // Changing the n values of Main must not change Side, which inherited them.
    auto main = again[0];
    main.nChannel = 0.02;
    main.points.push_back({120, 12});
    writeTransect(m.document(), main);
    const auto edited = readTransects(m.document());
    CHECK_NEAR(edited[0].nChannel, 0.02, 1e-12);
    CHECK_EQ(edited[0].points.size(), std::size_t(7));
    CHECK_NEAR(edited[1].nChannel, 0.03, 1e-12);
    CHECK(edited[1].ownRoughness);
    // X1 carries the station count.
    CHECK(m.toText().find("X1 Main") != std::string::npos);
    bool counted = false;
    for (const auto& l : sectionLines(m, "TRANSECTS"))
    {
        const auto t = text::splitInpLine(l).tokens;
        if (t.size() > 2 && t[0] == "X1" && t[1] == "Main")
            counted = t[2] == "7";
    }
    CHECK(counted);

    // Rename and add a new transect.
    auto side = edited[1];
    side.name = "Side2";
    writeTransect(m.document(), side, "Side");
    Transect nt;
    nt.name = "New";
    nt.nLeft = nt.nRight = nt.nChannel = 0.04;
    nt.points = {{0, 3}, {1, 0}, {3, 0}, {4, 3}};
    nt.leftBank = 0;
    nt.rightBank = 4;
    writeTransect(m.document(), nt);
    const auto names = transectNames(m.document());
    CHECK_EQ(names.size(), std::size_t(3));
    CHECK_EQ(names[1], std::string("Side2"));
    CHECK(readTransect(m.document(), "New")->ownRoughness);
}

TEST_CASE("transects: renaming updates irregular cross-sections")
{
    auto m = model();
    CHECK_EQ(renameSwmmObjectReferences(m.document(), "TRANSECTS", "Main", "Main2"), 1);
    const auto rows = m.document().find("XSECTIONS")->allRowTokens();
    CHECK_EQ(rows[0][2], std::string("Main2"));
    CHECK_EQ(rows[1][2], std::string("2"));
    CHECK_EQ(renameSwmmObjectReferences(m.document(), "LID_CONTROLS", "BioCell", "Cell"), 2);
    CHECK_EQ(readLidUsage(m.document(), "S2")[0].lid, std::string("Cell"));
}

TEST_CASE("transects: validation")
{
    Transect t;
    t.name = "T";
    t.points = {{0, 2}, {5, 0}, {4, 2}};
    t.leftBank = 6;
    t.rightBank = 1;
    const auto issues = validateTransect(t);
    CHECK(hasIssue(issues, "stations must increase"));
    CHECK(hasIssue(issues, "Manning's n must be above 0"));
    CHECK(hasIssue(issues, "left bank station must not be right"));
    t.points = {{0, 1}, {5, 1}};
    CHECK(hasIssue(validateTransect(t), "flat"));
    const auto block = formatTransect(t, true);
    CHECK_EQ(block.size(), std::size_t(3));
    CHECK(block[0].rfind("NC", 0) == 0);
}

// ---------------------------------------------------------------------------
// Unit hydrographs

TEST_CASE("hydrographs: reads new and old line formats")
{
    const auto m = model();
    CHECK_EQ(unitHydrographNames(m.document()).size(), std::size_t(2));
    const auto g = readUnitHydrograph(m.document(), "UH1");
    CHECK(g.has_value());
    CHECK_EQ(g->gage, std::string("RG1"));
    CHECK(g->params[0][2].defined);
    CHECK_NEAR(g->params[0][2].k, 4.0, 1e-12);
    CHECK_NEAR(*g->params[0][2].iaRecovery, 0.1, 1e-12);
    CHECK(!g->params[0][0].iaMax);
    CHECK(g->monthDefined(7) && !g->monthDefined(8));
    CHECK(validateUnitHydrograph(*g, &m).empty());
    const auto old = readUnitHydrograph(m.document(), "UH2");
    CHECK(old->params[3][1].defined);
    CHECK_NEAR(old->params[3][2].t, 8.0, 1e-12);
}

TEST_CASE("hydrographs: writing keeps comments and converts old lines")
{
    auto m = model();
    auto g = *readUnitHydrograph(m.document(), "UH1");
    writeUnitHydrograph(m.document(), g);
    const auto lines = sectionLines(m, "HYDROGRAPHS");
    CHECK(std::any_of(lines.begin(), lines.end(),
                      [](const std::string& l) {
                          return l.find(";medium term") != std::string::npos;
                      }));
    g.params[7] = {};
    g.params[12][0] = {true, 0.1, 0.5, 1.5, std::nullopt, std::nullopt, std::nullopt};
    writeUnitHydrograph(m.document(), g);
    const auto r = *readUnitHydrograph(m.document(), "UH1");
    CHECK(!r.monthDefined(7));
    CHECK(r.monthDefined(12));
    auto old = *readUnitHydrograph(m.document(), "UH2");
    writeUnitHydrograph(m.document(), old);
    int uh2Rows = 0;
    for (const auto& t : m.document().find("HYDROGRAPHS")->allRowTokens())
        uh2Rows += t[0] == "UH2" ? 1 : 0;
    CHECK_EQ(uh2Rows, 4); // gage line + three new-format lines
    const auto again = *readUnitHydrograph(m.document(), "UH2");
    CHECK_NEAR(again.params[3][2].k, 4.0, 1e-12);
    // Rename.
    old.name = "UH3";
    writeUnitHydrograph(m.document(), old, "UH2");
    CHECK(!readUnitHydrograph(m.document(), "UH2"));
    CHECK(readUnitHydrograph(m.document(), "UH3"));
}

TEST_CASE("hydrographs: validation and triangles")
{
    const auto m = model();
    UnitHydrographGroup g;
    g.name = "U";
    g.gage = "Nope";
    g.params[0][0] = {true, 0.6, 0, 2, std::nullopt, std::nullopt, std::nullopt};
    g.params[0][1] = {true, 0.6, 2, 2, std::nullopt, std::nullopt, std::nullopt};
    const auto issues = validateUnitHydrograph(g, &m);
    CHECK(hasIssue(issues, "Rain gage Nope does not exist"));
    CHECK(hasIssue(issues, "T (time to peak) must be above 0"));
    CHECK(hasIssue(issues, "add up to more than 1"));
    const UnitHydrographResponse p{true, 0.1, 2, 3, std::nullopt, std::nullopt, std::nullopt};
    const auto tri = triangularHydrograph(p);
    CHECK_EQ(tri.size(), std::size_t(3));
    CHECK_NEAR(tri[2].first, 8.0, 1e-12);
    // Area of the triangle equals R.
    CHECK_NEAR(0.5 * tri[2].first * tri[1].second, 0.1, 1e-12);
}

// ---------------------------------------------------------------------------
// Control rules

TEST_CASE("rules: a valid SWMM rule set has no messages")
{
    const auto m = model();
    const auto ctx = RuleContext::fromModel(m);
    const auto lines = sectionLines(m, "CONTROLS");
    const auto msgs = validateRules(lines, RuleSyntax::Swmm, &ctx);
    for (const auto& msg : msgs)
        std::cout << "  line " << msg.line << ": " << msg.message << "\n";
    CHECK(msgs.empty());
    const auto blocks = splitRuleBlocks(lines);
    CHECK_EQ(blocks.size(), std::size_t(4));
    CHECK_EQ(blocks[1].name, std::string("PumpOn"));
    // The comment above RULE PumpOn belongs to that rule.
    CHECK(text::trim(lines[static_cast<std::size_t>(blocks[1].firstLine)]) == ";Start the pump");
    int total = 0;
    for (const auto& b : blocks)
        total += b.lineCount;
    CHECK_EQ(total, static_cast<int>(lines.size()));
    CHECK_EQ(ruleNames(lines).size(), std::size_t(3));
}

TEST_CASE("rules: SWMM syntax errors carry line numbers")
{
    const std::vector<std::string> lines = {
        "RULE R1",                          // 1
        "IF NODE J9 DEPTH > 5",             // 2 unknown node
        "AND NODE J1 FLOW > 2",             // 3 bad attribute
        "THEN PUMP C1 STATUS = ON",         // 4 C1 is a conduit
        "AND ORIFICE OR1 SETTING = 1.5",    // 5 out of range
        "OR NODE J1 DEPTH > 1",             // 6 OR after THEN
        "PRIORITY high",                    // 7 not a number
        "",                                 // 8
        "RULE R1",                          // 9 duplicate
        "IF SIMULATION MONTH = 13",         // 10 bad month
        "THEN CONDUIT C1 STATUS = ON",      // 11 conduit status OPEN/CLOSED
        "ELSE WEIR W1 SETTING = CURVE Zz",  // 12 unknown curve
        "",                                 // 13
        "RULE R3",                          // 14
        "IF LINK C1 FLOW >> 2",             // 15 bad operator
        "",                                 // 16
        "THEN PUMP P1 STATUS = ON",         // 17 (state error reported? no: IF state)
        "IF Undefined > 3",                 // 18 IF not after RULE + unknown name
        "WHEN X",                           // 19 not a keyword
        "IF GAGE RG1 99HR_PRECIP > 1",      // 20 bad gage attribute
        "RULE R4",                          // 21
        "IF NODE J1 DEPTH > 1",             // 22
        "RULE R5",                          // 23 R4 has no THEN
        "IF SIMULATION TIME > 1:30",        // 24
        "THEN ORIFICE OR1 SETTING = PID 1 2", // 25 PID needs 3
    };
    const auto m = model();
    const auto ctx = RuleContext::fromModel(m);
    const auto msgs = validateRules(lines, RuleSyntax::Swmm, &ctx);
    CHECK(hasMessage(msgs, 2, "Node J9 does not exist"));
    CHECK(hasMessage(msgs, 3, "not an attribute of NODE"));
    CHECK(hasMessage(msgs, 4, "C1 is a conduit, not a pump"));
    CHECK(hasMessage(msgs, 5, "fractions from 0 to 1"));
    CHECK(hasMessage(msgs, 6, "OR can only join conditions"));
    CHECK(hasMessage(msgs, 7, "PRIORITY needs one number"));
    CHECK(hasMessage(msgs, 9, "defined twice"));
    CHECK(hasMessage(msgs, 10, "Month must be from 1 to 12"));
    CHECK(hasMessage(msgs, 11, "status must be OPEN or CLOSED"));
    CHECK(hasMessage(msgs, 12, "Curve Zz does not exist"));
    CHECK(hasMessage(msgs, 15, "not a relational operator"));
    CHECK(hasMessage(msgs, 18, "IF must come right after the RULE line"));
    CHECK(hasMessage(msgs, 18, "Unknown object type"));
    CHECK(hasMessage(msgs, 19, "not a rule keyword"));
    CHECK(hasMessage(msgs, 20, "INTENSITY or nHR_PRECIP"));
    CHECK(hasMessage(msgs, 21, "Rule R4 has no THEN action"));
    CHECK(hasMessage(msgs, 25, "three coefficients"));
    for (const auto& msg : msgs)
        CHECK(msg.line >= 1 && msg.line <= static_cast<int>(lines.size()));
}

TEST_CASE("rules: named variables and expressions")
{
    const auto m = model();
    const auto ctx = RuleContext::fromModel(m);
    const std::vector<std::string> lines = {
        "VARIABLE V1 = NODE WW1 DEPTH",
        "VARIABLE DEPTH = NODE WW1 DEPTH",   // keyword as name
        "EXPRESSION E1 = (V1 + 2) * SQRT(V1",  // unbalanced
        "EXPRESSION E2 = V1 + Later",          // Later defined after
        "RULE A",
        "IF Later > 1",                        // used before definition
        "THEN PUMP P1 STATUS = ON",
        "VARIABLE Later = LINK C1 FLOW",
        "RULE B",
        "IF V1 > NODE J1 HEAD",                // different attributes: warning
        "THEN PUMP P1 SETTING = TIMESERIES Storm1",
    };
    const auto msgs = validateRules(lines, RuleSyntax::Swmm, &ctx);
    CHECK(hasMessage(msgs, 2, "is a keyword"));
    CHECK(hasMessage(msgs, 3, "parentheses"));
    CHECK(hasMessage(msgs, 4, "not a named variable"));
    CHECK(hasMessage(msgs, 6, "used before it is defined"));
    CHECK(hasMessage(msgs, 10, "different attributes"));
    CHECK(std::none_of(msgs.begin(), msgs.end(),
                       [](const RuleMessage& x) { return x.line == 11; }));
}

TEST_CASE("rules: templates are valid against the model")
{
    const auto m = model();
    const auto ctx = RuleContext::fromModel(m);
    for (const auto& t : ruleTemplates(RuleSyntax::Swmm, ctx, {"PumpOn"}))
    {
        std::vector<std::string> lines = t.variables;
        lines.insert(lines.end(), t.lines.begin(), t.lines.end());
        const auto msgs = validateRules(lines, RuleSyntax::Swmm, &ctx);
        if (!msgs.empty())
            throw test::Failure(t.title + ": " + msgs[0].message);
        CHECK(!ruleNames(t.lines).count("PumpOn"));
    }
    CHECK_EQ(uniqueRuleName({"R", "r_2"}, "R"), std::string("R_3"));
    CHECK_EQ(ruleObjectIds(ctx, RuleSyntax::Swmm, "PUMP").size(), std::size_t(1));
    CHECK_EQ(ruleObjectIds(ctx, RuleSyntax::Swmm, "LINK").size(), std::size_t(5));
    CHECK(ruleAttributes(RuleSyntax::Swmm, "CONDUIT", true) == std::vector<std::string>{"STATUS"});
    CHECK(!ruleObjectHasId(RuleSyntax::Swmm, "simulation"));
}

TEST_CASE("rules: whole-model check reports file line numbers")
{
    std::string text = kModel;
    text = text::replaceAll(text, "THEN PUMP P1 STATUS = OFF", "THEN PUMP P9 STATUS = OFF");
    text = text::replaceAll(text, "Roof1  DRAINMAT 3  0.5  0.1", "Roof1  DRAINMAT 0  0.5  0.1");
    const auto m = Model::fromText(text);
    const auto issues = validateObjectSections(m);
    // 1-based line of the changed action in the text.
    const auto at = text.find("THEN PUMP P9");
    const auto before = text.substr(0, at);
    const int line = 1 + static_cast<int>(std::count(before.begin(), before.end(), '\n'));
    CHECK(std::any_of(issues.begin(), issues.end(), [&](const ModelIssue& i) {
        const std::string want = "[CONTROLS] line " + std::to_string(line) + ": Link P9";
        return i.message.find(want) != std::string::npos;
    }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const ModelIssue& i) {
        return i.message.find("LID control Roof1") != std::string::npos;
    }));
    CHECK(validateObjectSections(model()).empty());
}

TEST_CASE("rules: EPANET rules and simple controls")
{
    RuleContext ctx;
    ctx.addNode("J1", "Junction");
    ctx.addNode("T1", "Tank");
    ctx.addLink("P1", "Pipe");
    ctx.addLink("PU1", "Pump");
    ctx.addLink("V1", "Valve");
    const std::vector<std::string> rules = {
        "RULE 1",
        "IF TANK T1 LEVEL BELOW 2",
        "AND SYSTEM CLOCKTIME >= 7 AM",
        "THEN PUMP PU1 STATUS IS OPEN",
        "AND VALVE V1 SETTING IS 40",
        "ELSE PIPE P1 STATUS IS CLOSED",
        "PRIORITY 3",
        "",
        "RULE 2",
        "IF JUNCTION T1 PRESSURE ABOVE 30", // T1 is a tank: warning
        "AND TANK T1 VOLUME > 3",           // bad attribute
        "THEN PIPE P1 STATUS IS HALF",      // bad status
    };
    const auto msgs = validateRules(rules, RuleSyntax::Epanet, &ctx);
    CHECK(std::none_of(msgs.begin(), msgs.end(), [](const RuleMessage& m) { return m.line < 8; }));
    CHECK(hasMessage(msgs, 10, "not a junction"));
    CHECK(hasMessage(msgs, 11, "not an attribute of TANK"));
    CHECK(hasMessage(msgs, 12, "OPEN, CLOSED or ACTIVE"));
    for (const auto& t : ruleTemplates(RuleSyntax::Epanet, ctx, {}))
    {
        std::vector<std::string> lines = t.lines;
        const auto tm = validateRules(lines, RuleSyntax::Epanet, &ctx);
        if (!tm.empty())
            throw test::Failure(t.title + ": " + tm[0].message);
    }
    const std::vector<std::string> simple = {
        "LINK P1 OPEN AT TIME 2",
        "LINK PU1 CLOSED AT CLOCKTIME 10 PM",
        "LINK V1 1.5 IF NODE T1 ABOVE 3",
        "LINK X9 OPEN AT TIME 2",
        "LINK P1 OPEN IF NODE T1 OVER 3",
        "PUMP P1 OPEN",
    };
    const auto sm = validateSimpleControls(simple, &ctx);
    CHECK(std::none_of(sm.begin(), sm.end(), [](const RuleMessage& m) { return m.line < 4; }));
    CHECK(hasMessage(sm, 4, "Link X9 does not exist"));
    CHECK(hasMessage(sm, 5, "ABOVE or BELOW"));
    CHECK(hasMessage(sm, 6, "A control has the form"));
}
