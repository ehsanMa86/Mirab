/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/resilience/src/Resilience.cpp
 * Purpose: Implementation of the Todini index, service availability, topology, criticality and fire-flow analyses.
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
#include "mirab/resilience/Resilience.h"

#include "mirab/core/Text.h"
#include "mirab/core/Units.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stack>
#include <unordered_map>

namespace mirab::resilience
{

using core::ResultSet;
using core::ResultTable;

namespace
{
/// Converts a pressure in project pressure units to head in project length units.
double pressureToHead(const core::ProjectUnits& u, double p)
{
    return p * u.toSI("pressure") / u.toSI("head");
}

double elevationOf(const core::Model& model, const core::NodeItem& n)
{
    const auto* s = model.document().find(n.section);
    if (!s)
        return 0.0;
    const auto tokens = s->rowTokens(n.row);
    if (tokens.size() < 2)
        return 0.0;
    return text::toDouble(tokens[1]).value_or(0.0);
}
} // namespace

TodiniResult todiniIndex(const core::Model& model, const ResultSet& results, double requiredPressure)
{
    TodiniResult out;
    const ResultTable* nodes = results.table("node");
    const ResultTable* links = results.table("link");
    if (!nodes || !links)
    {
        out.note = "the results have no node or link tables";
        return out;
    }
    const int vDemand = nodes->variableIndex("Demand");
    const int vHead = nodes->variableIndex("Head");
    const int vFlow = links->variableIndex("Flow");
    if (vDemand < 0 || vHead < 0 || vFlow < 0)
    {
        out.note = "Todini's index needs node demand and head and link flow results";
        return out;
    }
    const auto units = model.units();
    const double hReqOffset = pressureToHead(units, requiredPressure);

    struct J
    {
        int idx;
        double hstar;
    };
    std::vector<J> junctions;
    std::vector<int> sources;
    for (const auto& n : model.nodes())
    {
        const int idx = nodes->elementIndex(n.id);
        if (idx < 0)
            continue;
        if (n.kind == "Junction")
            junctions.push_back({idx, elevationOf(model, n) + hReqOffset});
        else
            sources.push_back(idx);
    }
    struct P
    {
        int link, from, to;
    };
    std::vector<P> pumps;
    for (const auto& l : model.links())
    {
        if (l.kind != "Pump")
            continue;
        pumps.push_back({links->elementIndex(l.id), nodes->elementIndex(l.from), nodes->elementIndex(l.to)});
    }

    double sum = 0.0;
    double minimum = std::numeric_limits<double>::max();
    for (int p = 0; p < results.periodCount(); ++p)
    {
        double surplus = 0.0, required = 0.0, input = 0.0;
        for (const auto& j : junctions)
        {
            const double q = nodes->value(p, vDemand, j.idx);
            if (q <= 0)
                continue;
            surplus += q * (nodes->value(p, vHead, j.idx) - j.hstar);
            required += q * j.hstar;
        }
        for (int s : sources)
        {
            const double q = -nodes->value(p, vDemand, s); // supply is a negative demand
            if (q > 0)
                input += q * nodes->value(p, vHead, s);
        }
        for (const auto& pu : pumps)
        {
            if (pu.link < 0 || pu.from < 0 || pu.to < 0)
                continue;
            const double q = std::fabs(links->value(p, vFlow, pu.link));
            const double gain = nodes->value(p, vHead, pu.to) - nodes->value(p, vHead, pu.from);
            if (gain > 0)
                input += q * gain;
        }
        const double denom = input - required;
        const double ir = std::fabs(denom) > 1e-12 ? surplus / denom : 0.0;
        out.times.push_back(results.times[static_cast<std::size_t>(p)]);
        out.index.push_back(ir);
        sum += ir;
        minimum = std::min(minimum, ir);
    }
    if (!out.index.empty())
    {
        out.mean = sum / static_cast<double>(out.index.size());
        out.minimum = minimum;
    }
    return out;
}

ServiceResult waterServiceAvailability(const ResultSet& results)
{
    ServiceResult out;
    const ResultTable* nodes = results.table("node");
    if (!nodes)
    {
        out.note = "the results have no node table";
        return out;
    }
    const int vDemand = nodes->variableIndex("Demand");
    const int vFull = nodes->variableIndex("Full Demand");
    const int vDeficit = nodes->variableIndex("Demand Deficit");
    if (vDemand < 0)
    {
        out.note = "service availability needs node demand results";
        return out;
    }
    if (vFull < 0 && vDeficit < 0)
        out.note = "the run was demand-driven, so every demand was met by definition; "
                   "run with DEMAND MODEL PDA for a meaningful availability";
    const int n = nodes->elementCount();
    std::vector<double> delivered(static_cast<std::size_t>(n), 0.0), expected(static_cast<std::size_t>(n), 0.0);
    double totalDelivered = 0.0, totalExpected = 0.0;
    for (int p = 0; p < results.periodCount(); ++p)
    {
        double d = 0.0, e = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double q = nodes->value(p, vDemand, i);
            double full = q;
            if (vFull >= 0)
                full = nodes->value(p, vFull, i);
            else if (vDeficit >= 0)
                full = q + nodes->value(p, vDeficit, i);
            if (full <= 0)
                continue;
            d += std::max(0.0, q);
            e += full;
            delivered[static_cast<std::size_t>(i)] += std::max(0.0, q);
            expected[static_cast<std::size_t>(i)] += full;
        }
        out.times.push_back(results.times[static_cast<std::size_t>(p)]);
        out.system.push_back(e > 0 ? std::min(1.0, d / e) : 1.0);
        totalDelivered += d;
        totalExpected += e;
    }
    for (int i = 0; i < n; ++i)
    {
        if (expected[static_cast<std::size_t>(i)] > 0)
            out.nodes.emplace_back(nodes->ids[static_cast<std::size_t>(i)],
                                   std::min(1.0, delivered[static_cast<std::size_t>(i)] /
                                                     expected[static_cast<std::size_t>(i)]));
    }
    out.overall = totalExpected > 0 ? std::min(1.0, totalDelivered / totalExpected) : 1.0;
    return out;
}

TopologyResult topology(const core::Model& model)
{
    TopologyResult out;
    const auto& nodes = model.nodes();
    const int n = static_cast<int>(nodes.size());
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < n; ++i)
        index.emplace(nodes[static_cast<std::size_t>(i)].id, i);

    struct Edge
    {
        int to;
        int link;
    };
    std::vector<std::vector<Edge>> adj(static_cast<std::size_t>(n));
    std::vector<std::string> linkIds;
    for (const auto& l : model.links())
    {
        auto a = index.find(l.from);
        auto b = index.find(l.to);
        if (a == index.end() || b == index.end() || a->second == b->second)
            continue;
        const int id = static_cast<int>(linkIds.size());
        linkIds.push_back(l.id);
        adj[static_cast<std::size_t>(a->second)].push_back({b->second, id});
        adj[static_cast<std::size_t>(b->second)].push_back({a->second, id});
    }
    const int m = static_cast<int>(linkIds.size());
    out.nodes = n;
    out.links = m;
    if (n == 0)
        return out;
    out.averageDegree = 2.0 * m / n;
    out.linkDensity = n > 1 ? 2.0 * m / (static_cast<double>(n) * (n - 1)) : 0.0;
    out.meshedness = n > 2 ? std::max(0.0, (m - n + 1.0) / (2.0 * n - 5.0)) : 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto deg = adj[static_cast<std::size_t>(i)].size();
        if (deg == 0)
            out.isolatedNodes.push_back(nodes[static_cast<std::size_t>(i)].id);
        else if (deg == 1)
            out.deadEnds.push_back(nodes[static_cast<std::size_t>(i)].id);
    }

    // Iterative Tarjan: bridges and articulation points (parallel links handled
    // by skipping only the tree edge, not every edge to the parent).
    std::vector<int> disc(static_cast<std::size_t>(n), -1), low(static_cast<std::size_t>(n), 0);
    std::vector<bool> isArt(static_cast<std::size_t>(n), false);
    int timer = 0;
    struct Frame
    {
        int node;
        int parentLink;
        std::size_t next;
        int children;
    };
    for (int root = 0; root < n; ++root)
    {
        if (disc[static_cast<std::size_t>(root)] >= 0)
            continue;
        ++out.components;
        std::stack<Frame> st;
        disc[static_cast<std::size_t>(root)] = low[static_cast<std::size_t>(root)] = timer++;
        st.push({root, -1, 0, 0});
        while (!st.empty())
        {
            Frame& f = st.top();
            const auto& edges = adj[static_cast<std::size_t>(f.node)];
            if (f.next < edges.size())
            {
                const Edge e = edges[f.next++];
                if (e.link == f.parentLink)
                    continue;
                if (disc[static_cast<std::size_t>(e.to)] < 0)
                {
                    disc[static_cast<std::size_t>(e.to)] = low[static_cast<std::size_t>(e.to)] = timer++;
                    ++f.children;
                    st.push({e.to, e.link, 0, 0});
                }
                else
                {
                    low[static_cast<std::size_t>(f.node)] =
                        std::min(low[static_cast<std::size_t>(f.node)], disc[static_cast<std::size_t>(e.to)]);
                }
                continue;
            }
            const Frame done = f;
            st.pop();
            if (st.empty())
            {
                if (done.children > 1)
                    isArt[static_cast<std::size_t>(done.node)] = true;
                continue;
            }
            Frame& parent = st.top();
            low[static_cast<std::size_t>(parent.node)] =
                std::min(low[static_cast<std::size_t>(parent.node)], low[static_cast<std::size_t>(done.node)]);
            if (low[static_cast<std::size_t>(done.node)] > disc[static_cast<std::size_t>(parent.node)])
                out.bridgeLinks.push_back(linkIds[static_cast<std::size_t>(done.parentLink)]);
            if (parent.parentLink >= 0 &&
                low[static_cast<std::size_t>(done.node)] >= disc[static_cast<std::size_t>(parent.node)])
                isArt[static_cast<std::size_t>(parent.node)] = true;
        }
    }
    for (int i = 0; i < n; ++i)
    {
        if (isArt[static_cast<std::size_t>(i)])
            out.articulationNodes.push_back(nodes[static_cast<std::size_t>(i)].id);
    }
    return out;
}

namespace
{
struct HydraulicSummary
{
    std::vector<double> minPressure; ///< per node index (0-based)
    double deficit = 0.0;
    double demand = 0.0;
};

/// Runs an extended-period hydraulic simulation and records minimum pressures.
HydraulicSummary runHydraulics(engine::EpanetSession& s, const std::vector<int>& junctions,
                               bool pda)
{
    const int n = s.nodeCount();
    HydraulicSummary h;
    h.minPressure.assign(static_cast<std::size_t>(n), std::numeric_limits<double>::max());
    s.openH();
    s.initH(engine::en::NOSAVE);
    long tstep = 0;
    do
    {
        s.runH();
        for (int j : junctions)
        {
            const double p = s.nodeValue(j, engine::en::PRESSURE);
            auto& slot = h.minPressure[static_cast<std::size_t>(j - 1)];
            slot = std::min(slot, p);
            const double d = s.nodeValue(j, engine::en::DEMAND);
            double deficit = 0.0;
            if (pda)
                s.tryNodeValue(j, engine::en::DEMANDDEFICIT, deficit);
            h.demand += std::max(0.0, d) + std::max(0.0, deficit);
            h.deficit += std::max(0.0, deficit);
        }
        tstep = s.nextH();
    } while (tstep > 0);
    s.closeH();
    return h;
}
} // namespace

std::vector<CriticalityEntry> pipeCriticality(const engine::EpanetApi& api,
                                              const std::filesystem::path& input,
                                              const CriticalityOptions& options,
                                              const Progress& progress)
{
    engine::EpanetSession s(api);
    s.open(input);
    if (options.pressureDriven)
    {
        const double preq = options.requiredPressure > 0 ? options.requiredPressure : options.pressureThreshold;
        s.setDemandModel(engine::en::PDA, options.minimumPressure,
                         std::max(preq, options.minimumPressure + 0.1), 0.5);
    }
    s.setTimeParam(engine::en::QUALSTEP, s.timeParam(engine::en::HYDSTEP));

    std::vector<int> junctions;
    for (int i = 1; i <= s.nodeCount(); ++i)
    {
        if (s.nodeType(i) == engine::en::JUNCTION)
            junctions.push_back(i);
    }
    std::vector<int> targets;
    if (options.links.empty())
    {
        for (int i = 1; i <= s.linkCount(); ++i)
        {
            const int t = s.linkType(i);
            if (t == engine::en::PIPE || t == engine::en::CVPIPE)
                targets.push_back(i);
        }
    }
    else
    {
        for (const auto& id : options.links)
        {
            if (int i = s.linkIndex(id))
                targets.push_back(i);
        }
    }

    const HydraulicSummary base = runHydraulics(s, junctions, options.pressureDriven);
    std::vector<CriticalityEntry> out;
    for (std::size_t k = 0; k < targets.size(); ++k)
    {
        const int link = targets[k];
        CriticalityEntry e;
        e.link = s.linkId(link);
        if (progress && !progress(static_cast<double>(k) / targets.size(), "Closing pipe " + e.link))
            break;
        const double original = s.linkValue(link, engine::en::INITSTATUS);
        try
        {
            s.setLinkValue(link, engine::en::INITSTATUS, engine::en::CLOSED);
            const HydraulicSummary h = runHydraulics(s, junctions, options.pressureDriven);
            double worst = std::numeric_limits<double>::max();
            for (int j : junctions)
            {
                const double p = h.minPressure[static_cast<std::size_t>(j - 1)];
                const double p0 = base.minPressure[static_cast<std::size_t>(j - 1)];
                if (p < options.pressureThreshold && p0 >= options.pressureThreshold)
                    ++e.nodesBelowThreshold;
                if (p < worst)
                {
                    worst = p;
                    e.worstNode = s.nodeId(j);
                }
            }
            e.minimumPressure = worst == std::numeric_limits<double>::max() ? 0.0 : worst;
            e.unmetDemandFraction = h.demand > 0 ? std::max(0.0, (h.deficit - base.deficit) / h.demand) : 0.0;
        }
        catch (const engine::EpanetError& err)
        {
            e.failed = true;
            e.error = err.what();
        }
        s.setLinkValue(link, engine::en::INITSTATUS, original);
        out.push_back(e);
    }
    std::stable_sort(out.begin(), out.end(), [](const CriticalityEntry& a, const CriticalityEntry& b) {
        if (a.nodesBelowThreshold != b.nodesBelowThreshold)
            return a.nodesBelowThreshold > b.nodesBelowThreshold;
        return a.unmetDemandFraction > b.unmetDemandFraction;
    });
    if (progress)
        progress(1.0, "Criticality analysis finished");
    return out;
}

std::vector<FireFlowEntry> fireFlow(const engine::EpanetApi& api, const std::filesystem::path& input,
                                    const FireFlowOptions& options, const Progress& progress)
{
    engine::EpanetSession s(api);
    s.open(input);
    // Single-period snapshot at the requested time of day.
    s.setTimeParam(engine::en::DURATION, 0);
    s.setTimeParam(engine::en::PATTERNSTART, static_cast<long>(options.timeOfDay));
    s.addPattern("MIRAB_FIRE", {1.0});

    std::vector<int> junctions;
    for (int i = 1; i <= s.nodeCount(); ++i)
    {
        if (s.nodeType(i) == engine::en::JUNCTION)
            junctions.push_back(i);
    }
    std::vector<int> targets;
    if (options.nodes.empty())
        targets = junctions;
    else
    {
        for (const auto& id : options.nodes)
        {
            if (int i = s.nodeIndex(id))
                targets.push_back(i);
        }
    }

    s.openH();
    auto solveWith = [&](int node, double q, double& atNode, double& minP, int& minNode) {
        const int before = s.demandCount(node);
        s.addDemand(node, q, "MIRAB_FIRE", "fire");
        bool ok = true;
        try
        {
            s.initH(engine::en::NOSAVE);
            s.runH();
            atNode = s.nodeValue(node, engine::en::PRESSURE);
            minP = std::numeric_limits<double>::max();
            for (int j : junctions)
            {
                const double p = s.nodeValue(j, engine::en::PRESSURE);
                if (p < minP)
                {
                    minP = p;
                    minNode = j;
                }
            }
        }
        catch (const engine::EpanetError&)
        {
            ok = false;
        }
        s.deleteDemand(node, before + 1);
        return ok;
    };

    std::vector<FireFlowEntry> out;
    for (std::size_t k = 0; k < targets.size(); ++k)
    {
        const int node = targets[k];
        FireFlowEntry e;
        e.node = s.nodeId(node);
        if (progress && !progress(static_cast<double>(k) / targets.size(), "Fire flow at " + e.node))
            break;
        double atNode = 0, minP = 0;
        int minNode = node;
        if (!solveWith(node, options.fireFlow, atNode, minP, minNode))
        {
            e.failed = true;
            e.error = "hydraulics did not converge with the fire flow applied";
            out.push_back(e);
            continue;
        }
        e.residualPressure = atNode;
        e.minimumPressure = minP;
        e.criticalNode = s.nodeId(minNode);
        e.adequate = minP >= options.residualPressure;
        if (options.computeAvailableFlow)
        {
            double lo = 0.0;
            double hi = options.maximumFlow > 0 ? options.maximumFlow : 4.0 * std::max(options.fireFlow, 1.0);
            for (int it = 0; it < 14; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                double a = 0, mp = 0;
                int mn = node;
                if (solveWith(node, mid, a, mp, mn) && mp >= options.residualPressure)
                    lo = mid;
                else
                    hi = mid;
            }
            e.availableFlow = lo;
        }
        out.push_back(e);
    }
    s.closeH();
    if (progress)
        progress(1.0, "Fire-flow analysis finished");
    return out;
}

} // namespace mirab::resilience
