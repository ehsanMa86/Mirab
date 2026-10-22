/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/src/WaterLink.cpp
 * Purpose: Loading the water model, results and service areas that a water-sewer link refers to.
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
#include "mirab/interop/WaterLink.h"

#include "mirab/core/Crs.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Text.h"
#include "mirab/interop/Import.h"

#include <optional>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace mirab::interop
{

fs::path resolveLinkPath(const fs::path& swmmFile, const std::string& stored)
{
    const fs::path p = fs::u8path(stored);
    if (stored.empty() || p.is_absolute())
        return p;
    return (swmmFile.parent_path() / p).lexically_normal();
}

std::string storedLinkPath(const fs::path& swmmFile, const fs::path& file)
{
    std::error_code ec;
    const fs::path base = fs::absolute(swmmFile, ec).parent_path();
    const fs::path target = fs::absolute(file, ec);
    fs::path rel = target.lexically_relative(base);
    if (rel.empty() || target.root_name() != base.root_name())
        rel = target;
    return rel.generic_u8string();
}

std::vector<core::ServiceArea> readServiceAreas(const fs::path& file, const std::string& targetCrs)
{
    std::vector<ImportedLayer> layers;
    const std::string ext = text::toLower(file.extension().u8string());
    if (ext == ".shp")
        layers.push_back(readShapefile(file));
    else if (ext == ".dxf")
        layers = readDxf(file);
    else
        throw std::runtime_error("service areas must be a shapefile (.shp) or a DXF file");
    std::vector<core::ServiceArea> out;
    for (const auto& layer : layers)
    {
        if (layer.geometry != GeometryType::Polygon)
            continue;
        std::optional<crs::Crs> from, to;
        if (!layer.crsText.empty() && !targetCrs.empty())
        {
            from = crs::parse(layer.crsText);
            to = crs::parse(targetCrs);
            if (from && to && crs::toString(*from) == crs::toString(*to))
                from.reset();
        }
        for (const auto& f : layer.features)
        {
            core::ServiceArea a;
            a.name = !f.values.empty() && !text::trim(f.values[0]).empty()
                         ? text::trim(f.values[0])
                         : std::to_string(out.size() + 1);
            for (auto p : f.points)
            {
                if (from && to)
                    crs::transform(*from, *to, p.x, p.y, p.x, p.y);
                a.ring.push_back(p);
            }
            if (a.ring.size() >= 3)
                out.push_back(std::move(a));
        }
    }
    if (out.empty())
        throw std::runtime_error(file.u8string() + " has no polygons");
    return out;
}

std::unique_ptr<LinkedModels> loadLinkedModels(const fs::path& swmmFile, const core::Model& swmm,
                                               const std::string& swmmCrs,
                                               const core::WaterLinkSettings& settings)
{
    if (settings.waterModel.empty())
        throw std::runtime_error("no water distribution model is linked");
    auto out = std::make_unique<LinkedModels>();
    out->waterPath = resolveLinkPath(swmmFile, settings.waterModel);
    if (!fs::exists(out->waterPath))
        throw std::runtime_error("cannot find the water model " + out->waterPath.u8string());
    out->water = core::Model::fromFile(out->waterPath);
    if (out->water.dialect() != core::Dialect::Epanet)
        throw std::runtime_error(out->waterPath.u8string() + " is not an EPANET model");
    out->context.waterCrs = core::ProjectSettings::loadFor(out->waterPath).crs;
    out->context.swmmCrs = swmmCrs;
    if (text::iequals(settings.serviceAreas, "subcatchments"))
    {
        out->context.serviceAreas = core::subcatchmentAreas(swmm);
        if (out->context.serviceAreas.empty())
            out->notes.push_back(
                "The SWMM model has no subcatchment outlines to use as service areas.");
    }
    else if (!settings.serviceAreas.empty())
    {
        out->context.serviceAreas =
            readServiceAreas(resolveLinkPath(swmmFile, settings.serviceAreas), swmmCrs);
    }
    if (!settings.results.empty())
    {
        const fs::path results = resolveLinkPath(swmmFile, settings.results);
        out->results = std::make_unique<core::ResultSet>(core::ResultSet::loadAny(results));
        out->context.results = out->results.get();
    }
    return out;
}

} // namespace mirab::interop
