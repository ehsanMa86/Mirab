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
} // namespace

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
    }
    return out;
}

} // namespace mirab::core
