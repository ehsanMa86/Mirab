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
    SwmmControls
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

/// Feature that the data of a section needs, e.g. [POLLUTANTS] needs SWMM
/// water quality. @p tokens refines the answer for rows whose meaning depends
/// on their values (an [INFLOWS] row of a pollutant). Returns false when the
/// section needs no particular feature.
bool featureForSection(Dialect dialect, std::string_view section,
                       const std::vector<std::string>& tokens, Feature& feature);

} // namespace mirab::core
