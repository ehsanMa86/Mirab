/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/EngineRegistry.cpp
 * Purpose: Built-in engine catalog with capability manifests, and engine library discovery.
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
#include "mirab/engine/EngineRegistry.h"

#include "Adapters.h"
#include "mirab/MirabConfig.h"
#include "mirab/core/Text.h"
#include "mirab/engine/EngineInstall.h"

#include <cstdlib>

namespace mirab::engine
{

using core::Dialect;
using core::Feature;

std::vector<EngineDescriptor> builtinDescriptors()
{
    std::vector<EngineDescriptor> v;

    EngineDescriptor epa22;
    epa22.id = "epanet-2.2-epa";
    epa22.name = "EPA EPANET 2.2";
    epa22.version = "2.2";
    epa22.provider = "U.S. Environmental Protection Agency";
    epa22.license = "Public domain and MIT";
    epa22.upstreamUrl = "https://github.com/USEPA/epanet-engine";
    epa22.description = "The official EPA release of the EPANET hydraulic and water quality solver.";
    epa22.dialect = Dialect::Epanet;
    epa22.library = "mirab_epanet22_epa";
    epa22.support = {{Feature::EpanetLeakage, Support::Ignored},
                     {Feature::EpanetPcvValve, Support::Unsupported},
                     {Feature::EpanetFlowUnitsCms, Support::Unsupported},
                     {Feature::EpanetPressureUnitsExtended, Support::Ignored},
                     {Feature::EpanetEmitterBackflow, Support::Ignored}};
    v.push_back(epa22);

    EngineDescriptor owa23;
    owa23.id = "epanet-2.3-owa";
    owa23.name = "OWA EPANET 2.3";
    owa23.version = "2.3.5";
    owa23.provider = "Open Water Analytics community";
    owa23.license = "MIT";
    owa23.upstreamUrl = "https://github.com/OpenWaterAnalytics/EPANET";
    owa23.description = "Community EPANET with pressure-driven demands, FAVAD pipe leakage, "
                        "positional control valves and extended units.";
    owa23.dialect = Dialect::Epanet;
    owa23.library = "mirab_epanet23_owa";
    v.push_back(owa23);

    EngineDescriptor msx22 = epa22;
    msx22.id = "epanet-msx-2.0-epa22";
    msx22.name = "EPANET-MSX 2.0 on EPA EPANET 2.2";
    msx22.version = "2.0";
    msx22.license = "MIT";
    msx22.upstreamUrl = "https://github.com/USEPA/EPANETMSX";
    msx22.description = "Multi-species reactive transport on top of EPA EPANET 2.2 hydraulics.";
    msx22.library = "mirab_msx20_22_epa";
    msx22.companion = "mirab_epanet22_epa";
    msx22.multiSpecies = true;
    msx22.baseEngineId = epa22.id;
    v.push_back(msx22);

    EngineDescriptor msx23 = owa23;
    msx23.id = "epanet-msx-2.0-owa23";
    msx23.name = "EPANET-MSX 2.0 on OWA EPANET 2.3";
    msx23.version = "2.0";
    msx23.provider = "U.S. EPA (MSX) and Open Water Analytics (EPANET)";
    msx23.upstreamUrl = "https://github.com/USEPA/EPANETMSX";
    msx23.description = "Multi-species reactive transport on top of OWA EPANET 2.3 hydraulics.";
    msx23.library = "mirab_msx20_23_owa";
    msx23.companion = "mirab_epanet23_owa";
    msx23.multiSpecies = true;
    msx23.baseEngineId = owa23.id;
    v.push_back(msx23);

    EngineDescriptor swmm;
    swmm.id = "swmm-5.2-epa";
    swmm.name = "EPA SWMM 5.2";
    swmm.version = "5.2.4";
    swmm.provider = "U.S. Environmental Protection Agency";
    swmm.license = "Public domain";
    swmm.upstreamUrl = "https://github.com/USEPA/Stormwater-Management-Model";
    swmm.description = "The official EPA Storm Water Management Model solver.";
    swmm.dialect = Dialect::Swmm;
    swmm.library = "mirab_swmm52_epa";
    v.push_back(swmm);

    EngineDescriptor s5p;
    s5p.id = "swmm5plus-1.0";
    s5p.name = "SWMM5+ 1.0";
    s5p.version = "1.0";
    s5p.provider = "CIMM (University of Texas at Austin)";
    s5p.license = "Public domain (Unlicense)";
    s5p.upstreamUrl = "https://github.com/CIMM-ORG/SWMM5plus";
    s5p.description = "Finite-volume dynamic-wave hydraulics coupled to EPA SWMM hydrology, "
                      "loaded through the swmm5plus-capi C interface.";
    s5p.dialect = Dialect::Swmm;
    s5p.library = "swmm5plus_capi";
    s5p.optional = true;
    s5p.support = {{Feature::SwmmWaterQuality, Support::Ignored},
                   {Feature::SwmmLowImpactDevelopment, Support::Ignored},
                   {Feature::SwmmStreetsAndInlets, Support::Ignored},
                   {Feature::SwmmNonDynamicRouting, Support::Ignored},
                   {Feature::SwmmInertialDamping, Support::Ignored},
                   {Feature::SwmmDeadEndJunctions, Support::Ignored},
                   {Feature::SwmmTerminalJunctions, Support::Unsupported}};
    v.push_back(s5p);

    return v;
}

EngineRegistry::EngineRegistry() : EngineRegistry(defaultSearchDirectories()) {}

EngineRegistry::EngineRegistry(std::vector<std::filesystem::path> searchDirectories)
    : dirs_(std::move(searchDirectories)), descriptors_(builtinDescriptors())
{
}

std::vector<std::filesystem::path> EngineRegistry::defaultSearchDirectories()
{
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("MIRAB_ENGINE_PATH"))
    {
#if defined(_WIN32)
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string cur;
        for (const char* p = env;; ++p)
        {
            if (*p == sep || *p == '\0')
            {
                if (!cur.empty())
                    dirs.emplace_back(std::filesystem::u8path(cur));
                cur.clear();
                if (*p == '\0')
                    break;
            }
            else
            {
                cur += *p;
            }
        }
    }
    const auto exe = executableDirectory();
    std::string rel = MIRAB_ENGINE_RELATIVE_DIRS;
    std::string cur;
    for (char c : rel + ";")
    {
        if (c == ';')
        {
            if (!cur.empty())
                dirs.push_back((exe / std::filesystem::u8path(cur)).lexically_normal());
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    dirs.push_back(exe);
    dirs.push_back(userEngineDirectory());
    return dirs;
}

const EngineDescriptor* EngineRegistry::descriptor(std::string_view id) const
{
    for (const auto& d : descriptors_)
    {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

std::vector<const EngineDescriptor*> EngineRegistry::forDialect(Dialect dialect,
                                                                bool multiSpecies) const
{
    std::vector<const EngineDescriptor*> out;
    for (const auto& d : descriptors_)
    {
        if (d.dialect == dialect && d.multiSpecies == multiSpecies)
            out.push_back(&d);
    }
    return out;
}

const EngineDescriptor* EngineRegistry::msxVariantOf(std::string_view baseEngineId) const
{
    for (const auto& d : descriptors_)
    {
        if (d.multiSpecies && d.baseEngineId == baseEngineId)
            return &d;
    }
    return nullptr;
}

namespace
{
std::optional<std::filesystem::path> findLibrary(const std::vector<std::filesystem::path>& dirs,
                                                 const std::string& stem)
{
    for (const auto& dir : dirs)
    {
        for (bool prefix : {false, true})
        {
            const auto candidate = dir / DynamicLibrary::fileName(stem, prefix);
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec))
                return candidate;
        }
    }
    return std::nullopt;
}
} // namespace

std::optional<std::filesystem::path> EngineRegistry::locate(const EngineDescriptor& d) const
{
    return findLibrary(dirs_, d.library);
}

bool EngineRegistry::isAvailable(std::string_view id, std::string* reason) const
{
    const EngineDescriptor* d = descriptor(id);
    if (!d)
    {
        if (reason)
            *reason = "unknown engine " + std::string(id);
        return false;
    }
    if (!locate(*d))
    {
        if (reason)
            *reason = DynamicLibrary::fileName(d->library) + " was not found in the engine folders";
        return false;
    }
    if (!d->companion.empty() && !findLibrary(dirs_, d->companion))
    {
        if (reason)
            *reason = DynamicLibrary::fileName(d->companion) + " was not found in the engine folders";
        return false;
    }
    return true;
}

std::unique_ptr<Engine> EngineRegistry::create(std::string_view id, std::string* error) const
{
    const EngineDescriptor* d = descriptor(id);
    std::string reason;
    if (!d || !isAvailable(id, &reason))
    {
        if (error)
            *error = d ? reason : "unknown engine " + std::string(id);
        return nullptr;
    }
    const auto lib = *locate(*d);
    std::unique_ptr<Engine> engine;
    if (d->multiSpecies)
        engine = detail::makeMsxEngine(*d, lib, *findLibrary(dirs_, d->companion));
    else if (d->id == "swmm5plus-1.0")
        engine = detail::makeSwmm5PlusEngine(*d, lib);
    else if (d->dialect == Dialect::Swmm)
        engine = detail::makeSwmmEngine(*d, lib);
    else
        engine = detail::makeEpanetEngine(*d, lib);
    if (!engine->load(error))
        return nullptr;
    return engine;
}

std::string EngineRegistry::defaultEngineId(Dialect dialect)
{
    return dialect == Dialect::Swmm ? "swmm-5.2-epa" : "epanet-2.3-owa";
}

} // namespace mirab::engine
