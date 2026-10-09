/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
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

#include "anahita/core/Model.h"
#include "anahita/core/Text.h"
#include "anahita/engine/Compatibility.h"
#include "anahita/engine/EngineRegistry.h"
#include "anahita/interop/Interop.h"
#include "anahita/resilience/Resilience.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace anahita;
namespace fs = std::filesystem;

namespace
{
fs::path example(const char* rel)
{
    return fs::path(ANAHITA_SOURCE_DIR) / "examples" / rel;
}

fs::path scratch(const std::string& name)
{
    const auto dir = fs::temp_directory_path() / "anahita-tests";
    fs::create_directories(dir);
    return dir / name;
}

engine::EngineRegistry& registry()
{
    static engine::EngineRegistry r({fs::u8path(ANAHITA_ENGINE_DIR)});
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
    const auto a = run("epanet-2.2-epa", example("epanet/anahita-demo.inp"));
    const auto b = run("epanet-2.3-owa", example("epanet/anahita-demo.inp"));
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
    const auto model = core::Model::fromFile(example("epanet/anahita-demo-leakage.inp"));
    const auto* epa = registry().descriptor("epanet-2.2-epa");
    const auto report = engine::checkCompatibility(model, *epa);
    CHECK(report.runnable());
    CHECK_EQ(report.count(engine::Support::Ignored), 1);
    CHECK(engine::prepareInput(model, *epa).find("[LEAKAGE]") == std::string::npos);

    const auto owa = run("epanet-2.3-owa", example("epanet/anahita-demo-leakage.inp"));
    double total = 0.0;
    const auto* nodes = owa.table("node");
    const int v = nodes->variableIndex("Leakage");
    CHECK(v >= 0);
    for (int e = 0; e < nodes->elementCount(); ++e)
        total += nodes->value(8, v, e);
    CHECK(total > 0.1);
    const auto old = run("epanet-2.2-epa", example("epanet/anahita-demo-leakage.inp"));
    CHECK(old.table("node")->variableIndex("Leakage") < 0);
}

TEST_CASE("engines: EPANET-MSX runs on both EPANET bases")
{
    for (const char* id : {"epanet-msx-2.0-epa22", "epanet-msx-2.0-owa23"})
    {
        const auto r = run(id, example("epanet/anahita-demo.inp"), example("msx/anahita-demo.msx"));
        CHECK_NEAR(value(r, "node", "CL2", "R1", 0), 1.2, 1e-6);
        const double cl = value(r, "node", "CL2", "J9", 20);
        CHECK(cl > 0.5 && cl < 1.2);
        CHECK(value(r, "node", "TOC", "J9", 20) < 1.5);
        CHECK(value(r, "link", "CL2", "P1", 20) > 0.5);
    }
}

TEST_CASE("engines: EPA SWMM 5.2 routes the design storm")
{
    const auto r = run("swmm-5.2-epa", example("swmm/anahita-urban.inp"));
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

TEST_CASE("resilience: Todini, topology, criticality and fire flow")
{
    const auto model = core::Model::fromFile(example("epanet/anahita-demo.inp"));
    const auto results = run("epanet-2.3-owa", example("epanet/anahita-demo.inp"));
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
    const auto crit = resilience::pipeCriticality(lib.api(), example("epanet/anahita-demo.inp"), co);
    CHECK_EQ(crit.size(), std::size_t(13));

    resilience::FireFlowOptions fo;
    fo.fireFlow = 30.0;
    fo.residualPressure = 14.0;
    fo.nodes = {"J5"};
    const auto ff = resilience::fireFlow(lib.api(), example("epanet/anahita-demo.inp"), fo);
    CHECK_EQ(ff.size(), std::size_t(1));
    CHECK(ff[0].adequate);
    CHECK(ff[0].availableFlow > 30.0);
}

TEST_CASE("resilience: water service availability from a pressure-driven run")
{
    const auto r = run("epanet-2.3-owa", example("epanet/anahita-demo-leakage.inp"));
    const auto s = resilience::waterServiceAvailability(r);
    CHECK(s.note.empty());
    CHECK(s.overall > 0.9 && s.overall <= 1.0);
}

TEST_CASE("interop: WNTR JSON round trip reproduces the hydraulics")
{
    const auto model = core::Model::fromFile(example("epanet/anahita-demo.inp"));
    const auto json = interop::toWntrJson(model);
    CHECK_EQ(json.get("nodes").size(), std::size_t(11));
    CHECK_EQ(json.get("links").size(), std::size_t(14));
    const auto back = interop::fromWntrJson(json::Value::parse(json.dump()));
    const auto file = scratch("wntr-roundtrip.inp");
    back.save(file);
    const auto a = run("epanet-2.3-owa", example("epanet/anahita-demo.inp"));
    const auto b = run("epanet-2.3-owa", file);
    for (int p : {0, 7, 19})
        CHECK_NEAR(value(a, "node", "Pressure", "J8", p), value(b, "node", "Pressure", "J8", p), 1e-3);
}

TEST_CASE("interop: GeoJSON and GeoPackage export")
{
    const auto model = core::Model::fromFile(example("swmm/anahita-urban.inp"));
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
    const auto model = core::Model::fromFile(example("swmm/anahita-urban.inp"));
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
