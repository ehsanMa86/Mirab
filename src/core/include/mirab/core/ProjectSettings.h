/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/ProjectSettings.h
 * Purpose: Per-project Mirab settings stored next to the input file (<name>.mirab.json).
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

#include "mirab/core/Json.h"
#include "mirab/core/Scenario.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mirab::core
{

/// A raster shown on the map: an elevation model or georeferenced imagery.
struct RasterLayerSettings
{
    std::string path;               ///< relative to the .inp file
    std::string style = "hillshade"; ///< "hillshade", "color", "hillshade-color" or "image"
    double opacity = 0.7;
    bool visible = true;
    double verticalUnit = 0.0;      ///< metres per DEM unit; 0 = as declared in the file
    double exaggeration = 1.0;      ///< vertical exaggeration of the hillshade
};

/// A user-defined web map service used as the basemap.
struct MapServiceSettings
{
    std::string id;     ///< "custom:<name>"
    std::string name;
    std::string type;   ///< "xyz", "wms" or "wmts"
    std::string url;    ///< tile template (xyz), GetMap endpoint (wms) or tile endpoint/template (wmts)
    std::string layer;
    std::string style;
    std::string format;
    std::string crs;    ///< "EPSG:3857" or the system the server draws in
    std::string matrixSet; ///< WMTS tile matrix set
    std::string version;   ///< WMS version
    std::string attribution;
    bool valid() const { return !id.empty() && !url.empty(); }
};

/// Settings that are not part of the engine input format. They live in a
/// small JSON file beside the .inp so that the .inp stays the canonical,
/// engine-readable model.
struct ProjectSettings
{
    std::string engineId;     ///< selected engine, e.g. "epanet-2.3-owa"
    bool useMsx = false;      ///< run EPANET-MSX on top of the selected EPANET engine
    std::string msxFile;      ///< path relative to the .inp file
    std::string crs;          ///< coordinate reference system, e.g. "EPSG:32614"
    std::string backdropImage; ///< georeferenced image (with world file)
    std::string basemap;      ///< web basemap shown under the network, e.g. "osm"; empty for none
    double pressureThreshold = 0.0; ///< minimum service pressure for resilience tools
    std::vector<RasterLayerSettings> rasters; ///< drawn under the network, first at the bottom
    MapServiceSettings mapService; ///< definition of a custom basemap, so the project opens anywhere
    json::Value calibration;  ///< observed data and calibration set-up (core::CalibrationConfig)
    std::vector<Scenario> scenarios; ///< alternatives of the model, stored as changes (see Scenario.h)

    static std::filesystem::path sidecarPath(const std::filesystem::path& inpFile);
    /// Returns default settings when the sidecar does not exist or is invalid.
    static ProjectSettings loadFor(const std::filesystem::path& inpFile);
    void saveFor(const std::filesystem::path& inpFile) const;
};

} // namespace mirab::core
