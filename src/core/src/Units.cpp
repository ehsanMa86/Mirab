/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Units.cpp
 * Purpose: Unit labels and SI conversion factors for EPANET and SWMM projects.
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
#include "mirab/core/Units.h"

#include "mirab/core/Text.h"

namespace mirab::core
{

double flowToCms(std::string_view flowUnits)
{
    const std::string u = text::toUpper(flowUnits);
    if (u == "CFS")
        return 0.028316846592;
    if (u == "GPM")
        return 6.30901964e-5;
    if (u == "MGD")
        return 0.0438126364;
    if (u == "IMGD")
        return 0.0526168042;
    if (u == "AFD")
        return 0.0142764102;
    if (u == "LPS")
        return 0.001;
    if (u == "LPM")
        return 1.0 / 60000.0;
    if (u == "MLD")
        return 1000.0 / 86400.0;
    if (u == "CMH")
        return 1.0 / 3600.0;
    if (u == "CMD")
        return 1.0 / 86400.0;
    if (u == "CMS")
        return 1.0;
    return 0.0;
}

bool isSiFlowUnit(Dialect, std::string_view flowUnits)
{
    const std::string u = text::toUpper(flowUnits);
    return u == "LPS" || u == "LPM" || u == "MLD" || u == "CMH" || u == "CMD" || u == "CMS";
}

ProjectUnits ProjectUnits::fromFlowUnits(Dialect dialect, std::string_view flowUnits,
                                         std::string_view pressureUnits)
{
    ProjectUnits u;
    u.dialect = dialect;
    u.flow = text::toUpper(flowUnits.empty() ? (dialect == Dialect::Swmm ? "CFS" : "GPM")
                                             : flowUnits);
    u.system = isSiFlowUnit(dialect, u.flow) ? UnitSystem::SI : UnitSystem::US;
    u.pressure = text::toUpper(pressureUnits);
    if (u.pressure.empty())
        u.pressure = u.system == UnitSystem::SI ? "METERS" : "PSI";
    return u;
}

std::string ProjectUnits::label(std::string_view quantity) const
{
    const std::string q = text::toLower(quantity);
    const bool si = system == UnitSystem::SI;
    if (q == "flow")
        return text::toLower(flow) == "cfs" ? "cfs" : flow;
    if (q == "length" || q == "elevation" || q == "head" || q == "depth")
        return si ? "m" : "ft";
    if (q == "diameter")
        return dialect == Dialect::Swmm ? (si ? "m" : "ft") : (si ? "mm" : "in");
    if (q == "pressure")
    {
        if (pressure == "METERS")
            return "m";
        if (pressure == "FEET")
            return "ft";
        return text::toLower(pressure);
    }
    if (q == "velocity")
        return si ? "m/s" : "ft/s";
    if (q == "area")
        return dialect == Dialect::Swmm ? (si ? "ha" : "ac") : (si ? "m2" : "ft2");
    if (q == "surfacearea")
        return si ? "m2" : "ft2";
    if (q == "volume")
        return si ? "m3" : "ft3";
    if (q == "power")
        return "kW";
    if (q == "rainfall")
        return si ? "mm/hr" : "in/hr";
    if (q == "headloss")
        return si ? "m/km" : "ft/kft";
    if (q == "smalldepth")
        return si ? "mm" : "in";
    if (q == "rate")
        return si ? "mm/hr" : "in/hr";
    if (q == "evaporation")
        return si ? "mm/day" : "in/day";
    if (q == "loading")
        return si ? "kg/ha" : "lbs/ac";
    if (q == "temperature")
        return si ? "\xC2\xB0" "C" : "\xC2\xB0" "F";
    if (q == "meltrate")
        return si ? "mm/hr/\xC2\xB0" "C" : "in/hr/\xC2\xB0" "F";
    if (q == "percent")
        return "%";
    if (q == "days")
        return "days";
    if (q == "decay")
        return "1/days";
    return {};
}

double ProjectUnits::toSI(std::string_view quantity) const
{
    const std::string q = text::toLower(quantity);
    const bool si = system == UnitSystem::SI;
    if (q == "flow")
        return flowToCms(flow);
    if (q == "length" || q == "elevation" || q == "head" || q == "depth")
        return si ? 1.0 : 0.3048;
    if (q == "diameter")
        return dialect == Dialect::Swmm ? (si ? 1.0 : 0.3048) : (si ? 0.001 : 0.0254);
    if (q == "pressure")
    {
        if (pressure == "PSI")
            return 0.703069578;
        if (pressure == "KPA")
            return 0.101971621;
        if (pressure == "BAR")
            return 10.1971621;
        if (pressure == "FEET")
            return 0.3048;
        return 1.0;
    }
    if (q == "velocity")
        return si ? 1.0 : 0.3048;
    if (q == "area")
        return dialect == Dialect::Swmm ? (si ? 10000.0 : 4046.8564224) : (si ? 1.0 : 0.09290304);
    if (q == "surfacearea")
        return si ? 1.0 : 0.09290304;
    if (q == "volume")
        return si ? 1.0 : 0.028316846592;
    if (q == "smalldepth")
        return si ? 0.001 : 0.0254;
    return 1.0;
}

} // namespace mirab::core
