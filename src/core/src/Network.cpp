/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Network.cpp
 * Purpose: Network graph, traces and shortest paths.
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
#include "mirab/core/Network.h"

#include "mirab/core/Geometry.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>

namespace mirab::core
{

int NetworkGraph::addNode(const std::string& id)
{
    const auto it = nodeIdx_.find(id);
    if (it != nodeIdx_.end())
        return it->second;
    const int i = static_cast<int>(nodeIds_.size());
    nodeIds_.push_back(id);
    incident_.emplace_back();
    nodeIdx_.emplace(id, i);
    return i;
}

int NetworkGraph::addLink(const std::string& id, const std::string& from, const std::string& to,
                          double length, bool closed)
{
    Link l;
    l.from = addNode(from);
    l.to = addNode(to);
    l.length = length;
    l.closed = closed;
    const int i = static_cast<int>(links_.size());
    links_.push_back(l);
    linkIds_.push_back(id);
    linkIdx_.emplace(id, i);
    incident_[static_cast<std::size_t>(l.from)].push_back(i);
    if (l.to != l.from)
        incident_[static_cast<std::size_t>(l.to)].push_back(i);
    return i;
}

int NetworkGraph::nodeIndex(std::string_view id) const
{
    const auto it = nodeIdx_.find(std::string(id));
    return it == nodeIdx_.end() ? -1 : it->second;
}

int NetworkGraph::linkIndex(std::string_view id) const
{
    const auto it = linkIdx_.find(std::string(id));
    return it == linkIdx_.end() ? -1 : it->second;
}

NetworkGraph NetworkGraph::build(const Model& model)
{
    NetworkGraph g;
    for (const auto& n : model.nodes())
        g.addNode(n.id);
    // Row tokens of each defining section, read once.
    std::map<std::string, std::vector<std::vector<std::string>>> rows;
    auto rowOf = [&](const std::string& section, int row) -> const std::vector<std::string>* {
        auto it = rows.find(section);
        if (it == rows.end())
        {
            const InpSection* s = model.document().find(section);
            it = rows.emplace(section, s ? s->allRowTokens() : std::vector<std::vector<std::string>>{})
                     .first;
        }
        if (row < 0 || row >= static_cast<int>(it->second.size()))
            return nullptr;
        return &it->second[static_cast<std::size_t>(row)];
    };
    const bool epanet = model.dialect() != Dialect::Swmm;
    for (const auto& l : model.links())
    {
        if (!model.findNode(l.from) || !model.findNode(l.to))
            continue;
        double length = 0.0;
        bool closed = false;
        const SectionDef* def = findSectionDef(model.dialect(), l.section);
        if (const auto* tokens = rowOf(l.section, l.row); tokens && def)
        {
            for (std::size_t c = 0; c < def->columns.size() && c < tokens->size(); ++c)
            {
                const std::string& name = def->columns[c].name;
                if (name == "Length")
                    length = text::toDouble((*tokens)[c]).value_or(0.0);
                else if (epanet && name == "Status")
                    closed = text::toUpper((*tokens)[c]) == "CLOSED";
            }
        }
        if (!(length > 0.0))
            length = polylineLength(linkPolyline(model, l));
        g.addLink(l.id, l.from, l.to, std::max(length, 1e-9), closed);
    }
    if (epanet)
    {
        // Initial status of pipes, pumps and valves.
        if (const InpSection* s = model.document().find("STATUS"))
        {
            for (const auto& tokens : s->allRowTokens())
            {
                if (tokens.size() < 2)
                    continue;
                const int i = g.linkIndex(tokens[0]);
                if (i < 0)
                    continue;
                const std::string v = text::toUpper(tokens[1]);
                if (v == "CLOSED")
                    g.links_[static_cast<std::size_t>(i)].closed = true;
                else if (v == "OPEN")
                    g.links_[static_cast<std::size_t>(i)].closed = false;
            }
        }
    }
    return g;
}

TraceResult trace(const NetworkGraph& graph, const std::vector<int>& startNodes,
                  const std::vector<int>& startLinks, const TraceOptions& options)
{
    TraceResult out;
    const int nn = graph.nodeCount(), nl = graph.linkCount();
    std::vector<char> nodeSeen(static_cast<std::size_t>(nn), 0);
    std::vector<char> linkSeen(static_cast<std::size_t>(nl), 0);
    std::queue<int> queue;

    auto isClosed = [&](int e) {
        if (!options.closed.empty() && e < static_cast<int>(options.closed.size()))
            return static_cast<bool>(options.closed[static_cast<std::size_t>(e)]);
        return graph.link(e).closed;
    };
    // +1 when water runs from -> to, -1 when it runs to -> from.
    auto sense = [&](int e) {
        if (e < static_cast<int>(options.flows.size()))
        {
            const double q = options.flows[static_cast<std::size_t>(e)];
            if (std::isfinite(q) && std::fabs(q) > options.zeroFlow)
                return q > 0 ? 1 : -1;
        }
        return 1;
    };
    auto reachNode = [&](int n) {
        if (n < 0 || n >= nn || nodeSeen[static_cast<std::size_t>(n)])
            return;
        nodeSeen[static_cast<std::size_t>(n)] = 1;
        out.nodes.push_back(n);
        queue.push(n);
    };
    auto reachLink = [&](int e) {
        if (linkSeen[static_cast<std::size_t>(e)])
            return false;
        linkSeen[static_cast<std::size_t>(e)] = 1;
        out.links.push_back(e);
        return true;
    };
    // Node a link leads to when entered from @p at (-1 when it cannot be followed).
    auto across = [&](int e, int at) {
        const auto& l = graph.link(e);
        const int other = l.from == at ? l.to : l.from;
        if (options.direction == TraceDirection::Connected)
            return other;
        const int s = sense(e);
        const int downstreamEnd = s > 0 ? l.to : l.from;
        const int upstreamEnd = s > 0 ? l.from : l.to;
        if (options.direction == TraceDirection::Downstream)
            return at == upstreamEnd ? downstreamEnd : -1;
        return at == downstreamEnd ? upstreamEnd : -1;
    };

    for (int n : startNodes)
        reachNode(n);
    for (int e : startLinks)
    {
        if (e < 0 || e >= nl)
            continue;
        reachLink(e);
        const auto& l = graph.link(e);
        const int s = sense(e);
        if (options.direction != TraceDirection::Upstream)
            reachNode(s > 0 ? l.to : l.from);
        if (options.direction != TraceDirection::Downstream)
            reachNode(s > 0 ? l.from : l.to);
    }
    while (!queue.empty())
    {
        const int at = queue.front();
        queue.pop();
        for (int e : graph.incident(at))
        {
            if (options.stopAtClosed && isClosed(e))
                continue;
            const int next = across(e, at);
            if (next < 0)
                continue;
            reachLink(e);
            reachNode(next);
        }
    }
    return out;
}

PathResult shortestPath(const NetworkGraph& graph, int from, int to)
{
    PathResult out;
    const int nn = graph.nodeCount();
    if (from < 0 || from >= nn || to >= nn || from == to)
        return out;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(static_cast<std::size_t>(nn), inf);
    std::vector<int> viaLink(static_cast<std::size_t>(nn), -1);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    dist[static_cast<std::size_t>(from)] = 0.0;
    queue.push({0.0, from});
    while (!queue.empty())
    {
        const auto [d, u] = queue.top();
        queue.pop();
        if (d > dist[static_cast<std::size_t>(u)])
            continue;
        if (u == to)
            break;
        for (int e : graph.incident(u))
        {
            const auto& l = graph.link(e);
            const int v = l.from == u ? l.to : l.from;
            const double nd = d + l.length;
            if (nd < dist[static_cast<std::size_t>(v)])
            {
                dist[static_cast<std::size_t>(v)] = nd;
                viaLink[static_cast<std::size_t>(v)] = e;
                queue.push({nd, v});
            }
        }
    }
    int end = to;
    if (to < 0)
    {
        double best = 0.0;
        for (int n = 0; n < nn; ++n)
        {
            const double d = dist[static_cast<std::size_t>(n)];
            if (std::isfinite(d) && d > best)
            {
                best = d;
                end = n;
            }
        }
    }
    if (end < 0 || !std::isfinite(dist[static_cast<std::size_t>(end)]))
        return out;
    for (int at = end; at != from;)
    {
        const int e = viaLink[static_cast<std::size_t>(at)];
        out.nodes.push_back(at);
        out.distances.push_back(dist[static_cast<std::size_t>(at)]);
        out.links.push_back(e);
        const auto& l = graph.link(e);
        at = l.from == at ? l.to : l.from;
    }
    out.nodes.push_back(from);
    out.distances.push_back(0.0);
    std::reverse(out.nodes.begin(), out.nodes.end());
    std::reverse(out.distances.begin(), out.distances.end());
    std::reverse(out.links.begin(), out.links.end());
    return out;
}

} // namespace mirab::core
