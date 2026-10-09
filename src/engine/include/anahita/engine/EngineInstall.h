/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/engine/include/anahita/engine/EngineInstall.h
 * Purpose: Installation of separately distributed engines (such as SWMM5+) from checksummed manifests.
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
#include <vector>

namespace anahita::engine
{

/// Platform identifier used in engine manifests: linux-x86_64, macos-arm64, ...
std::string platformId();

/// Per-user folder for engines installed after Anahita (searched by the registry).
std::filesystem::path userEngineDirectory();

/// Download location of the newest SWMM5+ manifest for this platform.
std::string defaultSwmm5PlusManifestUrl();

/// Description of an engine package ("anahita-engine/1" manifest).
struct EngineManifest
{
    struct File
    {
        std::string name;  ///< file name inside the engine folder
        std::string asset; ///< file name of the download
        long long size = 0;
        std::string sha256;
    };
    std::string engine;  ///< engine id, e.g. "swmm5plus-1.0"
    std::string name;
    std::string version;
    std::string packageVersion;
    std::string platform;
    int apiVersion = 0;
    std::vector<File> files;

    /// Parses manifest text. Throws std::runtime_error when it is not valid.
    static EngineManifest parse(const std::string& text);
    static EngineManifest load(const std::filesystem::path& file);
};

/// Verifies every file of @p manifest in @p sourceDir (by asset name, size and
/// SHA-256) and copies them into @p targetDir. Nothing is copied unless every
/// file verifies. A record of the installation is kept as
/// <targetDir>/<engine>.installed.json. Throws std::runtime_error on failure.
void installFromManifest(const EngineManifest& manifest, const std::filesystem::path& sourceDir,
                         const std::filesystem::path& targetDir);

/// Removes the files recorded for @p engineId in @p targetDir. Returns false
/// when no installation record exists.
bool uninstallEngine(const std::string& engineId, const std::filesystem::path& targetDir);

} // namespace anahita::engine
