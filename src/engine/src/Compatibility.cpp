/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/Compatibility.cpp
 * Purpose: Compares detected project features with an engine manifest and strips ignorable ones.
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
#include "mirab/engine/Compatibility.h"

#include "mirab/core/Text.h"

namespace mirab::engine
{

using core::Feature;

bool CompatibilityReport::runnable() const
{
    return count(Support::Unsupported) == 0;
}

int CompatibilityReport::count(Support level) const
{
    int n = 0;
    for (const auto& i : issues)
    {
        if (i.level == level)
            ++n;
    }
    return n;
}

namespace
{
std::string consequence(Feature f, Support level, const EngineDescriptor& e)
{
    const std::string name = core::featureName(f);
    if (level == Support::Unsupported)
        return name + " is not available in " + e.name + "; choose another engine or remove it";
    switch (f)
    {
    case Feature::EpanetLeakage:
        return name + " is not modeled by " + e.name + "; the [LEAKAGE] section will be skipped";
    case Feature::EpanetPressureUnitsExtended:
        return name + " are not supported by " + e.name +
               "; pressures will be reported in the default units";
    case Feature::EpanetEmitterBackflow:
        return name + " is not supported by " + e.name + "; the option will be skipped";
    case Feature::SwmmStreetsAndInlets:
        return name + " are not supported by " + e.name + "; those sections will be skipped";
    case Feature::SwmmNonDynamicRouting:
        return e.name + " always solves the full dynamic wave equations; the routing option is ignored";
    case Feature::SwmmInertialDamping:
        return e.name + " supports INERTIAL_DAMPING NONE only; it will be set to NONE for the run";
    default:
        return name + " is ignored by " + e.name;
    }
}
} // namespace

CompatibilityReport checkCompatibility(const core::Model& model, const EngineDescriptor& engine)
{
    CompatibilityReport report;
    if (model.dialect() != engine.dialect)
    {
        CompatibilityIssue issue;
        issue.level = Support::Unsupported;
        issue.message = std::string("this is an ") + core::dialectName(model.dialect()) +
                        " project but " + engine.name + " runs " + core::dialectName(engine.dialect) +
                        " models";
        report.issues.push_back(issue);
        return report;
    }
    for (const auto& use : core::detectFeatures(model))
    {
        const Support s = engine.supportFor(use.feature);
        if (s == Support::Full)
            continue;
        CompatibilityIssue issue;
        issue.level = s;
        issue.feature = use.feature;
        issue.message = consequence(use.feature, s, engine) + " (" + use.detail + ")";
        issue.section = use.section;
        issue.row = use.row;
        report.issues.push_back(issue);
    }
    return report;
}

std::string prepareInput(const core::Model& model, const EngineDescriptor& engine,
                         std::vector<std::string>* notes)
{
    core::InpDocument doc = model.document();
    auto note = [&](const std::string& n) {
        if (notes)
            notes->push_back(n);
    };
    for (const auto& use : core::detectFeatures(model))
    {
        if (engine.supportFor(use.feature) != Support::Ignored)
            continue;
        switch (use.feature)
        {
        case Feature::EpanetLeakage:
            if (doc.remove("LEAKAGE"))
                note("removed the [LEAKAGE] section");
            break;
        case Feature::EpanetPressureUnitsExtended:
            if (doc.removeKeyValue("OPTIONS", "PRESSURE"))
                note("removed the PRESSURE units option");
            break;
        case Feature::EpanetEmitterBackflow:
            if (doc.removeKeyValue("OPTIONS", "EMITTER BACKFLOW"))
                note("removed the EMITTER BACKFLOW option");
            break;
        case Feature::SwmmInertialDamping:
            doc.setKeyValue("OPTIONS", "INERTIAL_DAMPING", "NONE");
            note("set INERTIAL_DAMPING to NONE");
            break;
        case Feature::SwmmStreetsAndInlets:
            for (const char* s : {"STREETS", "INLETS", "INLET_USAGE"})
            {
                if (doc.remove(s))
                    note(std::string("removed the [") + s + "] section");
            }
            break;
        default:
            break;
        }
    }
    return doc.toString();
}

} // namespace mirab::engine
