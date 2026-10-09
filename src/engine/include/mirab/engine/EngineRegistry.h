/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/include/mirab/engine/EngineRegistry.h
 * Purpose: Catalog of known engines and discovery of their libraries on disk.
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

#include "mirab/engine/Engine.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::engine
{

class EngineRegistry
{
public:
    /// Uses defaultSearchDirectories().
    EngineRegistry();
    explicit EngineRegistry(std::vector<std::filesystem::path> searchDirectories);

    /// Folders next to the executable, the MIRAB_ENGINE_PATH environment
    /// variable (separated by ';' on Windows and ':' elsewhere) and the
    /// per-user engine folder.
    static std::vector<std::filesystem::path> defaultSearchDirectories();

    const std::vector<EngineDescriptor>& descriptors() const { return descriptors_; }
    const EngineDescriptor* descriptor(std::string_view id) const;
    std::vector<const EngineDescriptor*> forDialect(core::Dialect dialect, bool multiSpecies) const;
    /// MSX variant built on top of an EPANET engine, if any.
    const EngineDescriptor* msxVariantOf(std::string_view baseEngineId) const;

    std::optional<std::filesystem::path> locate(const EngineDescriptor& d) const;
    bool isAvailable(std::string_view id, std::string* reason = nullptr) const;

    /// Creates and loads an engine. Returns nullptr with @p error set on failure.
    std::unique_ptr<Engine> create(std::string_view id, std::string* error = nullptr) const;

    const std::vector<std::filesystem::path>& searchDirectories() const { return dirs_; }

    /// Default engine of a dialect (OWA EPANET 2.3 or EPA SWMM 5.2).
    static std::string defaultEngineId(core::Dialect dialect);

private:
    std::vector<std::filesystem::path> dirs_;
    std::vector<EngineDescriptor> descriptors_;
};

/// The built-in catalog of engine descriptors.
std::vector<EngineDescriptor> builtinDescriptors();

} // namespace mirab::engine
