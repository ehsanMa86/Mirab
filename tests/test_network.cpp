/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_network.cpp
 * Purpose: Tests of network tracing, shortest paths, geometry, queries and map edits.
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
#include "mirab/core/Editing.h"
#include "mirab/core/Geometry.h"
#include "mirab/core/Model.h"
#include "mirab/core/Network.h"
#include "mirab/core/Query.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace mirab;
using core::Category;
using core::Point;

namespace
{
// A small looped EPANET network: R1 - P1 - J1, J1 splits into a loop J2/J3
// that joins at J4, with a closed pipe P6 to J5 and a pump PU1 to tank T1.
//
//   R1 --P1-- J1 --P2-- J2 --P4-- J4 --P6(closed)-- J5
//              \               /
//               P3 -- J3 --P5-
//   J4 --PU1-- T1
const char* kEpanet = R"([TITLE]
test

[JUNCTIONS]
;ID  Elev  Demand
J1   10    1
J2   12    1
J3   8     1
J4   14    1
J5   20    1

[RESERVOIRS]
R1   50

[TANKS]
T1   30   2   0   5   10   0

[PIPES]
P1   R1   J1   100   150   100   0   Open
P2   J1   J2   200   150   100   0   Open
P3   J1   J3   50    150   100   0   Open
P4   J2   J4   200   150   100   0   Open
P5   J3   J4   60    150   100   2   CV
P6   J4   J5   100   150   100   0   Closed

[PUMPS]
PU1  J4   T1   POWER 5

[REACTIONS]
WALL P2 -0.5

[TAGS]
LINK P2 Main

[OPTIONS]
UNITS LPS

[COORDINATES]
R1   0     0
J1   100   0
J2   200   100
J3   200   -100
J4   300   0
J5   400   0
T1   300   100

[VERTICES]
P2   150   100

[END]
)";

const char* kSwmm = R"([TITLE]
test

[OPTIONS]
FLOW_UNITS CMS
LINK_OFFSETS DEPTH

[JUNCTIONS]
J1   100   3   0   0   0
J2   98    4   0   0   0
J3   97    3   0   0   0

[OUTFALLS]
O1   95   FREE   NO

[CONDUITS]
C1   J1   J2   200   0.013   0.5   0.2   0   0
C2   J2   J3   100   0.013   0     0     0   0
C3   J3   O1   100   0.013   0     0     0   0

[XSECTIONS]
C1   CIRCULAR   0.6   0   0   0   1
C2   CIRCULAR   0.6   0   0   0   1
C3   CIRCULAR   0.6   0   0   0   1

[LOSSES]
C1   0.5   0.8   0.1   YES

[COORDINATES]
J1   0     0
J2   100   0
J3   200   0
O1   300   0

[VERTICES]
C1   25    10
C1   75    10
)";

std::set<std::string> nodeIds(const core::NetworkGraph& g, const std::vector<int>& idx)
{
    std::set<std::string> out;
    for (int i : idx)
        out.insert(g.nodeId(i));
    return out;
}

std::set<std::string> linkIds(const core::NetworkGraph& g, const std::vector<int>& idx)
{
    std::set<std::string> out;
    for (int i : idx)
        out.insert(g.linkId(i));
    return out;
}

std::vector<std::string> rowOf(const core::Model& m, Category c, const std::string& id, const char* section)
{
    for (const auto& r : m.rowsFor(c, id))
        if (r.section == section)
            return r.tokens;
    return {};
}
} // namespace

TEST_CASE("network: graph from a model, lengths and closed links")
{
    const auto m = core::Model::fromText(kEpanet);
    const auto g = core::NetworkGraph::build(m);
    CHECK_EQ(g.nodeCount(), 7);
    CHECK_EQ(g.linkCount(), 7);
    CHECK_NEAR(g.link(g.linkIndex("P2")).length, 200.0, 1e-9);
    // The pump has no length: its drawn length is used.
    CHECK_NEAR(g.link(g.linkIndex("PU1")).length, 100.0, 1e-9);
    CHECK(g.link(g.linkIndex("P6")).closed);
    CHECK(!g.link(g.linkIndex("P5")).closed);
    CHECK_EQ(g.incident(g.nodeIndex("J4")).size(), std::size_t(4));
}

TEST_CASE("network: downstream and upstream traces by link direction")
{
    const auto m = core::Model::fromText(kEpanet);
    const auto g = core::NetworkGraph::build(m);
    core::TraceOptions o;
    auto down = core::trace(g, {g.nodeIndex("J1")}, {}, o);
    CHECK((nodeIds(g, down.nodes) == std::set<std::string>{"J1", "J2", "J3", "J4", "T1"}));
    CHECK((linkIds(g, down.links) == std::set<std::string>{"P2", "P3", "P4", "P5", "PU1"}));
    // Without stopping at closed links, J5 is reached through P6.
    o.stopAtClosed = false;
    down = core::trace(g, {g.nodeIndex("J1")}, {}, o);
    CHECK(nodeIds(g, down.nodes).count("J5"));

    o.stopAtClosed = true;
    o.direction = core::TraceDirection::Upstream;
    auto up = core::trace(g, {g.nodeIndex("J4")}, {}, o);
    CHECK((nodeIds(g, up.nodes) == std::set<std::string>{"J4", "J2", "J3", "J1", "R1"}));
    CHECK(!linkIds(g, up.links).count("PU1"));

    // From a link: downstream starts at its "to" end, upstream at its "from" end.
    o.direction = core::TraceDirection::Downstream;
    down = core::trace(g, {}, {g.linkIndex("P4")}, o);
    CHECK((nodeIds(g, down.nodes) == std::set<std::string>{"J4", "T1"}));
    CHECK((linkIds(g, down.links) == std::set<std::string>{"P4", "PU1"}));
    o.direction = core::TraceDirection::Upstream;
    up = core::trace(g, {}, {g.linkIndex("P4")}, o);
    CHECK((nodeIds(g, up.nodes) == std::set<std::string>{"J2", "J1", "R1"}));
}

TEST_CASE("network: traces follow the sign of the flows")
{
    const auto m = core::Model::fromText(kEpanet);
    const auto g = core::NetworkGraph::build(m);
    core::TraceOptions o;
    o.flows.assign(static_cast<std::size_t>(g.linkCount()), 1.0);
    // Water in P4 runs from J4 back to J2, and P5 has no flow.
    o.flows[static_cast<std::size_t>(g.linkIndex("P4"))] = -2.0;
    o.flows[static_cast<std::size_t>(g.linkIndex("P5"))] = 0.0;
    auto down = core::trace(g, {g.nodeIndex("J4")}, {}, o);
    CHECK((nodeIds(g, down.nodes) == std::set<std::string>{"J4", "J2", "T1"}));
    // A zero flow falls back to the drawn direction (J3 -> J4), so J3 is upstream of J4.
    o.direction = core::TraceDirection::Upstream;
    auto up = core::trace(g, {g.nodeIndex("J4")}, {}, o);
    CHECK(nodeIds(g, up.nodes).count("J3"));
    CHECK(!nodeIds(g, up.nodes).count("J2"));
    // Links closed at the time of interest stop the trace.
    o.direction = core::TraceDirection::Downstream;
    o.flows.clear();
    o.closed.assign(static_cast<std::size_t>(g.linkCount()), false);
    o.closed[static_cast<std::size_t>(g.linkIndex("PU1"))] = true;
    down = core::trace(g, {g.nodeIndex("J4")}, {}, o);
    CHECK((nodeIds(g, down.nodes) == std::set<std::string>{"J4", "J5"}));
}

TEST_CASE("network: connected part and shortest path")
{
    const auto m = core::Model::fromText(kEpanet);
    const auto g = core::NetworkGraph::build(m);
    core::TraceOptions o;
    o.direction = core::TraceDirection::Connected;
    auto all = core::trace(g, {g.nodeIndex("J5")}, {}, o);
    CHECK_EQ(all.nodes.size(), std::size_t(1)); // P6 is closed
    o.stopAtClosed = false;
    all = core::trace(g, {g.nodeIndex("J5")}, {}, o);
    CHECK_EQ(all.nodes.size(), std::size_t(7));
    CHECK_EQ(all.links.size(), std::size_t(7));

    // R1 -> J4: through J3 (100 + 50 + 60) rather than J2 (100 + 200 + 200).
    const auto path = core::shortestPath(g, g.nodeIndex("R1"), g.nodeIndex("J4"));
    CHECK(path.found());
    CHECK_EQ(path.nodes.size(), std::size_t(4));
    CHECK_EQ(g.nodeId(path.nodes[2]), std::string("J3"));
    CHECK_EQ(g.linkId(path.links[2]), std::string("P5"));
    CHECK_NEAR(path.length(), 210.0, 1e-9);
    CHECK_NEAR(path.distances[1], 100.0, 1e-9);
    // The farthest node from R1 is J5 (210 + 100).
    const auto far = core::shortestPath(g, g.nodeIndex("R1"), -1);
    CHECK_EQ(g.nodeId(far.nodes.back()), std::string("J5"));
    CHECK_NEAR(far.length(), 310.0, 1e-9);

    // A hand-built graph with two parts: no path between them.
    core::NetworkGraph h;
    h.addLink("a", "1", "2", 5);
    h.addLink("b", "3", "4", 5);
    CHECK(!core::shortestPath(h, h.nodeIndex("1"), h.nodeIndex("4")).found());
    CHECK(!core::shortestPath(h, h.nodeIndex("1"), h.nodeIndex("1")).found());
}

TEST_CASE("network: SWMM traces follow conduit direction")
{
    const auto m = core::Model::fromText(kSwmm);
    const auto g = core::NetworkGraph::build(m);
    core::TraceOptions o;
    const auto down = core::trace(g, {g.nodeIndex("J2")}, {}, o);
    CHECK((nodeIds(g, down.nodes) == std::set<std::string>{"J2", "J3", "O1"}));
    o.direction = core::TraceDirection::Upstream;
    const auto up = core::trace(g, {}, {g.linkIndex("C3")}, o);
    CHECK((nodeIds(g, up.nodes) == std::set<std::string>{"J3", "J2", "J1"}));
}

TEST_CASE("geometry: distances, lengths, areas and positions on a line")
{
    CHECK_NEAR(core::segmentDistance({0, 5}, {-10, 0}, {10, 0}), 5.0, 1e-12);
    CHECK_NEAR(core::segmentDistance({20, 0}, {-10, 0}, {10, 0}), 10.0, 1e-12);
    CHECK(core::segmentsIntersect({0, 0}, {10, 10}, {0, 10}, {10, 0}));
    CHECK(!core::segmentsIntersect({0, 0}, {10, 0}, {0, 1}, {10, 1}));
    CHECK_NEAR(core::segmentsDistance({0, 0}, {10, 0}, {0, 3}, {10, 3}), 3.0, 1e-12);
    CHECK_NEAR(core::polylineLength({{0, 0}, {3, 4}, {3, 10}}), 11.0, 1e-12);
    CHECK_NEAR(core::polygonArea({{0, 0}, {10, 0}, {10, 5}, {0, 5}}), 50.0, 1e-12);
    CHECK(core::pointInPolygon({{0, 0}, {10, 0}, {10, 5}, {0, 5}}, {5, 2}));
    CHECK(!core::pointInPolygon({{0, 0}, {10, 0}, {10, 5}, {0, 5}}, {11, 2}));

    const auto pos = core::locateOnPolyline({{0, 0}, {10, 0}, {10, 10}}, {12, 4});
    CHECK_EQ(pos.segment, std::size_t(1));
    CHECK_NEAR(pos.t, 0.4, 1e-12);
    CHECK_NEAR(pos.along, 14.0, 1e-12);
    CHECK_NEAR(pos.fraction(), 0.7, 1e-12);
    CHECK_NEAR(pos.offset, 2.0, 1e-12);
    CHECK_NEAR(pos.point.x, 10.0, 1e-12);
}

TEST_CASE("geometry: elements in a rectangle, a lasso and within a distance")
{
    const auto m = core::Model::fromText(kEpanet);
    // Window: J2, J4, T1 and the links between them that lie fully inside.
    const std::vector<Point> rect = {{150, -10}, {350, -10}, {350, 150}, {150, 150}};
    auto inside = core::elementsInPolygon(m, rect, false);
    std::set<std::string> ids;
    for (const auto& e : inside)
        ids.insert(e.id);
    CHECK((ids == std::set<std::string>{"J2", "J4", "T1", "P4", "PU1"}));
    // Crossing: links that touch the rectangle too.
    ids.clear();
    for (const auto& e : core::elementsInPolygon(m, rect, true))
        ids.insert(e.id);
    CHECK(ids.count("P2") && ids.count("P5") && ids.count("P6"));
    CHECK(!ids.count("P1"));
    // Triangle lasso around J1 only.
    ids.clear();
    for (const auto& e : core::elementsInPolygon(m, {{80, -20}, {120, -20}, {100, 20}}, false))
        ids.insert(e.id);
    CHECK((ids == std::set<std::string>{"J1"}));

    ids.clear();
    for (const auto& e : core::elementsWithinDistance(m, {{Category::Node, "J5"}}, 50.0))
        ids.insert(e.id);
    CHECK((ids == std::set<std::string>{"J5", "P6"}));
    ids.clear();
    for (const auto& e : core::elementsWithinDistance(m, {{Category::Link, "P6"}}, 1.0))
        ids.insert(e.id);
    CHECK((ids == std::set<std::string>{"J4", "J5", "P4", "P5", "P6", "PU1"}));
}

TEST_CASE("geometry: planar and geodesic measurement")
{
    // Projected in US survey feet.
    const auto feet = crs::parse("EPSG:2229");
    CHECK(feet.has_value());
    auto m = core::measure({{0, 0}, {1000, 0}, {1000, 1000}}, false, &*feet);
    CHECK(m.metres && !m.geodesic);
    CHECK_NEAR(m.length, 2000 * 0.3048006096012192, 1e-6);
    m = core::measure({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}, true, &*feet);
    CHECK_NEAR(m.length, 4000 * 0.3048006096012192, 1e-6);
    CHECK_NEAR(m.area, 1e6 * 0.3048006096012192 * 0.3048006096012192, 1e-3);
    // Unknown coordinate system: map units.
    m = core::measure({{0, 0}, {3, 4}}, false, nullptr);
    CHECK(!m.metres);
    CHECK_NEAR(m.length, 5.0, 1e-12);
    // Geographic: one degree of longitude on the equator is 111319.49 m on WGS 84,
    // one degree of latitude near 45 degrees is about 111132 m.
    const auto wgs = crs::parse("EPSG:4326");
    CHECK(wgs.has_value());
    m = core::measure({{0, 0}, {1, 0}}, false, &*wgs);
    CHECK(m.geodesic);
    CHECK_NEAR(m.length, 111319.49, 0.05);
    CHECK_NEAR(core::geodesicDistance(10, 44.5, 10, 45.5), 111132.0, 2.0);
    // A 0.01 x 0.01 degree cell at the equator: about 1.2308e6 m2.
    const double area = core::measure({{0, 0}, {0.01, 0}, {0.01, 0.01}, {0, 0.01}}, true, &*wgs).area;
    CHECK_NEAR(area / 1.2308e6, 1.0, 2e-3);
}

TEST_CASE("query: parsing, operators and logic")
{
    std::string err;
    auto q = core::Query::parse("Pressure < 20", &err);
    CHECK(q.has_value());
    const core::ElementFields f = {{"ID", "J1"},         {"Type", "Junction"}, {"Elevation", "12.5"},
                                   {"Pattern", "\"\""},  {"Total Inflow", "3"}, {"result.Pressure", "18"},
                                   {"result.Demand", "2"}, {"Demand", "1"}};
    auto field = [&](const std::string& n) { return core::fieldValue(f, n); };
    CHECK(q->matches(field));
    CHECK(core::Query::parse("pressure>=18")->matches(field));
    CHECK(!core::Query::parse("Pressure > 18")->matches(field));
    CHECK(core::Query::parse("Elevation = 12.5 AND Type = junction")->matches(field));
    CHECK(core::Query::parse("Kind = Junction")->matches(field));
    CHECK(core::Query::parse("Elevation > 100 OR ID contains j")->matches(field));
    CHECK(!core::Query::parse("NOT (Elevation > 10 AND Elevation <= 12.5)")->matches(field));
    CHECK(core::Query::parse("Total Inflow == 3")->matches(field));
    CHECK(core::Query::parse("total_inflow = 3 AND ELEVATION > 12")->matches(field));
    CHECK(core::Query::parse("Type <> Pipe")->matches(field));
    CHECK(core::Query::parse("Type != 'Pipe'")->matches(field));
    // A property wins over a result of the same name; "result." picks the result.
    CHECK(core::Query::parse("Demand = 1")->matches(field));
    CHECK(core::Query::parse("result.Demand = 2")->matches(field));
    // Unknown fields make the condition false.
    CHECK(!core::Query::parse("Diameter > 0")->matches(field));
    CHECK(core::Query::parse("NOT Diameter > 0")->matches(field));
    // Empty query matches everything.
    CHECK(core::Query::parse("  ")->empty());
    CHECK(core::Query::parse("")->matches(field));

    const auto fields = core::Query::parse("(A < 1 OR B > 2) AND A = 3")->fields();
    CHECK((fields == std::vector<std::string>{"A", "B"}));

    for (const char* bad : {"Pressure <", "< 20", "Pressure 20", "(Pressure < 20", "Pressure < 20)",
                            "Pressure ! 20", "Name = \"x", "Pressure < 20 AND"})
    {
        err.clear();
        CHECK(!core::Query::parse(bad, &err).has_value());
        CHECK(!err.empty());
    }
}

TEST_CASE("query: selecting elements by properties and results")
{
    const auto m = core::Model::fromText(kEpanet);
    auto ids = [](const std::vector<core::ElementRef>& v) {
        std::set<std::string> out;
        for (const auto& e : v)
            out.insert(e.id);
        return out;
    };
    auto q = core::Query::parse("Elevation >= 12");
    CHECK((ids(core::selectByQuery(m, *q, Category::Node)) == std::set<std::string>{"J2", "J4", "J5", "T1"}));
    CHECK((ids(core::selectByQuery(m, *q, Category::Node, "Junction")) == std::set<std::string>{"J2", "J4", "J5"}));
    q = core::Query::parse("Status = CV OR Length < 60");
    CHECK((ids(core::selectByQuery(m, *q, Category::Link)) == std::set<std::string>{"P3", "P5"}));
    q = core::Query::parse("ID contains 5");
    CHECK((ids(core::selectByQuery(m, *q, Category::None)) == std::set<std::string>{"J5", "P5"}));

    core::ResultSet r;
    r.addPeriod(0);
    r.addPeriod(3600);
    auto& t = r.defineTable("node", {"J1", "J2", "J3"}, {{"Pressure", "m"}});
    t.resizePeriods(2);
    t.set(1, 0, 0, 40.f);
    t.set(1, 0, 1, 15.f);
    t.set(1, 0, 2, 25.f);
    q = core::Query::parse("Pressure < 30");
    CHECK((ids(core::selectByQuery(m, *q, Category::Node, {}, &r, 1)) == std::set<std::string>{"J2", "J3"}));

    const core::FieldTable table(m, Category::Node, &r, 1);
    const auto names = table.fieldNames();
    CHECK(std::find(names.begin(), names.end(), "Elevation") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Pressure") != names.end());
    CHECK(core::fieldValue(table.fields("J2"), "Pressure") == std::optional<std::string>("15"));
    CHECK(table.fields("nope").empty());
}

TEST_CASE("query: CSV of selected elements")
{
    const auto m = core::Model::fromText(kEpanet);
    const std::string csv =
        core::elementsCsv(m, {{Category::Node, "J1"}, {Category::Link, "P2"}, {Category::Node, "R1"}});
    const auto lines = [&] {
        std::vector<std::string> v;
        std::string cur;
        for (char c : csv)
        {
            if (c == '\n')
            {
                v.push_back(cur);
                cur.clear();
            }
            else
                cur += c;
        }
        return v;
    }();
    CHECK_EQ(lines.size(), std::size_t(4));
    CHECK(text::istartsWith(lines[0], "Category,Type,ID,Elevation,Demand"));
    CHECK(lines[0].find("Diameter") != std::string::npos);
    CHECK(text::istartsWith(lines[1], "Node,Junction,J1,10,1"));
    CHECK(text::istartsWith(lines[2], "Link,Pipe,P2,"));
    CHECK(lines[2].find(",200,150,") != std::string::npos);
    CHECK(text::istartsWith(lines[3], "Node,Reservoir,R1,"));
}

TEST_CASE("model: removing many elements at once")
{
    auto m = core::Model::fromText(kEpanet);
    const int removed = m.removeElements({{Category::Node, "J4"}, {Category::Link, "P1"}, {Category::Link, "P4"},
                                          {Category::Node, "nope"}});
    CHECK_EQ(removed, 3);
    CHECK(!m.findNode("J4"));
    // Links of J4 went with it.
    for (const char* l : {"P1", "P4", "P5", "P6", "PU1"})
        CHECK(!m.findLink(l));
    CHECK(m.findLink("P2"));
    CHECK_EQ(m.links().size(), std::size_t(2));
    CHECK_EQ(m.removeElements({}), 0);
}

TEST_CASE("editing: splitting an EPANET pipe")
{
    auto m = core::Model::fromText(kEpanet);
    core::SplitResult r;
    std::string err;
    // P2 runs J1 (100,0) -> vertex (150,100) -> J2 (200,100); drawn length 111.80 + 50.
    CHECK(core::splitLink(m, "P2", {170, 104}, &r, &err));
    CHECK_EQ(r.link, std::string("P2_2"));
    const double drawn = std::hypot(50.0, 100.0) + 50.0;
    CHECK_NEAR(r.fraction, (std::hypot(50.0, 100.0) + 20.0) / drawn, 1e-9);
    const auto* n = m.findNode(r.node);
    CHECK(n && n->placed);
    CHECK_NEAR(n->pos.x, 170.0, 1e-9);
    CHECK_NEAR(n->pos.y, 100.0, 1e-9);
    const auto first = rowOf(m, Category::Link, "P2", "PIPES");
    const auto second = rowOf(m, Category::Link, "P2_2", "PIPES");
    CHECK_EQ(first[2], r.node);
    CHECK_EQ(second[1], r.node);
    CHECK_EQ(second[2], std::string("J2"));
    const double l1 = *text::toDouble(first[3]), l2 = *text::toDouble(second[3]);
    CHECK_NEAR(l1 + l2, 200.0, 1e-6);
    CHECK_NEAR(l1, 200.0 * r.fraction, 1e-3);
    CHECK_EQ(second[4], std::string("150"));
    // Elevation interpolated between J1 (10) and J2 (12).
    const auto junction = rowOf(m, Category::Node, r.node, "JUNCTIONS");
    CHECK_NEAR(*text::toDouble(junction[1]), 10 + 2 * r.fraction, 1e-4);
    CHECK_EQ(junction[2], std::string("0"));
    // The vertex stays with the first part; the reaction and tag are copied.
    CHECK_EQ(m.findLink("P2")->vertices.size(), std::size_t(1));
    CHECK(m.findLink("P2_2")->vertices.empty());
    int wall = 0;
    for (const auto& t : m.document().find("REACTIONS")->allRowTokens())
        wall += t.size() >= 2 && text::iequals(t[0], "WALL") ? 1 : 0;
    CHECK_EQ(wall, 2);
    CHECK_EQ(m.document().find("TAGS")->rowCount(), 2);
    // Minor loss stays on the first part only.
    CHECK(core::splitLink(m, "P5", {250, -50}, &r, &err));
    CHECK_EQ(rowOf(m, Category::Link, "P5", "PIPES")[6], std::string("2"));
    CHECK_EQ(rowOf(m, Category::Link, r.link, "PIPES")[6], std::string("0"));
    CHECK_EQ(rowOf(m, Category::Link, r.link, "PIPES")[7], std::string("CV"));

    // Only pipes, not too close to an end, and only with coordinates.
    CHECK(!core::splitLink(m, "PU1", {300, 50}, nullptr, &err));
    CHECK(!core::splitLink(m, "P1", {0.01, 0}, nullptr, &err));
    CHECK(!core::splitLink(m, "P6", {350, 0}, nullptr, &err, "J1"));
    for (const auto& issue : m.validate())
        CHECK(issue.severity != core::ModelIssue::Severity::Error);
}

TEST_CASE("editing: splitting a SWMM conduit and reversing links")
{
    auto m = core::Model::fromText(kSwmm);
    core::SplitResult r;
    std::string err;
    // C1: J1 (0,0) -> (25,10) -> (75,10) -> J2 (100,0). Split on the middle segment.
    CHECK(core::splitLink(m, "C1", {50, 12}, &r, &err));
    const double seg = std::hypot(25.0, 10.0);
    const double f = (seg + 25.0) / (2 * seg + 50.0);
    CHECK_NEAR(r.fraction, f, 1e-9);
    CHECK_EQ(r.link, std::string("C1_2"));
    const std::string node1 = r.node;
    // Invert from 100 + 0.5 at the inlet to 98 + 0.2 at the outlet.
    const double invert = 100.5 + f * (98.2 - 100.5);
    const auto j = rowOf(m, Category::Node, node1, "JUNCTIONS");
    CHECK_NEAR(*text::toDouble(j[1]), invert, 1e-4);
    // Rim from 103 to 102.
    CHECK_NEAR(*text::toDouble(j[2]), 103 + f * (102 - 103) - invert, 1e-4);
    const auto c1 = rowOf(m, Category::Link, "C1", "CONDUITS");
    const auto c2 = rowOf(m, Category::Link, "C1_2", "CONDUITS");
    CHECK_EQ(c1[5], std::string("0.5"));
    CHECK_EQ(c1[6], std::string("0"));
    CHECK_EQ(c2[5], std::string("0"));
    CHECK_EQ(c2[6], std::string("0.2"));
    CHECK_NEAR(*text::toDouble(c1[3]) + *text::toDouble(c2[3]), 200.0, 1e-6);
    CHECK_EQ(m.findLink("C1")->vertices.size(), std::size_t(1));
    CHECK_EQ(m.findLink("C1_2")->vertices.size(), std::size_t(1));
    CHECK_EQ(rowOf(m, Category::Link, "C1_2", "XSECTIONS")[1], std::string("CIRCULAR"));
    const auto l1 = rowOf(m, Category::Link, "C1", "LOSSES");
    const auto l2 = rowOf(m, Category::Link, "C1_2", "LOSSES");
    CHECK_EQ(l1[1], std::string("0.5"));
    CHECK_EQ(l1[2], std::string("0"));
    CHECK_EQ(l1[4], std::string("YES"));
    CHECK_EQ(l2[1], std::string("0"));
    CHECK_EQ(l2[2], std::string("0.8"));
    CHECK_EQ(l2[4], std::string("NO"));

    // With elevation offsets, the new offsets are the invert elevation.
    auto e = core::Model::fromText(std::string(kSwmm) + "\n");
    e.setOption("LINK_OFFSETS", "ELEVATION");
    auto& conduits = *e.document().find("CONDUITS");
    conduits.setRowTokens(0, {"C1", "J1", "J2", "200", "0.013", "100.5", "98.2", "0", "0"});
    e.rebuild();
    CHECK(core::splitLink(e, "C1", {50, 12}, &r, &err));
    CHECK_NEAR(*text::toDouble(rowOf(e, Category::Link, "C1", "CONDUITS")[6]), invert, 1e-4);
    CHECK_NEAR(*text::toDouble(rowOf(e, Category::Link, r.link, "CONDUITS")[5]), invert, 1e-4);

    // Reversing swaps the nodes, offsets, losses and vertex order.
    CHECK(core::reverseLink(m, "C1_2", &err));
    const auto rev = rowOf(m, Category::Link, "C1_2", "CONDUITS");
    CHECK_EQ(rev[1], std::string("J2"));
    CHECK_EQ(rev[2], node1);
    CHECK_EQ(rev[5], std::string("0.2"));
    CHECK_EQ(rev[6], std::string("0"));
    CHECK_EQ(rowOf(m, Category::Link, "C1_2", "LOSSES")[1], std::string("0.8"));
    CHECK(core::reverseLink(m, "C1", &err));
    CHECK_NEAR(m.findLink("C1")->vertices.front().x, 25.0, 1e-9);
    CHECK(!core::reverseLink(m, "nope", &err));
}
