/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Features.h
 * Purpose: Detection of version-specific modeling features used by a project.
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
#include <vector>

namespace mirab::core
{

/// Modeling features whose support differs between engines.
enum class Feature
{
    // EPANET
    EpanetPressureDrivenDemand,
    EpanetLeakage,
    EpanetPcvValve,
    EpanetFlowUnitsCms,
    EpanetPressureUnitsExtended,
    EpanetEmitterBackflow,
    EpanetWaterQuality,
    // SWMM
    SwmmHydrology,
    SwmmWaterQuality,
    SwmmLowImpactDevelopment,
    SwmmGroundwater,
    SwmmSnowmelt,
    SwmmRdii,
    SwmmStreetsAndInlets,
    SwmmNonDynamicRouting,
    SwmmInertialDamping,
    SwmmControls,
    SwmmDeadEndJunctions,  ///< upstream end junctions with no inflow of their own
    SwmmTerminalJunctions, ///< junctions where a link ends and no other link leaves
    SwmmShortInflowSeries  ///< [INFLOWS] time series that end before the simulation
};

const char* featureName(Feature f);

struct FeatureUse
{
    Feature feature;
    std::string detail;
    std::string section;
    int row = -1;
};

std::vector<FeatureUse> detectFeatures(const Model& model);

/// SWMM junctions connected to a single link.
struct DeadEndJunctions
{
    std::vector<std::string> withoutInflow; ///< the link leaves them; no FLOW row in [INFLOWS] or [DWF]
    std::vector<std::string> terminal;      ///< the link ends at them
};
DeadEndJunctions deadEndJunctions(const Model& model);

/// An inflow time series (inline data of [TIMESERIES]) whose last point lies
/// before the end of the simulation. EPA SWMM takes such an inflow as zero
/// after the last point.
struct ShortInflowSeries
{
    std::string name;
    double endSeconds = 0.0; ///< last point, from the simulation start
    /// [TIMESERIES] rows that continue the series with zero past the end of
    /// the simulation, in the series' own time format.
    std::vector<std::vector<std::string>> zeroRows;
};
std::vector<ShortInflowSeries> shortInflowSeries(const Model& model);

/// Feature that the data of a section needs, e.g. [POLLUTANTS] needs SWMM
/// water quality. @p tokens refines the answer for rows whose meaning depends
/// on their values (an [INFLOWS] row of a pollutant). Returns false when the
/// section needs no particular feature.
bool featureForSection(Dialect dialect, std::string_view section,
                       const std::vector<std::string>& tokens, Feature& feature);

} // namespace mirab::core
