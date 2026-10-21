/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_scenarios.cpp
 * Purpose: Tests of scenarios: differences, applying chains, conflicts and storage in the sidecar.
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

#include "mirab/core/Model.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Scenario.h"

#include <filesystem>
#include <fstream>
#include <functional>

using namespace mirab;
namespace fs = std::filesystem;
using core::ScenarioChange;

namespace
{
fs::path example(const char* rel)
{
    return fs::path(MIRAB_SOURCE_DIR) / "examples" / rel;
}

fs::path scratch(const char* name)
{
    const auto dir = fs::temp_directory_path() / "mirab-tests";
    fs::create_directories(dir);
    return dir / name;
}

core::Model demo()
{
    return core::Model::fromFile(example("epanet/mirab-demo.inp"));
}

std::string token(const core::Model& m, const char* section, const char* key, int column, int ordinal = 0)
{
    const auto* s = m.document().find(section);
    if (!s)
        return "(no section)";
    const core::SectionKeys keys(*s, m.dialect());
    const int r = keys.find(key, ordinal);
    if (r < 0)
        return "(no row)";
    const auto t = s->rowTokens(r);
    return column < static_cast<int>(t.size()) ? t[static_cast<std::size_t>(column)] : "(no column)";
}

/// Edits a copy of @p base and records the edit as the changes of a scenario.
core::Scenario record(const core::Model& parent, const std::string& name,
                      const std::function<void(core::Model&)>& edit, const std::string& parentName = {})
{
    core::Model m = parent;
    edit(m);
    core::Scenario s;
    s.name = name;
    s.parent = parentName;
    s.changes = core::diffDocuments(parent.document(), m.document(), m.dialect());
    return s;
}

int pipeRow(const core::Model& m, const char* id)
{
    return m.findLink(id)->row;
}
} // namespace

TEST_CASE("scenarios: row keys follow IDs, option names, rules and transects")
{
    const auto doc = core::InpDocument::parse("[OPTIONS]\n Demand Model PDA\n Demand Multiplier 1.2\n Units LPS\n"
                                              "[RULES]\nRULE R1\nIF TANK T1 LEVEL ABOVE 5\nTHEN PUMP PU1 STATUS IS CLOSED\n"
                                              "RULE R2\nIF TANK T1 LEVEL BELOW 2\nTHEN PUMP PU1 STATUS IS OPEN\n"
                                              "[PATTERNS]\n P1 1 2\n P2 3\n P1 4 5\n");
    const core::SectionKeys options(*doc.find("OPTIONS"), core::Dialect::Epanet);
    CHECK_EQ(options.key(0), std::string("DEMAND MODEL"));
    CHECK_EQ(options.key(1), std::string("DEMAND MULTIPLIER"));
    CHECK_EQ(options.key(2), std::string("UNITS"));
    const core::SectionKeys rules(*doc.find("RULES"), core::Dialect::Epanet);
    CHECK_EQ(rules.key(4), std::string("R2"));
    CHECK_EQ(rules.ordinal(4), 1);
    CHECK_EQ(rules.find("R1", 2), 2);
    const core::SectionKeys patterns(*doc.find("PATTERNS"), core::Dialect::Epanet);
    CHECK_EQ(patterns.find("P1", 1), 2);
    CHECK_EQ(patterns.find("P2", 1), -1);

    const auto swmm = core::InpDocument::parse("[TRANSECTS]\nNC 0.1 0.1 0.03\nX1 Main 3 0 0 0 0 0 0 0 0\n"
                                               "GR 5 0 0 10 5 20\nNC 0.2 0.2 0.04\nX1 Side 2 0 0 0 0 0 0 0 0\n"
                                               "GR 2 0 2 5\n[INFLOWS]\n J1 FLOW TS1\n J1 TSS TS2\n");
    const core::SectionKeys transects(*swmm.find("TRANSECTS"), core::Dialect::Swmm);
    CHECK_EQ(transects.key(0), std::string("Main"));
    CHECK_EQ(transects.key(3), std::string("Side"));
    CHECK_EQ(transects.ordinal(5), 2);
    const core::SectionKeys inflows(*swmm.find("INFLOWS"), core::Dialect::Swmm);
    CHECK_EQ(inflows.key(1), std::string("J1 TSS"));
}

TEST_CASE("scenarios: differences apply back to the same model")
{
    const core::Model base = demo();
    core::Model edited = base;
    CHECK(edited.setToken("PIPES", pipeRow(edited, "P2"), 4, "250"));
    CHECK(edited.setToken("JUNCTIONS", edited.findNode("J5")->row, 2, "12"));
    edited.setOption("DEMAND MULTIPLIER", "1.25");
    CHECK(edited.addNode("Junction", "J10", {3500, 1500}));
    CHECK(edited.addLink("Pipe", "P14", "J9", "J10"));
    CHECK(edited.removeElement(core::Category::Link, "P12"));
    edited.document().find("TITLE")->lines = {"Upsized network", ""};
    edited.rebuild();

    const auto changes = core::diffDocuments(base.document(), edited.document(), base.dialect());
    CHECK(!changes.empty());
    core::InpDocument applied = base.document();
    std::vector<core::ScenarioConflict> conflicts;
    core::applyChanges(applied, base.dialect(), changes, &conflicts, "test");
    CHECK(conflicts.empty());
    CHECK(core::diffDocuments(applied, edited.document(), base.dialect()).empty());
    const core::Model m(applied, base.dialect());
    CHECK_EQ(token(m, "PIPES", "P2", 4), std::string("250"));
    CHECK_EQ(token(m, "JUNCTIONS", "J5", 2), std::string("12"));
    CHECK_EQ(m.option("DEMAND MULTIPLIER"), std::string("1.25"));
    CHECK(m.findNode("J10") && m.findNode("J10")->placed);
    CHECK(m.findLink("P14"));
    CHECK(!m.findLink("P12"));
    CHECK_EQ(m.title(), std::string("Upsized network"));
    // Untouched rows keep their text.
    CHECK(m.toText().find(" P1              J1              J2              300             200") != std::string::npos);

    // Each kind of change is recorded once, with the parent value.
    int sets = 0;
    for (const auto& c : changes)
    {
        if (c.kind == ScenarioChange::Kind::Set && c.section == "PIPES")
        {
            ++sets;
            CHECK_EQ(c.key, std::string("P2"));
            CHECK_EQ(c.column, 4);
            CHECK_EQ(c.from.front(), std::string("150"));
            CHECK_EQ(c.to.front(), std::string("250"));
        }
    }
    CHECK_EQ(sets, 1);
}

TEST_CASE("scenarios: changes find their rows after the base model changed")
{
    const core::Model base = demo();
    const auto s = record(base, "Upsizing", [](core::Model& m) {
        m.setToken("PIPES", pipeRow(m, "P10"), 4, "200");
        m.document().setKeyValue("TIMES", "PATTERN TIMESTEP", "0:30");
        m.rebuild();
    });
    // The base model gets new rows in front of the edited ones.
    core::Model changed = base;
    auto& pipes = changed.document().find("PIPES")->lines;
    pipes.insert(pipes.begin() + 1, " P0              J1              J9              100             80              120             0               Open");
    auto& times = changed.document().find("TIMES")->lines;
    times.insert(times.begin(), " Rule Timestep 0:10");
    changed.rebuild();

    std::vector<core::ScenarioConflict> conflicts;
    const core::Model m = core::scenarioModel(changed, {s}, "Upsizing", &conflicts);
    CHECK(conflicts.empty());
    CHECK_EQ(token(m, "PIPES", "P10", 4), std::string("200"));
    CHECK_EQ(token(m, "PIPES", "P0", 4), std::string("80"));
    CHECK_EQ(*m.document().keyValue("TIMES", "PATTERN TIMESTEP"), std::string("0:30"));
    CHECK_EQ(*m.document().keyValue("TIMES", "RULE TIMESTEP"), std::string("0:10"));
}

TEST_CASE("scenarios: chains inherit the changes of their parents")
{
    const core::Model base = demo();
    std::vector<core::Scenario> list;
    list.push_back(record(base, "Growth", [](core::Model& m) { m.setOption("DEMAND MULTIPLIER", "1.2"); }));
    const core::Model growth = core::scenarioModel(base, list, "Growth");
    list.push_back(record(growth, "Growth and upsizing",
                          [](core::Model& m) { m.setToken("PIPES", pipeRow(m, "P5"), 4, "150"); }, "Growth"));

    const core::Model child = core::scenarioModel(base, list, "Growth and upsizing");
    CHECK_EQ(child.option("DEMAND MULTIPLIER"), std::string("1.2"));
    CHECK_EQ(token(child, "PIPES", "P5", 4), std::string("150"));
    const core::Model parent = core::scenarioModel(base, list, "Growth");
    CHECK_EQ(token(parent, "PIPES", "P5", 4), std::string("100"));
    CHECK_EQ(core::scenarioModel(base, list, "").toText(), base.toText());
    // The child records only its own change.
    CHECK_EQ(list[1].changes.size(), std::size_t(1));

    const auto chain = core::scenarioChain(list, "Growth and upsizing");
    CHECK_EQ(chain.size(), std::size_t(2));
    CHECK_EQ(chain.front()->name, std::string("Growth"));
    std::string error;
    list[0].parent = "Growth and upsizing";
    CHECK(core::scenarioChain(list, "Growth", &error).empty());
    CHECK(error.find("loop") != std::string::npos);
    bool threw = false;
    try
    {
        core::scenarioModel(base, list, "Missing");
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("scenarios: conflicts when the base changed underneath")
{
    const core::Model base = demo();
    const auto s = record(base, "Edits", [](core::Model& m) {
        m.setToken("PIPES", pipeRow(m, "P4"), 4, "300");
        m.setToken("PIPES", pipeRow(m, "P7"), 3, "450");
        m.document().find("DEMANDS")->appendRow({"J8", "2.5", "PAT1"});
        m.rebuild();
    });
    core::Model changed = base;
    CHECK(changed.removeElement(core::Category::Link, "P4"));
    CHECK(changed.setToken("PIPES", pipeRow(changed, "P7"), 3, "500"));
    CHECK(changed.removeElement(core::Category::Node, "J8"));

    std::vector<core::ScenarioConflict> conflicts;
    const core::Model m = core::scenarioModel(changed, {s}, "Edits", &conflicts);
    CHECK_EQ(conflicts.size(), std::size_t(3));
    int missing = 0, valueChanged = 0;
    for (const auto& c : conflicts)
    {
        CHECK_EQ(c.scenario, std::string("Edits"));
        if (c.missing)
            ++missing;
        else
            ++valueChanged;
    }
    CHECK_EQ(missing, 2); // P4 is gone, and the demand of the removed J8 is left out
    CHECK_EQ(valueChanged, 1);
    // The scenario value wins over the changed base value.
    CHECK_EQ(token(m, "PIPES", "P7", 3), std::string("450"));
    CHECK(!m.findLink("P4"));
    CHECK(m.document().find("DEMANDS")->rowCount() == 0);
}

TEST_CASE("scenarios: stored in the sidecar next to the other settings")
{
    const fs::path inp = scratch("scenarios-demo.inp");
    // Removed first: overwrite_existing fails with some MinGW runtimes.
    std::error_code ec;
    fs::remove(inp, ec);
    fs::copy_file(example("epanet/mirab-demo.inp"), inp);
    fs::remove(core::ProjectSettings::sidecarPath(inp), ec);
    // A sidecar without scenarios (older versions) loads with none.
    {
        std::ofstream f(core::ProjectSettings::sidecarPath(inp));
        f << R"({"format": "mirab-project-settings/1", "engine": "epanet-2.2-epa", "calibration": {"x": 1}})";
    }
    auto settings = core::ProjectSettings::loadFor(inp);
    CHECK(settings.scenarios.empty());
    CHECK_EQ(settings.engineId, std::string("epanet-2.2-epa"));

    const core::Model base = core::Model::fromFile(inp);
    auto a = record(base, "Pipe upsizing", [](core::Model& m) {
        m.setToken("PIPES", pipeRow(m, "P9"), 4, "150");
        m.removeElement(core::Category::Link, "P11");
        m.document().find("TITLE")->lines = {"Alternative A", ""};
        m.rebuild();
    });
    a.description = "Larger pipes in the south";
    a.engineId = "epanet-2.3-owa";
    a.useMsx = 0;
    auto b = record(core::scenarioModel(base, {a}, "Pipe upsizing"), "Pipe upsizing, 2040",
                    [](core::Model& m) { m.setOption("DEMAND MULTIPLIER", "1.3"); }, "Pipe upsizing");
    settings.scenarios = {a, b};
    settings.saveFor(inp);

    const auto loaded = core::ProjectSettings::loadFor(inp);
    CHECK_EQ(loaded.scenarios.size(), std::size_t(2));
    const auto& la = loaded.scenarios[0];
    CHECK_EQ(la.name, a.name);
    CHECK_EQ(la.description, a.description);
    CHECK_EQ(la.engineId, a.engineId);
    CHECK_EQ(la.useMsx, 0);
    CHECK(la.changes == a.changes);
    CHECK_EQ(loaded.scenarios[1].parent, std::string("Pipe upsizing"));
    CHECK(loaded.scenarios[1].changes == b.changes);
    CHECK_EQ(core::scenarioModel(base, loaded.scenarios, "Pipe upsizing, 2040").toText(),
             core::scenarioModel(base, settings.scenarios, "Pipe upsizing, 2040").toText());

    const auto e = core::scenarioEngine(loaded.scenarios, "Pipe upsizing, 2040", {"epanet-2.2-epa", true, "q.msx"});
    CHECK_EQ(e.engineId, std::string("epanet-2.3-owa"));
    CHECK(!e.useMsx);
    CHECK_EQ(e.msxFile, std::string("q.msx"));
}

TEST_CASE("scenarios: names and results file names")
{
    std::vector<core::Scenario> list(1);
    list[0].name = "Pipe upsizing";
    CHECK_EQ(core::scenarioFileStem("Pipe upsizing"), std::string("Pipe-upsizing"));
    CHECK_EQ(core::scenarioFileStem("10-year storm (2050)"), std::string("10-year-storm-2050"));
    CHECK_EQ(core::scenarioFileStem("***"), std::string("scenario"));
    CHECK(core::checkScenarioName(list, "Demand growth").empty());
    CHECK(!core::checkScenarioName(list, "Pipe upsizing").empty());
    CHECK(!core::checkScenarioName(list, "pipe-upsizing").empty()); // same results file
    CHECK(!core::checkScenarioName(list, "Base").empty());
    CHECK(!core::checkScenarioName(list, "a,b").empty());
    CHECK(!core::checkScenarioName(list, "").empty());
}

TEST_CASE("scenarios: result summary for comparisons")
{
    const core::Model base = demo();
    core::ResultSet r;
    r.flowUnits = "LPS";
    r.reportStep = 3600;
    r.times = {0, 3600};
    r.defineTable("node", {"J1", "J2", "R1"}, {{"Demand", "LPS"}, {"Head", "m"}, {"Pressure", "m"}});
    r.defineTable("link", {"P1", "PU1"}, {{"Flow", "LPS"}, {"Velocity", "m/s"}, {"Pump Power", "kW"}});
    auto& nodes = *r.table("node");
    auto& links = *r.table("link");
    nodes.resizePeriods(2);
    links.resizePeriods(2);
    for (int p = 0; p < 2; ++p)
    {
        nodes.set(p, 0, 0, 10.0f);
        nodes.set(p, 0, 1, 20.0f);
        nodes.set(p, 2, 0, 30.0f + p);
        nodes.set(p, 2, 1, 25.0f + p);
        nodes.set(p, 2, 2, 0.0f); // reservoirs are not junctions
        links.set(p, 1, 0, 1.5f);
        links.set(p, 2, 1, 10.0f);
    }
    const auto summary = core::resultSummary(r, base);
    auto find = [&](const char* name) {
        for (const auto& s : summary)
        {
            if (s.name == name)
                return s.value;
        }
        return -1.0;
    };
    CHECK_NEAR(find("Minimum junction pressure"), 25.0, 1e-9);
    CHECK_NEAR(find("Mean junction pressure"), 28.0, 1e-9);
    CHECK_NEAR(find("Maximum velocity"), 1.5, 1e-6);
    CHECK_NEAR(find("Demand volume"), 2 * 30 * 3.6, 1e-6); // 30 L/s for two hours, in m3
    CHECK_NEAR(find("Pump energy"), 20.0, 1e-9);
}
