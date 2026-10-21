/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_engines.cpp
 * Purpose: Integration tests that run every bundled engine, the resilience tools and the interchange formats.
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

#include "mirab/core/Features.h"
#include "mirab/core/LiveResults.h"
#include "mirab/core/Model.h"
#include "mirab/core/Text.h"
#include "mirab/core/Hash.h"
#include "mirab/engine/Compatibility.h"
#include "mirab/engine/EngineInstall.h"
#include "mirab/engine/EngineRegistry.h"
#include "mirab/interop/Interop.h"
#include "mirab/resilience/Resilience.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

using namespace mirab;
namespace fs = std::filesystem;

namespace
{
fs::path example(const char* rel)
{
    return fs::path(MIRAB_SOURCE_DIR) / "examples" / rel;
}

fs::path scratch(const std::string& name)
{
    // One folder per process: ctest runs the test groups in parallel.
    static const std::string tag =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto dir = fs::temp_directory_path() / ("mirab-tests-" + tag);
    fs::create_directories(dir);
    return dir / name;
}

engine::EngineRegistry& registry()
{
    static engine::EngineRegistry r({fs::u8path(MIRAB_ENGINE_DIR)});
    return r;
}

core::ResultSet run(const std::string& engineId, const fs::path& inp, const fs::path& msx = {})
{
    std::string error;
    auto e = registry().create(engineId, &error);
    if (!e)
        throw std::runtime_error(engineId + ": " + error);
    const auto model = core::Model::fromFile(inp);
    std::vector<std::string> notes;
    const std::string prepared = engine::prepareInput(model, e->descriptor(), &notes);
    const fs::path input = scratch(engineId + "-" + inp.filename().u8string());
    std::ofstream(input, std::ios::binary) << prepared;
    engine::RunRequest req;
    req.input = input;
    req.msxInput = msx;
    req.reportFile = scratch(engineId + ".rpt");
    req.workDir = scratch(engineId + "-work");
    fs::create_directories(req.workDir);
    auto outcome = e->run(req, {});
    if (!outcome.success)
        throw std::runtime_error(engineId + " failed: " + outcome.message);
    return outcome.results;
}

double value(const core::ResultSet& r, const char* table, const char* var, const char* id, int period)
{
    const auto* t = r.table(table);
    if (!t)
        throw std::runtime_error(std::string("no table ") + table);
    const int v = t->variableIndex(var);
    const int e = t->elementIndex(id);
    if (v < 0 || e < 0)
        throw std::runtime_error(std::string("missing ") + var + " for " + id);
    return t->value(period, v, e);
}
} // namespace

TEST_CASE("engines: bundled engines are discovered")
{
    for (const char* id : {"epanet-2.2-epa", "epanet-2.3-owa", "epanet-msx-2.0-epa22", "epanet-msx-2.0-owa23",
                           "swmm-5.2-epa"})
    {
        std::string why;
        CHECK(registry().isAvailable(id, &why));
    }
    std::string why;
    // SWMM5+ is installed separately; its absence must be reported, not fatal.
    if (!registry().isAvailable("swmm5plus-1.0", &why))
        CHECK(!why.empty());
}

TEST_CASE("engines: EPA 2.2 and OWA 2.3 agree on a plain network")
{
    const auto a = run("epanet-2.2-epa", example("epanet/mirab-demo.inp"));
    const auto b = run("epanet-2.3-owa", example("epanet/mirab-demo.inp"));
    CHECK_EQ(a.periodCount(), 25);
    CHECK_EQ(b.periodCount(), 25);
    for (int p : {0, 8, 24})
    {
        CHECK_NEAR(value(a, "node", "Pressure", "J9", p), value(b, "node", "Pressure", "J9", p), 1e-3);
        CHECK_NEAR(value(a, "link", "Flow", "PU1", p), value(b, "link", "Flow", "PU1", p), 1e-3);
    }
    CHECK(value(b, "node", "Pressure", "J5", 8) > 20.0);
    CHECK(value(b, "node", "CHLORINE", "J9", 12) > 0.5);
}

TEST_CASE("engines: OWA 2.3 models FAVAD leakage, EPA 2.2 skips it")
{
    const auto model = core::Model::fromFile(example("epanet/mirab-demo-leakage.inp"));
    const auto* epa = registry().descriptor("epanet-2.2-epa");
    const auto report = engine::checkCompatibility(model, *epa);
    CHECK(report.runnable());
    CHECK_EQ(report.count(engine::Support::Ignored), 1);
    CHECK(engine::prepareInput(model, *epa).find("[LEAKAGE]") == std::string::npos);

    const auto owa = run("epanet-2.3-owa", example("epanet/mirab-demo-leakage.inp"));
    double total = 0.0;
    const auto* nodes = owa.table("node");
    const int v = nodes->variableIndex("Leakage");
    CHECK(v >= 0);
    for (int e = 0; e < nodes->elementCount(); ++e)
        total += nodes->value(8, v, e);
    CHECK(total > 0.1);
    const auto old = run("epanet-2.2-epa", example("epanet/mirab-demo-leakage.inp"));
    CHECK(old.table("node")->variableIndex("Leakage") < 0);
}

TEST_CASE("engines: EPANET-MSX runs on both EPANET bases")
{
    for (const char* id : {"epanet-msx-2.0-epa22", "epanet-msx-2.0-owa23"})
    {
        const auto r = run(id, example("epanet/mirab-demo.inp"), example("msx/mirab-demo.msx"));
        CHECK_NEAR(value(r, "node", "CL2", "R1", 0), 1.2, 1e-6);
        const double cl = value(r, "node", "CL2", "J9", 20);
        CHECK(cl > 0.5 && cl < 1.2);
        CHECK(value(r, "node", "TOC", "J9", 20) < 1.5);
        CHECK(value(r, "link", "CL2", "P1", 20) > 0.5);
    }
}

TEST_CASE("engines: EPA SWMM 5.2 routes the design storm")
{
    const auto r = run("swmm-5.2-epa", example("swmm/mirab-urban.inp"));
    CHECK_EQ(r.periodCount(), 72);
    double peak = 0.0;
    const auto* links = r.table("link");
    const int v = links->variableIndex("Flow");
    const int e = links->elementIndex("C5");
    for (int p = 0; p < r.periodCount(); ++p)
        peak = std::max(peak, static_cast<double>(links->value(p, v, e)));
    CHECK(peak > 0.05);
    CHECK(r.table("subcatchment") != nullptr);
    CHECK_NEAR(std::stod(r.metadata.at("flowContinuityErrorPct")), 0.0, 5.0);
}

TEST_CASE("engines: EPA SWMM 5.2 runs pollutants, dry weather flow, groundwater and RDII")
{
    const auto r = run("swmm-5.2-epa", example("swmm/mirab-quality.inp"));
    CHECK_EQ(r.periodCount(), 144);
    const auto* nodes = r.table("node");
    CHECK(nodes && nodes->variableIndex("TSS") >= 0 && nodes->variableIndex("Lead") >= 0);
    double tss = 0.0;
    for (int p = 0; p < r.periodCount(); ++p)
        tss = std::max(tss, value(r, "node", "TSS", "O1", p));
    CHECK(tss > 1.0);
    // Dry weather flow reaches J1 before the storm starts.
    CHECK(value(r, "node", "Lateral Inflow", "J1", 0) > 0.0);
    double gw = 0.0;
    for (int p = 0; p < r.periodCount(); ++p)
        gw = std::max(gw, value(r, "subcatchment", "Groundwater Flow", "S1", p));
    CHECK(gw > 0.0);
}

TEST_CASE("resilience: Todini, topology, criticality and fire flow")
{
    const auto model = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    const auto results = run("epanet-2.3-owa", example("epanet/mirab-demo.inp"));
    const auto todini = resilience::todiniIndex(model, results, 20.0);
    CHECK_EQ(todini.index.size(), std::size_t(25));
    CHECK(todini.minimum > 0.0 && todini.minimum < 1.0);

    const auto topo = resilience::topology(model);
    CHECK_EQ(topo.components, 1);
    CHECK(std::find(topo.bridgeLinks.begin(), topo.bridgeLinks.end(), "P13") != topo.bridgeLinks.end());
    CHECK(std::find(topo.bridgeLinks.begin(), topo.bridgeLinks.end(), "PU1") != topo.bridgeLinks.end());
    CHECK(std::find(topo.articulationNodes.begin(), topo.articulationNodes.end(), "J1") !=
          topo.articulationNodes.end());

    engine::EpanetLibrary lib;
    std::string error;
    CHECK(lib.load(*registry().locate(*registry().descriptor("epanet-2.3-owa")), &error));
    resilience::CriticalityOptions co;
    co.pressureThreshold = 20.0;
    const auto crit = resilience::pipeCriticality(lib.api(), example("epanet/mirab-demo.inp"), co);
    CHECK_EQ(crit.size(), std::size_t(13));

    resilience::FireFlowOptions fo;
    fo.fireFlow = 30.0;
    fo.residualPressure = 14.0;
    fo.nodes = {"J5"};
    const auto ff = resilience::fireFlow(lib.api(), example("epanet/mirab-demo.inp"), fo);
    CHECK_EQ(ff.size(), std::size_t(1));
    CHECK(ff[0].adequate);
    CHECK(ff[0].availableFlow > 30.0);
}

TEST_CASE("resilience: water service availability from a pressure-driven run")
{
    const auto r = run("epanet-2.3-owa", example("epanet/mirab-demo-leakage.inp"));
    const auto s = resilience::waterServiceAvailability(r);
    CHECK(s.note.empty());
    CHECK(s.overall > 0.9 && s.overall <= 1.0);
}

TEST_CASE("interop: WNTR JSON round trip reproduces the hydraulics")
{
    const auto model = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    const auto json = interop::toWntrJson(model);
    CHECK_EQ(json.get("nodes").size(), std::size_t(11));
    CHECK_EQ(json.get("links").size(), std::size_t(14));
    const auto back = interop::fromWntrJson(json::Value::parse(json.dump()));
    const auto file = scratch("wntr-roundtrip.inp");
    back.save(file);
    const auto a = run("epanet-2.3-owa", example("epanet/mirab-demo.inp"));
    const auto b = run("epanet-2.3-owa", file);
    for (int p : {0, 7, 19})
        CHECK_NEAR(value(a, "node", "Pressure", "J8", p), value(b, "node", "Pressure", "J8", p), 1e-3);
}

TEST_CASE("interop: GeoJSON and GeoPackage export")
{
    const auto model = core::Model::fromFile(example("swmm/mirab-urban.inp"));
    interop::GisOptions g;
    g.crs = "EPSG:32614";
    const auto gj = interop::toGeoJson(model, g);
    CHECK_EQ(gj.get("features").size(), std::size_t(6 + 5 + 3 + 1));
    if (interop::geoPackageAvailable())
    {
        const auto file = scratch("urban.gpkg");
        interop::writeGeoPackage(model, file, g);
        std::ifstream in(file, std::ios::binary);
        char head[16] = {};
        in.read(head, 16);
        CHECK_EQ(std::string(head, 15), std::string("SQLite format 3"));
    }
}

TEST_CASE("engines: SWMM5+ manifest adapts SWMM 5.2 models")
{
    const auto model = core::Model::fromFile(example("swmm/mirab-urban.inp"));
    const auto* s5p = registry().descriptor("swmm5plus-1.0");
    CHECK(s5p != nullptr);
    const auto report = engine::checkCompatibility(model, *s5p);
    CHECK(report.runnable());
    CHECK(report.count(engine::Support::Ignored) >= 1);
    std::vector<std::string> notes;
    const auto text = engine::prepareInput(model, *s5p, &notes);
    const auto prepared = core::Model::fromText(text);
    CHECK_EQ(text::splitInpLine(prepared.option("INERTIAL_DAMPING")).tokens.at(0), std::string("NONE"));
    CHECK(!notes.empty());
    // EPA SWMM 5.2 keeps the option as written.
    const auto* swmm = registry().descriptor("swmm-5.2-epa");
    CHECK(engine::checkCompatibility(model, *swmm).issues.empty());
}

namespace
{
/// Values of one period, table by table (the order of a frame event).
std::vector<float> periodValues(const core::ResultSet& r, int slot)
{
    std::vector<float> out;
    for (const auto& t : r.tables)
    {
        const std::size_t block = t.ids.size() * t.variables.size();
        const auto first = t.data.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(slot) * block);
        out.insert(out.end(), first, first + static_cast<std::ptrdiff_t>(block));
    }
    return out;
}

core::ResultSet layoutOf(const core::ResultSet& r)
{
    core::ResultSet s;
    for (const auto& t : r.tables)
        s.defineTable(t.kind, t.ids, t.variables);
    return s;
}

struct Recorded
{
    int period = 0;
    double time = 0.0;
    int layout = 0;
    std::vector<float> values;
    core::ResultSet shape;
};

struct LiveRun
{
    engine::RunOutcome outcome;
    std::vector<Recorded> frames;
    std::vector<double> checkpoints;
};

/// Runs an engine with frame and checkpoint callbacks. @p stop, when set,
/// requests cancellation.
LiveRun runLive(const std::string& engineId, const fs::path& inp, double interval, const fs::path& msx = {},
                const std::function<bool(const LiveRun&)>& stop = {})
{
    std::string error;
    auto e = registry().create(engineId, &error);
    if (!e)
        throw std::runtime_error(engineId + ": " + error);
    const auto model = core::Model::fromFile(inp);
    const fs::path input = scratch("live-" + engineId + "-" + inp.filename().u8string());
    std::ofstream(input, std::ios::binary) << engine::prepareInput(model, e->descriptor());
    engine::RunRequest req;
    req.input = input;
    req.msxInput = msx;
    req.reportFile = scratch("live-" + engineId + ".rpt");
    req.workDir = scratch("live-" + engineId + "-work");
    fs::create_directories(req.workDir);
    LiveRun out;
    engine::RunCallbacks cb;
    cb.frameInterval = interval;
    cb.frame = [&](const engine::ResultFrame& f) {
        Recorded r;
        r.period = f.period;
        r.time = f.time;
        r.layout = f.layout;
        r.values = periodValues(*f.results, f.slot);
        r.shape = layoutOf(*f.results);
        CHECK_NEAR(f.results->times[static_cast<std::size_t>(f.slot)], f.time, 1e-9);
        out.frames.push_back(std::move(r));
    };
    cb.checkpoint = [&](double seconds) { out.checkpoints.push_back(seconds); };
    cb.cancelRequested = [&] { return stop && stop(out); };
    out.outcome = e->run(req, cb);
    return out;
}

bool sameValues(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (!(a[i] == b[i] || (std::isnan(a[i]) && std::isnan(b[i]))))
            return false;
    }
    return true;
}

/// The urban example with pollutants, land uses, LID controls and
/// kinematic wave routing.
std::string urbanWithQuality()
{
    std::ifstream in(example("swmm/mirab-urban.inp"), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string routing = "FLOW_ROUTING         DYNWAVE";
    text.replace(text.find(routing), routing.size(), "FLOW_ROUTING         KINWAVE");
    text += R"(
[POLLUTANTS]
;;Name           Units  Crain  Cgw    Crdii  Kdecay SnowOnly Co-Pollutant Co-Frac Cdwf Cinit
TSS              MG/L   0      0      0      0      NO       *            0       0    0
Lead             UG/L   0      0      0      0      NO       TSS          0.2     0    0

[LANDUSES]
;;Name           Sweeping Fraction Last
Residential      0        0        0

[COVERAGES]
S1               Residential      100
S2               Residential      100
S3               Residential      100

[LOADINGS]
S1               TSS              1.0

[BUILDUP]
Residential      TSS              POW        50     0.5    2      AREA
Residential      Lead             NONE       0      0      0      AREA

[WASHOFF]
Residential      TSS              EXP        0.1    1      0      0
Residential      Lead             EMC        0      0      0      0

[TREATMENT]
J5               TSS              R = 0.2

[INFLOWS]
;;Node           Constituent      Time Series      Type     Mfactor  Sfactor  Baseline Pattern
J1               FLOW             ""               FLOW     1.0      1.0      0.01
J1               TSS              ""               CONCEN   1.0      1.0      20

[DWF]
J2               FLOW             0.005
J2               TSS              15

[LID_CONTROLS]
RainGarden       RG
RainGarden       SURFACE          150        0.0        0.1        1.0        5
RainGarden       SOIL             450        0.5        0.2        0.1        50         10.0       3.5
RainGarden       STORAGE          0          0.75       0.5        0
RainGarden       REMOVALS         TSS        50

[LID_USAGE]
S1               RainGarden       1          500        0          0          25         0
)";
    return text;
}
} // namespace

TEST_CASE("engines: EPANET frames follow every period and end on the final results")
{
    for (const char* id : {"epanet-2.2-epa", "epanet-2.3-owa"})
    {
        const auto live = runLive(id, example("epanet/mirab-demo.inp"), 0.0);
        CHECK(live.outcome.success);
        const auto& r = live.outcome.results;
        CHECK_EQ(live.frames.size(), static_cast<std::size_t>(r.periodCount()));
        for (std::size_t i = 0; i < live.frames.size(); ++i)
        {
            const auto& f = live.frames[i];
            CHECK_EQ(f.period, static_cast<int>(i));
            CHECK_NEAR(f.time, r.times[i], 1e-9);
            CHECK(core::live::sameLayout(f.shape, r));
            CHECK(sameValues(f.values, periodValues(r, f.period)));
        }
        CHECK(!live.checkpoints.empty());
        CHECK(std::is_sorted(live.checkpoints.begin(), live.checkpoints.end()));
    }
}

TEST_CASE("engines: frames are paced and the final state is always delivered")
{
    const auto live = runLive("epanet-2.3-owa", example("epanet/mirab-demo.inp"), 3600.0);
    CHECK(live.outcome.success);
    const auto& r = live.outcome.results;
    CHECK_EQ(live.frames.size(), static_cast<std::size_t>(2));
    CHECK_EQ(live.frames.front().period, 0);
    CHECK_EQ(live.frames.back().period, r.periodCount() - 1);
    CHECK(sameValues(live.frames.back().values, periodValues(r, r.periodCount() - 1)));
}

TEST_CASE("engines: a cancelled run delivers its last computed period")
{
    const auto afterFrames = [](std::size_t n) { return [n](const LiveRun& r) { return r.frames.size() >= n; }; };
    // Every period is sent: cancellation after the third frame ends there.
    const auto a = runLive("epanet-2.3-owa", example("epanet/mirab-demo.inp"), 0.0, {}, afterFrames(3));
    CHECK(a.outcome.cancelled);
    CHECK(!a.outcome.success);
    CHECK_EQ(a.frames.size(), static_cast<std::size_t>(3));
    CHECK_EQ(a.frames.back().period, a.outcome.results.periodCount() - 1);
    // Paced frames: the period reached when the run stops is still sent.
    const auto b = runLive("epanet-2.3-owa", example("epanet/mirab-demo.inp"), 3600.0, {}, [](const LiveRun& r) {
        return !r.checkpoints.empty() && r.checkpoints.back() >= 6 * 3600.0;
    });
    CHECK(b.outcome.cancelled);
    CHECK_EQ(b.frames.size(), static_cast<std::size_t>(2));
    CHECK_EQ(b.frames.back().period, b.outcome.results.periodCount() - 1);
    CHECK_EQ(b.frames.back().period, 6);
    CHECK(sameValues(b.frames.back().values, periodValues(b.outcome.results, b.frames.back().period)));
    const auto c = runLive("swmm-5.2-epa", example("swmm/mirab-urban.inp"), 0.0, {}, afterFrames(5));
    CHECK(c.outcome.cancelled);
    CHECK_EQ(c.frames.size(), static_cast<std::size_t>(5));
    CHECK_EQ(c.frames.back().period, 4);
}

TEST_CASE("engines: EPANET-MSX frames switch to the species layout")
{
    const auto live = runLive("epanet-msx-2.0-owa23", example("epanet/mirab-demo.inp"), 0.0,
                              example("msx/mirab-demo.msx"));
    CHECK(live.outcome.success);
    const auto& r = live.outcome.results;
    const auto n = static_cast<std::size_t>(r.periodCount());
    CHECK_EQ(live.frames.size(), 2 * n);
    CHECK_EQ(live.frames.front().layout, 1);
    CHECK(!core::live::sameLayout(live.frames.front().shape, r));
    for (std::size_t i = n; i < 2 * n; ++i)
    {
        CHECK_EQ(live.frames[i].layout, 2);
        CHECK_EQ(live.frames[i].period, static_cast<int>(i - n));
        CHECK(core::live::sameLayout(live.frames[i].shape, r));
    }
    CHECK(sameValues(live.frames.back().values, periodValues(r, r.periodCount() - 1)));
}

TEST_CASE("engines: EPA SWMM 5.2 frames match the binary output")
{
    const auto live = runLive("swmm-5.2-epa", example("swmm/mirab-urban.inp"), 0.0);
    CHECK(live.outcome.success);
    const auto& r = live.outcome.results;
    // Every period live, then the last one again from the output file.
    CHECK_EQ(live.frames.size(), static_cast<std::size_t>(r.periodCount() + 1));
    const auto* nodes = r.table("node");
    const auto* links = r.table("link");
    const int depth = nodes->variableIndex("Depth");
    const int flow = links->variableIndex("Flow");
    const int volume = links->variableIndex("Volume");
    float maxDepth = 0, maxFlow = 0;
    for (int p = 0; p < r.periodCount(); ++p)
    {
        for (int e = 0; e < nodes->elementCount(); ++e)
            maxDepth = std::max(maxDepth, nodes->value(p, depth, e));
        for (int e = 0; e < links->elementCount(); ++e)
            maxFlow = std::max(maxFlow, std::fabs(links->value(p, flow, e)));
    }
    for (std::size_t i = 0; i < live.frames.size(); ++i)
    {
        const auto& f = live.frames[i];
        const std::size_t p = std::min(i, r.times.size() - 1);
        CHECK_EQ(f.period, static_cast<int>(p));
        CHECK_NEAR(f.time, r.times[p], 1e-6);
        CHECK(core::live::sameLayout(f.shape, r));
        if (i + 1 == live.frames.size())
            break;
        // The live state is taken at the routing step that reaches the
        // reporting time; the output file interpolates to it.
        core::ResultSet one = f.shape;
        CHECK_EQ(core::live::appendFrame(one, {f.layout, f.period, f.time, f.values}), 0);
        for (int e = 0; e < nodes->elementCount(); ++e)
            CHECK_NEAR(one.table("node")->value(0, depth, e), nodes->value(f.period, depth, e), 0.05 * maxDepth);
        for (int e = 0; e < links->elementCount(); ++e)
        {
            CHECK_NEAR(one.table("link")->value(0, flow, e), links->value(f.period, flow, e), 0.05 * maxFlow);
            CHECK(std::isnan(one.table("link")->value(0, volume, e)));
        }
    }
    CHECK(sameValues(live.frames.back().values, periodValues(r, r.periodCount() - 1)));
}

TEST_CASE("engines: SWMM5+ input preparation removes every ignored feature")
{
    const std::string original = urbanWithQuality();
    const fs::path withQuality = scratch("urban-quality.inp");
    std::ofstream(withQuality, std::ios::binary) << original;
    const auto model = core::Model::fromText(original);

    // EPA SWMM 5.2 runs the full model, and its live layout names the pollutants.
    const auto full = runLive("swmm-5.2-epa", withQuality, 0.0);
    CHECK(full.outcome.success);
    CHECK(full.outcome.results.table("node")->variableIndex("Lead") >= 0);
    CHECK(core::live::sameLayout(full.frames.front().shape, full.outcome.results));

    const auto* s5p = registry().descriptor("swmm5plus-1.0");
    std::vector<std::string> notes;
    const std::string text = engine::prepareInput(model, *s5p, &notes);
    const auto prepared = core::Model::fromText(text);
    const auto& doc = prepared.document();
    for (const char* s : {"POLLUTANTS", "LANDUSES", "COVERAGES", "LOADINGS", "BUILDUP", "WASHOFF", "TREATMENT",
                          "LID_CONTROLS", "LID_USAGE"})
        CHECK(!doc.has(s));
    CHECK_EQ(doc.find("INFLOWS")->rowCount(), 1);
    CHECK_EQ(doc.find("INFLOWS")->rowTokens(0).at(1), std::string("FLOW"));
    CHECK_EQ(doc.find("DWF")->rowCount(), 1);
    CHECK_EQ(text::splitInpLine(prepared.option("FLOW_ROUTING")).tokens.at(0), std::string("DYNWAVE"));
    CHECK_EQ(text::splitInpLine(prepared.option("INERTIAL_DAMPING")).tokens.at(0), std::string("NONE"));
    std::string all;
    for (const auto& n : notes)
        all += n + "\n";
    CHECK(all.find("[POLLUTANTS]") != std::string::npos);
    CHECK(all.find("[INFLOWS]") != std::string::npos);
    CHECK(all.find("[LID_USAGE]") != std::string::npos);
    CHECK(all.find("DYNWAVE") != std::string::npos);
    // Nothing the engine ignores is left for it to see.
    for (const auto& use : core::detectFeatures(prepared))
        CHECK(s5p->supportFor(use.feature) != engine::Support::Ignored);

    // The prepared input is still a valid SWMM model.
    const fs::path stripped = scratch("urban-stripped.inp");
    std::ofstream(stripped, std::ios::binary) << text;
    const auto r = run("swmm-5.2-epa", stripped);
    CHECK_EQ(r.periodCount(), 72);
    CHECK(r.table("node")->variableIndex("TSS") < 0);

    // When LIDs stay (SWMM 5.2 support), only the pollutant rows go with the pollutants.
    engine::EngineDescriptor pollutantsOnly = *registry().descriptor("swmm-5.2-epa");
    pollutantsOnly.support = {{core::Feature::SwmmWaterQuality, engine::Support::Ignored}};
    const auto partial = core::Model::fromText(engine::prepareInput(model, pollutantsOnly));
    CHECK(partial.document().has("LID_USAGE"));
    CHECK_EQ(partial.document().find("LID_CONTROLS")->rowCount(), 4);
}

TEST_CASE("engines: packages install only when every checksum matches")
{
    const auto src = scratch("pkg-src");
    const auto dst = scratch("pkg-dst");
    fs::remove_all(src);
    fs::remove_all(dst);
    fs::create_directories(src);
    std::ofstream(src / "asset-a.bin", std::ios::binary) << "first library";
    std::ofstream(src / "asset-b.bin", std::ios::binary) << "second library";
    auto manifestText = [&](const std::string& platform, const std::string& shaB, const std::string& nameB) {
        return std::string("{\"format\":\"mirab-engine/1\",\"engine\":\"swmm5plus-1.0\",\"name\":\"SWMM5+\",") +
               "\"version\":\"1.0.3\",\"packageVersion\":\"0.1.0\",\"apiVersion\":1,\"platform\":\"" + platform +
               "\",\"files\":[{\"name\":\"a.so\",\"asset\":\"asset-a.bin\",\"size\":13,\"sha256\":\"" +
               core::sha256Hex("first library") + "\"},{\"name\":\"" + nameB +
               "\",\"asset\":\"asset-b.bin\",\"size\":14,\"sha256\":\"" + shaB + "\"}]}";
    };
    const std::string goodB = core::sha256Hex("second library");

    // Correct package installs both files and a record, and uninstalls cleanly.
    const auto good = engine::EngineManifest::parse(manifestText(engine::platformId(), goodB, "b.so"));
    engine::installFromManifest(good, src, dst);
    CHECK(fs::exists(dst / "a.so"));
    CHECK(fs::exists(dst / "b.so"));
    CHECK(fs::exists(dst / "swmm5plus-1.0.installed.json"));
    CHECK(engine::uninstallEngine("swmm5plus-1.0", dst));
    CHECK(!fs::exists(dst / "a.so"));

    // A tampered file aborts before anything is copied.
    const auto bad = engine::EngineManifest::parse(manifestText(engine::platformId(), std::string(64, 'f'), "b.so"));
    bool threw = false;
    try
    {
        engine::installFromManifest(bad, src, dst);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    CHECK(threw);
    CHECK(!fs::exists(dst / "a.so"));

    // Packages for another platform are refused.
    threw = false;
    try
    {
        engine::installFromManifest(engine::EngineManifest::parse(manifestText("plan9-mips", goodB, "b.so")), src, dst);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    CHECK(threw);

    // File names that try to leave the engine folder are rejected.
    threw = false;
    try
    {
        engine::EngineManifest::parse(manifestText(engine::platformId(), goodB, "../evil.so"));
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    CHECK(threw);
}
