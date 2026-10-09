/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/include/anahita/core/ProjectSettings.h
 * Purpose: Per-project Anahita settings stored next to the input file (<name>.anahita.json).
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

#include <filesystem>
#include <string>

namespace anahita::core
{

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
    double pressureThreshold = 0.0; ///< minimum service pressure for resilience tools

    static std::filesystem::path sidecarPath(const std::filesystem::path& inpFile);
    /// Returns default settings when the sidecar does not exist or is invalid.
    static ProjectSettings loadFor(const std::filesystem::path& inpFile);
    void saveFor(const std::filesystem::path& inpFile) const;
};

} // namespace anahita::core
