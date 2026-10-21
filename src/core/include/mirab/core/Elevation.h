/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Elevation.h
 * Purpose: Node elevations from a digital elevation model (DEM).
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

#include "mirab/core/Crs.h"
#include "mirab/core/Model.h"
#include "mirab/core/Raster.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::core
{

/// Ground elevation from a DEM at model coordinates, in the project's
/// elevation units. Coordinates are reprojected from the model coordinate
/// system to the DEM's when both are known; otherwise they are assumed to be
/// in the same system.
class TerrainSampler
{
public:
    enum class Status
    {
        Ok,
        Outside, ///< the point lies outside the DEM
        NoData   ///< the DEM has no value there
    };

    /// @p modelCrs is the project's coordinate system ("EPSG:2229", WKT or
    /// empty). @p demUnitToMeters is the length of one DEM value unit in
    /// metres; 0 uses the units declared in the file, else those of the
    /// horizontal coordinate system when it is in feet, else metres.
    TerrainSampler(std::shared_ptr<raster::Raster> dem, std::string_view modelCrs,
                   const ProjectUnits& units, double demUnitToMeters = 0.0);

    Status sample(double x, double y, double& value) const;
    std::optional<double> groundAt(double x, double y) const;

    /// Factor from DEM values to project elevation units.
    double verticalFactor() const { return factor_; }
    double demUnitToMeters() const { return demUnit_; }
    bool reprojects() const { return reproject_; }
    /// Remarks on how coordinates and units are matched; empty when nothing was assumed.
    const std::vector<std::string>& notes() const { return notes_; }
    raster::Raster& raster() const { return *dem_; }

private:
    std::shared_ptr<raster::Raster> dem_;
    std::optional<crs::Crs> model_, demCrs_;
    bool reproject_ = false;
    double demUnit_ = 1.0;
    double factor_ = 1.0;
    std::vector<std::string> notes_;
};

enum class ElevationScope
{
    All,     ///< every node of the chosen kinds
    Missing, ///< only nodes whose value is zero or missing
    Selected ///< only the nodes listed in ElevationOptions::selected
};

/// How SWMM nodes take the ground elevation, which is their rim.
enum class SwmmElevationMode
{
    Invert,  ///< invert elevation = ground - maximum depth
    MaxDepth ///< maximum depth = ground - invert elevation
};

struct ElevationOptions
{
    ElevationScope scope = ElevationScope::All;
    std::vector<std::string> selected;
    bool junctions = true;   ///< EPANET junctions, SWMM junctions
    bool tanks = true;       ///< EPANET tanks, SWMM storage units
    bool reservoirs = false; ///< EPANET reservoirs: set the total head to the ground
    SwmmElevationMode swmmMode = SwmmElevationMode::Invert;
    int decimals = 2; ///< rounding of the new values
};

struct ElevationChange
{
    enum class Status
    {
        Update,   ///< the value changes
        Same,     ///< the value already matches the DEM
        Outside,  ///< the node lies outside the DEM
        NoData,   ///< the DEM has no value at the node
        Unplaced, ///< the node has no coordinates
        Skipped   ///< the node kind has no matching value (SWMM outfalls, dividers)
    };
    std::string id;
    std::string kind;
    std::string section;
    int row = -1;
    int column = -1;
    std::string field; ///< "Elevation", "Head" or "MaxDepth"
    std::optional<double> ground;
    std::optional<double> oldValue;
    std::optional<double> newValue;
    Status status = Status::Skipped;
    std::string note;
};

const char* statusName(ElevationChange::Status status);

/// Works out the new values without changing the model.
std::vector<ElevationChange> planElevations(const Model& model, const TerrainSampler& terrain,
                                            const ElevationOptions& options);

/// Writes the rows whose status is Update and returns how many changed.
int applyElevations(Model& model, const std::vector<ElevationChange>& changes, int decimals = 2);

} // namespace mirab::core
