/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/include/mirab/interop/WaterLink.h
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
#pragma once

#include "mirab/core/Linkage.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mirab::interop
{

/// The EPANET model of a link with what the computations need from its files.
struct LinkedModels
{
    std::filesystem::path waterPath;
    core::Model water;
    std::unique_ptr<core::ResultSet> results;
    core::LinkContext context; ///< context.results points into results
    std::vector<std::string> notes;
};

/// Resolves a path stored relative to the SWMM file.
std::filesystem::path resolveLinkPath(const std::filesystem::path& swmmFile,
                                      const std::string& stored);
/// The path to store for @p file: relative to the SWMM file when possible, with forward slashes.
std::string storedLinkPath(const std::filesystem::path& swmmFile,
                           const std::filesystem::path& file);

/// Polygons of a shapefile or DXF file as service areas, reprojected to
/// @p targetCrs when the shapefile has a .prj file. Areas are named by their
/// first attribute, or numbered. Throws std::runtime_error on unreadable files.
std::vector<core::ServiceArea> readServiceAreas(const std::filesystem::path& file,
                                                const std::string& targetCrs);

/// Reads the EPANET model of @p settings (with its sidecar's coordinate
/// system), the service areas and, when set, the EPANET results. Paths are
/// relative to @p swmmFile. Throws std::runtime_error when a file cannot be read.
std::unique_ptr<LinkedModels> loadLinkedModels(const std::filesystem::path& swmmFile,
                                               const core::Model& swmm,
                                               const std::string& swmmCrs,
                                               const core::WaterLinkSettings& settings);

} // namespace mirab::interop
