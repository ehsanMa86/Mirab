/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Linkage.h
 * Purpose: Link between a water distribution model and a sewer model: node matching and dry-weather flow from water use.
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

#include "mirab/core/Json.h"
#include "mirab/core/Model.h"
#include "mirab/core/Results.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

/// A SWMM project can be linked to the EPANET model of the same area. Each
/// EPANET junction with a demand is assigned to a SWMM node (the nearest
/// one, within limits, or a manual choice), and the water used there,
/// times a return factor, becomes the dry-weather inflow ([DWF]) of that
/// node, with hourly patterns derived from the EPANET demand patterns or
/// simulation results.
namespace mirab::core
{

/// Settings of the link, stored as "waterLink" in the SWMM project's sidecar.
struct WaterLinkSettings
{
    std::string waterModel;   ///< EPANET input file, relative to the SWMM file
    double maxDistance = 0.0; ///< in SWMM map units; 0 means no limit
    std::string nodeTag;      ///< only SWMM nodes with this tag ([TAGS]); empty: all
    /// Service areas: "" (none), "subcatchments" (the SWMM subcatchment
    /// outlines) or a polygon file (shapefile or DXF) relative to the SWMM file.
    /// An EPANET node inside an area only drains to SWMM nodes in the same area.
    std::string serviceAreas;
    bool storage = true;        ///< storage units can receive flow (junctions always can)
    double returnFactor = 0.8;  ///< share of the water used that reaches the sewer
    std::map<std::string, double> categoryFactors; ///< return factor per demand category
    std::vector<std::string> categories;           ///< only these categories; empty: all
    std::string patternPrefix = "WW_";
    bool weekend = false;    ///< weekly sources: WEEKEND and DAILY patterns too
    int weekStart = 0;       ///< day of week at EPANET time zero, 0 = Monday
    double lagHours = 0.0;   ///< sewer travel time added to the patterns
    std::string results;     ///< EPANET results (.anr) relative to the SWMM file; empty: demands
    /// Manual choices: EPANET node -> SWMM node; an empty SWMM node excludes it.
    std::map<std::string, std::string> overrides;
    std::map<std::string, std::string> links; ///< assignment written by the last apply
    bool showReference = true; ///< draw the water network under the SWMM map

    /// Return factor of a demand category ("" is the uncategorised demand);
    /// 0 when the category is left out.
    double factorFor(const std::string& category) const;
    static WaterLinkSettings fromJson(const json::Value& v);
    json::Value toJson() const;
};

/// One demand of an EPANET junction, in the EPANET flow units, with the
/// global demand multiplier applied.
struct WaterDemand
{
    std::string node;
    std::string category; ///< [DEMANDS] category name; empty for none
    double base = 0.0;
    std::string pattern;  ///< resolved pattern ID; empty for a constant demand
};

/// Demands of the junctions: [DEMANDS] rows replace the [JUNCTIONS] demand of
/// their junction. Category names are read from the row comment (EPANET's
/// format) or a fourth value.
std::vector<WaterDemand> waterDemands(const Model& water);
/// Category names used by the demands, sorted; "" stands for demands without one.
std::vector<std::string> demandCategories(const Model& water);

struct ServiceArea
{
    std::string name;
    std::vector<Point> ring; ///< in SWMM map coordinates
};
/// Outlines of the SWMM subcatchments as service areas.
std::vector<ServiceArea> subcatchmentAreas(const Model& swmm);

/// What the link needs besides the two models and the settings.
struct LinkContext
{
    std::string waterCrs; ///< coordinate systems of the models; when both are set and
    std::string swmmCrs;  ///< differ, EPANET coordinates are reprojected
    std::vector<ServiceArea> serviceAreas;
    const ResultSet* results = nullptr; ///< EPANET results, used instead of demands
};

struct NodeAssignment
{
    enum class Status
    {
        Matched,       ///< nearest SWMM node
        Manual,        ///< chosen in the overrides
        TooFar,        ///< nearest SWMM node farther than the distance limit
        NoCoordinates, ///< the EPANET node has no coordinates
        OutsideAreas,  ///< outside every service area
        NoCandidate,   ///< no SWMM node can receive flow (in its area)
        Excluded       ///< left out in the overrides
    };
    std::string waterNode;
    std::string swmmNode; ///< empty when not assigned
    Status status = Status::Matched;
    Point waterPos;       ///< in SWMM map coordinates
    bool placed = false;
    double distance = -1.0; ///< to the assigned node, or to the nearest one when too far
    std::string nearest;    ///< nearest SWMM node when too far
    std::string area;       ///< service area of the EPANET node
    double demand = 0.0;    ///< average water demand, SWMM flow units
    double returned = 0.0;  ///< average returned flow, SWMM flow units
};
const char* statusName(NodeAssignment::Status status);

/// Assigns each EPANET junction with a demand to a SWMM node.
std::vector<NodeAssignment> matchWaterNodes(const Model& water, const Model& swmm,
                                            const WaterLinkSettings& settings,
                                            const LinkContext& context,
                                            std::vector<std::string>* notes = nullptr);

struct SwmmPattern
{
    std::string name;
    std::string type; ///< HOURLY, WEEKEND or DAILY
    std::vector<double> factors;
};

/// Dry-weather inflow of one SWMM node.
struct DwfInflow
{
    std::string node;
    double average = 0.0; ///< SWMM flow units
    std::string daily, hourly, weekend; ///< pattern names; empty for none
    std::vector<std::string> sources;   ///< EPANET nodes
};

struct DwfPlan
{
    std::vector<NodeAssignment> assignments;
    std::vector<DwfInflow> inflows;
    std::vector<SwmmPattern> patterns;
    std::string flowUnits; ///< SWMM flow units of the values below
    double totalDemand = 0.0;    ///< average water demand of every category
    double includedDemand = 0.0; ///< of the categories that return flow
    double returned = 0.0;       ///< average dry-weather inflow written
    double unassignedFlow = 0.0; ///< returned flow of nodes without a SWMM node
    int unassigned = 0;          ///< demand nodes without a SWMM node
    int tooFar = 0;              ///< of which farther than the distance limit
    int days = 1;                ///< days of the source profiles
    std::vector<std::string> notes;
};

/// Computes the dry-weather inflows. Throws std::runtime_error when the
/// results do not cover a day or the flow units are unknown.
DwfPlan planDwf(const Model& water, const Model& swmm, const WaterLinkSettings& settings,
                const LinkContext& context);

/// Writes the plan into the SWMM model: the FLOW row of [DWF] of every node
/// that receives flow, the patterns, and removes FLOW rows and patterns of
/// an earlier apply (patterns named with @p patternPrefix) that are no longer
/// used. Other rows keep their text. Returns the number of rows written or removed.
int applyDwf(Model& swmm, const DwfPlan& plan, const std::string& patternPrefix);

// Building blocks, exposed for tests.

/// Converts a flow between two flow unit keywords (GPM, LPS, CFS, CMS, MGD, ...).
double convertFlow(double value, std::string_view from, std::string_view to);

/// Timing of an EPANET pattern.
struct PatternTiming
{
    double step = 3600.0;  ///< pattern time step, seconds
    double start = 0.0;    ///< pattern start, seconds
    double clock = 0.0;    ///< start clock time, seconds after midnight
};
/// Hourly averages of an EPANET pattern over @p days days, hour by clock hour
/// from midnight of the first day (the pattern repeats).
std::vector<double> hourlyPatternSeries(const std::vector<double>& multipliers,
                                        const PatternTiming& timing, int days);
/// Hourly averages of values reported at @p times (seconds from the start,
/// each held until the next one) over @p days days of clock hours. Hours
/// before the first value use the value of the same clock hour a day later.
std::vector<double> hourlyResultSeries(const std::vector<double>& times,
                                       const std::vector<double>& values, double clock, int days);
/// Delays a cyclic hourly series by @p hours (fractions interpolate linearly).
std::vector<double> lagSeries(const std::vector<double>& series, double hours);
/// Scales a series to a mean of 1; returns it unchanged when the mean is 0.
std::vector<double> normalised(std::vector<double> series);

/// Hourly and daily profiles of a multi-day hourly series (from midnight).
struct DailyProfiles
{
    std::vector<double> hourly;  ///< 24 values, mean 1 (weekdays when split)
    std::vector<double> weekend; ///< 24 values, mean 1; empty when not split
    std::vector<double> daily;   ///< 7 day factors, Sunday first, mean 1; empty when not split
    double mean = 0.0;           ///< mean of the whole series
};
/// With @p weekend and at least 7 days, weekdays and the weekend get their own
/// hourly profiles and the day totals a DAILY profile. @p weekStart is the
/// day of week of the first day (0 = Monday).
DailyProfiles dailyProfiles(const std::vector<double>& series, bool weekend, int weekStart);

} // namespace mirab::core
