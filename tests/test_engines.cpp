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
#include <map>

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
    // The build's engine folder, then the folders of MIRAB_ENGINE_PATH, where
    // a separately built SWMM5+ library can be found.
    static engine::EngineRegistry r([] {
        std::vector<fs::path> dirs{fs::u8path(MIRAB_ENGINE_DIR)};
#if defined(_WIN32)
        const char sep = ';';
#else
        const char sep = ':';
#endif
        if (const char* env = std::getenv("MIRAB_ENGINE_PATH"))
        {
            std::string dir;
            for (const char* p = env;; ++p)
            {
                if (*p == sep || *p == '\0')
                {
                    if (!dir.empty())
                        dirs.push_back(fs::u8path(dir));
                    dir.clear();
                    if (*p == '\0')
                        break;
                }
                else
                {
                    dir += *p;
                }
            }
        }
        return dirs;
    }());
    return r;
}

core::ResultSet run(const std::string& engineId, const fs::path& inp, const fs::path& msx = {},
                    const engine::RunCallbacks& callbacks = {})
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
    auto outcome = e->run(req, callbacks);
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

TEST_CASE("engines: SWMM5+ gets inflows at dead-end junctions and refuses terminal ones")
{
    // J1 and J4 start a branch and receive only runoff. SWMM5+ turns a
    // dead-end junction into an upstream boundary only when it has an
    // inflow, and stops the program otherwise.
    const auto model = core::Model::fromFile(example("swmm/mirab-urban.inp"));
    const auto ends = core::deadEndJunctions(model);
    CHECK(ends.withoutInflow == std::vector<std::string>({"J1", "J4"}));
    CHECK(ends.terminal.empty());

    const auto* s5p = registry().descriptor("swmm5plus-1.0");
    std::vector<std::string> notes;
    const auto prepared = core::Model::fromText(engine::prepareInput(model, *s5p, &notes));
    const auto* inflows = prepared.document().find("INFLOWS");
    CHECK(inflows != nullptr);
    CHECK_EQ(inflows->rowCount(), 2);
    for (int r = 0; r < inflows->rowCount(); ++r)
    {
        const auto t = inflows->rowTokens(r);
        CHECK_EQ(t.at(1), std::string("FLOW"));
        CHECK_EQ(std::stod(t.at(6)), 0.0);
    }
    CHECK(core::deadEndJunctions(prepared).withoutInflow.empty());
    std::string all;
    for (const auto& n : notes)
        all += n + "\n";
    CHECK(all.find("J1, J4") != std::string::npos);

    // A conduit that ends at a junction: SWMM5+ cannot run it, SWMM 5.2 can.
    std::string text = model.toText();
    const auto insertAfter = [&](const std::string& header, const std::string& row) {
        const auto at = text.find('\n', text.find(header)) + 1;
        text.insert(at, row + "\n");
    };
    insertAfter("[JUNCTIONS]", "J6 97.5 3 0 0 0");
    insertAfter("[CONDUITS]", "C6 J5 J6 80 0.013 0 0 0 0");
    insertAfter("[XSECTIONS]", "C6 CIRCULAR 0.3 0 0 0 1");
    const auto terminal = core::Model::fromText(text);
    CHECK(core::deadEndJunctions(terminal).terminal == std::vector<std::string>({"J6"}));
    const auto report = engine::checkCompatibility(terminal, *s5p);
    CHECK(!report.runnable());
    bool named = false;
    for (const auto& i : report.issues)
        named |= i.level == engine::Support::Unsupported && i.message.find("J6") != std::string::npos;
    CHECK(named);
    CHECK(engine::checkCompatibility(terminal, *registry().descriptor("swmm-5.2-epa")).runnable());

    // Two links ending at the junction crash SWMM5+ too ("unexpected null link").
    insertAfter("[CONDUITS]", "C7 J3 J6 90 0.013 0 0 0 0");
    insertAfter("[XSECTIONS]", "C7 CIRCULAR 0.3 0 0 0 1");
    const auto twoIn = core::Model::fromText(text);
    CHECK(core::deadEndJunctions(twoIn).terminal == std::vector<std::string>({"J6"}));
    CHECK(!engine::checkCompatibility(twoIn, *s5p).runnable());
}

TEST_CASE("engines: SWMM5+ gets inflow time series that reach the end of the simulation")
{
    // Inflow1 ends at 6:00 in a 12 h simulation; SWMM5+ stops when an inflow
    // has no data up to the end, EPA SWMM takes it as zero after its last point.
    const fs::path original = example("swmm/mirab-quality.inp");
    const auto model = core::Model::fromFile(original);
    const auto series = core::shortInflowSeries(model);
    CHECK_EQ(series.size(), std::size_t(1));
    CHECK_EQ(series.at(0).name, std::string("Inflow1"));
    CHECK_NEAR(series.at(0).endSeconds, 6 * 3600.0, 1e-9);
    // Its last value is already zero: one row an hour past the end is enough.
    CHECK(series.at(0).zeroRows == std::vector<std::vector<std::string>>({{"Inflow1", "13", "0"}}));

    const auto* s5p = registry().descriptor("swmm5plus-1.0");
    CHECK(core::shortInflowSeries(core::Model::fromText(engine::prepareInput(model, *s5p))).empty());

    // With only this change, the same model as EPA SWMM reads it: identical results.
    engine::EngineDescriptor seriesOnly = *registry().descriptor("swmm-5.2-epa");
    seriesOnly.support = {{core::Feature::SwmmShortInflowSeries, engine::Support::Ignored}};
    const std::string text = engine::prepareInput(model, seriesOnly);
    const fs::path prepared = scratch("quality-continued.inp");
    std::ofstream(prepared, std::ios::binary) << text;
    const auto a = run("swmm-5.2-epa", original);
    const auto b = run("swmm-5.2-epa", prepared);
    CHECK_EQ(a.periodCount(), b.periodCount());
    const auto* la = a.table("link");
    const auto* lb = b.table("link");
    const int flow = la->variableIndex("Flow");
    for (const char* id : {"C1", "C2", "C3", "C4", "C5"})
    {
        for (int p = 0; p < a.periodCount(); ++p)
            CHECK_NEAR(la->value(p, flow, la->elementIndex(id)), lb->value(p, flow, lb->elementIndex(id)), 1e-6);
    }

    // A dated series that ends on a non-zero value drops to zero a second
    // later, and keeps its date format; SWMM also writes dates with '-'.
    // A series that only a pollutant uses is left alone.
    std::string dated = model.toText();
    const auto at = dated.find("Inflow1          0:00");
    dated.insert(at, "Dated1 01/01/2026 00:00 0.02\nDated1 01-01-2026 04:30 0.03\nTss1 0:00 10\nTss1 1:00 20\n");
    const auto inflow = dated.find("J2               TSS");
    dated.insert(inflow, "J3               FLOW             Dated1           FLOW     1.0      1.0      0\n"
                         "J3               TSS              Tss1             CONCEN   1.0      1.0      0\n");
    bool found = false;
    for (const auto& s : core::shortInflowSeries(core::Model::fromText(dated)))
    {
        CHECK(s.name != "Tss1");
        if (s.name != "Dated1")
            continue;
        found = true;
        CHECK_NEAR(s.endSeconds, 4.5 * 3600.0, 1e-9);
        CHECK(s.zeroRows == std::vector<std::vector<std::string>>(
                                {{"Dated1", "01/01/2026", "04:30:01", "0"}, {"Dated1", "01/01/2026", "13:00:00", "0"}}));
    }
    CHECK(found);
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

TEST_CASE("engines: SWMM5+ frames equal the final results, and runoff agrees with SWMM 5.2")
{
    // SWMM5+ is installed separately; a locally built library is found
    // through MIRAB_ENGINE_PATH. It runs one model per process, so all checks
    // use this one run.
    std::string why;
    if (!registry().isAvailable("swmm5plus-1.0", &why))
        return;
    const fs::path inp = fs::path(MIRAB_SOURCE_DIR) / "tests" / "data" / "swmm5plus-catchments.inp";
    const auto live = runLive("swmm5plus-1.0", inp, 0.0);
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
    CHECK(live.checkpoints.size() > live.frames.size());
    CHECK(std::is_sorted(live.checkpoints.begin(), live.checkpoints.end()));
    const auto* subs = r.table("subcatchment");
    if (!subs)
        return; // a version 1 library: nodes and links only

    // Interface version 2: every level SWMM5+ stores (0 to 2 h every 5
    // minutes), in the units of the input file, with subcatchments and
    // system totals named like the SWMM 5.2 results.
    CHECK_EQ(r.periodCount(), 25);
    CHECK_EQ(r.flowUnits, std::string("CFS"));
    CHECK_NEAR(r.times.back(), 7200.0, 1e-9);
    const auto* system = r.table("system");
    CHECK(system != nullptr);
    const auto epa = run("swmm-5.2-epa", inp);
    const auto* epaSubs = epa.table("subcatchment");
    const int rain = subs->variableIndex("Rainfall"), epaRain = epaSubs->variableIndex("Rainfall");
    const int runoff = subs->variableIndex("Runoff"), epaRunoff = epaSubs->variableIndex("Runoff");
    CHECK(rain >= 0 && runoff >= 0 && subs->variableIndex("Infiltration Loss") >= 0);
    for (const char* id : {"S1", "S2", "S3"})
    {
        const int e = subs->elementIndex(id), g = epaSubs->elementIndex(id);
        CHECK(e >= 0 && g >= 0);
        double peak = 0.0, epaPeak = 0.0;
        for (int p = 0; p < r.periodCount(); ++p)
            peak = std::max(peak, static_cast<double>(subs->value(p, runoff, e)));
        for (int k = 0; k < epa.periodCount(); ++k)
            epaPeak = std::max(epaPeak, static_cast<double>(epaSubs->value(k, epaRunoff, g)));
        CHECK(epaPeak > 1.0);
        CHECK_NEAR(peak, epaPeak, 0.05 * epaPeak);
        // Same reporting times: SWMM5+ starts at 0, SWMM 5.2 one step later.
        for (int k = 0; k < epa.periodCount(); ++k)
        {
            const double t = epa.times[static_cast<std::size_t>(k)];
            const int p = static_cast<int>(std::lround(t / r.reportStep));
            if (p >= r.periodCount())
                break;
            CHECK_NEAR(r.times[static_cast<std::size_t>(p)], t, 1e-6);
            CHECK_NEAR(subs->value(p, rain, e), epaSubs->value(k, epaRain, g), 1e-4);
            // Both run the EPA SWMM hydrology (5.1.13 inside SWMM5+).
            CHECK_NEAR(subs->value(p, runoff, e), epaSubs->value(k, epaRunoff, g), 0.05 * epaPeak);
        }
    }
    // System runoff is the sum of the subcatchments, and the outfall carries
    // a peak like SWMM 5.2's.
    for (int p = 0; p < r.periodCount(); ++p)
    {
        double sum = 0.0;
        for (int e = 0; e < subs->elementCount(); ++e)
            sum += subs->value(p, runoff, e);
        CHECK_NEAR(system->value(p, system->variableIndex("Runoff"), 0), sum, 1e-3 * (1.0 + sum));
    }
    const auto* nodes = r.table("node");
    const auto* epaNodes = epa.table("node");
    const int inflow = nodes->variableIndex("Total Inflow");
    const int epaInflow = epaNodes->variableIndex("Total Inflow");
    const int o1 = nodes->elementIndex("O1"), epaO1 = epaNodes->elementIndex("O1");
    double outfall = 0.0, epaOutfall = 0.0;
    for (int p = 0; p < r.periodCount(); ++p)
        outfall = std::max(outfall, static_cast<double>(nodes->value(p, inflow, o1)));
    for (int k = 0; k < epa.periodCount(); ++k)
        epaOutfall = std::max(epaOutfall, static_cast<double>(epaNodes->value(k, epaInflow, epaO1)));
    CHECK(epaOutfall > 5.0);
    CHECK_NEAR(outfall, epaOutfall, 0.25 * epaOutfall);
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
    // J1's flow inflow stays; the dead-end J4 has none and gets a zero one.
    CHECK_EQ(doc.find("INFLOWS")->rowCount(), 2);
    CHECK_EQ(doc.find("INFLOWS")->rowTokens(0).at(1), std::string("FLOW"));
    CHECK_EQ(doc.find("INFLOWS")->rowTokens(1).at(0), std::string("J4"));
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

    // So is an engine id that is a path: it names the install record.
    for (const char* id : {"../../escaped", "/tmp/escaped", "C:\\escaped", "..", ""})
    {
        std::string text = manifestText(engine::platformId(), goodB, "b.so");
        text.replace(text.find("swmm5plus-1.0"), 13, id);
        threw = false;
        try
        {
            engine::EngineManifest::parse(text);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        CHECK(threw);
    }
    CHECK_EQ(engine::EngineManifest::parse(manifestText(engine::platformId(), goodB, "b.so")).engine,
             std::string("swmm5plus-1.0"));
}

TEST_CASE("engines: EPA SWMM 5.2 periods keep their times when reporting starts late")
{
    // Reporting starts six hours into the run. The binary output then holds
    // the date one report step before the first period in its header, not
    // the simulation start, so the periods are placed from the model.
    const auto file = scratch("report-start.inp");
    std::ofstream(file) << "[TITLE]\nReport start\n\n[OPTIONS]\nFLOW_UNITS CFS\nINFILTRATION HORTON\n"
                           "FLOW_ROUTING DYNWAVE\nSTART_DATE 01/01/2026\nSTART_TIME 00:00:00\n"
                           "REPORT_START_DATE 01/01/2026\nREPORT_START_TIME 06:00:00\nEND_DATE 01/01/2026\n"
                           "END_TIME 12:00:00\nREPORT_STEP 01:00:00\nWET_STEP 00:05:00\nDRY_STEP 01:00:00\n"
                           "ROUTING_STEP 0:00:05\n\n[JUNCTIONS]\nJ1 100 10 0 0 0\nJ2 99 10 0 0 0\n\n"
                           "[OUTFALLS]\nO1 98 FREE\n\n[CONDUITS]\nC1 J1 J2 400 0.013 0 0 0 0\n"
                           "C2 J2 O1 400 0.013 0 0 0 0\n\n[XSECTIONS]\nC1 CIRCULAR 1.5 0 0 0 1\n"
                           "C2 CIRCULAR 1.5 0 0 0 1\n\n[INFLOWS]\nJ1 FLOW TS1 FLOW 1.0 1.0\n\n[REPORT]\n"
                           "NODES ALL\nLINKS ALL\n\n[COORDINATES]\nJ1 0 0\nJ2 100 0\nO1 200 0\n\n"
                           "[TIMESERIES]\nTS1 0:00 1.0\nTS1 7:59 1.0\nTS1 8:00 5.0\nTS1 12:00 5.0\n";
    const auto r = run("swmm-5.2-epa", file);
    CHECK_EQ(r.periodCount(), 7);
    CHECK_NEAR(r.reportStart, 21600.0, 1e-9);
    CHECK_NEAR(r.times.front(), 21600.0, 1e-9);
    CHECK_NEAR(r.times.back(), 43200.0, 1e-9);
    CHECK_NEAR(r.reportStep, 3600.0, 1e-9);
    // The inflow steps from 1 to 5 cfs at 08:00, the third period.
    CHECK(value(r, "node", "Total Inflow", "J1", 1) < 1.5);
    CHECK(value(r, "node", "Total Inflow", "J1", 2) > 4.0);
}

TEST_CASE("engines: EPA 2.2 labels pressure as it reports it, OWA 2.3 as the PRESSURE option says")
{
    auto net = [&](const char* name, const char* units, const char* pressure) {
        const auto file = scratch(name);
        std::ofstream(file) << "[JUNCTIONS]\n J1 0 1\n\n[RESERVOIRS]\n R1 50\n\n[PIPES]\n"
                               " P1 R1 J1 100 150 100 0 Open\n\n[OPTIONS]\n Units "
                            << units << "\n Pressure " << pressure
                            << "\n Headloss H-W\n\n[TIMES]\n Duration 0\n\n[COORDINATES]\n J1 0 0\n R1 -100 0\n";
        return file;
    };
    auto unit = [](const core::ResultSet& r) {
        const auto* t = r.table("node");
        return t->variables[static_cast<std::size_t>(t->variableIndex("Pressure"))].unit;
    };
    // EPA 2.2 reports psi with US units and never psi with SI units, whatever
    // the option says; OWA 2.3 follows the option.
    const auto us = net("pressure-us.inp", "GPM", "METERS");
    const auto si = net("pressure-si.inp", "LPS", "PSI");
    const auto us22 = run("epanet-2.2-epa", us);
    const auto us23 = run("epanet-2.3-owa", us);
    const auto si22 = run("epanet-2.2-epa", si);
    const auto si23 = run("epanet-2.3-owa", si);
    CHECK_EQ(unit(us22), std::string("psi"));
    CHECK_EQ(unit(us23), std::string("m"));
    CHECK_EQ(unit(si22), std::string("m"));
    CHECK_EQ(unit(si23), std::string("psi"));
    // The labels fit the numbers: one metre of head is 1.422 psi.
    CHECK_NEAR(value(us22, "node", "Pressure", "J1", 0), 1.4220 * value(us23, "node", "Pressure", "J1", 0), 0.05);
    CHECK_NEAR(value(si23, "node", "Pressure", "J1", 0), 1.4220 * value(si22, "node", "Pressure", "J1", 0), 0.05);
}

TEST_CASE("engines: pipe headloss from a run is per 1000 length units, as in the binary output")
{
    const auto model = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    const auto r = run("epanet-2.3-owa", example("epanet/mirab-demo.inp"));
    const auto* pipes = model.document().find("PIPES");
    for (const char* id : {"P1", "P2", "P13"})
    {
        const auto* link = model.findLink(id);
        CHECK(link != nullptr);
        if (!link)
            continue;
        const double length = text::toDouble(pipes->rowTokens(link->row)[3]).value_or(0.0);
        CHECK(length > 0.0);
        for (int p : {0, 7, 19})
        {
            const double drop = std::fabs(value(r, "node", "Head", link->from.c_str(), p) -
                                          value(r, "node", "Head", link->to.c_str(), p));
            CHECK_NEAR(value(r, "link", "Headloss", id, p), 1000.0 * drop / length, 0.01 + 0.01 * drop);
        }
    }
    // A pump keeps its head gain (a negative loss).
    CHECK_NEAR(value(r, "link", "Headloss", "PU1", 0),
               value(r, "node", "Head", "R1", 0) - value(r, "node", "Head", "J1", 0), 0.01);
}

TEST_CASE("results: an EPANET binary output reads like the run")
{
    // A closed pipe: the file holds EPANET's status codes (2 for closed, 3
    // for open), the run records 0 and 1.
    const auto file = scratch("binary-status.inp");
    {
        std::ifstream in(example("epanet/mirab-demo.inp"), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        text.replace(text.find("[STATUS]"), 8, "[STATUS]\n P6 Closed");
        std::ofstream(file, std::ios::binary) << text;
    }
    engine::EpanetLibrary lib;
    std::string error;
    CHECK(lib.load(*registry().locate(*registry().descriptor("epanet-2.3-owa")), &error));
    const auto out = scratch("binary-status.out");
    {
        engine::EpanetSession s(lib.api());
        s.open(file, scratch("binary-status.rpt"), out);
        s.openH();
        s.initH(engine::en::SAVE);
        s.openQ();
        s.initQ(engine::en::SAVE);
        long tstep = 0;
        do
        {
            s.runH();
            s.runQ();
            tstep = s.nextH();
            s.nextQ();
        } while (tstep > 0);
        s.closeQ();
        s.closeH();
        s.close();
    }
    const auto fromFile = core::ResultSet::loadAny(out);
    const auto r = run("epanet-2.3-owa", file);
    CHECK_EQ(fromFile.periodCount(), r.periodCount());
    const auto* t = fromFile.table("link");
    CHECK_EQ(t->variables[static_cast<std::size_t>(t->variableIndex("Status"))].unit, std::string("0=closed 1=open"));
    for (const char* id : {"P6", "P1", "PU1"})
    {
        for (int p : {0, 12})
        {
            CHECK_NEAR(value(fromFile, "link", "Status", id, p), value(r, "link", "Status", id, p), 1e-6);
            CHECK_NEAR(value(fromFile, "link", "Flow", id, p), value(r, "link", "Flow", id, p), 1e-3);
            CHECK_NEAR(value(fromFile, "link", "Headloss", id, p), value(r, "link", "Headloss", id, p), 1e-3);
        }
    }
    CHECK_NEAR(value(fromFile, "link", "Status", "P6", 0), 0.0, 1e-9);
    CHECK_NEAR(value(fromFile, "link", "Status", "P1", 0), 1.0, 1e-9);
}

TEST_CASE("resilience: service availability counts the consumer demand delivered, not emitter flow")
{
    const auto file = scratch("wsa-emitter.inp");
    std::ofstream(file) << "[JUNCTIONS]\n J1 0 0\n J2 0 20\n\n[RESERVOIRS]\n R1 25\n\n[PIPES]\n"
                           " P1 R1 J1 500 150 100 0 Open\n P2 J1 J2 500 100 100 0 Open\n\n[EMITTERS]\n J2 3\n\n"
                           "[OPTIONS]\n Units LPS\n Headloss H-W\n Demand Model PDA\n Minimum Pressure 0\n"
                           " Required Pressure 30\n Pressure Exponent 0.5\n\n[TIMES]\n Duration 0\n\n"
                           "[COORDINATES]\n J1 0 0\n J2 100 0\n R1 -100 0\n";
    const auto r = run("epanet-2.3-owa", file);
    const auto s = resilience::waterServiceAvailability(r);
    const double full = value(r, "node", "Full Demand", "J2", 0);
    const double deficit = value(r, "node", "Demand Deficit", "J2", 0);
    const double demand = value(r, "node", "Demand", "J2", 0);
    CHECK(full > 19.9 && deficit > 1.0);
    // "Demand" also holds the emitter flow; the availability does not.
    CHECK(demand > full - deficit + 1.0);
    CHECK_NEAR(s.overall, (full - deficit) / full, 1e-4);
    CHECK(s.overall < 0.5);
}

TEST_CASE("resilience: fire flow at a time of day follows the start clock time and the pattern start")
{
    auto net = [&](const char* name, const char* times) {
        const auto file = scratch(name);
        std::ofstream(file) << "[JUNCTIONS]\n J1 0 10 PAT1\n J2 0 10 PAT1\n\n[RESERVOIRS]\n R1 40\n\n[PIPES]\n"
                               " P1 R1 J1 500 150 100 0 Open\n P2 J1 J2 500 100 100 0 Open\n\n[PATTERNS]\n"
                               " PAT1 0.5 1.0 1.5 2.0\n\n[TIMES]\n Duration 24:00\n Hydraulic Timestep 6:00\n"
                               " Pattern Timestep 6:00\n Report Timestep 6:00\n"
                            << times << "\n\n[OPTIONS]\n Units LPS\n Headloss H-W\n\n[COORDINATES]\n J1 0 0\n"
                               " J2 100 0\n R1 -100 0\n";
        return file;
    };
    engine::EpanetLibrary lib;
    std::string error;
    CHECK(lib.load(*registry().locate(*registry().descriptor("epanet-2.3-owa")), &error));
    auto snapshot = [&](const fs::path& file, double hour) {
        resilience::FireFlowOptions fo;
        fo.fireFlow = 1e-4;
        fo.residualPressure = 1.0;
        fo.timeOfDay = hour * 3600.0;
        fo.computeAvailableFlow = false;
        fo.nodes = {"J2"};
        const auto e = resilience::fireFlow(lib.api(), file, fo);
        return e.size() == 1 && !e[0].failed ? e[0].residualPressure : -1e9;
    };
    // The clock starts at 6 am: period 0 of a run is 6 am, period 2 is 6 pm
    // and period 3 midnight.
    const auto clock6 = net("fire-clock.inp", " Start ClockTime 6 am");
    const auto a = run("epanet-2.3-owa", clock6);
    CHECK(std::fabs(value(a, "node", "Pressure", "J2", 0) - value(a, "node", "Pressure", "J2", 3)) > 1.0);
    CHECK_NEAR(snapshot(clock6, 6.0), value(a, "node", "Pressure", "J2", 0), 1e-3);
    CHECK_NEAR(snapshot(clock6, 18.0), value(a, "node", "Pressure", "J2", 2), 1e-3);
    CHECK_NEAR(snapshot(clock6, 0.0), value(a, "node", "Pressure", "J2", 3), 1e-3);
    // The pattern starts six hours in: at midnight the snapshot sees the
    // second pattern step, as the run does at its start.
    const auto pat6 = net("fire-pattern.inp", " Pattern Start 6:00");
    const auto b = run("epanet-2.3-owa", pat6);
    CHECK_NEAR(snapshot(pat6, 0.0), value(b, "node", "Pressure", "J2", 0), 1e-3);
}

TEST_CASE("interop: WNTR import keeps statuses, options and demands without a pattern")
{
    const auto file = scratch("wntr-statuses.inp");
    std::ofstream(file) << "[TITLE]\nStatuses\n\n[JUNCTIONS]\n J1 100 3\n J2 100 8\n J3 95 5\n J4 90 2\n\n"
                           "[RESERVOIRS]\n R1 150\n R2 120\n\n[PIPES]\n P1 R1 J1 1000 300 100 0 Open\n"
                           " P2 J1 J2 1000 200 100 0 Open\n P3 J2 J3 1000 200 100 0 Closed\n"
                           " P4 J1 J3 1200 150 100 0 Open\n\n[PUMPS]\n PU1 R2 J2 HEAD C1\n\n[VALVES]\n"
                           " V1 J3 J4 150 TCV 0 0\n\n[DEMANDS]\n J1 3 PAT1 ;Domestic\n J1 1 ;Commercial\n\n"
                           "[EMITTERS]\n J3 2\n\n[STATUS]\n PU1 Closed\n V1 Open\n\n[PATTERNS]\n PAT1 1.0 2.0\n\n"
                           "[CURVES]\n C1 50 40\n\n[TIMES]\n Duration 1:00\n Hydraulic Timestep 1:00\n"
                           " Pattern Timestep 1:00\n Pattern Start 1:00\n Report Timestep 1:00\n"
                           " Start ClockTime 6 am\n\n[OPTIONS]\n Units LPS\n Headloss H-W\n Demand Multiplier 1.5\n"
                           " Emitter Exponent 0.7\n\n[COORDINATES]\n J1 0 0\n J2 100 0\n J3 100 100\n J4 200 100\n"
                           " R1 -100 0\n R2 100 -100\n";
    const auto model = core::Model::fromFile(file);
    auto json = interop::toWntrJson(model);
    // WNTR itself writes the statuses as "Closed", "Open" and "Active".
    json::Value& links = json["links"];
    bool valveOpen = false;
    for (std::size_t i = 0; i < links.size(); ++i)
    {
        const std::string st = links[i].get("initial_status").asString();
        links[i].set("initial_status", st == "CLOSED" ? "Closed" : st == "OPEN" ? "Open" : "Active");
        if (links[i].get("name").asString() == "V1")
            valveOpen = st == "OPEN";
    }
    CHECK(valveOpen);

    const auto back = interop::fromWntrJson(json);
    const auto& doc = back.document();
    auto option = [&](const char* section, const char* key) {
        return doc.keyValue(section, key).value_or("<missing>");
    };
    CHECK_NEAR(text::toDouble(option("OPTIONS", "DEMAND MULTIPLIER")).value_or(0.0), 1.5, 1e-9);
    CHECK_NEAR(text::toDouble(option("OPTIONS", "EMITTER EXPONENT")).value_or(0.0), 0.7, 1e-9);
    CHECK_EQ(option("TIMES", "PATTERN START"), std::string("1:00"));
    CHECK_EQ(option("TIMES", "START CLOCKTIME"), std::string("6:00"));
    std::map<std::string, std::string> statuses;
    if (const auto* status = doc.find("STATUS"))
    {
        for (int i = 0; i < status->rowCount(); ++i)
        {
            const auto t = status->rowTokens(i);
            if (t.size() >= 2)
                statuses[t[0]] = t[1];
        }
    }
    CHECK_EQ(statuses["PU1"], std::string("Closed"));
    CHECK_EQ(statuses["V1"], std::string("Open"));
    bool pipeClosed = false;
    for (int i = 0; i < doc.find("PIPES")->rowCount(); ++i)
    {
        const auto t = doc.find("PIPES")->rowTokens(i);
        if (t.size() >= 8 && t[0] == "P3")
            pipeClosed = t[7] == "Closed";
    }
    CHECK(pipeClosed);
    const auto* demands = doc.find("DEMANDS");
    CHECK(demands != nullptr && demands->rowCount() == 2);
    for (int i = 0; demands && i < demands->rowCount(); ++i)
    {
        const auto t = demands->rowTokens(i);
        CHECK(std::find(t.begin(), t.end(), "*") == t.end());
    }

    // Both files run alike: the closed pipe carries nothing, and the demand
    // of J1 is (3 x 2.0 + 1) x 1.5 at the start (the pattern starts at its
    // second step).
    const auto imported = scratch("wntr-statuses-back.inp");
    back.save(imported);
    const auto a = run("epanet-2.3-owa", file);
    const auto b = run("epanet-2.3-owa", imported);
    CHECK_EQ(b.periodCount(), a.periodCount());
    CHECK(value(a, "node", "Demand", "J1", 0) > 10.0);
    for (int p = 0; p < a.periodCount(); ++p)
    {
        CHECK_NEAR(value(b, "node", "Demand", "J1", p), value(a, "node", "Demand", "J1", p), 1e-3);
        CHECK_NEAR(value(b, "node", "Pressure", "J2", p), value(a, "node", "Pressure", "J2", p), 1e-3);
        CHECK_NEAR(value(b, "link", "Flow", "P3", p), 0.0, 1e-6);
    }
}
