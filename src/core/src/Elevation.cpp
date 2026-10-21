/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Elevation.cpp
 * Purpose: Samples a DEM at node coordinates and updates node elevations.
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
#include "mirab/core/Elevation.h"

#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace mirab::core
{

namespace
{

bool sameSystem(const crs::Crs& a, const crs::Crs& b)
{
    if (a.epsg != 0 && a.epsg == b.epsg)
        return true;
    return crs::toString(a) == crs::toString(b);
}

int columnOf(const SectionDef* def, std::string_view name)
{
    if (!def)
        return -1;
    for (std::size_t i = 0; i < def->columns.size(); ++i)
    {
        if (text::iequals(def->columns[i].name, name))
            return static_cast<int>(i);
    }
    return -1;
}

double roundTo(double v, int decimals)
{
    const double f = std::pow(10.0, std::clamp(decimals, 0, 8));
    return std::round(v * f) / f;
}

} // namespace

TerrainSampler::TerrainSampler(std::shared_ptr<raster::Raster> dem, std::string_view modelCrs,
                               const ProjectUnits& units, double demUnitToMeters)
    : dem_(std::move(dem))
{
    const raster::RasterInfo& info = dem_->info();
    demCrs_ = info.crs;
    if (!text::trim(modelCrs).empty())
        model_ = crs::parse(text::trim(modelCrs));
    if (model_ && demCrs_)
    {
        reproject_ = !sameSystem(*model_, *demCrs_);
    }
    else if (demCrs_ && !model_)
    {
        notes_.push_back(
            "The project has no coordinate system; the node coordinates are assumed to be in the "
            "coordinate system of the DEM.");
    }
    else if (model_ && !demCrs_)
    {
        notes_.push_back("The DEM has no coordinate system; it is assumed to be in the project's.");
    }
    if (!info.georeferenced)
        notes_.push_back("The DEM is not georeferenced; pixel positions are used as coordinates.");

    if (demUnitToMeters > 0)
    {
        demUnit_ = demUnitToMeters;
    }
    else if (info.verticalUnitToMeters > 0)
    {
        demUnit_ = info.verticalUnitToMeters;
    }
    else if (demCrs_ && !demCrs_->isGeographic() && std::fabs(demCrs_->unitToMeters - 1.0) > 1e-6)
    {
        demUnit_ = demCrs_->unitToMeters;
        notes_.push_back(
            "The DEM does not declare its vertical units; they are assumed to be the same as its "
            "horizontal units (feet).");
    }
    else
    {
        demUnit_ = 1.0;
        if (info.verticalUnitToMeters <= 0)
            notes_.push_back("The DEM does not declare its vertical units; metres are assumed.");
    }
    const double projectUnit = units.toSI("elevation");
    factor_ = projectUnit > 0 ? demUnit_ / projectUnit : demUnit_;
}

TerrainSampler::Status TerrainSampler::sample(double x, double y, double& value) const
{
    double dx = x, dy = y;
    if (reproject_ && !crs::transform(*model_, *demCrs_, x, y, dx, dy))
        return Status::Outside;
    const raster::RasterInfo& info = dem_->info();
    double col = 0, row = 0;
    if (!info.transform.mapToPixel(dx, dy, col, row))
        return Status::Outside;
    if (!(col >= 0.0 && row >= 0.0 && col <= info.width && row <= info.height))
        return Status::Outside;
    const auto v = dem_->samplePixel(col, row);
    if (!v)
        return Status::NoData;
    value = *v * factor_;
    return Status::Ok;
}

std::optional<double> TerrainSampler::groundAt(double x, double y) const
{
    double v = 0;
    if (sample(x, y, v) != Status::Ok)
        return std::nullopt;
    return v;
}

const char* statusName(ElevationChange::Status status)
{
    switch (status)
    {
    case ElevationChange::Status::Update:
        return "update";
    case ElevationChange::Status::Same:
        return "unchanged";
    case ElevationChange::Status::Outside:
        return "outside the DEM";
    case ElevationChange::Status::NoData:
        return "no data";
    case ElevationChange::Status::Unplaced:
        return "no coordinates";
    case ElevationChange::Status::Skipped:
        return "skipped";
    }
    return "";
}

std::vector<ElevationChange> planElevations(const Model& model, const TerrainSampler& terrain,
                                            const ElevationOptions& options)
{
    const Dialect dialect = model.dialect();
    std::unordered_set<std::string> selected(options.selected.begin(), options.selected.end());
    const double tolerance = 0.5 * std::pow(10.0, -std::clamp(options.decimals, 0, 8));
    std::vector<ElevationChange> out;
    for (const NodeItem& n : model.nodes())
    {
        if (options.scope == ElevationScope::Selected && !selected.count(n.id))
            continue;
        ElevationChange c;
        c.id = n.id;
        c.kind = n.kind;
        c.section = n.section;
        c.row = n.row;
        const SectionDef* def = findSectionDef(dialect, n.section);
        const InpSection* sec = model.document().find(n.section);
        const auto tokens = sec ? sec->rowTokens(n.row) : std::vector<std::string>{};
        auto valueAt = [&](int column) -> std::optional<double>
        {
            if (column < 0 || column >= static_cast<int>(tokens.size()))
                return std::nullopt;
            return text::toDouble(tokens[static_cast<std::size_t>(column)]);
        };
        const int elevCol = columnOf(
            def, dialect == Dialect::Epanet && n.kind == "Reservoir" ? "Head" : "Elevation");
        const int depthCol = columnOf(def, "MaxDepth");

        bool wanted = false;
        bool swmmRim = false;
        if (dialect == Dialect::Epanet)
        {
            wanted = (n.kind == "Junction" && options.junctions) ||
                     (n.kind == "Tank" && options.tanks) ||
                     (n.kind == "Reservoir" && options.reservoirs);
            c.field = n.kind == "Reservoir" ? "Head" : "Elevation";
            c.column = elevCol;
        }
        else
        {
            wanted = (n.kind == "Junction" && options.junctions) ||
                     (n.kind == "Storage" && options.tanks) || n.kind == "Outfall" ||
                     n.kind == "Divider";
            swmmRim = n.kind == "Junction" || n.kind == "Storage";
            const bool depthMode = options.swmmMode == SwmmElevationMode::MaxDepth;
            c.field = depthMode ? "MaxDepth" : "Elevation";
            c.column = depthMode ? depthCol : elevCol;
        }
        if (!wanted)
            continue;
        c.oldValue = valueAt(c.column);
        if (options.scope == ElevationScope::Missing && c.oldValue && *c.oldValue != 0.0)
            continue;
        if (!n.placed)
        {
            c.status = ElevationChange::Status::Unplaced;
            out.push_back(c);
            continue;
        }
        double ground = 0;
        const auto s = terrain.sample(n.pos.x, n.pos.y, ground);
        if (s != TerrainSampler::Status::Ok)
        {
            c.status = s == TerrainSampler::Status::Outside ? ElevationChange::Status::Outside
                                                            : ElevationChange::Status::NoData;
            out.push_back(c);
            continue;
        }
        c.ground = roundTo(ground, options.decimals);
        if (dialect == Dialect::Epanet)
        {
            c.newValue = c.ground;
        }
        else if (!swmmRim)
        {
            c.status = ElevationChange::Status::Skipped;
            c.note = "an " + text::toLower(n.kind) + " has no rim elevation";
            if (n.kind == "Divider")
                c.note = "a divider's maximum depth is not changed";
            out.push_back(c);
            continue;
        }
        else if (options.swmmMode == SwmmElevationMode::Invert)
        {
            const double depth = valueAt(depthCol).value_or(0.0);
            c.newValue = roundTo(ground - depth, options.decimals);
            if (depth <= 0)
                c.note = "maximum depth is 0: the invert is set to the ground";
        }
        else
        {
            const double invert = valueAt(elevCol).value_or(0.0);
            double depth = ground - invert;
            if (depth < 0)
            {
                c.note = "the ground lies below the invert; maximum depth set to 0";
                depth = 0;
            }
            c.newValue = roundTo(depth, options.decimals);
        }
        if (c.column < 0)
        {
            c.status = ElevationChange::Status::Skipped;
            c.note = "the section has no " + c.field + " column";
        }
        else if (c.oldValue && std::fabs(*c.oldValue - *c.newValue) < tolerance)
        {
            c.status = ElevationChange::Status::Same;
        }
        else
        {
            c.status = ElevationChange::Status::Update;
        }
        out.push_back(c);
    }
    return out;
}

int applyElevations(Model& model, const std::vector<ElevationChange>& changes, int decimals)
{
    int count = 0;
    for (const ElevationChange& c : changes)
    {
        if (c.status != ElevationChange::Status::Update || !c.newValue || c.column < 0)
            continue;
        InpSection* sec = model.document().find(c.section);
        if (!sec || c.row < 0 || c.row >= sec->rowCount())
            continue;
        auto tokens = sec->rowTokens(c.row);
        if (tokens.empty() || text::unquote(tokens.front()) != c.id)
            continue; // the document changed since the plan was made
        while (static_cast<int>(tokens.size()) <= c.column)
            tokens.push_back("0");
        tokens[static_cast<std::size_t>(c.column)] =
            text::formatDouble(roundTo(*c.newValue, decimals), 12);
        sec->setRowTokens(c.row, tokens);
        ++count;
    }
    if (count > 0)
        model.rebuild();
    return count;
}

} // namespace mirab::core
