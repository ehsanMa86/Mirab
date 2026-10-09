/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/include/mirab/interop/Interop.h
 * Purpose: GIS and model interchange: GeoJSON, GeoPackage and WNTR JSON.
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
#include "mirab/core/Model.h"
#include "mirab/core/Results.h"

#include <filesystem>
#include <string>

namespace mirab::interop
{

struct GisOptions
{
    std::string crs;                          ///< e.g. "EPSG:32614"; empty when unknown
    const core::ResultSet* results = nullptr; ///< optional results to attach
    int period = -1;                          ///< results period (-1: last period)
};

/// One feature layer per element kind: junctions, pipes, conduits, subcatchments, ...
struct Layer
{
    std::string name;      ///< e.g. "junctions"
    std::string geometry;  ///< POINT, LINESTRING or POLYGON
    std::vector<std::string> columns;
    std::vector<bool> numeric;
    struct Feature
    {
        std::vector<core::Point> coordinates;
        std::vector<std::string> values;
    };
    std::vector<Feature> features;
};

std::vector<Layer> buildLayers(const core::Model& model, const GisOptions& options);

/// GeoJSON FeatureCollection; each feature carries a "layer" property.
json::Value toGeoJson(const core::Model& model, const GisOptions& options);
void writeGeoJson(const core::Model& model, const std::filesystem::path& path,
                  const GisOptions& options);

/// True when Mirab was built with GeoPackage support.
bool geoPackageAvailable();
/// Writes an OGC GeoPackage with one feature table per layer. Throws on failure.
void writeGeoPackage(const core::Model& model, const std::filesystem::path& path,
                     const GisOptions& options);

/// Converts an EPANET model into the JSON layout of wntr.network.write_json
/// (SI units). Unsupported items are listed in @p notes.
json::Value toWntrJson(const core::Model& model, std::vector<std::string>* notes = nullptr);
void writeWntrJson(const core::Model& model, const std::filesystem::path& path,
                   std::vector<std::string>* notes = nullptr);
/// Builds an EPANET model (LPS units) from a WNTR JSON document.
core::Model fromWntrJson(const json::Value& wntr, std::vector<std::string>* notes = nullptr);
core::Model readWntrJson(const std::filesystem::path& path, std::vector<std::string>* notes = nullptr);

} // namespace mirab::interop
