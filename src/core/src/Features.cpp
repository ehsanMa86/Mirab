/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Features.cpp
 * Purpose: Scans an input document for features that only some engines support.
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
#include "mirab/core/Features.h"

#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace mirab::core
{

const char* featureName(Feature f)
{
    switch (f)
    {
    case Feature::EpanetPressureDrivenDemand:
        return "Pressure-driven demand";
    case Feature::EpanetLeakage:
        return "FAVAD pipe leakage";
    case Feature::EpanetPcvValve:
        return "Positional control valve (PCV)";
    case Feature::EpanetFlowUnitsCms:
        return "CMS flow units";
    case Feature::EpanetPressureUnitsExtended:
        return "BAR or FEET pressure units";
    case Feature::EpanetEmitterBackflow:
        return "Emitter backflow option";
    case Feature::EpanetWaterQuality:
        return "Single-species water quality";
    case Feature::SwmmHydrology:
        return "Runoff hydrology";
    case Feature::SwmmWaterQuality:
        return "Pollutant transport";
    case Feature::SwmmLowImpactDevelopment:
        return "Low impact development controls";
    case Feature::SwmmGroundwater:
        return "Groundwater";
    case Feature::SwmmSnowmelt:
        return "Snowmelt";
    case Feature::SwmmRdii:
        return "Rainfall-dependent infiltration/inflow";
    case Feature::SwmmStreetsAndInlets:
        return "Streets and inlets (SWMM 5.2)";
    case Feature::SwmmNonDynamicRouting:
        return "Kinematic wave or steady flow routing";
    case Feature::SwmmInertialDamping:
        return "Inertial damping (PARTIAL or FULL)";
    case Feature::SwmmControls:
        return "Control rules";
    case Feature::SwmmDeadEndJunctions:
        return "Dead-end junctions without inflow";
    case Feature::SwmmTerminalJunctions:
        return "Terminal junctions";
    case Feature::SwmmShortInflowSeries:
        return "Inflow time series shorter than the simulation";
    }
    return "?";
}

namespace
{
bool hasRows(const InpDocument& doc, const char* section)
{
    const InpSection* s = doc.find(section);
    return s && s->rowCount() > 0;
}

std::string firstWord(const std::string& value)
{
    const auto t = text::splitInpLine(value).tokens;
    return t.empty() ? std::string() : text::toUpper(t[0]);
}

/// "J1, J4" or "J1, J2, J3, J4, J5 and 3 more".
std::string idList(const std::vector<std::string>& ids)
{
    std::string s;
    const std::size_t shown = std::min<std::size_t>(ids.size(), 5);
    for (std::size_t i = 0; i < shown; ++i)
        s += (i ? ", " : "") + ids[i];
    if (ids.size() > shown)
        s += " and " + std::to_string(ids.size() - shown) + " more";
    return s;
}

int nodeRow(const Model& model, const std::string& id)
{
    const NodeItem* n = model.findNode(id);
    return n ? n->row : -1;
}
} // namespace

DeadEndJunctions deadEndJunctions(const Model& model)
{
    DeadEndJunctions out;
    if (model.dialect() != Dialect::Swmm)
        return out;
    std::map<std::string, int> leaving, ending;
    for (const auto& l : model.links())
    {
        ++leaving[text::toUpper(l.from)];
        ++ending[text::toUpper(l.to)];
    }
    std::set<std::string> inflow;
    for (const char* s : {"INFLOWS", "DWF"})
    {
        if (const InpSection* sec = model.document().find(s))
        {
            for (const auto& t : sec->allRowTokens())
            {
                if (t.size() > 1 && text::iequals(t[1], "FLOW"))
                    inflow.insert(text::toUpper(t[0]));
            }
        }
    }
    for (const auto& n : model.nodes())
    {
        if (n.kind != "Junction")
            continue;
        // SWMM IDs are not case sensitive.
        const std::string key = text::toUpper(n.id);
        const int leaves = leaving.count(key) ? leaving[key] : 0;
        const int ends = ending.count(key) ? ending[key] : 0;
        if (leaves == 1 && ends == 0 && !inflow.count(key))
            out.withoutInflow.push_back(n.id);
        else if (leaves == 0 && ends == 1)
            out.terminal.push_back(n.id);
    }
    return out;
}

std::vector<ShortInflowSeries> shortInflowSeries(const Model& model)
{
    std::vector<ShortInflowSeries> out;
    const InpDocument& doc = model.document();
    const InpSection* inflows = doc.find("INFLOWS");
    const InpSection* series = doc.find("TIMESERIES");
    const double duration = model.durationSeconds();
    if (model.dialect() != Dialect::Swmm || !inflows || !series || duration <= 0.0)
        return out;
    std::set<std::string> used;
    for (const auto& t : inflows->allRowTokens())
    {
        if (t.size() > 2 && !t[2].empty() && t[2] != "\"\"")
            used.insert(text::toUpper(t[2]));
    }
    // Dated points are compared with the simulation start.
    long long startDays = 0;
    const bool haveStart = parseSwmmDate(model.option("START_DATE"), startDays);
    const double start = static_cast<double>(startDays) * 86400.0 +
                         text::parseClockOrDuration(model.option("START_TIME")).value_or(0.0);

    struct Points
    {
        std::string name;
        bool readable = true; ///< inline data that parses
        bool dated = false;
        long long date = 0;  ///< carried from the last date token, as SWMM does
        bool any = false;
        double end = 0.0;
        double last = 0.0;
    };
    std::vector<Points> all;
    std::map<std::string, std::size_t> index;
    for (const auto& t : series->allRowTokens())
    {
        if (t.empty() || !used.count(text::toUpper(t[0])))
            continue;
        auto [it, added] = index.emplace(text::toUpper(t[0]), all.size());
        if (added)
            all.push_back({t[0]});
        Points& p = all[it->second];
        if (t.size() > 1 && text::iequals(t[1], "FILE"))
            p.readable = false;
        for (std::size_t i = 1; p.readable && i < t.size();)
        {
            if (t[i].find('/') != std::string::npos)
            {
                p.readable = haveStart && parseSwmmDate(t[i], p.date);
                p.dated = true;
                ++i;
                continue;
            }
            const auto clock = text::parseClockOrDuration(t[i]);
            const auto value = i + 1 < t.size() ? text::toDouble(t[i + 1]) : std::nullopt;
            if (!clock || !value)
            {
                p.readable = false;
                break;
            }
            p.end = p.dated ? static_cast<double>(p.date) * 86400.0 + *clock - start : *clock;
            p.last = *value;
            p.any = true;
            i += 2;
        }
    }
    auto timeTokens = [&](const Points& p, double seconds) {
        if (!p.dated)
            return std::vector<std::string>{text::formatDouble(seconds / 3600.0)};
        const double at = start + seconds;
        const long long days = static_cast<long long>(std::floor(at / 86400.0));
        const int clock = static_cast<int>(std::lround(at - static_cast<double>(days) * 86400.0));
        int y = 0, m = 0, d = 0;
        civilFromDays(days, y, m, d);
        char date[16], time[16];
        std::snprintf(date, sizeof date, "%02d/%02d/%04d", m, d, y);
        std::snprintf(time, sizeof time, "%02d:%02d:%02d", clock / 3600, clock / 60 % 60, clock % 60);
        return std::vector<std::string>{date, time};
    };
    for (const auto& p : all)
    {
        if (!p.readable || !p.any || p.end > duration)
            continue;
        ShortInflowSeries s{p.name, p.end, {}};
        auto row = [&](double seconds) {
            std::vector<std::string> r{p.name};
            for (auto& tok : timeTokens(p, seconds))
                r.push_back(tok);
            r.push_back("0");
            s.zeroRows.push_back(r);
        };
        // The inflow drops to zero right after the last point, then stays
        // there an hour past the end of the simulation.
        if (p.last != 0.0)
            row(p.end + 1.0);
        row(duration + 3600.0);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<FeatureUse> detectFeatures(const Model& model)
{
    std::vector<FeatureUse> out;
    const InpDocument& doc = model.document();
    if (model.dialect() == Dialect::Epanet)
    {
        if (model.demandModel() == "PDA")
            out.push_back({Feature::EpanetPressureDrivenDemand, "DEMAND MODEL PDA", "OPTIONS", -1});
        if (hasRows(doc, "LEAKAGE"))
            out.push_back({Feature::EpanetLeakage,
                           std::to_string(doc.find("LEAKAGE")->rowCount()) + " leaking pipe(s)",
                           "LEAKAGE", 0});
        if (const InpSection* v = doc.find("VALVES"))
        {
            for (int r = 0; r < v->rowCount(); ++r)
            {
                const auto t = v->rowTokens(r);
                if (t.size() > 4 && text::iequals(t[4], "PCV"))
                    out.push_back({Feature::EpanetPcvValve, "valve " + t[0], "VALVES", r});
            }
        }
        if (firstWord(model.option("UNITS")) == "CMS")
            out.push_back({Feature::EpanetFlowUnitsCms, "UNITS CMS", "OPTIONS", -1});
        const std::string press = firstWord(model.option("PRESSURE"));
        if (press == "BAR" || press == "FEET")
            out.push_back({Feature::EpanetPressureUnitsExtended, "PRESSURE " + press, "OPTIONS", -1});
        if (doc.keyValue("OPTIONS", "EMITTER BACKFLOW"))
            out.push_back({Feature::EpanetEmitterBackflow, "EMITTER BACKFLOW", "OPTIONS", -1});
        const std::string qual = firstWord(model.option("QUALITY"));
        if (!qual.empty() && qual != "NONE")
            out.push_back({Feature::EpanetWaterQuality, "QUALITY " + qual, "OPTIONS", -1});
    }
    else if (model.dialect() == Dialect::Swmm)
    {
        if (hasRows(doc, "SUBCATCHMENTS"))
            out.push_back({Feature::SwmmHydrology, "subcatchments", "SUBCATCHMENTS", 0});
        if (hasRows(doc, "POLLUTANTS"))
            out.push_back({Feature::SwmmWaterQuality, "pollutants", "POLLUTANTS", 0});
        if (hasRows(doc, "LID_USAGE"))
            out.push_back({Feature::SwmmLowImpactDevelopment, "LID usage", "LID_USAGE", 0});
        if (hasRows(doc, "GROUNDWATER"))
            out.push_back({Feature::SwmmGroundwater, "groundwater", "GROUNDWATER", 0});
        if (hasRows(doc, "SNOWPACKS"))
            out.push_back({Feature::SwmmSnowmelt, "snow packs", "SNOWPACKS", 0});
        if (hasRows(doc, "RDII"))
            out.push_back({Feature::SwmmRdii, "RDII inflows", "RDII", 0});
        for (const char* s : {"STREETS", "INLETS", "INLET_USAGE"})
        {
            if (hasRows(doc, s))
            {
                out.push_back({Feature::SwmmStreetsAndInlets, s, s, 0});
                break;
            }
        }
        const std::string routing = firstWord(model.option("FLOW_ROUTING"));
        if (routing == "KINWAVE" || routing == "STEADY")
            out.push_back({Feature::SwmmNonDynamicRouting, "FLOW_ROUTING " + routing, "OPTIONS", -1});
        // SWMM's default inertial damping is PARTIAL when the option is absent.
        const std::string damping = firstWord(model.option("INERTIAL_DAMPING"));
        if (damping != "NONE")
            out.push_back({Feature::SwmmInertialDamping,
                           "INERTIAL_DAMPING " + (damping.empty() ? std::string("PARTIAL (default)") : damping),
                           "OPTIONS", -1});
        if (hasRows(doc, "CONTROLS"))
            out.push_back({Feature::SwmmControls, "control rules", "CONTROLS", 0});
        const auto ends = deadEndJunctions(model);
        if (!ends.withoutInflow.empty())
            out.push_back({Feature::SwmmDeadEndJunctions, idList(ends.withoutInflow), "JUNCTIONS",
                           nodeRow(model, ends.withoutInflow.front())});
        if (!ends.terminal.empty())
            out.push_back({Feature::SwmmTerminalJunctions, idList(ends.terminal), "JUNCTIONS",
                           nodeRow(model, ends.terminal.front())});
        std::vector<std::string> shortSeries;
        for (const auto& s : shortInflowSeries(model))
            shortSeries.push_back(s.name + " ends at " + text::formatDuration(s.endSeconds));
        if (!shortSeries.empty())
            out.push_back({Feature::SwmmShortInflowSeries, idList(shortSeries), "TIMESERIES", 0});
    }
    return out;
}

bool featureForSection(Dialect dialect, std::string_view section,
                       const std::vector<std::string>& tokens, Feature& feature)
{
    const std::string s = text::toUpper(section);
    if (dialect == Dialect::Epanet)
    {
        if (s == "LEAKAGE")
        {
            feature = Feature::EpanetLeakage;
            return true;
        }
        return false;
    }
    if (dialect != Dialect::Swmm)
        return false;
    struct Map
    {
        const char* section;
        Feature feature;
    };
    static const Map maps[] = {{"POLLUTANTS", Feature::SwmmWaterQuality},
                               {"LANDUSES", Feature::SwmmWaterQuality},
                               {"BUILDUP", Feature::SwmmWaterQuality},
                               {"WASHOFF", Feature::SwmmWaterQuality},
                               {"COVERAGES", Feature::SwmmWaterQuality},
                               {"LOADINGS", Feature::SwmmWaterQuality},
                               {"TREATMENT", Feature::SwmmWaterQuality},
                               {"LID_CONTROLS", Feature::SwmmLowImpactDevelopment},
                               {"LID_USAGE", Feature::SwmmLowImpactDevelopment},
                               {"AQUIFERS", Feature::SwmmGroundwater},
                               {"GROUNDWATER", Feature::SwmmGroundwater},
                               {"GWF", Feature::SwmmGroundwater},
                               {"SNOWPACKS", Feature::SwmmSnowmelt},
                               {"HYDROGRAPHS", Feature::SwmmRdii},
                               {"RDII", Feature::SwmmRdii},
                               {"STREETS", Feature::SwmmStreetsAndInlets},
                               {"INLETS", Feature::SwmmStreetsAndInlets},
                               {"INLET_USAGE", Feature::SwmmStreetsAndInlets},
                               {"CONTROLS", Feature::SwmmControls}};
    for (const auto& m : maps)
    {
        if (s == m.section)
        {
            feature = m.feature;
            return true;
        }
    }
    // Inflows of a pollutant (rather than of FLOW) need water quality.
    if ((s == "INFLOWS" || s == "DWF") && tokens.size() > 1 && !text::iequals(tokens[1], "FLOW"))
    {
        feature = Feature::SwmmWaterQuality;
        return true;
    }
    return false;
}

} // namespace mirab::core
