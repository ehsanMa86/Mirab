/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_core.cpp
 * Purpose: Unit tests of the core library: text, JSON, input documents, model editing and results files.
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
#include "mirab/core/Hash.h"
#include "mirab/core/Json.h"
#include "mirab/core/LiveResults.h"
#include "mirab/core/Model.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Results.h"
#include "mirab/core/Text.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace mirab;
namespace fs = std::filesystem;

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
} // namespace

TEST_CASE("text: locale independent numbers")
{
    CHECK_NEAR(*text::toDouble("1.5"), 1.5, 1e-12);
    CHECK_NEAR(*text::toDouble(" -2e3 "), -2000.0, 1e-9);
    CHECK(!text::toDouble("1,5"));
    CHECK(!text::toDouble("abc"));
    CHECK_EQ(text::formatDouble(3.0), std::string("3"));
    CHECK_EQ(text::formatDouble(0.25), std::string("0.25"));
}

TEST_CASE("text: input line splitting keeps quotes and comments")
{
    const auto s = text::splitInpLine(R"( RG1  INTENSITY  1:00 1.0 FILE "my rain.dat" STA1 MM ;comment)");
    CHECK_EQ(s.tokens.size(), std::size_t(8));
    CHECK_EQ(s.tokens[5], std::string("\"my rain.dat\""));
    CHECK(s.hasComment);
    CHECK_EQ(s.comment, std::string("comment"));
}

TEST_CASE("text: clock and duration parsing")
{
    CHECK_NEAR(*text::parseClockOrDuration("24:00"), 86400.0, 1e-9);
    CHECK_NEAR(*text::parseClockOrDuration("0:05"), 300.0, 1e-9);
    CHECK_NEAR(*text::parseClockOrDuration("30", "MIN"), 1800.0, 1e-9);
    CHECK_NEAR(*text::parseClockOrDuration("12", "AM"), 0.0, 1e-9);
    CHECK_NEAR(*text::parseClockOrDuration("1", "PM"), 13 * 3600.0, 1e-9);
}

TEST_CASE("json: parse and dump round trip")
{
    const auto v = json::Value::parse(R"({"a":[1,2.5,"xé"],"b":{"c":true,"d":null}})");
    CHECK_EQ(v.get("a").size(), std::size_t(3));
    CHECK_NEAR(v.get("a")[1].asNumber(), 2.5, 1e-12);
    CHECK_EQ(v.get("a")[2].asString(), std::string("x\xc3\xa9"));
    CHECK(v.get("b").get("c").asBool());
    CHECK(v.get("b").get("d").isNull());
    const auto again = json::Value::parse(v.dump(2));
    CHECK_EQ(again.dump(), v.dump());
    bool threw = false;
    try
    {
        json::Value::parse("{\"a\":}");
    }
    catch (const json::ParseError&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("inp: unchanged documents round trip byte for byte")
{
    for (const char* f : {"epanet/mirab-demo.inp", "swmm/mirab-urban.inp", "msx/mirab-demo.msx"})
    {
        std::ifstream in(example(f), std::ios::binary);
        std::string original((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto doc = core::InpDocument::parse(original);
        CHECK_EQ(doc.toString(), original);
    }
}

TEST_CASE("inp: key/value options with multi-word keys")
{
    auto doc = core::InpDocument::parse("[OPTIONS]\n Pressure Exponent 0.5\n Units LPS\n Demand Model PDA\n");
    CHECK(!doc.keyValue("OPTIONS", "PRESSURE"));
    CHECK_EQ(*doc.keyValue("OPTIONS", "PRESSURE EXPONENT"), std::string("0.5"));
    CHECK_EQ(*doc.keyValue("OPTIONS", "DEMAND MODEL"), std::string("PDA"));
    doc.setKeyValue("OPTIONS", "PRESSURE", "BAR");
    CHECK_EQ(*doc.keyValue("OPTIONS", "PRESSURE"), std::string("BAR"));
    CHECK_EQ(*doc.keyValue("OPTIONS", "PRESSURE EXPONENT"), std::string("0.5"));
    CHECK(doc.removeKeyValue("OPTIONS", "PRESSURE"));
    CHECK(doc.keyValue("OPTIONS", "PRESSURE EXPONENT"));
}

TEST_CASE("model: EPANET demo network structure")
{
    const auto m = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    CHECK(m.dialect() == core::Dialect::Epanet);
    CHECK_EQ(m.nodes().size(), std::size_t(11));
    CHECK_EQ(m.links().size(), std::size_t(14));
    CHECK_EQ(m.findLink("PU1")->kind, std::string("Pump"));
    CHECK_EQ(m.findLink("P13")->to, std::string("T1"));
    CHECK(m.findNode("J5")->placed);
    CHECK_NEAR(m.durationSeconds(), 86400.0, 1e-9);
    CHECK_NEAR(m.reportStepSeconds(), 3600.0, 1e-9);
    CHECK_EQ(m.units().flow, std::string("LPS"));
    CHECK(m.units().system == core::UnitSystem::SI);
    CHECK(m.validate().empty());
}

TEST_CASE("model: SWMM demo network structure")
{
    const auto m = core::Model::fromFile(example("swmm/mirab-urban.inp"));
    CHECK(m.dialect() == core::Dialect::Swmm);
    CHECK_EQ(m.nodes().size(), std::size_t(6));
    CHECK_EQ(m.links().size(), std::size_t(5));
    CHECK_EQ(m.areas().size(), std::size_t(3));
    CHECK_EQ(m.areas()[0].polygon.size(), std::size_t(4));
    CHECK_EQ(m.gages().size(), std::size_t(1));
    CHECK_NEAR(m.durationSeconds(), 6 * 3600.0, 1e-9);
    CHECK_NEAR(m.reportStepSeconds(), 300.0, 1e-9);
}

TEST_CASE("model: add, rename and remove elements")
{
    auto m = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    std::string err;
    CHECK(m.addNode("Junction", "J10", {3500, 1000}, &err));
    CHECK(m.addLink("Pipe", "P14", "J9", "J10", &err));
    CHECK(!m.addLink("Pipe", "P15", "J9", "NOPE", &err));
    CHECK(m.renameElement(core::Category::Node, "J9", "J9B", &err));
    CHECK_EQ(m.findLink("P12")->to, std::string("J9B"));
    CHECK_EQ(m.findLink("P14")->from, std::string("J9B"));
    CHECK(m.removeElement(core::Category::Node, "J9B"));
    CHECK(!m.findLink("P12"));
    CHECK(!m.findLink("P10"));
    CHECK(!m.findLink("P14"));
    CHECK_EQ(m.nodes().size(), std::size_t(11));
    const auto again = core::Model::fromText(m.toText());
    CHECK_EQ(again.nodes().size(), m.nodes().size());
    CHECK_EQ(again.links().size(), m.links().size());
}

TEST_CASE("model: new projects are valid input files")
{
    for (auto d : {core::Dialect::Epanet, core::Dialect::Swmm})
    {
        auto m = core::Model::newProject(d, "");
        CHECK(m.dialect() == d);
        CHECK(core::Model::detectDialect(core::InpDocument::parse(m.toText())) == d);
        std::string err;
        CHECK(m.addNode(core::elementKinds(d, core::Category::Node).front(), "N1", {0, 0}, &err));
        CHECK(m.addNode(core::elementKinds(d, core::Category::Node).front(), "N2", {10, 0}, &err));
        CHECK(m.addLink(core::elementKinds(d, core::Category::Link).front(), "L1", "N1", "N2", &err));
        CHECK_EQ(m.links().size(), std::size_t(1));
    }
}

TEST_CASE("features: OWA EPANET 2.3 features are detected")
{
    const auto m = core::Model::fromFile(example("epanet/mirab-demo-leakage.inp"));
    bool leakage = false, pda = false;
    for (const auto& f : core::detectFeatures(m))
    {
        leakage |= f.feature == core::Feature::EpanetLeakage;
        pda |= f.feature == core::Feature::EpanetPressureDrivenDemand;
    }
    CHECK(leakage);
    CHECK(pda);
}

TEST_CASE("results: .anr files round trip")
{
    core::ResultSet r;
    r.engineId = "test";
    r.flowUnits = "LPS";
    r.defineTable("node", {"A", "B"}, {{"Pressure", "m"}, {"Head", "m"}});
    for (int p = 0; p < 3; ++p)
    {
        const int k = r.addPeriod(p * 600.0);
        r.table("node")->set(k, 0, 1, static_cast<float>(10 + p));
    }
    r.table("node")->appendVariables({{"Extra", ""}});
    r.table("node")->set(2, 2, 0, 7.0f);
    const auto file = scratch("roundtrip.anr");
    r.save(file);
    const auto back = core::ResultSet::load(file);
    CHECK_EQ(back.periodCount(), 3);
    CHECK_NEAR(back.table("node")->value(2, 0, 1), 12.0, 1e-6);
    CHECK_NEAR(back.table("node")->value(2, 2, 0), 7.0, 1e-6);
    CHECK_EQ(back.table("node")->variables[2].name, std::string("Extra"));
    CHECK_NEAR(back.times[2], 1200.0, 1e-9);
}

TEST_CASE("results: SWMM system totals carry the units of the binary output")
{
    auto unitOf = [](const std::vector<core::Variable>& vars, const std::string& name) {
        for (const auto& v : vars)
        {
            if (v.name == name)
                return v.unit;
        }
        return std::string("missing");
    };
    const auto us = core::swmmVariables("CFS", {}, {});
    CHECK_EQ(us.system.size(), std::size_t(15));
    CHECK_EQ(unitOf(us.system, "Rainfall"), std::string("in/hr"));
    CHECK_EQ(unitOf(us.system, "Snow Depth"), std::string("in"));
    CHECK_EQ(unitOf(us.system, "Runoff"), std::string("cfs"));
    CHECK_EQ(unitOf(us.system, "Outflow"), std::string("cfs"));
    CHECK_EQ(unitOf(us.system, "Storage Volume"), std::string("ft3"));
    CHECK_EQ(unitOf(us.system, "Evaporation"), std::string("in/day"));
    CHECK_EQ(unitOf(us.subcatchment, "Evaporation Loss"), std::string("in/day"));
    const auto si = core::swmmVariables("LPS", {}, {});
    CHECK_EQ(unitOf(si.system, "Infiltration"), std::string("mm/hr"));
    CHECK_EQ(unitOf(si.system, "Flooding"), std::string("LPS"));
    CHECK_EQ(unitOf(si.system, "Potential ET"), std::string("mm/day"));
    CHECK_EQ(unitOf(si.system, "Air Temperature"), std::string("\xC2\xB0" "C"));
}

TEST_CASE("results: base64 matches RFC 4648")
{
    const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; ++i)
    {
        const std::string p = plain[i];
        CHECK_EQ(core::live::base64Encode(p.data(), p.size()), std::string(coded[i]));
        std::vector<unsigned char> back;
        CHECK(core::live::base64Decode(coded[i], back));
        CHECK_EQ(std::string(back.begin(), back.end()), p);
    }
    std::vector<unsigned char> out;
    CHECK(!core::live::base64Decode("Zm9", out));
    CHECK(!core::live::base64Decode("Zm9v!A==", out));
    CHECK(!core::live::base64Decode("Z=9v", out));
}

TEST_CASE("results: live layout and frames rebuild the result set")
{
    core::ResultSet r;
    r.engineId = "test";
    r.engineName = "Test engine";
    r.flowUnits = "LPS";
    r.unitSystem = "SI";
    r.reportStep = 600;
    r.defineTable("node", {"A", "B", "C"}, {{"Pressure", "m"}, {"Head", "m"}});
    r.defineTable("link", {"P1", "P2"}, {{"Flow", "LPS"}});
    r.defineTable("system", {"System"}, {});
    for (int p = 0; p < 4; ++p)
    {
        r.addPeriod(p * 600.0);
        for (int e = 0; e < 3; ++e)
        {
            r.table("node")->set(p, 0, e, static_cast<float>(p * 10 + e) + 0.25f);
            r.table("node")->set(p, 1, e, -static_cast<float>(e) / 3.0f);
        }
        r.table("link")->set(p, 0, 1, p == 2 ? std::numeric_limits<float>::quiet_NaN() : 1e-7f * p);
    }

    json::Value event = core::live::layoutPayload(r, 7);
    event.set("event", "layout");
    int layout = 0;
    core::ResultSet live = core::live::parseLayout(json::Value::parse(event.dump()), &layout);
    CHECK_EQ(layout, 7);
    CHECK(core::live::sameLayout(live, r));
    CHECK_EQ(live.periodCount(), 0);
    CHECK_EQ(live.engineName, r.engineName);
    CHECK_EQ(core::live::frameSize(live), static_cast<std::size_t>(8));

    for (int p = 0; p < 4; ++p)
    {
        const std::string line = core::live::frameLine(r, p, 7, p, p * 600.0);
        CHECK(line.rfind("{\"event\":\"frame\"", 0) == 0);
        const auto frame = core::live::parseFrameLine(line);
        CHECK_EQ(frame.layout, 7);
        CHECK_EQ(frame.period, p);
        CHECK_EQ(core::live::appendFrame(live, frame), p);
        // The general JSON reader understands the same line.
        CHECK_EQ(json::Value::parse(line).get("period").asNumber(), static_cast<double>(p));
    }
    CHECK_EQ(live.periodCount(), 4);
    CHECK_NEAR(live.times[3], 1800.0, 1e-9);
    for (const auto& t : r.tables)
    {
        const auto* lt = live.table(t.kind);
        CHECK(lt != nullptr);
        CHECK_EQ(lt->data.size(), t.data.size());
        for (std::size_t i = 0; i < t.data.size(); ++i)
            CHECK(lt->data[i] == t.data[i] || (std::isnan(lt->data[i]) && std::isnan(t.data[i])));
    }

    // A frame for another layout does not fit.
    core::live::Frame wrong;
    wrong.values.resize(5);
    CHECK_EQ(core::live::appendFrame(live, wrong), -1);
    CHECK_EQ(live.periodCount(), 4);
    // A frame repeating the latest time replaces that period.
    core::live::Frame again = core::live::parseFrameLine(core::live::frameLine(r, 0, 7, 3, 1800.0));
    CHECK_EQ(core::live::appendFrame(live, again), 3);
    CHECK_EQ(live.periodCount(), 4);
    CHECK_NEAR(live.table("node")->value(3, 0, 2), 2.25, 1e-6);
    core::ResultSet other = r;
    other.table("link")->variables[0].unit = "GPM";
    CHECK(!core::live::sameLayout(other, r));
    bool threw = false;
    try
    {
        core::live::parseFrameLine("{\"event\":\"frame\",\"layout\":1,\"period\":0,\"time\":0,\"data\":\"#\"}");
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("model: report start offsets")
{
    auto swmm = core::Model::fromFile(example("swmm/mirab-urban.inp"));
    CHECK_NEAR(swmm.reportStartSeconds(), 0.0, 1e-9);
    swmm.setOption("REPORT_START_TIME", "01:30:00");
    CHECK_NEAR(swmm.reportStartSeconds(), 5400.0, 1e-9);
    swmm.setOption("REPORT_START_DATE", "01/02/2026");
    CHECK_NEAR(swmm.reportStartSeconds(), 86400.0 + 5400.0, 1e-9);
    const auto epanet = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    CHECK_NEAR(epanet.reportStartSeconds(), 0.0, 1e-9);
}

TEST_CASE("settings: sidecar file round trip")
{
    const auto inp = scratch("settings-model.inp");
    core::ProjectSettings s;
    s.engineId = "epanet-2.3-owa";
    s.useMsx = true;
    s.msxFile = "model.msx";
    s.crs = "EPSG:32614";
    s.saveFor(inp);
    const auto back = core::ProjectSettings::loadFor(inp);
    CHECK_EQ(back.engineId, s.engineId);
    CHECK(back.useMsx);
    CHECK_EQ(back.crs, s.crs);
}

TEST_CASE("hash: SHA-256 test vectors")
{
    CHECK_EQ(core::sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(core::sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(core::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    const std::string million(1000000, 'a');
    CHECK_EQ(core::sha256Hex(million), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
