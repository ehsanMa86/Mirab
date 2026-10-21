/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Network.h
 * Purpose: Network graph of a model: upstream and downstream traces, connected parts and shortest paths.
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
#pragma once

#include "mirab/core/Model.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mirab::core
{

/// Nodes and links of a model as a graph. Indices follow Model::nodes() and
/// Model::links().
class NetworkGraph
{
public:
    struct Link
    {
        int from = -1;
        int to = -1;
        double length = 0.0; ///< "Length" property, or the drawn length when there is none
        bool closed = false; ///< closed in the input (EPANET pipe status or [STATUS])
    };

    static NetworkGraph build(const Model& model);

    int nodeCount() const { return static_cast<int>(nodeIds_.size()); }
    int linkCount() const { return static_cast<int>(links_.size()); }
    const std::string& nodeId(int i) const { return nodeIds_[static_cast<std::size_t>(i)]; }
    const std::string& linkId(int i) const { return linkIds_[static_cast<std::size_t>(i)]; }
    const Link& link(int i) const { return links_[static_cast<std::size_t>(i)]; }
    /// Links attached to a node.
    const std::vector<int>& incident(int node) const { return incident_[static_cast<std::size_t>(node)]; }
    int nodeIndex(std::string_view id) const;
    int linkIndex(std::string_view id) const;

    /// Adds a link (for tests and hand-built graphs); creates missing nodes.
    int addLink(const std::string& id, const std::string& from, const std::string& to,
                double length = 1.0, bool closed = false);

private:
    int addNode(const std::string& id);

    std::vector<std::string> nodeIds_;
    std::vector<std::string> linkIds_;
    std::vector<Link> links_;
    std::vector<std::vector<int>> incident_;
    std::unordered_map<std::string, int> nodeIdx_, linkIdx_;
};

enum class TraceDirection
{
    Downstream,
    Upstream,
    Connected ///< everything reachable, ignoring direction
};

struct TraceOptions
{
    TraceDirection direction = TraceDirection::Downstream;
    /// Signed flow of every link (graph order) at the time of interest. A
    /// negative flow runs from the "to" node to the "from" node. Empty: links
    /// are followed in their drawn direction (from -> to). Links with a flow
    /// of at most @c zeroFlow in magnitude also follow their drawn direction.
    std::vector<double> flows;
    double zeroFlow = 1e-9;
    /// Links closed at the time of interest (graph order); empty to use the
    /// status in the input.
    std::vector<bool> closed;
    /// Closed links stop the trace.
    bool stopAtClosed = true;
};

struct TraceResult
{
    std::vector<int> nodes; ///< in the order they were reached
    std::vector<int> links;
};

/// Nodes and links reached from the start nodes and links. Start elements are
/// part of the result. A start link leads on from its downstream end (for a
/// downstream trace), its upstream end (upstream) or both ends (connected).
TraceResult trace(const NetworkGraph& graph, const std::vector<int>& startNodes,
                  const std::vector<int>& startLinks, const TraceOptions& options);

struct PathResult
{
    std::vector<int> nodes;        ///< from the first node to the last
    std::vector<int> links;        ///< links between consecutive nodes
    std::vector<double> distances; ///< distance of each node along the path
    bool found() const { return !nodes.empty(); }
    double length() const { return distances.empty() ? 0.0 : distances.back(); }
};

/// Shortest path by length between two nodes, ignoring link direction. With
/// @p to = -1, the path to the farthest node reachable from @p from.
PathResult shortestPath(const NetworkGraph& graph, int from, int to);

} // namespace mirab::core
