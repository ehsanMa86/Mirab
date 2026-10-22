/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_linkage.cpp
 * Purpose: Tests of the water-sewer link: matching, flow and pattern conversion, dry-weather flow writing and a run of the example pair.
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

#include "mirab/core/Crs.h"
#include "mirab/core/Linkage.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Text.h"
#include "mirab/engine/Compatibility.h"
#include "mirab/engine/EngineRegistry.h"
#include "mirab/interop/WaterLink.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>

using namespace mirab;
namespace fs = std::filesystem;

namespace
{

fs::path example(const char* rel)
{
    return fs::path(MIRAB_SOURCE_DIR) / "examples" / rel;
}

double meanOf(const std::vector<double>& v)
{
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

/// Water network: four junctions on a line, 100 m apart, in LPS.
const char* kWater = R"([TITLE]
Link test water network

[JUNCTIONS]
;ID   Elev   Demand   Pattern
 W1   10     2        DAY
 W2   10     0
 W3   10     4        NIGHT
 W4   10     1
 W5   10     3

[RESERVOIRS]
 R1   50

[PIPES]
 P1   R1   W1   100   200   100
 P2   W1   W2   100   200   100
 P3   W2   W3   100   200   100
 P4   W3   W4   100   200   100
 P5   W4   W5   100   200   100

[DEMANDS]
;Junction  Demand  Pattern  Category
 W2        1.5     DAY      ;Residential
 W2        0.5              ;Irrigation

[PATTERNS]
 DAY     0.5  0.5  0.5  0.5  0.5  0.5  1.5  1.5  1.5  1.5  1.5  1.5
 DAY     1    1    1    1    1    1    1    1    1    1    1    1
 NIGHT   2
 NIGHT   0

[TIMES]
 Duration           24:00
 Pattern Timestep   1:00

[OPTIONS]
 Units              LPS
 Demand Multiplier  2

[COORDINATES]
 R1   -100   0
 W1   0      0
 W2   100    0
 W3   200    0
 W4   300    0
 W5   900    0

[END]
)";

/// Sewer: manholes 10 m south of W1, W3 and W4, a storage unit near W2, in CMS.
const char* kSewer = R"([TITLE]
Link test sewer

[OPTIONS]
FLOW_UNITS           CMS
FLOW_ROUTING         DYNWAVE
START_DATE           01/01/2026
END_DATE             01/02/2026

[JUNCTIONS]
;;Name  Elevation  MaxDepth
 S1     5          3
 S3     4          3
 S4     3          3

[OUTFALLS]
 OUT    1   FREE

[STORAGE]
 ST2    4.5   3   0   FUNCTIONAL   1000   0   0

[CONDUITS]
 C1   S1    ST2   100   0.013   0   0
 C2   ST2   S3    100   0.013   0   0
 C3   S3    S4    100   0.013   0   0
 C4   S4    OUT   100   0.013   0   0

[XSECTIONS]
 C1   CIRCULAR   0.3   0   0   0   1
 C2   CIRCULAR   0.3   0   0   0   1
 C3   CIRCULAR   0.3   0   0   0   1
 C4   CIRCULAR   0.3   0   0   0   1

[DWF]
;;Node  Constituent  Baseline  Patterns
 S1     FLOW         9.9       ; replaced by the link
 S1     TSS          120
 S4     FLOW         0.002

[PATTERNS]
 KEEP    HOURLY   1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1

[TAGS]
 Node   S1    Sanitary
 Node   S3    Sanitary
 Node   ST2   Sanitary

[COORDINATES]
 S1    0     -10
 ST2   100   -10
 S3    200   -10
 S4    300   -10
 OUT   400   -10

[END]
)";

core::Model water()
{
    return core::Model::fromText(kWater);
}

core::Model sewer()
{
    return core::Model::fromText(kSewer);
}

const core::NodeAssignment& find(const std::vector<core::NodeAssignment>& list, const std::string& id)
{
    for (const auto& a : list)
    {
        if (a.waterNode == id)
            return a;
    }
    throw std::runtime_error("no assignment for " + id);
}

const core::DwfInflow* inflow(const core::DwfPlan& plan, const std::string& node)
{
    for (const auto& in : plan.inflows)
    {
        if (in.node == node)
            return &in;
    }
    return nullptr;
}

} // namespace

TEST_CASE("linkage: flow unit conversion")
{
    CHECK_NEAR(core::convertFlow(448.831, "GPM", "CFS"), 1.0, 1e-5);
    CHECK_NEAR(core::convertFlow(1000, "LPS", "CMS"), 1.0, 1e-12);
    CHECK_NEAR(core::convertFlow(1, "MGD", "CFS"), 1.5472, 1e-4);
    CHECK_NEAR(core::convertFlow(1, "MLD", "LPS"), 11.5741, 1e-4);
    CHECK_NEAR(core::convertFlow(86400, "CMD", "CMS"), 1.0, 1e-12);
    CHECK_NEAR(core::convertFlow(1, "CFS", "LPS"), 28.3168, 1e-4);
    CHECK_NEAR(core::convertFlow(3600, "CMH", "LPS"), 1000.0, 1e-9);
    CHECK_NEAR(core::convertFlow(60, "lpm", "lps"), 1.0, 1e-12);
    bool threw = false;
    try
    {
        core::convertFlow(1, "GPM", "XYZ");
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("linkage: EPANET patterns become hourly series by clock hour")
{
    std::vector<double> hourly(24);
    for (int i = 0; i < 24; ++i)
        hourly[static_cast<std::size_t>(i)] = i;
    core::PatternTiming t;
    auto s = core::hourlyPatternSeries(hourly, t, 1);
    CHECK_EQ(s.size(), std::size_t(24));
    CHECK_NEAR(s[5], 5.0, 1e-12);
    // Two-hour steps: each multiplier covers two hours.
    t.step = 7200;
    s = core::hourlyPatternSeries({1, 2, 3}, t, 1);
    CHECK_NEAR(s[0], 1, 1e-12);
    CHECK_NEAR(s[1], 1, 1e-12);
    CHECK_NEAR(s[2], 2, 1e-12);
    CHECK_NEAR(s[6], 1, 1e-12); // repeats every six hours
    // Half-hour steps are averaged.
    t.step = 1800;
    s = core::hourlyPatternSeries({1, 3}, t, 1);
    CHECK_NEAR(s[0], 2.0, 1e-12);
    CHECK_NEAR(s[23], 2.0, 1e-12);
    // A start clock time of 6 am: clock hour 6 is the first multiplier.
    t.step = 3600;
    t.clock = 6 * 3600;
    s = core::hourlyPatternSeries(hourly, t, 1);
    CHECK_NEAR(s[6], 0.0, 1e-12);
    CHECK_NEAR(s[7], 1.0, 1e-12);
    CHECK_NEAR(s[0], 18.0, 1e-12);
    // A pattern start of 2 hours moves the pattern forward.
    t.clock = 0;
    t.start = 7200;
    s = core::hourlyPatternSeries(hourly, t, 1);
    CHECK_NEAR(s[0], 2.0, 1e-12);
    CHECK_NEAR(s[22], 0.0, 1e-12);
    // Two days of a 36-hour pattern.
    t.start = 0;
    std::vector<double> longer(36, 1.0);
    longer[30] = 7;
    s = core::hourlyPatternSeries(longer, t, 2);
    CHECK_EQ(s.size(), std::size_t(48));
    CHECK_NEAR(s[30], 7.0, 1e-12);
    CHECK_NEAR(s[42], 1.0, 1e-12); // hour 42 is pattern hour 6
}

TEST_CASE("linkage: result series, lag and normalisation")
{
    std::vector<double> times, values;
    for (int i = 0; i <= 48; ++i)
    {
        times.push_back(i * 1800.0);
        values.push_back(i % 2 == 0 ? 1.0 : 3.0);
    }
    auto s = core::hourlyResultSeries(times, values, 0, 1);
    CHECK_NEAR(s[0], 2.0, 1e-9);
    CHECK_NEAR(s[23], 2.0, 1e-9);
    // Hourly reports from 18:00: clock hour 18 is the first value; earlier hours a day later.
    times.clear();
    values.clear();
    for (int i = 0; i <= 24; ++i)
    {
        times.push_back(i * 3600.0);
        values.push_back(i);
    }
    s = core::hourlyResultSeries(times, values, 18 * 3600.0, 1);
    CHECK_NEAR(s[18], 0.0, 1e-9);
    CHECK_NEAR(s[23], 5.0, 1e-9);
    CHECK_NEAR(s[0], 6.0, 1e-9);
    CHECK_NEAR(s[17], 23.0, 1e-9);

    const std::vector<double> base = {0, 1, 2, 3};
    auto lagged = core::lagSeries(base, 1);
    CHECK_NEAR(lagged[0], 3, 1e-12);
    CHECK_NEAR(lagged[1], 0, 1e-12);
    lagged = core::lagSeries(base, 0.5);
    CHECK_NEAR(lagged[1], 0.5, 1e-12);
    CHECK_NEAR(lagged[0], 1.5, 1e-12); // between the last and the first value
    lagged = core::lagSeries(base, -1);
    CHECK_NEAR(lagged[3], 0, 1e-12);

    auto n = core::normalised({1, 2, 3, 6});
    CHECK_NEAR(meanOf(n), 1.0, 1e-12);
    CHECK_NEAR(n[3], 2.0, 1e-12);
    n = core::normalised({0, 0});
    CHECK_NEAR(n[0], 0.0, 1e-12);
}

TEST_CASE("linkage: weekday, weekend and daily profiles")
{
    // Monday start: weekdays twice the weekend flow, with different shapes.
    std::vector<double> series;
    for (int d = 0; d < 7; ++d)
    {
        for (int h = 0; h < 24; ++h)
            series.push_back(d < 5 ? (h < 12 ? 1.0 : 3.0) : (h < 12 ? 1.5 : 0.5));
    }
    auto p = core::dailyProfiles(series, true, 0);
    CHECK_EQ(p.hourly.size(), std::size_t(24));
    CHECK_NEAR(p.hourly[0], 0.5, 1e-12);
    CHECK_NEAR(p.hourly[12], 1.5, 1e-12);
    CHECK_NEAR(p.weekend[0], 1.5, 1e-12);
    CHECK_NEAR(meanOf(p.weekend), 1.0, 1e-12);
    const double mean = (5 * 2.0 + 2 * 1.0) / 7;
    CHECK_NEAR(p.mean, mean, 1e-12);
    CHECK_EQ(p.daily.size(), std::size_t(7));
    CHECK_NEAR(p.daily[0], 1.0 / mean, 1e-12); // Sunday
    CHECK_NEAR(p.daily[1], 2.0 / mean, 1e-12); // Monday
    CHECK_NEAR(p.daily[6], 1.0 / mean, 1e-12); // Saturday
    // Starting on a Saturday moves the weekend to the first two days.
    p = core::dailyProfiles(series, true, 5);
    CHECK_NEAR(p.weekend[0], 0.5, 1e-12);
    // Without the split every day is folded into one profile.
    p = core::dailyProfiles(series, false, 0);
    CHECK(p.weekend.empty());
    CHECK(p.daily.empty());
    CHECK_NEAR(meanOf(p.hourly), 1.0, 1e-12);
}

TEST_CASE("linkage: water demands with categories and the demand multiplier")
{
    const auto demands = core::waterDemands(water());
    // W2's [JUNCTIONS] demand is replaced by its two [DEMANDS] rows.
    int w2 = 0;
    for (const auto& d : demands)
    {
        if (d.node != "W2")
            continue;
        ++w2;
        if (d.category == "Residential")
        {
            CHECK_NEAR(d.base, 3.0, 1e-12); // 1.5 x demand multiplier 2
            CHECK_EQ(d.pattern, std::string("DAY"));
        }
        else
        {
            CHECK_EQ(d.category, std::string("Irrigation"));
            CHECK_EQ(d.pattern, std::string()); // no pattern "1": constant
        }
    }
    CHECK_EQ(w2, 2);
    const auto cats = core::demandCategories(water());
    CHECK_EQ(cats.size(), std::size_t(3));
    CHECK_EQ(cats[0], std::string());
    CHECK_EQ(cats[1], std::string("Irrigation"));
}

TEST_CASE("linkage: nearest node matching with distance, tags, storage and overrides")
{
    const auto w = water();
    const auto s = sewer();
    core::WaterLinkSettings set;
    core::LinkContext ctx;
    auto m = core::matchWaterNodes(w, s, set, ctx);
    CHECK_EQ(m.size(), std::size_t(5));
    CHECK_EQ(find(m, "W1").swmmNode, std::string("S1"));
    CHECK_NEAR(find(m, "W1").distance, 10.0, 1e-9);
    CHECK_EQ(find(m, "W2").swmmNode, std::string("ST2"));
    CHECK_EQ(find(m, "W5").swmmNode, std::string("S4")); // the outfall never receives flow

    set.maxDistance = 50;
    m = core::matchWaterNodes(w, s, set, ctx);
    CHECK(find(m, "W5").swmmNode.empty());
    CHECK(find(m, "W5").status == core::NodeAssignment::Status::TooFar);
    CHECK_EQ(find(m, "W5").nearest, std::string("S4"));
    CHECK_NEAR(find(m, "W5").distance, std::hypot(600.0, 10.0), 1e-9);

    set.storage = false;
    set.maxDistance = 0;
    m = core::matchWaterNodes(w, s, set, ctx);
    CHECK(find(m, "W2").swmmNode == "S1" || find(m, "W2").swmmNode == "S3"); // equally far
    set.storage = true;

    set.nodeTag = "sanitary";
    set.maxDistance = 0;
    m = core::matchWaterNodes(w, s, set, ctx);
    CHECK_EQ(find(m, "W4").swmmNode, std::string("S3"));
    set.nodeTag.clear();

    set.overrides = {{"W1", "S4"}, {"W3", ""}, {"W4", "NOPE"}};
    std::vector<std::string> notes;
    m = core::matchWaterNodes(w, s, set, ctx, &notes);
    CHECK_EQ(find(m, "W1").swmmNode, std::string("S4"));
    CHECK(find(m, "W1").status == core::NodeAssignment::Status::Manual);
    CHECK(find(m, "W3").swmmNode.empty());
    CHECK(find(m, "W3").status == core::NodeAssignment::Status::Excluded);
    CHECK_EQ(find(m, "W4").swmmNode, std::string("S4"));
    CHECK_EQ(notes.size(), std::size_t(1));
}

TEST_CASE("linkage: service areas keep flow in its own area")
{
    const auto w = water();
    const auto s = sewer();
    core::WaterLinkSettings set;
    core::LinkContext ctx;
    // Area A holds W1, W2 and S1 only; area B holds W3 and S3, S4.
    ctx.serviceAreas = {{"A", {{-50, -50}, {150, -50}, {150, 50}, {-50, 50}}},
                        {"B", {{150, -50}, {350, -50}, {350, 50}, {150, 50}}}};
    auto m = core::matchWaterNodes(w, s, set, ctx);
    CHECK_EQ(find(m, "W2").swmmNode, std::string("ST2")); // ST2 at x=100 lies in area A
    CHECK_EQ(find(m, "W2").area, std::string("A"));
    CHECK_EQ(find(m, "W3").swmmNode, std::string("S3"));
    CHECK(find(m, "W5").status == core::NodeAssignment::Status::OutsideAreas);
    // An area without SWMM nodes.
    ctx.serviceAreas = {{"Empty", {{-50, -1}, {150, -1}, {150, 50}, {-50, 50}}}};
    m = core::matchWaterNodes(w, s, set, ctx);
    CHECK(find(m, "W1").status == core::NodeAssignment::Status::NoCandidate);
}

TEST_CASE("linkage: water coordinates are reprojected into the sewer system")
{
    // The water network in geographic coordinates, the sewer in State Plane feet.
    const auto sp = crs::fromEpsg(2229);
    const auto geo = crs::fromEpsg(4326);
    CHECK(sp && geo);
    auto w = water();
    auto s = sewer();
    const double x0 = 6504000, y0 = 1742000;
    for (const auto& n : s.nodes())
        s.moveNode(n.id, {x0 + n.pos.x * 3.28, y0 + n.pos.y * 3.28});
    for (const auto& n : w.nodes())
    {
        double lon = 0, lat = 0;
        CHECK(crs::toLonLat(*sp, x0 + n.pos.x * 3.28, y0 + n.pos.y * 3.28 + 5, lon, lat));
        w.moveNode(n.id, {lon, lat});
    }
    core::WaterLinkSettings set;
    set.maxDistance = 50;
    core::LinkContext ctx;
    ctx.waterCrs = "EPSG:4326";
    ctx.swmmCrs = "EPSG:2229";
    std::vector<std::string> notes;
    const auto m = core::matchWaterNodes(w, s, set, ctx, &notes);
    CHECK_EQ(find(m, "W1").swmmNode, std::string("S1"));
    CHECK_NEAR(find(m, "W1").distance, 10 * 3.28 + 5, 0.05);
    CHECK_EQ(find(m, "W3").swmmNode, std::string("S3"));
    CHECK_NEAR(find(m, "W3").waterPos.x, x0 + 200 * 3.28, 0.05);
    CHECK(!notes.empty());
    // Without a coordinate system on one side, coordinates are used as they are.
    ctx.waterCrs.clear();
    const auto raw = core::matchWaterNodes(w, s, set, ctx);
    CHECK(find(raw, "W1").swmmNode.empty());
}

TEST_CASE("linkage: dry-weather flow from demands, categories and patterns")
{
    const auto w = water();
    const auto s = sewer();
    core::WaterLinkSettings set;
    set.returnFactor = 0.8;
    set.categoryFactors["Irrigation"] = 0;
    core::LinkContext ctx;
    const auto plan = core::planDwf(w, s, set, ctx);
    CHECK_EQ(plan.flowUnits, std::string("CMS"));
    // W1: 2 LPS x2 (multiplier) x mean(DAY) = 4 x 1 = 4 LPS; 80 percent returns.
    const auto* s1 = inflow(plan, "S1");
    CHECK(s1 != nullptr);
    CHECK_NEAR(s1->average, 4 * 0.8 / 1000, 1e-12);
    CHECK_EQ(s1->hourly, std::string("WW_DAY"));
    // W2: residential 3 LPS (DAY) returns, irrigation 1 LPS does not.
    const auto* st2 = inflow(plan, "ST2");
    CHECK(st2 != nullptr);
    CHECK_NEAR(st2->average, 3 * 0.8 / 1000, 1e-12);
    CHECK_EQ(st2->hourly, std::string("WW_DAY")); // same profile, same pattern
    // W3 (8 LPS, NIGHT averages 1) drains to S3; W4 and W5 (constant 2 and 6 LPS) to S4.
    const auto* s3 = inflow(plan, "S3");
    CHECK_NEAR(s3->average, 8 * 0.8 / 1000, 1e-12);
    const auto* s4 = inflow(plan, "S4");
    CHECK_NEAR(s4->average, (2 + 6) * 0.8 / 1000, 1e-12);
    CHECK(s4->hourly.empty()); // constant flow needs no pattern
    // Totals in CMS: every category, then the ones that return.
    CHECK_NEAR(plan.totalDemand, (4 + 3 + 1 + 8 + 2 + 6) / 1000.0, 1e-12);
    CHECK_NEAR(plan.includedDemand, (4 + 3 + 8 + 2 + 6) / 1000.0, 1e-12);
    CHECK_NEAR(plan.returned, plan.includedDemand * 0.8, 1e-12);
    CHECK_EQ(plan.unassigned, 0);

    // The DAY pattern: 0.5 for six hours, 1.5 for six, then 1; mean 1.
    bool found = false;
    for (const auto& p : plan.patterns)
    {
        if (p.name != "WW_DAY")
            continue;
        found = true;
        CHECK_EQ(p.type, std::string("HOURLY"));
        CHECK_NEAR(p.factors[0], 0.5, 1e-12);
        CHECK_NEAR(p.factors[6], 1.5, 1e-12);
        CHECK_NEAR(meanOf(p.factors), 1.0, 1e-9);
    }
    CHECK(found);
}

TEST_CASE("linkage: patterns of several sources are weighted by flow, with a lag")
{
    auto w = water();
    const auto s = sewer();
    core::WaterLinkSettings set;
    set.returnFactor = 1.0;
    set.overrides = {{"W1", "S3"}}; // W1 (DAY, 4 LPS) and W3 (NIGHT, 8 LPS average) share S3
    set.lagHours = 2;
    const auto plan = core::planDwf(w, s, set, {});
    const auto* s3 = inflow(plan, "S3");
    CHECK(s3 != nullptr);
    CHECK_NEAR(s3->average, (4 + 8) / 1000.0, 1e-12);
    CHECK_EQ(s3->hourly, std::string("WW_S3"));
    for (const auto& p : plan.patterns)
    {
        if (p.name != "WW_S3")
            continue;
        // Hour h (before the lag): W1 4 x DAY[h], W3 16 at even hours, 0 at odd ones.
        auto expected = [](int h) {
            const double day = h < 6 ? 0.5 : h < 12 ? 1.5 : 1.0;
            return (4 * day + (h % 2 == 0 ? 16.0 : 0.0)) / 12.0;
        };
        CHECK_NEAR(p.factors[2], expected(0), 1e-4);
        CHECK_NEAR(p.factors[9], expected(7), 1e-4);
        CHECK_NEAR(p.factors[0], expected(22), 1e-4);
    }
}

TEST_CASE("linkage: weekly patterns give weekend and daily patterns")
{
    auto w = water();
    // A weekly pattern for W1: weekdays 1, weekend days 0.5 with an evening peak.
    auto& pats = *w.document().find("PATTERNS");
    for (int d = 0; d < 7; ++d)
    {
        for (int h = 0; h < 24; ++h)
            pats.appendRow({"WEEK", d < 5 ? "1" : (h == 20 ? "12.5" : "0")});
    }
    w.document().find("JUNCTIONS")->setRowTokens(0, {"W1", "10", "2", "WEEK"});
    w.rebuild();
    const auto s = sewer();
    core::WaterLinkSettings set;
    set.weekend = true;
    set.returnFactor = 1;
    set.weekStart = 0;
    const auto plan = core::planDwf(w, s, set, {});
    CHECK_EQ(plan.days, 7);
    const auto* s1 = inflow(plan, "S1");
    CHECK_EQ(s1->hourly, std::string("WW_WEEK"));
    CHECK_EQ(s1->weekend, std::string("WW_WEEK_WE"));
    CHECK_EQ(s1->daily, std::string("WW_WEEK_DAY"));
    const double weekMean = (5 * 1.0 + 2 * (12.5 / 24)) / 7;
    CHECK_NEAR(s1->average, 4 * weekMean / 1000, 1e-9);
    for (const auto& p : plan.patterns)
    {
        if (p.name == "WW_WEEK_WE")
        {
            CHECK_NEAR(p.factors[20], 24.0, 1e-9);
            CHECK_NEAR(p.factors[0], 0.0, 1e-9);
        }
        if (p.name == "WW_WEEK_DAY")
        {
            CHECK_EQ(p.factors.size(), std::size_t(7));
            CHECK_NEAR(p.factors[0], std::round((12.5 / 24) / weekMean * 1e4) / 1e4, 1e-9); // Sunday
            CHECK_NEAR(p.factors[3], std::round(1.0 / weekMean * 1e4) / 1e4, 1e-9);         // Wednesday
        }
    }
    // Daily sources cannot give a weekend pattern.
    const auto daily = core::planDwf(water(), s, set, {});
    CHECK(inflow(daily, "S1")->weekend.empty());
    CHECK(!daily.notes.empty());
}

TEST_CASE("linkage: results give the delivered demand")
{
    const auto w = water();
    const auto s = sewer();
    core::ResultSet r;
    r.flowUnits = "LPS";
    std::vector<std::string> ids = {"R1", "W1", "W2", "W3", "W4", "W5"};
    r.defineTable("node", ids, {{"Demand", "LPS"}, {"Emitter Flow", "LPS"}});
    for (int p = 0; p <= 48; ++p)
    {
        const int period = r.addPeriod(p * 3600.0);
        auto* t = r.table("node");
        t->resizePeriods(r.periodCount());
        for (int e = 0; e < 6; ++e)
        {
            t->set(period, 0, e, e == 1 ? (p % 24 < 12 ? 1.0f : 3.0f) : 2.0f);
            t->set(period, 1, e, e == 5 ? 1.0f : 0.0f);
        }
    }
    core::WaterLinkSettings set;
    set.returnFactor = 0.5;
    set.categoryFactors["Irrigation"] = 0;
    core::LinkContext ctx;
    ctx.results = &r;
    const auto plan = core::planDwf(w, s, set, ctx);
    CHECK_EQ(plan.days, 2);
    CHECK_NEAR(inflow(plan, "S1")->average, 2.0 * 0.5 / 1000, 1e-9);
    for (const auto& p : plan.patterns)
    {
        if (p.name == "WW_DAY")
        {
            CHECK_NEAR(p.factors[0], 0.5, 1e-9);
            CHECK_NEAR(p.factors[12], 1.5, 1e-9);
        }
    }
    // W2 delivers 2 LPS; residential (3 x 1) and irrigation (1 x 1): 3/4 at 0.5 returns.
    CHECK_NEAR(inflow(plan, "ST2")->average, 2.0 * 0.75 * 0.5 / 1000, 1e-9);
    // W5's emitter flow is left out: 1 LPS of 2, together with W4's 2 LPS.
    CHECK_NEAR(inflow(plan, "S4")->average, 3.0 * 0.5 / 1000, 1e-9);

    core::ResultSet shortRun = r;
    shortRun.times.resize(10);
    bool threw = false;
    try
    {
        ctx.results = &shortRun;
        core::planDwf(w, s, set, ctx);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("linkage: writing [DWF] and [PATTERNS] keeps the rest of the file")
{
    const auto w = water();
    auto s = sewer();
    core::WaterLinkSettings set;
    auto plan = core::planDwf(w, s, set, {});
    const int changed = core::applyDwf(s, plan, set.patternPrefix);
    CHECK(changed > 0);
    const std::string text = s.toText();
    // Untouched rows and comments stay.
    CHECK(text.find(" S1     TSS          120") != std::string::npos);
    CHECK(text.find(";;Node  Constituent  Baseline  Patterns") != std::string::npos);
    CHECK(text.find("KEEP    HOURLY") != std::string::npos);
    CHECK(text.find("; replaced by the link") != std::string::npos); // the row's comment
    CHECK(text.find("9.9") == std::string::npos);

    // The written file reads back with the same values.
    const auto back = core::Model::fromText(text);
    std::map<std::string, std::vector<std::string>> flowRows;
    for (const auto& t : back.document().find("DWF")->allRowTokens())
    {
        if (t.size() > 1 && t[1] == "FLOW")
        {
            CHECK(!flowRows.count(t[0]));
            flowRows[t[0]] = t;
        }
    }
    CHECK_EQ(flowRows.size(), std::size_t(4));
    CHECK_NEAR(*text::toDouble(flowRows["S1"][2]), inflow(plan, "S1")->average, 1e-9);
    CHECK_EQ(flowRows["S1"][5], std::string("WW_DAY"));
    CHECK_EQ(flowRows["S1"][3], std::string("\"\""));
    CHECK_EQ(flowRows["S4"].size(), std::size_t(3));
    CHECK_EQ(back.rowsOfKey("PATTERNS", "WW_DAY").size(), std::size_t(4));
    CHECK_EQ(back.document().find("PATTERNS")->rowTokens(back.rowsOfKey("PATTERNS", "WW_DAY")[0])[1],
             std::string("HOURLY"));

    // Applying the same plan again changes nothing.
    CHECK_EQ(core::applyDwf(s, plan, set.patternPrefix), 0);

    // Excluding W1 and W2 removes the FLOW rows written for S1 and ST2, and
    // WW_DAY, which nothing uses any more.
    set.overrides = {{"W1", ""}, {"W2", ""}};
    plan = core::planDwf(w, s, set, {});
    CHECK(core::applyDwf(s, plan, set.patternPrefix) > 0);
    const auto after = core::Model::fromText(s.toText());
    int s1Flow = 0;
    for (const auto& t : after.document().find("DWF")->allRowTokens())
        s1Flow += t[0] == "S1" && t[1] == "FLOW" ? 1 : 0;
    CHECK_EQ(s1Flow, 0);
    CHECK(after.rowsOfKey("PATTERNS", "WW_DAY").empty());
    CHECK(!after.rowsOfKey("PATTERNS", "KEEP").empty());
}

TEST_CASE("linkage: sections are created where SWMM lists them")
{
    auto s = core::Model::fromText("[OPTIONS]\nFLOW_UNITS CFS\n\n[JUNCTIONS]\n J1 0 3\n\n"
                                   "[COORDINATES]\n J1 0 0\n\n[END]\n");
    auto w = core::Model::fromText("[JUNCTIONS]\n W1 0 100 P\n\n[PATTERNS]\n P 1 2\n\n"
                                   "[OPTIONS]\n Units GPM\n\n[COORDINATES]\n W1 1 1\n\n[END]\n");
    core::WaterLinkSettings set;
    const auto plan = core::planDwf(w, s, set, {});
    CHECK_NEAR(plan.inflows.at(0).average, core::convertFlow(100 * 1.5 * 0.8, "GPM", "CFS"), 1e-9);
    core::applyDwf(s, plan, set.patternPrefix);
    const auto& secs = s.document().sections;
    std::vector<std::string> names;
    for (const auto& sec : secs)
        names.push_back(sec.name);
    CHECK_EQ(text::join(names, ","), std::string("OPTIONS,JUNCTIONS,DWF,PATTERNS,COORDINATES,END"));
}

TEST_CASE("linkage: settings round trip through the project sidecar")
{
    core::WaterLinkSettings s;
    s.waterModel = "../water/net.inp";
    s.maxDistance = 120;
    s.nodeTag = "Sanitary";
    s.serviceAreas = "subcatchments";
    s.returnFactor = 0.75;
    s.categoryFactors = {{"Irrigation", 0.0}, {"", 0.9}};
    s.categories = {"Residential", ""};
    s.weekend = true;
    s.weekStart = 6;
    s.lagHours = 1.5;
    s.results = "../water/net.anr";
    s.overrides = {{"J1", "MH2"}, {"J2", ""}};
    s.links = {{"J3", "MH4"}};
    s.storage = false;
    s.showReference = false;

    const fs::path dir = fs::temp_directory_path() / "mirab-tests";
    fs::create_directories(dir);
    const fs::path inp = dir / "linked-settings.inp";
    core::ProjectSettings ps;
    ps.crs = "EPSG:2229";
    ps.waterLink = s.toJson();
    ps.saveFor(inp);
    const auto loaded = core::ProjectSettings::loadFor(inp);
    const auto r = core::WaterLinkSettings::fromJson(loaded.waterLink);
    CHECK_EQ(r.waterModel, s.waterModel);
    CHECK_NEAR(r.maxDistance, 120, 1e-12);
    CHECK_EQ(r.nodeTag, s.nodeTag);
    CHECK_EQ(r.serviceAreas, s.serviceAreas);
    CHECK_NEAR(r.returnFactor, 0.75, 1e-12);
    CHECK_NEAR(r.factorFor(""), 0.9, 1e-12);
    CHECK_NEAR(r.factorFor("Irrigation"), 0.0, 1e-12);
    CHECK_NEAR(r.factorFor("Residential"), 0.75, 1e-12);
    CHECK_NEAR(r.factorFor("Commercial"), 0.0, 1e-12); // not in the category list
    CHECK(r.weekend);
    CHECK_EQ(r.weekStart, 6);
    CHECK_NEAR(r.lagHours, 1.5, 1e-12);
    CHECK_EQ(r.results, s.results);
    CHECK(r.overrides == s.overrides);
    CHECK(r.links == s.links);
    CHECK(!r.storage);
    CHECK(!r.showReference);
    CHECK_EQ(loaded.crs, std::string("EPSG:2229"));
    fs::remove(core::ProjectSettings::sidecarPath(inp));

    // Stored paths are relative to the SWMM file.
    CHECK_EQ(interop::storedLinkPath(dir / "sewer" / "a.inp", dir / "water" / "b.inp"),
             std::string("../water/b.inp"));
    CHECK(interop::resolveLinkPath(dir / "sewer" / "a.inp", "../water/b.inp") ==
          (dir / "water" / "b.inp").lexically_normal());
}

TEST_CASE("linkage: the example pair is linked and its dry-weather flow is current")
{
    const fs::path inp = example("swmm/mirab-longbeach-sewer.inp");
    auto swmm = core::Model::fromFile(inp);
    const auto settings = core::ProjectSettings::loadFor(inp);
    const auto link = core::WaterLinkSettings::fromJson(settings.waterLink);
    CHECK(!link.waterModel.empty());
    auto linked = interop::loadLinkedModels(inp, swmm, settings.crs, link);
    const auto plan = core::planDwf(linked->water, swmm, link, linked->context);
    CHECK_EQ(plan.unassigned, 0);
    CHECK_EQ(plan.inflows.size(), std::size_t(8));
    // 715 gpm of base demand times the mean of PAT1, 80 percent returned, in cfs.
    CHECK_NEAR(plan.returned, core::convertFlow(715 * 23.3 / 24 * 0.8, "GPM", "CFS"), 1e-4);
    // The shipped model holds exactly this plan, and the stored links match.
    CHECK_EQ(core::applyDwf(swmm, plan, link.patternPrefix), 0);
    for (const auto& a : plan.assignments)
        CHECK_EQ(link.links.at(a.waterNode), a.swmmNode);
}

namespace
{

engine::EngineRegistry& registry()
{
    static engine::EngineRegistry r({fs::u8path(MIRAB_ENGINE_DIR)});
    return r;
}

core::ResultSet runModel(const std::string& engineId, const core::Model& model, const fs::path& input)
{
    std::string error;
    auto e = registry().create(engineId, &error);
    if (!e)
        throw std::runtime_error(engineId + ": " + error);
    std::ofstream(input, std::ios::binary) << engine::prepareInput(model, e->descriptor());
    engine::RunRequest req;
    req.input = input;
    req.reportFile = fs::path(input).replace_extension(".rpt");
    req.workDir = fs::path(input).replace_extension("");
    fs::create_directories(req.workDir);
    auto outcome = e->run(req, {});
    if (!outcome.success)
        throw std::runtime_error(engineId + " failed: " + outcome.message);
    return outcome.results;
}

} // namespace

TEST_CASE("linkage-run: EPANET results drive the sewer and SWMM carries the flow")
{
    // Start from an empty folder: MinGW's copy_file fails on existing files even with overwrite_existing.
    const fs::path dir = fs::temp_directory_path() / "mirab-tests" / "linkage";
    fs::remove_all(dir);
    fs::create_directories(dir / "swmm");
    fs::create_directories(dir / "epanet");
    for (const char* f : {"swmm/mirab-longbeach-sewer.inp", "swmm/mirab-longbeach-sewer.inp.mirab.json",
                          "epanet/mirab-demo-longbeach.inp", "epanet/mirab-demo-longbeach.inp.mirab.json"})
        fs::copy_file(example(f), dir / f);
    const fs::path inp = dir / "swmm/mirab-longbeach-sewer.inp";
    auto swmm = core::Model::fromFile(inp);
    const auto settings = core::ProjectSettings::loadFor(inp);
    const auto link = core::WaterLinkSettings::fromJson(settings.waterLink);
    auto linked = interop::loadLinkedModels(inp, swmm, settings.crs, link);

    // The delivered demand of an EPANET run gives the same flow as the demands.
    const auto water = runModel("epanet-2.2-epa", linked->water, dir / "epanet/run.inp");
    linked->context.results = &water;
    const auto fromResults = core::planDwf(linked->water, swmm, link, linked->context);
    linked->context.results = nullptr;
    const auto fromDemands = core::planDwf(linked->water, swmm, link, linked->context);
    CHECK_NEAR(fromResults.returned, fromDemands.returned, 1e-3 * fromDemands.returned);
    CHECK_EQ(fromResults.inflows.size(), fromDemands.inflows.size());

    // The sewer run: the outfall carries the dry-weather flow on the second day.
    CHECK(core::applyDwf(swmm, fromResults, link.patternPrefix) >= 0);
    const auto sewer = runModel("swmm-5.2-epa", swmm, dir / "swmm/run.inp");
    const auto* nodes = sewer.table("node");
    CHECK(nodes != nullptr);
    const int var = nodes->variableIndex("Total Inflow");
    const int out = nodes->elementIndex("OF-1");
    CHECK(var >= 0 && out >= 0);
    double sum = 0;
    int n = 0;
    for (int p = 0; p < sewer.periodCount(); ++p)
    {
        if (sewer.times[static_cast<std::size_t>(p)] <= 86400.0)
            continue;
        sum += nodes->value(p, var, out);
        ++n;
    }
    CHECK(n > 0);
    CHECK_NEAR(sum / n, fromResults.returned, 0.02 * fromResults.returned);
}
