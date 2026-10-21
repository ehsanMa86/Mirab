/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_scripting.cpp
 * Purpose: Tests of the JSON model view and the batch edits used by mirab-cli model get/set.
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

#include "mirab/core/Json.h"
#include "mirab/core/Model.h"
#include "mirab/core/ModelJson.h"
#include "mirab/core/Text.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace mirab;
namespace fs = std::filesystem;
using json::Value;

namespace
{
fs::path example(const char* rel)
{
    return fs::path(MIRAB_SOURCE_DIR) / "examples" / rel;
}

const Value* findById(const Value& list, const std::string& id)
{
    for (const auto& e : list.items())
    {
        if (e.get("id").asString() == id)
            return &e;
    }
    return nullptr;
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);)
        out.push_back(l);
    return out;
}

/// Lines of @p after that differ from @p before (same line count expected).
std::vector<std::string> changedLines(const std::string& before, const std::string& after)
{
    const auto a = lines(before);
    const auto b = lines(after);
    std::vector<std::string> out;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i)
    {
        if (i >= a.size() || i >= b.size() || a[i] != b[i])
            out.push_back(i < b.size() ? b[i] : std::string());
    }
    return out;
}

void applyText(core::Model& m, const char* edits)
{
    core::applyEdits(m, Value::parse(edits));
}
} // namespace

TEST_CASE("modeljson: EPANET elements, options and sections")
{
    const auto m = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    const Value v = core::modelToJson(m);
    CHECK_EQ(v.get("format").asString(), std::string(core::kModelJsonFormat));
    CHECK_EQ(v.get("dialect").asString(), std::string("EPANET"));
    CHECK_EQ(v.get("units").get("flow").asString(), std::string("LPS"));
    CHECK_NEAR(v.get("times").get("duration").asNumber(), 86400.0, 1e-9);
    CHECK_EQ(v.get("nodes").size(), std::size_t(11));
    CHECK_EQ(v.get("links").size(), std::size_t(14));

    const Value* j1 = findById(v.get("nodes"), "J1");
    CHECK(j1 != nullptr);
    CHECK_EQ(j1->get("kind").asString(), std::string("Junction"));
    CHECK_NEAR(j1->get("properties").get("Elevation").asNumber(), 20.0, 1e-12);
    CHECK_EQ(j1->get("properties").get("Pattern").asString(), std::string("PAT1"));
    CHECK_NEAR(j1->get("x").asNumber(), 1000.0, 1e-12);

    const Value* t1 = findById(v.get("nodes"), "T1");
    CHECK(t1->get("properties").contains("VolCurve"));
    CHECK(t1->get("properties").get("VolCurve").isNull());

    const Value* r1 = findById(v.get("nodes"), "R1");
    CHECK_NEAR(r1->get("data").get("QUALITY")[0].get("InitQual").asNumber(), 1.0, 1e-12);

    const Value* pu1 = findById(v.get("links"), "PU1");
    CHECK_EQ(pu1->get("properties").get("Parameters").asString(), std::string("HEAD C1"));
    CHECK_EQ(pu1->get("from").asString(), std::string("R1"));

    CHECK_EQ(v.get("options").get("TIMES").get("DURATION").asString(), std::string("24:00"));
    const Value& options = v.get("options");
    CHECK_EQ(options.get("OPTIONS").get("UNBALANCED").asString(), std::string("Continue 10"));
    CHECK_EQ(options.get("REACTIONS").get("GLOBAL BULK").asString(), std::string("-0.5"));
    CHECK_EQ(v.get("sections").get("PATTERNS").size(), std::size_t(4));
    CHECK_EQ(v.get("sections").get("CURVES")[0][2].asString(), std::string("40"));
    // The view must survive a dump and parse unchanged.
    CHECK_EQ(Value::parse(v.dump()).dump(), v.dump());
}

TEST_CASE("modeljson: SWMM element data and named objects")
{
    const auto m = core::Model::fromFile(example("swmm/mirab-quality.inp"));
    const Value v = core::modelToJson(m);
    CHECK_EQ(v.get("dialect").asString(), std::string("SWMM"));
    CHECK(v.get("subcatchments").size() > 0);
    CHECK(v.get("gages").size() > 0);
    const Value& link = v.get("links")[0];
    CHECK(link.get("data").contains("XSECTIONS"));
    CHECK(link.get("data").get("XSECTIONS")[0].get("Geom1").isNumber());
    const Value& pollutants = v.get("objects").get("Pollutant");
    CHECK(pollutants.size() >= 2);
    const Value* lead = findById(pollutants, "Lead");
    CHECK(lead != nullptr);
    CHECK_EQ(lead->get("rows")[0].get("values").get("CoPollutant").asString(), std::string("TSS"));
    // Land uses carry their buildup and washoff rows.
    bool buildup = false;
    for (const auto& lu : v.get("objects").get("Landuse").items())
        for (const auto& r : lu.get("rows").items())
            buildup = buildup || r.get("section").asString() == "BUILDUP";
    CHECK(buildup);
    CHECK(!v.get("sections").contains("POLLUTANTS"));
}

TEST_CASE("modeljson: option keys are unique and reusable")
{
    const auto doc = core::InpDocument::parse("[ENERGY]\n Global Efficiency 75\n"
                                              " Pump PU1 Price 0.1\n Pump PU1 Pattern P2\n"
                                              "[OPTIONS]\n Demand Model PDA\n");
    const auto energy = core::optionEntries(doc, "ENERGY");
    CHECK_EQ(energy.size(), std::size_t(3));
    CHECK_EQ(energy[0].first, std::string("GLOBAL EFFICIENCY"));
    CHECK_EQ(energy[1].first, std::string("PUMP PU1 PRICE"));
    CHECK_EQ(energy[1].second, std::string("0.1"));
    CHECK_EQ(energy[2].first, std::string("PUMP PU1 PATTERN"));
    for (const auto& [key, value] : energy)
        CHECK_EQ(doc.keyValue("ENERGY", key).value_or(""), value);
    CHECK_EQ(core::optionEntries(doc, "OPTIONS")[0].first, std::string("DEMAND MODEL"));
    CHECK_EQ(core::columnKey("Base demand"), core::columnKey("base_demand"));
}

TEST_CASE("modeljson: edits change only the edited values")
{
    auto m = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    const std::string before = m.toText();
    const auto s = core::applyEdits(m, Value::parse(R"({"edits": [
        {"id": "J1", "column": "elevation", "value": 21.5},
        {"id": "P1", "column": "Diameter", "value": "250"},
        {"id": "J2", "column": "Demand", "value": 5},
        {"option": "Duration", "section": "TIMES", "value": "12:00"}]})"));
    CHECK_EQ(s.applied, 4);
    CHECK_EQ(s.changed, 3); // J2 already has a demand of 5
    const auto changed = changedLines(before, m.toText());
    CHECK_EQ(changed.size(), std::size_t(3));
    CHECK(changed[2].find("Duration") != std::string::npos);
    CHECK(changed[2].find("12:00") != std::string::npos);
    CHECK_NEAR(*text::toDouble(m.rowsFor(core::Category::Node, "J1")[0].tokens[1]), 21.5, 1e-12);
    CHECK_EQ(m.document().keyValue("TIMES", "DURATION").value_or(""), std::string("12:00"));
    CHECK_NEAR(m.durationSeconds(), 12 * 3600.0, 1e-9);
}

TEST_CASE("modeljson: edits add data rows, replace series and rename")
{
    auto m = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    applyText(m, R"([
        {"section": "EMITTERS", "id": "J3", "column": "Coefficient", "value": 0.4},
        {"section": "PATTERNS", "id": "PAT1", "rows": [[1, 1.1, 1.2], [0.9, 0.8]]},
        {"option": "DEMAND MODEL", "value": "PDA"},
        {"id": "J9", "column": 0, "value": "J9X"}])");
    const auto emitters = m.rowsFor(core::Category::Node, "J3");
    bool found = false;
    for (const auto& r : emitters)
        found = found || (r.section == "EMITTERS" && r.tokens.size() == 2 && r.tokens[1] == "0.4");
    CHECK(found);
    CHECK_EQ(m.rowsOfKey("PATTERNS", "PAT1").size(), std::size_t(2));
    CHECK_EQ(m.demandModel(), std::string("PDA"));
    CHECK(m.findNode("J9X") != nullptr);
    CHECK_EQ(m.findLink("P12")->to, std::string("J9X"));

    // A null value removes an option; an unknown ID or column is an error
    // that names the edit.
    applyText(m, R"([{"option": "DEMAND MODEL", "value": null}])");
    CHECK(!m.document().keyValue("OPTIONS", "DEMAND MODEL"));
    bool threw = false;
    try
    {
        applyText(m, R"([{"id": "J1", "column": "Elevation", "value": 1},
                         {"id": "J1", "column": "Nope", "value": 1}])");
    }
    catch (const std::runtime_error& e)
    {
        threw = std::string(e.what()).find("edit 2") != std::string::npos;
    }
    CHECK(threw);
    threw = false;
    try
    {
        applyText(m, R"([{"id": "NOPE", "column": "Elevation", "value": 1}])");
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("modeljson: SWMM edits reach data rows and objects")
{
    auto m = core::Model::fromFile(example("swmm/mirab-quality.inp"));
    const auto& link = m.links().front();
    const std::string id = link.id;
    applyText(m, (R"([{"id": ")" + id + R"(", "column": "geom1", "value": 1.25},
                    {"id": "Lead", "column": "CoFraction", "value": 0.3}])")
                     .c_str());
    bool geom = false;
    for (const auto& r : m.rowsFor(core::Category::Link, id))
        geom = geom || (r.section == "XSECTIONS" && r.tokens[2] == "1.25");
    CHECK(geom);
    const auto rows = m.objectRows("Pollutant", "Lead");
    CHECK(!rows.empty());
    CHECK_EQ(rows[0].tokens[8], std::string("0.3"));
}
