/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Editing.cpp
 * Purpose: Splitting pipes and conduits and reversing links.
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
#include "mirab/core/Editing.h"

#include "mirab/core/Geometry.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace mirab::core
{

namespace
{
int columnIndex(const SectionDef* def, std::string_view name)
{
    if (!def)
        return -1;
    for (std::size_t c = 0; c < def->columns.size(); ++c)
    {
        if (def->columns[c].name == name)
            return static_cast<int>(c);
    }
    return -1;
}

std::optional<double> tokenNumber(const std::vector<std::string>& tokens, int column)
{
    if (column < 0 || column >= static_cast<int>(tokens.size()))
        return std::nullopt;
    return text::toDouble(tokens[static_cast<std::size_t>(column)]);
}

void setColumn(std::vector<std::string>& tokens, int column, const std::string& value)
{
    if (column < 0)
        return;
    while (static_cast<int>(tokens.size()) <= column)
        tokens.push_back("0");
    tokens[static_cast<std::size_t>(column)] = value;
}

/// Tokens of the row that defines a node, and its section definition.
const SectionDef* nodeRow(const Model& m, const std::string& id, std::vector<std::string>& tokens)
{
    const NodeItem* n = m.findNode(id);
    if (!n)
        return nullptr;
    const InpSection* s = m.document().find(n->section);
    if (!s || n->row < 0 || n->row >= s->rowCount())
        return nullptr;
    tokens = s->rowTokens(n->row);
    return findSectionDef(m.dialect(), n->section);
}

std::string fmt(double v)
{
    return text::formatDouble(std::fabs(v) < 1e-12 ? 0.0 : v, 6);
}

std::string newLinkName(const Model& m, const std::string& base, const std::string& prefix)
{
    for (int i = 2; i < 1000; ++i)
    {
        const std::string candidate = base + "_" + std::to_string(i);
        if (candidate.size() > 31)
            break;
        if (!m.idInUse(Category::Link, candidate))
            return candidate;
    }
    return m.uniqueId(Category::Link, prefix);
}
} // namespace

bool splitLink(Model& model, std::string_view linkId, Point at, SplitResult* result, std::string* error,
               std::string nodeId, std::string newLinkId)
{
    auto fail = [&](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    const LinkItem* found = model.findLink(linkId);
    if (!found)
        return fail("the link does not exist");
    const LinkItem link = *found;
    const bool swmm = model.dialect() == Dialect::Swmm;
    if (link.kind != (swmm ? "Conduit" : "Pipe"))
        return fail(swmm ? "only conduits can be split" : "only pipes can be split");
    const std::vector<Point> line = linkPolyline(model, link);
    if (line.size() < 2)
        return fail("the link has no map coordinates");
    const PolylinePosition pos = locateOnPolyline(line, at);
    const double f = pos.fraction();
    if (!(pos.length > 0) || f < 1e-3 || f > 1 - 1e-3)
        return fail("the point is too close to an end of the link");

    if (nodeId.empty())
        nodeId = model.uniqueId(Category::Node, "J");
    if (newLinkId.empty())
        newLinkId = newLinkName(model, link.id, swmm ? "C" : "P");
    if (model.idInUse(Category::Node, nodeId))
        return fail("the new node ID is already in use");
    if (model.idInUse(Category::Link, newLinkId) || newLinkId == link.id)
        return fail("the new link ID is already in use");

    InpSection* sec = model.document().find(link.section);
    const SectionDef* def = findSectionDef(model.dialect(), link.section);
    if (!sec || !def || link.row < 0 || link.row >= sec->rowCount())
        return fail("the link row was not found");
    const std::vector<std::string> original = sec->rowTokens(link.row);
    std::vector<std::string> first = original;
    std::vector<std::string> second = original;
    second[0] = newLinkId;
    setColumn(first, def->toColumn, nodeId);
    setColumn(second, def->fromColumn, nodeId);

    const int lenCol = columnIndex(def, "Length");
    const double length = tokenNumber(original, lenCol).value_or(pos.length);
    const std::string firstLength = fmt(length * f);
    const double secondLength = length - text::toDouble(firstLength).value_or(length * f);
    if (lenCol >= 0 && lenCol < static_cast<int>(original.size()))
    {
        setColumn(first, lenCol, firstLength);
        setColumn(second, lenCol, fmt(secondLength));
    }

    std::vector<std::string> fromTokens, toTokens;
    const SectionDef* fromDef = nodeRow(model, link.from, fromTokens);
    const SectionDef* toDef = nodeRow(model, link.to, toTokens);
    const double zFrom = tokenNumber(fromTokens, 1).value_or(0.0);
    const double zTo = tokenNumber(toTokens, 1).value_or(0.0);

    std::vector<std::string> nodeTokens;
    if (!swmm)
    {
        nodeTokens = {nodeId, fmt(zFrom + f * (zTo - zFrom)), "0"};
        const int lossCol = columnIndex(def, "MinorLoss");
        if (lossCol >= 0 && lossCol < static_cast<int>(second.size()))
            second[static_cast<std::size_t>(lossCol)] = "0";
    }
    else
    {
        const bool elevationOffsets = text::toUpper(model.option("LINK_OFFSETS")) == "ELEVATION";
        const int inCol = columnIndex(def, "InOffset"), outCol = columnIndex(def, "OutOffset");
        auto invertAt = [&](int col, double nodeInvert) {
            const auto v = tokenNumber(original, col);
            if (elevationOffsets)
                return v.value_or(nodeInvert); // "*": the node invert
            return nodeInvert + v.value_or(0.0);
        };
        const double start = invertAt(inCol, zFrom), end = invertAt(outCol, zTo);
        const double invert = start + f * (end - start);
        const std::string offset = elevationOffsets ? fmt(invert) : std::string("0");
        setColumn(first, outCol, offset);
        setColumn(second, inCol, offset);
        // Depth up to the rim interpolated between the end nodes.
        auto rim = [&](const SectionDef* d, const std::vector<std::string>& tokens, double z) -> std::optional<double> {
            const auto depth = tokenNumber(tokens, columnIndex(d, "MaxDepth"));
            if (depth && *depth > 0)
                return z + *depth;
            return std::nullopt;
        };
        const auto rimFrom = rim(fromDef, fromTokens, zFrom), rimTo = rim(toDef, toTokens, zTo);
        double maxDepth = 0.0;
        if (rimFrom && rimTo)
            maxDepth = *rimFrom + f * (*rimTo - *rimFrom) - invert;
        else if (rimFrom || rimTo)
            maxDepth = (rimFrom ? *rimFrom : *rimTo) - invert;
        maxDepth = std::max(0.0, maxDepth);
        if (maxDepth > 0)
        {
            for (const auto& r : model.rowsFor(Category::Link, link.id))
            {
                if (r.section == "XSECTIONS" && r.tokens.size() > 2)
                    maxDepth = std::max(maxDepth, text::toDouble(r.tokens[2]).value_or(0.0));
            }
        }
        nodeTokens = {nodeId, fmt(invert), fmt(maxDepth), "0", "0", "0"};
    }

    // Attached data of the link, copied before anything changes.
    struct Copy
    {
        std::string section;
        int row = -1;
        std::vector<std::string> keep;  ///< new tokens of the original row
        std::vector<std::string> added; ///< tokens of the new link's row
    };
    std::vector<Copy> copies;
    for (const auto& r : model.rowsFor(Category::Link, link.id))
    {
        if (r.section == link.section || r.section == "INLET_USAGE")
            continue; // street inlets stay on the original conduit
        Copy c{r.section, r.row, r.tokens, r.tokens};
        c.added[0] = newLinkId;
        if (r.section == "LOSSES")
        {
            // Entry loss and flap gate at the inlet, exit loss at the outlet.
            if (c.keep.size() > 2)
                c.keep[2] = "0";
            if (c.added.size() > 1)
                c.added[1] = "0";
            if (c.added.size() > 4)
                c.added[4] = "NO";
        }
        copies.push_back(std::move(c));
    }

    std::string err;
    if (!model.addNode("Junction", nodeId, pos.point, &err))
    {
        if (error)
            *error = err;
        return false;
    }
    if (const NodeItem* n = model.findNode(nodeId))
    {
        if (InpSection* ns = model.document().find(n->section))
            ns->setRowTokens(n->row, nodeTokens);
    }
    sec = model.document().find(link.section);
    sec->setRowTokens(link.row, first);
    sec->appendRow(second);
    for (const auto& c : copies)
    {
        InpSection& s = model.document().ensure(c.section);
        if (c.keep != s.rowTokens(c.row))
            s.setRowTokens(c.row, c.keep);
        s.appendRow(c.added);
    }
    // Tags and EPANET pipe reaction coefficients.
    auto duplicateRows = [&](const char* section, auto&& matches, std::size_t idColumn) {
        InpSection* s = model.document().find(section);
        if (!s)
            return;
        std::vector<std::vector<std::string>> more;
        for (const auto& tokens : s->allRowTokens())
        {
            if (tokens.size() > idColumn && tokens[idColumn] == link.id && matches(tokens))
            {
                auto t = tokens;
                t[idColumn] = newLinkId;
                more.push_back(std::move(t));
            }
        }
        for (const auto& t : more)
            s->appendRow(t);
    };
    duplicateRows("TAGS", [](const std::vector<std::string>& t) { return text::iequals(t[0], "LINK"); }, 1);
    if (!swmm)
        duplicateRows(
            "REACTIONS",
            [](const std::vector<std::string>& t) {
                return t.size() >= 3 && (text::iequals(t[0], "BULK") || text::iequals(t[0], "WALL"));
            },
            1);
    model.rebuild();

    // Vertices go to the part they lie on.
    const std::vector<Point>& v = link.vertices;
    std::size_t firstCount = std::min(pos.segment, v.size());
    std::size_t secondStart = firstCount;
    if (pos.t <= 1e-9 && firstCount > 0)
        firstCount -= 1; // the split point is on vertex pos.segment
    if (pos.t >= 1 - 1e-9 && secondStart < v.size())
        secondStart += 1;
    model.setVertices(link.id, std::vector<Point>(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(firstCount)));
    model.setVertices(newLinkId, std::vector<Point>(v.begin() + static_cast<std::ptrdiff_t>(secondStart), v.end()));
    if (result)
        *result = SplitResult{nodeId, newLinkId, f};
    return true;
}

bool reverseLink(Model& model, std::string_view linkId, std::string* error)
{
    const LinkItem* found = model.findLink(linkId);
    if (!found)
    {
        if (error)
            *error = "the link does not exist";
        return false;
    }
    const LinkItem link = *found;
    InpSection* sec = model.document().find(link.section);
    const SectionDef* def = findSectionDef(model.dialect(), link.section);
    if (!sec || !def || def->fromColumn < 0 || def->toColumn < 0)
    {
        if (error)
            *error = "the link row was not found";
        return false;
    }
    auto tokens = sec->rowTokens(link.row);
    const auto from = static_cast<std::size_t>(def->fromColumn), to = static_cast<std::size_t>(def->toColumn);
    if (tokens.size() <= std::max(from, to))
    {
        if (error)
            *error = "the link row is incomplete";
        return false;
    }
    std::swap(tokens[from], tokens[to]);
    const int inCol = columnIndex(def, "InOffset"), outCol = columnIndex(def, "OutOffset");
    if (inCol >= 0 && outCol >= 0 && outCol < static_cast<int>(tokens.size()))
        std::swap(tokens[static_cast<std::size_t>(inCol)], tokens[static_cast<std::size_t>(outCol)]);
    sec->setRowTokens(link.row, tokens);
    for (const auto& r : model.rowsFor(Category::Link, link.id))
    {
        if (r.section != "LOSSES" || r.tokens.size() < 3)
            continue;
        auto t = r.tokens;
        std::swap(t[1], t[2]);
        model.document().find(r.section)->setRowTokens(r.row, t);
    }
    model.rebuild();
    std::vector<Point> v = link.vertices;
    std::reverse(v.begin(), v.end());
    if (!v.empty())
        model.setVertices(link.id, v);
    return true;
}

} // namespace mirab::core
