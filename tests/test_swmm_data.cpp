/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_swmm_data.cpp
 * Purpose: Tests of the SWMM data schema: named objects, keyed data rows, renames, removals and checks.
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

#include <filesystem>
#include <fstream>
#include <map>

using namespace mirab;
namespace fs = std::filesystem;

namespace
{
fs::path example(const char* rel)
{
    return fs::path(MIRAB_SOURCE_DIR) / "examples" / rel;
}

/// Tokens of the rows of @p section whose first token is @p key.
std::vector<std::vector<std::string>> rowsWithKey(const core::Model& m, const char* section, const char* key)
{
    std::vector<std::vector<std::string>> out;
    if (const auto* s = m.document().find(section))
    {
        for (auto& t : s->allRowTokens())
        {
            if (!t.empty() && t[0] == key)
                out.push_back(t);
        }
    }
    return out;
}

bool hasError(const core::Model& m, const std::string& part)
{
    for (const auto& i : m.validate())
    {
        if (i.severity == core::ModelIssue::Severity::Error && i.message.find(part) != std::string::npos)
            return true;
    }
    return false;
}

std::size_t errorCount(const core::Model& m)
{
    std::size_t n = 0;
    for (const auto& i : m.validate())
        n += i.severity == core::ModelIssue::Severity::Error ? 1 : 0;
    return n;
}
} // namespace

TEST_CASE("swmm: quality example round trips byte for byte")
{
    std::ifstream in(example("swmm/mirab-quality.inp"), std::ios::binary);
    std::string original((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_EQ(core::InpDocument::parse(original).toString(), original);
    CHECK_EQ(core::Model::fromText(original).toText(), original);
}

TEST_CASE("swmm: schema layouts resolve variants and repeated columns")
{
    const auto* cov = core::findSectionDef(core::Dialect::Swmm, "COVERAGES");
    CHECK(cov && cov->keyCategory == core::Category::Area);
    const auto l = cov->layout({"S1", "A", "50", "B", "50"});
    CHECK_EQ(l.column(3)->name, std::string("Landuse"));
    CHECK_EQ(l.column(4)->name, std::string("Percent"));
    CHECK_EQ(l.columnName(3), std::string("Landuse 2"));
    const auto* snow = core::findSectionDef(core::Dialect::Swmm, "SNOWPACKS");
    CHECK(snow && snow->objectKind == "Snowpack");
    const auto removal = snow->layout({"SP", "REMOVAL", "1", "0", "0", "0", "0", "0", "S1"});
    CHECK(removal.column(8)->type == core::ColumnType::AreaRef);
    CHECK_EQ(snow->layout({"SP", "PLOWABLE"}).column(8)->name, std::string("SNN0"));
    const auto* inlets = core::findSectionDef(core::Dialect::Swmm, "INLETS");
    CHECK(inlets->layout({"I", "CUSTOM", "C1"}).column(2)->type == core::ColumnType::CurveRef);
    CHECK_EQ(inlets->layout({"I", "CURB", "1", "0.5"}).columns.size(), std::size_t(5));
    const auto* inflows = core::findSectionDef(core::Dialect::Swmm, "INFLOWS");
    CHECK(inflows->keyCategory == core::Category::Node && inflows->multiRowPerKey);
    CHECK_EQ(inflows->columns[2].blank, std::string("\"\""));
    CHECK_EQ(core::objectKinds(core::Dialect::Swmm).size(), std::size_t(10));
    for (const auto& kind : core::objectKinds(core::Dialect::Swmm))
        CHECK(core::sectionForObject(core::Dialect::Swmm, kind) != nullptr);
    CHECK(core::objectKinds(core::Dialect::Epanet).empty());
}

TEST_CASE("swmm: water quality, groundwater and RDII data")
{
    const auto m = core::Model::fromFile(example("swmm/mirab-quality.inp"));
    CHECK(m.dialect() == core::Dialect::Swmm);
    CHECK(m.validate().empty());
    CHECK((m.objectNames("Pollutant") == std::vector<std::string>{"TSS", "Lead"}));
    CHECK_EQ(m.objectNames("Landuse").size(), std::size_t(2));
    CHECK_EQ(m.objectNames("Aquifer").size(), std::size_t(1));
    CHECK_EQ(m.objectNames("Hydrograph").size(), std::size_t(1));
    std::map<std::string, int> j1, s1;
    for (const auto& r : m.rowsFor(core::Category::Node, "J1"))
        ++j1[r.section];
    for (const auto& r : m.rowsFor(core::Category::Area, "S1"))
        ++s1[r.section];
    CHECK_EQ(j1["DWF"], 2);
    CHECK_EQ(s1["GROUNDWATER"], 1);
    CHECK_EQ(s1["GWF"], 1);
    CHECK_EQ(s1["COVERAGES"], 1);
    CHECK_EQ(s1["LOADINGS"], 1);
    std::map<std::string, int> lu;
    for (const auto& r : m.objectRows("Landuse", "Residential"))
        ++lu[r.section];
    CHECK_EQ(lu["LANDUSES"], 1);
    CHECK_EQ(lu["BUILDUP"], 2);
    CHECK_EQ(lu["WASHOFF"], 2);
    CHECK_EQ(m.rowsOfKey("HYDROGRAPHS", "UH1").size(), std::size_t(3));
}

TEST_CASE("swmm: renaming objects updates every reference")
{
    auto m = core::Model::fromFile(example("swmm/mirab-quality.inp"));
    std::string err;
    CHECK(!m.renameObject("Pollutant", "TSS", "Lead", &err));
    CHECK(!m.renameObject("Pollutant", "TSS", "bad name", &err));
    CHECK(m.renameObject("Pollutant", "TSS", "SS", &err));
    CHECK((m.objectNames("Pollutant") == std::vector<std::string>{"SS", "Lead"}));
    CHECK_EQ(rowsWithKey(m, "INFLOWS", "J2")[1][1], std::string("SS"));
    CHECK_EQ(rowsWithKey(m, "DWF", "J1")[1][1], std::string("SS"));
    CHECK_EQ(rowsWithKey(m, "BUILDUP", "Commercial")[0][1], std::string("SS"));
    CHECK_EQ(rowsWithKey(m, "WASHOFF", "Residential")[0][1], std::string("SS"));
    CHECK_EQ(rowsWithKey(m, "LOADINGS", "S3")[0][1], std::string("SS"));
    CHECK_EQ(rowsWithKey(m, "POLLUTANTS", "Lead")[0][7], std::string("SS"));
    const auto treatment = rowsWithKey(m, "TREATMENT", "J5");
    CHECK_EQ(treatment[0][1], std::string("SS"));
    CHECK_EQ(treatment[1].back(), std::string("R_SS"));
    CHECK(m.renameObject("Landuse", "Commercial", "Shops", &err));
    CHECK_EQ(rowsWithKey(m, "COVERAGES", "S1")[0][3], std::string("Shops"));
    CHECK_EQ(rowsWithKey(m, "BUILDUP", "Shops").size(), std::size_t(2));
    CHECK(m.renameObject("Aquifer", "GW1", "Sand", &err));
    CHECK_EQ(rowsWithKey(m, "GROUNDWATER", "S1")[0][1], std::string("Sand"));
    CHECK(m.renameObject("Hydrograph", "UH1", "UH9", &err));
    CHECK_EQ(rowsWithKey(m, "RDII", "J4")[0][1], std::string("UH9"));
    CHECK_EQ(rowsWithKey(m, "HYDROGRAPHS", "UH9").size(), std::size_t(3));
    CHECK(m.validate().empty());
    // Element renames reach the keyed data sections too.
    CHECK(m.renameElement(core::Category::Node, "J1", "JA", &err));
    CHECK_EQ(rowsWithKey(m, "DWF", "JA").size(), std::size_t(2));
    CHECK_EQ(rowsWithKey(m, "GROUNDWATER", "S1")[0][2], std::string("JA"));
    CHECK(m.renameElement(core::Category::Area, "S1", "SA", &err));
    CHECK_EQ(rowsWithKey(m, "COVERAGES", "SA").size(), std::size_t(1));
    CHECK_EQ(rowsWithKey(m, "GWF", "SA").size(), std::size_t(1));
    CHECK(m.renameElement(core::Category::Gage, "RG1", "RG9", &err));
    CHECK_EQ(rowsWithKey(m, "HYDROGRAPHS", "UH9")[0][1], std::string("RG9"));
    CHECK(m.validate().empty());
    CHECK(core::Model::fromText(m.toText()).validate().empty());
}

TEST_CASE("swmm: removing objects removes the data that depends on them")
{
    auto m = core::Model::fromFile(example("swmm/mirab-quality.inp"));
    CHECK(m.removeObject("Pollutant", "TSS"));
    CHECK_EQ(rowsWithKey(m, "INFLOWS", "J2").size(), std::size_t(1));
    CHECK_EQ(rowsWithKey(m, "DWF", "J1").size(), std::size_t(1));
    CHECK_EQ(rowsWithKey(m, "BUILDUP", "Residential").size(), std::size_t(1));
    CHECK(rowsWithKey(m, "LOADINGS", "S1").empty());
    CHECK((rowsWithKey(m, "LOADINGS", "S3")[0] == std::vector<std::string>{"S3", "Lead", "0.5"}));
    CHECK_EQ(rowsWithKey(m, "TREATMENT", "J5").size(), std::size_t(1));
    CHECK_EQ(rowsWithKey(m, "POLLUTANTS", "Lead")[0][7], std::string("*"));
    CHECK(m.removeObject("Landuse", "Commercial"));
    CHECK((rowsWithKey(m, "COVERAGES", "S1")[0] == std::vector<std::string>{"S1", "Residential", "80"}));
    CHECK((rowsWithKey(m, "COVERAGES", "S3")[0] == std::vector<std::string>{"S3", "Residential", "30"}));
    CHECK(rowsWithKey(m, "WASHOFF", "Commercial").empty());
    CHECK(m.removeObject("Aquifer", "GW1"));
    CHECK(rowsWithKey(m, "GROUNDWATER", "S1").empty());
    CHECK(!m.removeObject("Aquifer", "GW1"));
    CHECK(m.removeElement(core::Category::Node, "J4"));
    CHECK(rowsWithKey(m, "RDII", "J4").empty());
    CHECK(m.removeElement(core::Category::Area, "S1"));
    CHECK(rowsWithKey(m, "GWF", "S1").empty());
    CHECK(rowsWithKey(m, "COVERAGES", "S1").empty());
    CHECK(m.removeObject("Hydrograph", "UH1"));
    CHECK_EQ(m.document().find("HYDROGRAPHS")->rowCount(), 0);
    // Only the outlet of S3 (the removed node J4) is left dangling.
    CHECK(hasError(m, "drains to unknown outlet J4"));
    CHECK_EQ(errorCount(m), std::size_t(1));
}

TEST_CASE("swmm: dangling references are reported")
{
    auto m = core::Model::fromFile(example("swmm/mirab-quality.inp"));
    m.document().find("INFLOWS")->appendRow({"J3", "Zinc", "\"\"", "CONCEN", "1", "1", "5"});
    m.document().find("COVERAGES")->appendRow({"S2", "Parks", "10"});
    m.document().find("GROUNDWATER")->appendRow({"S2", "GW1", "NOPE", "100", "0", "0", "0", "0", "0", "0"});
    m.document().find("POLLUTANTS")->appendRow({"Lead", "MG/L", "0", "0", "0", "0"});
    m.rebuild();
    CHECK(hasError(m, "unknown pollutant Zinc"));
    CHECK(hasError(m, "unknown land use Parks"));
    CHECK(hasError(m, "unknown node NOPE"));
    CHECK(hasError(m, "duplicate pollutant name Lead"));
    CHECK_EQ(errorCount(m), std::size_t(4));
}

TEST_CASE("swmm: objects and data rows are created with defaults")
{
    auto m = core::Model::newProject(core::Dialect::Swmm, "CMS");
    std::string err;
    CHECK(!m.addObject("Hydrograph", "UH1", &err)); // needs a rain gage
    CHECK(!m.addObject("Rule", "R1", &err));        // needs a regulator link
    CHECK(m.addGage("RG1", {0, 0}, &err));
    CHECK(m.addNode("Junction", "J1", {0, 0}, &err));
    CHECK(m.addNode("Junction", "J2", {10, 0}, &err));
    CHECK(m.addLink("Orifice", "OR1", "J1", "J2", &err));
    for (const auto& kind : core::objectKinds(core::Dialect::Swmm))
    {
        const std::string name = m.uniqueObjectName(kind, "X");
        CHECK(m.addObject(kind, name, &err));
        CHECK(m.objectExists(kind, name));
        CHECK(!m.addObject(kind, name, &err));
    }
    CHECK_EQ(m.uniqueObjectName("Pollutant", "X"), std::string("X2"));
    CHECK(m.addObject("Rule", "R2", &err));
    CHECK_EQ(m.objectNames("Rule").size(), std::size_t(2));
    CHECK_EQ(m.rowsOfKey("CONTROLS", "X1").size(), std::size_t(3));
    CHECK_EQ(m.rowsOfKey("TRANSECTS", "X1").size(), std::size_t(3));
    CHECK_EQ(m.rowsOfKey("SNOWPACKS", "X1").size(), std::size_t(4));
    CHECK_EQ(errorCount(m), std::size_t(0));
    CHECK(m.renameObject("Rule", "R2", "R3", &err));
    CHECK(m.renameObject("Transect", "X1", "T1", &err));
    CHECK((m.objectNames("Transect") == std::vector<std::string>{"T1"}));
    CHECK(m.removeObject("Rule", "X1"));
    CHECK((m.objectNames("Rule") == std::vector<std::string>{"R3"}));

    const auto* inflows = core::findSectionDef(core::Dialect::Swmm, "INFLOWS");
    const auto row = m.newDataRow(*inflows, "J1");
    CHECK_EQ(row[1], std::string("FLOW"));
    std::string missing;
    const auto fresh = core::Model::newProject(core::Dialect::Swmm, "CMS");
    CHECK(fresh.newDataRow(*core::findSectionDef(core::Dialect::Swmm, "TREATMENT"), "J1", &missing).empty());
    CHECK_EQ(missing, std::string("Pollutant"));
    const auto cov = m.newDataRow(*core::findSectionDef(core::Dialect::Swmm, "COVERAGES"), "S1");
    CHECK_EQ(cov[1], std::string("X1"));

    // Blank values inside a row use the token the engine expects.
    m.document().ensure("INFLOWS").appendRow(row);
    m.rebuild();
    const int r = m.document().find("INFLOWS")->rowCount() - 1;
    CHECK(m.setToken("INFLOWS", r, 2, "TS1"));
    CHECK(m.setToken("INFLOWS", r, 2, ""));
    CHECK_EQ(m.document().find("INFLOWS")->rowTokens(r)[2], std::string("\"\""));
}

TEST_CASE("swmm: replacing rows keeps unchanged lines and comments")
{
    auto m = core::Model::fromFile(example("swmm/mirab-quality.inp"));
    const std::string before = m.toText();
    auto rows = m.rowsOfKey("BUILDUP", "Residential");
    m.replaceRows("BUILDUP", rows,
                  {{"Residential", "TSS", "SAT", "50", "0", "2", "AREA"},
                   {"Residential", "Lead", "NONE", "0", "0", "0", "AREA"}});
    CHECK_EQ(m.toText(), before);
    m.replaceRows("BUILDUP", rows, {{"Residential", "TSS", "SAT", "60", "0", "2", "AREA"}});
    CHECK_EQ(rowsWithKey(m, "BUILDUP", "Residential").size(), std::size_t(1));
    CHECK_EQ(rowsWithKey(m, "BUILDUP", "Residential")[0][3], std::string("60"));
    rows = m.rowsOfKey("BUILDUP", "Residential");
    m.replaceRows("BUILDUP", rows,
                  {{"Residential", "TSS", "SAT", "60", "0", "2", "AREA"},
                   {"Residential", "Lead", "POW", "1", "1", "1", "CURB"}});
    CHECK_EQ(rowsWithKey(m, "BUILDUP", "Residential").size(), std::size_t(2));
    CHECK_EQ(m.document().find("BUILDUP")->rowTokens(1)[1], std::string("Lead"));
}
