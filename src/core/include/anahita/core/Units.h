/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/include/anahita/core/Units.h
 * Purpose: Unit systems of EPANET and SWMM projects and conversions to SI.
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

#include "anahita/core/Schema.h"

#include <string>
#include <string_view>

namespace anahita::core
{

enum class UnitSystem
{
    US,
    SI
};

/// Units of a project as declared by its flow units option.
struct ProjectUnits
{
    Dialect dialect = Dialect::Epanet;
    std::string flow = "GPM"; ///< flow units keyword (GPM, LPS, CFS, CMS, ...)
    std::string pressure;     ///< EPANET pressure units keyword (PSI, KPA, METERS, BAR, FEET)
    UnitSystem system = UnitSystem::US;

    static ProjectUnits fromFlowUnits(Dialect dialect, std::string_view flowUnits,
                                      std::string_view pressureUnits = {});

    /// Display label for a quantity family (length, diameter, flow, pressure,
    /// head, elevation, velocity, depth, area, volume, roughness, power, rainfall).
    std::string label(std::string_view quantity) const;

    /// Factor that converts a value of the quantity in project units to SI
    /// (m, m3/s, m/s, m2, m3, kW, pressure as metres of head).
    double toSI(std::string_view quantity) const;
};

/// Cubic metres per second per one unit of the flow keyword; 0 when unknown.
double flowToCms(std::string_view flowUnits);
bool isSiFlowUnit(Dialect dialect, std::string_view flowUnits);

} // namespace anahita::core
