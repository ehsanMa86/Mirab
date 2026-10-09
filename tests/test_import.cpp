/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_import.cpp
 * Purpose: Tests for the shapefile, DXF and CSV importers.
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
#include "mirab/core/Text.h"
#include "mirab/interop/Import.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace mirab;
using core::Category;
using core::Point;
using interop::GeometryType;
namespace fs = std::filesystem;

namespace
{

fs::path scratchDir(const char* name)
{
    const auto dir = fs::temp_directory_path() / "mirab-tests" / "import" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
}

/// Appends binary values in little- or big-endian byte order.
struct Bytes
{
    std::string b;
    void u8(unsigned v) { b += static_cast<char>(v & 0xFF); }
    void le16(unsigned v)
    {
        u8(v);
        u8(v >> 8);
    }
    void le32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            u8(v >> (8 * i));
    }
    void be32(std::uint32_t v)
    {
        for (int i = 3; i >= 0; --i)
            u8(v >> (8 * i));
    }
    void dbl(double d)
    {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &d, sizeof bits);
        for (int i = 0; i < 8; ++i)
            u8(static_cast<unsigned>(bits >> (8 * i)));
    }
    void raw(const std::string& s) { b += s; }
};

/// One shapefile record: parts of points (a single point for point shapes)
/// and optional Z values; a record without parts is a null shape.
struct ShpRecord
{
    std::vector<std::vector<Point>> parts;
    std::vector<double> z;
};

/// Writes a .shp file following the ESRI Shapefile Technical Description (1998).
void writeShp(const fs::path& path, int shapeType, const std::vector<ShpRecord>& records)
{
    Bytes body;
    std::uint32_t recNo = 0;
    for (const auto& r : records)
    {
        Bytes c;
        if (r.parts.empty())
        {
            c.le32(0);
        }
        else if (shapeType == 1 || shapeType == 11)
        {
            c.le32(static_cast<std::uint32_t>(shapeType));
            c.dbl(r.parts[0][0].x);
            c.dbl(r.parts[0][0].y);
            if (shapeType == 11)
            {
                c.dbl(r.z.empty() ? 0.0 : r.z[0]);
                c.dbl(0.0); // M
            }
        }
        else
        {
            std::size_t n = 0;
            for (const auto& p : r.parts)
                n += p.size();
            c.le32(static_cast<std::uint32_t>(shapeType));
            for (int k = 0; k < 4; ++k)
                c.dbl(0.0); // bounding box (not used by readers)
            c.le32(static_cast<std::uint32_t>(r.parts.size()));
            c.le32(static_cast<std::uint32_t>(n));
            std::size_t first = 0;
            for (const auto& p : r.parts)
            {
                c.le32(static_cast<std::uint32_t>(first));
                first += p.size();
            }
            for (const auto& part : r.parts)
            {
                for (const auto& p : part)
                {
                    c.dbl(p.x);
                    c.dbl(p.y);
                }
            }
            if (shapeType == 13 || shapeType == 15)
            {
                c.dbl(0.0);
                c.dbl(0.0);
                for (std::size_t i = 0; i < n; ++i)
                    c.dbl(i < r.z.size() ? r.z[i] : 0.0);
            }
        }
        body.be32(++recNo);
        body.be32(static_cast<std::uint32_t>(c.b.size() / 2));
        body.raw(c.b);
    }
    Bytes h;
    h.be32(9994);
    for (int i = 0; i < 5; ++i)
        h.be32(0);
    h.be32(static_cast<std::uint32_t>((100 + body.b.size()) / 2));
    h.le32(1000);
    h.le32(static_cast<std::uint32_t>(shapeType));
    for (int i = 0; i < 8; ++i)
        h.dbl(0.0);
    writeFile(path, h.b + body.b);
}

struct DbfField
{
    std::string name;
    char type;
    unsigned length;
    unsigned decimals;
};

/// Writes a dBASE III table with the given (already encoded) values.
void writeDbf(const fs::path& path, const std::vector<DbfField>& fields,
              const std::vector<std::vector<std::string>>& rows)
{
    Bytes d;
    unsigned recordLen = 1;
    for (const auto& f : fields)
        recordLen += f.length;
    d.u8(0x03);
    d.u8(126);
    d.u8(10);
    d.u8(9);
    d.le32(static_cast<std::uint32_t>(rows.size()));
    d.le16(32 + 32 * static_cast<unsigned>(fields.size()) + 1);
    d.le16(recordLen);
    for (int i = 0; i < 20; ++i)
        d.u8(0);
    for (const auto& f : fields)
    {
        std::string name = f.name;
        name.resize(11, '\0');
        d.raw(name);
        d.u8(static_cast<unsigned char>(f.type));
        d.le32(0);
        d.u8(f.length);
        d.u8(f.decimals);
        for (int i = 0; i < 14; ++i)
            d.u8(0);
    }
    d.u8(0x0D);
    for (const auto& row : rows)
    {
        d.u8(' ');
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            std::string v = i < row.size() ? row[i] : std::string();
            v.resize(std::min<std::size_t>(v.size(), fields[i].length));
            const std::string pad(fields[i].length - v.size(), ' ');
            d.raw(fields[i].type == 'C' ? v + pad : pad + v);
        }
    }
    d.u8(0x1A);
    writeFile(path, d.b);
}

std::string tokenOf(const core::Model& m, Category c, const std::string& id, const char* section,
                    std::size_t column)
{
    for (const auto& r : m.rowsFor(c, id))
    {
        if (r.section == section)
            return column < r.tokens.size() ? r.tokens[column] : std::string();
    }
    return {};
}

/// Round-trips the model text and checks that it validates without errors.
void checkModel(const core::Model& m)
{
    const std::string textValue = m.toText();
    const core::Model again = core::Model::fromText(textValue);
    CHECK(again.dialect() == m.dialect());
    CHECK_EQ(again.nodes().size(), m.nodes().size());
    CHECK_EQ(again.links().size(), m.links().size());
    CHECK_EQ(again.areas().size(), m.areas().size());
    CHECK(again.toText() == textValue);
    for (const auto& issue : again.validate())
    {
        if (issue.severity == core::ModelIssue::Severity::Error)
            throw test::Failure("validation error: " + issue.message);
    }
}

interop::ImportedFeature feature(std::vector<Point> pts, std::vector<std::string> values)
{
    interop::ImportedFeature f;
    f.points = std::move(pts);
    f.values = std::move(values);
    return f;
}

bool hasNote(const interop::ImportReport& r, const std::string& part)
{
    for (const auto& n : r.notes)
    {
        if (n.find(part) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("import: shapefile points with Z, attributes, Latin-1 and .prj")
{
    const auto dir = scratchDir("points");
    std::vector<ShpRecord> recs = {{{{{100.5, 200.25}}}, {12.5}}, {}, {{{{300, 400}}}, {7}}};
    writeShp(dir / "hydrants.shp", 11, recs);
    // An upper-case sibling extension is found as well.
    writeDbf(dir / "hydrants.DBF",
             {{"ID", 'C', 10, 0}, {"ELEV", 'N', 10, 3}, {"NAME", 'C', 12, 0}, {"OK", 'L', 1, 0}},
             {{"H1", "12.500", "Caf\xE9", "T"}, {"H2", "1", "null shape", "F"}, {"H3", "**********", "", "?"}});
    writeFile(dir / "hydrants.cpg", "ISO-8859-1\r\n");
    writeFile(dir / "hydrants.prj", "PROJCS[\"WGS 84 / UTM zone 14N\"]");

    const auto layer = interop::readShapefile(dir / "hydrants.shp");
    CHECK_EQ(layer.name, std::string("hydrants"));
    CHECK(layer.geometry == GeometryType::Point);
    CHECK_EQ(layer.fields.size(), std::size_t(5));
    CHECK_EQ(layer.fields[4], std::string("Z"));
    CHECK_EQ(layer.crsText, std::string("PROJCS[\"WGS 84 / UTM zone 14N\"]"));
    CHECK_EQ(layer.features.size(), std::size_t(2)); // the null shape is dropped
    const auto& f0 = layer.features[0];
    CHECK_NEAR(f0.points[0].x, 100.5, 1e-12);
    CHECK_NEAR(f0.points[0].y, 200.25, 1e-12);
    CHECK_EQ(f0.values[0], std::string("H1"));
    CHECK_EQ(f0.values[1], std::string("12.500"));
    CHECK_EQ(f0.values[2], std::string("Caf\xC3\xA9")); // converted to UTF-8
    CHECK_EQ(f0.values[3], std::string("true"));
    CHECK_EQ(f0.values[4], std::string("12.5"));
    // Attributes stay aligned with their shapes after the null record.
    const auto& f1 = layer.features[1];
    CHECK_EQ(f1.values[0], std::string("H3"));
    CHECK_EQ(f1.values[1], std::string(""));
    CHECK_EQ(f1.values[3], std::string(""));
    CHECK_EQ(f1.values[4], std::string("7"));
    CHECK_EQ(layer.fieldIndex("elev"), 1);
}

TEST_CASE("import: shapefile UTF-8 text without .cpg")
{
    const auto dir = scratchDir("utf8");
    writeShp(dir / "n.shp", 1, {{{{{1, 2}}}, {}}});
    writeDbf(dir / "n.dbf", {{"NAME", 'C', 20, 0}}, {{"Stra\xC3\x9F" "e"}});
    const auto layer = interop::readShapefile(dir / "n.shp");
    CHECK_EQ(layer.features.size(), std::size_t(1));
    CHECK_EQ(layer.features[0].values[0], std::string("Stra\xC3\x9F" "e"));
    CHECK(layer.crsText.empty());
}

TEST_CASE("import: shapefile multi-part polylines and polygons")
{
    const auto dir = scratchDir("lines");
    std::vector<ShpRecord> lines = {
        {{{{0, 0}, {10, 0}, {10, 10}}}, {}},
        {{{{20, 0}, {30, 0}}, {{40, 0}, {50, 0}, {50, 5}}}, {}},
    };
    writeShp(dir / "mains.shp", 13, lines); // PolyLineZ
    writeDbf(dir / "mains.dbf", {{"PIPE_ID", 'C', 8, 0}, {"DIAMETER", 'N', 8, 0}},
             {{"A", "150"}, {"B", "200"}});
    const auto ll = interop::readShapefile(dir / "mains.shp");
    CHECK(ll.geometry == GeometryType::Line);
    CHECK_EQ(ll.features.size(), std::size_t(3));
    CHECK_EQ(ll.features[0].points.size(), std::size_t(3));
    CHECK_EQ(ll.features[1].values[0], std::string("B"));
    CHECK_EQ(ll.features[2].values[0], std::string("B"));
    CHECK_EQ(ll.features[2].values[1], std::string("200"));
    CHECK_EQ(ll.features[2].points.size(), std::size_t(3));

    // Outer rings are clockwise and holes counter-clockwise; rings are closed.
    std::vector<Point> outer = {{0, 0}, {0, 100}, {100, 100}, {100, 0}, {0, 0}};
    std::vector<Point> hole = {{10, 10}, {20, 10}, {20, 20}, {10, 20}, {10, 10}};
    std::vector<Point> outer2 = {{200, 0}, {200, 50}, {250, 50}, {200, 0}};
    std::vector<ShpRecord> polys = {{{outer, hole}, {}}, {{outer, outer2}, {}}};
    writeShp(dir / "areas.shp", 5, polys);
    const auto pl = interop::readShapefile(dir / "areas.shp");
    CHECK(pl.geometry == GeometryType::Polygon);
    CHECK(pl.fields.empty());
    CHECK_EQ(pl.features.size(), std::size_t(3));
    CHECK_EQ(pl.features[0].points.size(), std::size_t(4)); // closing point and hole dropped
    CHECK_EQ(pl.features[2].points.size(), std::size_t(3));

    bool threw = false;
    try
    {
        writeFile(dir / "bad.shp", "not a shapefile");
        interop::readShapefile(dir / "bad.shp");
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("import: DXF lines, polylines and points on two layers")
{
    const auto dir = scratchDir("dxf");
    std::string d;
    auto g = [&](int code, const std::string& value) {
        d += "  " + std::to_string(code) + "\r\n" + value + "\r\n";
    };
    g(999, "test drawing");
    g(0, "SECTION");
    g(2, "HEADER");
    g(9, "$ACADVER");
    g(1, "AC1015");
    g(0, "ENDSEC");
    g(0, "SECTION");
    g(2, "BLOCKS");
    g(0, "BLOCK");
    g(8, "0");
    g(2, "VALVE");
    g(0, "LINE"); // part of a block definition, not of the drawing
    g(8, "PIPES");
    g(10, "0");
    g(20, "0");
    g(11, "1");
    g(21, "1");
    g(0, "ENDBLK");
    g(0, "ENDSEC");
    g(0, "SECTION");
    g(2, "ENTITIES");
    g(0, "LINE");
    g(5, "1A");
    g(8, "PIPES");
    g(10, "0.0");
    g(20, "0.0");
    g(30, "5.5");
    g(11, "100.0");
    g(21, "0.0");
    g(31, "6.0");
    g(0, "LWPOLYLINE");
    g(5, "1B");
    g(8, "pipes"); // same layer, different letter case
    g(90, "3");
    g(70, "0");
    g(10, "100");
    g(20, "0");
    g(10, "150");
    g(20, "50");
    g(10, "200");
    g(20, "50");
    g(0, "LWPOLYLINE");
    g(5, "1C");
    g(8, "PIPES");
    g(90, "4");
    g(70, "1");
    g(38, "2.5");
    g(10, "0");
    g(20, "0");
    g(10, "10");
    g(20, "0");
    g(10, "10");
    g(20, "10");
    g(10, "0");
    g(20, "10");
    g(0, "POLYLINE");
    g(5, "1D");
    g(8, "PIPES");
    g(66, "1");
    g(10, "0");
    g(20, "0");
    g(30, "0");
    g(70, "8");
    g(0, "VERTEX");
    g(8, "PIPES");
    g(10, "200");
    g(20, "50");
    g(30, "4");
    g(0, "VERTEX");
    g(8, "PIPES");
    g(10, "300");
    g(20, "50");
    g(30, "3");
    g(0, "SEQEND");
    g(8, "PIPES");
    g(0, "POINT");
    g(5, "2A");
    g(8, "Nodes");
    g(10, "100");
    g(20, "0");
    g(30, "12");
    g(0, "CIRCLE");
    g(8, "Nodes");
    g(10, "200");
    g(20, "50");
    g(30, "11");
    g(40, "1.5");
    g(0, "INSERT");
    g(8, "Nodes");
    g(2, "VALVE");
    g(10, "300");
    g(20, "50");
    g(0, "TEXT");
    g(8, "Nodes");
    g(10, "5");
    g(20, "5");
    g(1, "label");
    g(0, "ENDSEC");
    g(0, "EOF");
    writeFile(dir / "net.dxf", d);

    const auto layers = interop::readDxf(dir / "net.dxf");
    CHECK_EQ(layers.size(), std::size_t(3));
    const interop::ImportedLayer* lines = nullptr;
    const interop::ImportedLayer* polygons = nullptr;
    const interop::ImportedLayer* points = nullptr;
    for (const auto& l : layers)
    {
        if (l.geometry == GeometryType::Line)
            lines = &l;
        else if (l.geometry == GeometryType::Polygon)
            polygons = &l;
        else
            points = &l;
    }
    CHECK(lines && polygons && points);
    CHECK_EQ(lines->name, std::string("PIPES (lines)"));
    CHECK_EQ(lines->features.size(), std::size_t(3));
    CHECK_EQ(lines->fields[0], std::string("Layer"));
    CHECK_EQ(lines->features[0].values[1], std::string("1A"));
    CHECK_EQ(lines->features[0].values[2], std::string("5.5"));
    CHECK_EQ(lines->features[1].points.size(), std::size_t(3));
    CHECK_EQ(lines->features[2].points.size(), std::size_t(2));
    CHECK_EQ(lines->features[2].values[2], std::string("4"));
    CHECK_EQ(polygons->features.size(), std::size_t(1));
    CHECK_EQ(polygons->features[0].points.size(), std::size_t(4));
    CHECK_EQ(polygons->features[0].values[2], std::string("2.5"));
    CHECK_EQ(points->name, std::string("Nodes (points)"));
    CHECK_EQ(points->features.size(), std::size_t(3));
    CHECK_EQ(points->fields.back(), std::string("Block"));
    CHECK_EQ(points->features[2].values[3], std::string("VALVE"));
    CHECK_NEAR(points->features[1].points[0].x, 200.0, 1e-12);

    // A mirrored object coordinate system (extrusion 0,0,-1) flips X.
    std::string m;
    auto h = [&](int code, const std::string& value) { m += std::to_string(code) + "\n" + value + "\n"; };
    h(0, "SECTION");
    h(2, "ENTITIES");
    h(0, "CIRCLE");
    h(8, "M");
    h(10, "5");
    h(20, "7");
    h(230, "-1");
    h(0, "ENDSEC");
    h(0, "EOF");
    writeFile(dir / "mirror.dxf", m);
    const auto mirrored = interop::readDxf(dir / "mirror.dxf");
    CHECK_EQ(mirrored.size(), std::size_t(1));
    CHECK_NEAR(mirrored[0].features[0].points[0].x, -5.0, 1e-12);
    CHECK_NEAR(mirrored[0].features[0].points[0].y, 7.0, 1e-12);

    writeFile(dir / "binary.dxf", std::string("AutoCAD Binary DXF\r\n\x1a\0", 22) + "xxxx");
    bool threw = false;
    try
    {
        interop::readDxf(dir / "binary.dxf");
    }
    catch (const std::runtime_error& e)
    {
        threw = std::string(e.what()).find("binary") != std::string::npos;
    }
    CHECK(threw);
}

TEST_CASE("import: CSV with BOM, semicolons, quotes and CRLF")
{
    const auto dir = scratchDir("csv");
    writeFile(dir / "nodes.csv", "\xEF\xBB\xBFNode ID;Easting;Northing;Note;Elev\r\n"
                                 "N1;1000,5;2000;\"a;b\";10\r\n"
                                 "N2;1100;2100;\"say \"\"hi\"\"\r\nnext line\";12,5\r\n"
                                 "\r\n"
                                 "N3;;;plain;\r\n");
    const auto layer = interop::readCsv(dir / "nodes.csv");
    CHECK_EQ(layer.name, std::string("nodes"));
    CHECK_EQ(layer.fields.size(), std::size_t(5));
    CHECK_EQ(layer.fields[0], std::string("Node ID"));
    CHECK_EQ(layer.features.size(), std::size_t(3));
    CHECK_EQ(layer.features[0].points.size(), std::size_t(1));
    CHECK_NEAR(layer.features[0].points[0].x, 1000.5, 1e-12);
    CHECK_EQ(layer.features[0].values[3], std::string("a;b"));
    CHECK_EQ(layer.features[1].values[3], std::string("say \"hi\"\r\nnext line"));
    CHECK(layer.features[2].points.empty());

    writeFile(dir / "links.csv", "id,from,to,diameter\n\"P 1\",N1,N2,150\nP2,N2,N3,200\n");
    const auto links = interop::readCsv(dir / "links.csv");
    CHECK_EQ(links.features.size(), std::size_t(2));
    CHECK(links.features[0].points.empty());
    CHECK_EQ(links.features[0].values[0], std::string("P 1"));

    writeFile(dir / "tabs.txt", "name\tlon\tlat\nA\t-97.5\t30.25\n");
    const auto tabs = interop::readCsv(dir / "tabs.txt");
    CHECK_NEAR(tabs.features[0].points[0].y, 30.25, 1e-12);

    interop::CsvOptions opt;
    opt.xField = "Elev";
    opt.yField = "Elev";
    const auto custom = interop::readCsv(dir / "nodes.csv", opt);
    CHECK_NEAR(custom.features[1].points[0].x, 12.5, 1e-12);
    opt.xField = "missing";
    bool threw = false;
    try
    {
        interop::readCsv(dir / "nodes.csv", opt);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("import: EPANET junctions from a point shapefile")
{
    const auto dir = scratchDir("junctions");
    writeShp(dir / "j.shp", 1,
             {{{{{0, 0}}}, {}}, {{{{100, 0}}}, {}}, {{{{100, 100}}}, {}}, {{{{5, 5}}}, {}}, {{{{7, 7}}}, {}}});
    writeDbf(dir / "j.dbf", {{"FACILITYID", 'C', 10, 0}, {"ELEV", 'C', 8, 0}, {"DEMAND", 'N', 8, 2}},
             {{"J1", "10", "1.5"}, {"J2", "12.5", ""}, {"J3", "abc", "2"}, {"J1", "9", "0"}, {"", "8", "0"}});
    const auto layer = interop::readShapefile(dir / "j.shp");

    auto model = core::Model::newProject(core::Dialect::Epanet, "LPS");
    const auto mapping = interop::suggestMapping(model, layer, Category::Node, "Junction");
    CHECK_EQ(mapping.idField, std::string("FACILITYID"));
    CHECK_EQ(mapping.idPrefix, std::string("J"));
    CHECK_EQ(mapping.properties.at("Elevation"), std::string("ELEV"));
    CHECK_EQ(mapping.properties.at("Demand"), std::string("DEMAND"));

    const auto report = interop::importLayer(model, layer, mapping);
    CHECK_EQ(report.nodesAdded, 4);
    CHECK_EQ(report.skipped, 1);
    CHECK(hasNote(report, "duplicate node ID J1"));
    CHECK(hasNote(report, "'abc' is not a valid Elevation"));
    CHECK_EQ(model.nodes().size(), std::size_t(4));
    CHECK_EQ(tokenOf(model, Category::Node, "J1", "JUNCTIONS", 1), std::string("10"));
    CHECK_EQ(tokenOf(model, Category::Node, "J1", "JUNCTIONS", 2), std::string("1.5"));
    CHECK_EQ(tokenOf(model, Category::Node, "J2", "JUNCTIONS", 1), std::string("12.5"));
    CHECK_EQ(tokenOf(model, Category::Node, "J3", "JUNCTIONS", 1), std::string("0")); // default kept
    // The feature without an ID gets a generated one that avoids the explicit IDs.
    CHECK(model.findNode("J4"));
    CHECK(model.findNode("J4")->placed);
    CHECK_NEAR(model.findNode("J4")->pos.x, 7.0, 1e-12);
    checkModel(model);
}

TEST_CASE("import: rows match the ones written by addNode, addLink and setToken")
{
    auto viaImport = core::Model::newProject(core::Dialect::Epanet, "GPM");
    auto viaEdit = core::Model::newProject(core::Dialect::Epanet, "GPM");

    interop::ImportedLayer nodes;
    nodes.geometry = GeometryType::Point;
    nodes.fields = {"id", "elev"};
    nodes.features = {feature({{0, 0}}, {"A", "100"}), feature({{30, 40}}, {"B", "90"})};
    interop::ImportMapping nm;
    nm.target = Category::Node;
    nm.kind = "Junction";
    nm.idField = "id";
    nm.properties["Elevation"] = "elev";
    interop::importLayer(viaImport, nodes, nm);

    interop::ImportedLayer pipes;
    pipes.geometry = GeometryType::Line;
    pipes.fields = {"dia"};
    pipes.features = {feature({{0, 0}, {0, 40}, {30, 40}}, {"8"})};
    interop::ImportMapping pm;
    pm.target = Category::Link;
    pm.kind = "Pipe";
    pm.idPrefix = "P";
    pm.properties["diameter"] = "dia"; // column names match case-insensitively
    const auto r = interop::importLayer(viaImport, pipes, pm);
    CHECK_EQ(r.linksAdded, 1);
    CHECK_EQ(r.nodesAdded, 0);

    std::string err;
    CHECK(viaEdit.addNode("Junction", "A", {0, 0}, &err));
    CHECK(viaEdit.addNode("Junction", "B", {30, 40}, &err));
    auto row = viaEdit.rowsFor(Category::Node, "A").front();
    viaEdit.setToken(row.section, row.row, 1, "100");
    row = viaEdit.rowsFor(Category::Node, "B").front();
    viaEdit.setToken(row.section, row.row, 1, "90");
    CHECK(viaEdit.addLink("Pipe", "P1", "A", "B", &err));
    row = viaEdit.rowsFor(Category::Link, "P1").front();
    viaEdit.setToken(row.section, row.row, 4, "8");
    viaEdit.setToken(row.section, row.row, 3, "70");
    viaEdit.setVertices("P1", {{0, 40}});
    CHECK(viaImport.toText() == viaEdit.toText());
    checkModel(viaImport);
}

TEST_CASE("import: EPANET pipes snapped to existing junctions")
{
    auto model = core::Model::newProject(core::Dialect::Epanet, "LPS");
    std::string err;
    CHECK(model.addNode("Junction", "J1", {0, 0}, &err));
    CHECK(model.addNode("Junction", "J2", {100, 0}, &err));
    CHECK(model.addNode("Reservoir", "R1", {0, 100}, &err));

    interop::ImportedLayer layer;
    layer.name = "mains";
    layer.geometry = GeometryType::Line;
    layer.fields = {"PIPE_ID", "DIA", "C"};
    layer.features = {
        feature({{0.3, -0.2}, {50, 0}, {99.8, 0.1}}, {"P1", "200", "130"}), // both ends snap
        feature({{100.2, 0}, {100, 60}}, {"P2", "150", "120"}),          // new node at the far end
        feature({{0.4, 99.9}, {0.1, 0.2}}, {"P3", "300", "x"}),          // reservoir to J1
        feature({{0, 0}, {0.5, 0.5}}, {"P4", "100", "100"}),             // both ends on J1
        feature({{100, 60.4}, {200, 60}}, {"P1", "100", "100"}),         // duplicate ID
    };
    auto mapping = interop::suggestMapping(model, layer, Category::Link, "Pipe");
    CHECK_EQ(mapping.idField, std::string("PIPE_ID"));
    CHECK_EQ(mapping.properties.at("Diameter"), std::string("DIA"));
    CHECK_EQ(mapping.properties.at("Roughness"), std::string("C"));
    CHECK(mapping.fromField.empty());
    mapping.snapTolerance = 1.0;

    const auto report = interop::importLayer(model, layer, mapping);
    CHECK_EQ(report.linksAdded, 3);
    CHECK_EQ(report.nodesAdded, 1);
    CHECK_EQ(report.skipped, 2);
    CHECK(hasNote(report, "same node"));
    CHECK(hasNote(report, "duplicate link ID P1"));
    CHECK(hasNote(report, "'x' is not a valid Roughness"));

    const auto* p1 = model.findLink("P1");
    CHECK(p1 != nullptr);
    CHECK_EQ(p1->from, std::string("J1"));
    CHECK_EQ(p1->to, std::string("J2"));
    CHECK_EQ(p1->vertices.size(), std::size_t(1));
    CHECK_EQ(tokenOf(model, Category::Link, "P1", "PIPES", 3), std::string("100")); // between snapped ends
    CHECK_EQ(tokenOf(model, Category::Link, "P1", "PIPES", 4), std::string("200"));
    CHECK_EQ(tokenOf(model, Category::Link, "P1", "PIPES", 5), std::string("130"));
    const auto* p2 = model.findLink("P2");
    CHECK_EQ(p2->from, std::string("J2"));
    CHECK_EQ(p2->to, std::string("J3"));
    CHECK(model.findNode("J3") && model.findNode("J3")->placed);
    CHECK_EQ(tokenOf(model, Category::Link, "P2", "PIPES", 3), std::string("60"));
    const auto* p3 = model.findLink("P3");
    CHECK_EQ(p3->from, std::string("R1"));
    CHECK_EQ(p3->to, std::string("J1"));
    CHECK_EQ(tokenOf(model, Category::Link, "P3", "PIPES", 5), std::string("0.1")); // D-W default kept
    checkModel(model);
}

TEST_CASE("import: EPANET pipes from a CSV with from/to node columns")
{
    const auto dir = scratchDir("fromto");
    auto model = core::Model::newProject(core::Dialect::Epanet, "GPM");
    std::string err;
    CHECK(model.addNode("Junction", "N1", {0, 0}, &err));
    CHECK(model.addNode("Junction", "N2", {300, 400}, &err));
    CHECK(model.addNode("Tank", "N3", {300, 0}, &err));
    writeFile(dir / "links.csv", "id,from_node,to_node,diameter,length\n"
                                 "\"P 1\",N1,N2,12,\n"
                                 "P2,N2,N3,8,250\n"
                                 "P3,N3,N9,6,100\n");
    const auto layer = interop::readCsv(dir / "links.csv");
    auto mapping = interop::suggestMapping(model, layer, Category::Link, "Pipe");
    CHECK_EQ(mapping.fromField, std::string("from_node"));
    CHECK_EQ(mapping.toField, std::string("to_node"));
    CHECK_EQ(mapping.properties.at("Length"), std::string("length"));
    const auto report = interop::importLayer(model, layer, mapping);
    CHECK_EQ(report.linksAdded, 2);
    CHECK_EQ(report.skipped, 1);
    CHECK(hasNote(report, "unknown end node N9"));
    CHECK(hasNote(report, "imported as P_1"));
    CHECK(model.findLink("P_1"));
    CHECK_EQ(tokenOf(model, Category::Link, "P_1", "PIPES", 3), std::string("500")); // from coordinates
    CHECK_EQ(tokenOf(model, Category::Link, "P2", "PIPES", 3), std::string("250"));  // mapped
    CHECK_EQ(tokenOf(model, Category::Link, "P2", "PIPES", 5), std::string("100"));  // H-W default
    checkModel(model);
}

TEST_CASE("import: SWMM conduits and subcatchments with nearest outlets")
{
    auto model = core::Model::newProject(core::Dialect::Swmm, "CMS");
    std::string err;
    CHECK(model.addNode("Outfall", "OF1", {1000, 0}, &err));
    CHECK(model.addGage("RG1", {0, 500}, &err));

    interop::ImportedLayer mh;
    mh.geometry = GeometryType::Point;
    mh.fields = {"MH_ID", "INVERT", "DEPTH"};
    mh.features = {feature({{0, 0}}, {"MH1", "10", "3"}), feature({{500, 0}}, {"MH2", "9", "2.5"})};
    auto nm = interop::suggestMapping(model, mh, Category::Node, "Junction");
    CHECK_EQ(nm.idField, std::string("MH_ID"));
    CHECK_EQ(nm.properties.at("Elevation"), std::string("INVERT"));
    CHECK_EQ(nm.properties.at("MaxDepth"), std::string("DEPTH"));
    CHECK_EQ(interop::importLayer(model, mh, nm).nodesAdded, 2);
    CHECK_EQ(tokenOf(model, Category::Node, "MH2", "JUNCTIONS", 2), std::string("2.5"));

    interop::ImportedLayer sewers;
    sewers.geometry = GeometryType::Line;
    sewers.fields = {"ID", "DIAMETER", "N"};
    sewers.features = {feature({{0, 0}, {500, 0}}, {"C1", "0.6", "0.015"}),
                       feature({{500, 0}, {1000, 0}}, {"C2", "0.9", "0.013"})};
    auto cm = interop::suggestMapping(model, sewers, Category::Link, "Conduit");
    CHECK_EQ(cm.properties.at("Geom1"), std::string("DIAMETER"));
    CHECK_EQ(cm.properties.at("Roughness"), std::string("N"));
    const auto cr = interop::importLayer(model, sewers, cm);
    CHECK_EQ(cr.linksAdded, 2);
    CHECK_EQ(cr.nodesAdded, 0);
    CHECK_EQ(model.findLink("C2")->to, std::string("OF1"));
    CHECK_EQ(tokenOf(model, Category::Link, "C1", "CONDUITS", 3), std::string("500"));
    CHECK_EQ(tokenOf(model, Category::Link, "C1", "CONDUITS", 4), std::string("0.015"));
    CHECK_EQ(tokenOf(model, Category::Link, "C2", "XSECTIONS", 2), std::string("0.9"));
    CHECK_EQ(tokenOf(model, Category::Link, "C2", "XSECTIONS", 1), std::string("CIRCULAR"));

    interop::ImportedLayer subs;
    subs.geometry = GeometryType::Polygon;
    subs.fields = {"NAME", "IMPERV", "OUTLET"};
    subs.features = {
        feature({{-100, 50}, {-100, 250}, {100, 250}, {100, 50}}, {"S1", "40", ""}),    // near MH1
        feature({{400, 50}, {400, 150}, {600, 150}, {600, 50}}, {"S2", "60", ""}),      // near MH2
        feature({{900, 50}, {900, 150}, {1100, 150}, {1100, 50}}, {"S3", "70", "MH1"}), // explicit
        feature({{0, 0}, {10, 0}}, {"S4", "0", ""}),                                    // degenerate
    };
    auto sm = interop::suggestMapping(model, subs, Category::Area, "Subcatchment");
    CHECK_EQ(sm.idField, std::string("NAME"));
    CHECK_EQ(sm.outletField, std::string("OUTLET"));
    CHECK_EQ(sm.properties.at("PctImperv"), std::string("IMPERV"));
    const auto sr = interop::importLayer(model, subs, sm);
    CHECK_EQ(sr.areasAdded, 3);
    CHECK_EQ(sr.skipped, 1);
    CHECK_EQ(model.findArea("S1")->outlet, std::string("MH1"));
    CHECK_EQ(model.findArea("S2")->outlet, std::string("MH2"));
    CHECK_EQ(model.findArea("S3")->outlet, std::string("MH1"));
    CHECK_EQ(model.findArea("S1")->gage, std::string("RG1"));
    CHECK_EQ(model.findArea("S1")->polygon.size(), std::size_t(4));
    CHECK_EQ(tokenOf(model, Category::Area, "S1", "SUBCATCHMENTS", 3), std::string("4")); // 40000 m2 = 4 ha
    CHECK_EQ(tokenOf(model, Category::Area, "S2", "SUBCATCHMENTS", 4), std::string("60"));
    checkModel(model);

    const auto props = interop::mappableProperties(core::Dialect::Swmm, Category::Link, "Conduit");
    CHECK(props.size() > 5);
    CHECK_EQ(props.front(), std::string("Length"));
    CHECK(std::find(props.begin(), props.end(), "Geom1") != props.end());
    CHECK(std::find(props.begin(), props.end(), "FromNode") == props.end());
    CHECK(interop::mappableProperties(core::Dialect::Epanet, Category::Area, "Subcatchment").empty());

    bool threw = false;
    try
    {
        auto epanet = core::Model::newProject(core::Dialect::Epanet, "LPS");
        interop::ImportMapping bad;
        bad.target = Category::Area;
        bad.kind = "Subcatchment";
        interop::importLayer(epanet, subs, bad);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("import: duplicate IDs are skipped and notes are capped")
{
    auto model = core::Model::newProject(core::Dialect::Epanet, "LPS");
    interop::ImportedLayer layer;
    layer.geometry = GeometryType::Point;
    layer.fields = {"ID"};
    for (int i = 0; i < 80; ++i)
        layer.features.push_back(feature({{double(i), 0}}, {i < 10 ? "N" + std::to_string(i) : "N0"}));
    interop::ImportMapping m;
    m.kind = "Junction";
    m.idField = "ID";
    const auto r = interop::importLayer(model, layer, m);
    CHECK_EQ(r.nodesAdded, 10);
    CHECK_EQ(r.skipped, 70);
    CHECK_EQ(r.notes.size(), std::size_t(51));
    CHECK(r.notes.back().find("20 more") != std::string::npos);

    // Importing the same layer again adds nothing.
    const auto again = interop::importLayer(model, layer, m);
    CHECK_EQ(again.nodesAdded, 0);
    CHECK_EQ(again.skipped, 80);
    CHECK_EQ(model.nodes().size(), std::size_t(10));
    checkModel(model);
}

TEST_CASE("import: coordinate transform is applied and can reject points")
{
    auto model = core::Model::newProject(core::Dialect::Epanet, "LPS");
    interop::ImportedLayer layer;
    layer.geometry = GeometryType::Point;
    layer.fields = {"ID"};
    layer.features = {feature({{1, 2}}, {"A"}), feature({{-1, 2}}, {"B"})};
    interop::ImportMapping m;
    m.kind = "Junction";
    m.idField = "ID";
    m.transform = [](double& x, double& y) {
        if (x < 0)
            return false;
        x *= 1000;
        y *= 1000;
        return true;
    };
    const auto r = interop::importLayer(model, layer, m);
    CHECK_EQ(r.nodesAdded, 1);
    CHECK_EQ(r.skipped, 1);
    CHECK_NEAR(model.findNode("A")->pos.x, 1000.0, 1e-9);
    CHECK_NEAR(model.findNode("A")->pos.y, 2000.0, 1e-9);
}

TEST_CASE("import: 20000 pipes with endpoint snapping")
{
    auto model = core::Model::newProject(core::Dialect::Epanet, "LPS");
    interop::ImportedLayer layer;
    layer.geometry = GeometryType::Line;
    layer.fields = {"DIAMETER"};
    const int n = 20000;
    layer.features.reserve(n);
    // 100 rows of 200 pipes whose shared endpoints differ by up to 0.01.
    for (int i = 0; i < n; ++i)
    {
        const double x = (i % 200) * 50.0, y = (i / 200) * 50.0;
        const double jitter = (i % 3) * 0.005;
        layer.features.push_back(feature({{x + jitter, y}, {x + 25.0, y + 10.0}, {x + 50.0 - jitter, y}},
                                         {std::to_string(100 + (i % 4) * 50)}));
    }
    interop::ImportMapping m;
    m.target = Category::Link;
    m.kind = "Pipe";
    m.idPrefix = "P";
    m.properties["Diameter"] = "DIAMETER";
    m.snapTolerance = 0.1;
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = interop::importLayer(model, layer, m);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "  imported " << r.linksAdded << " pipes and " << r.nodesAdded << " junctions in "
              << seconds << " s\n";
    CHECK_EQ(r.linksAdded, n);
    CHECK_EQ(r.nodesAdded, 100 * 201);
    CHECK_EQ(r.skipped, 0);
    CHECK_EQ(model.links().size(), std::size_t(n));
    CHECK_EQ(model.nodes().size(), std::size_t(100 * 201));
    CHECK_EQ(model.links().back().vertices.size(), std::size_t(1));
    CHECK(seconds < 10.0); // about 0.3 s on a desktop computer; generous for slow CI runners
}
