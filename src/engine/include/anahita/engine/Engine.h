/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/engine/include/anahita/engine/Engine.h
 * Purpose: Common interface implemented by every simulation engine adapter.
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

#include "anahita/core/Features.h"
#include "anahita/core/Results.h"
#include "anahita/core/Schema.h"
#include "anahita/engine/DynamicLibrary.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace anahita::engine
{

enum class Support
{
    Full,        ///< the engine models the feature
    Ignored,     ///< Anahita removes or ignores it before the run, with a warning
    Unsupported  ///< the run cannot proceed
};

/// Static description of an engine: identity, provenance and capabilities.
/// This is the "capability manifest" the user interface uses to validate a
/// project and grey out features the selected engine cannot run.
struct EngineDescriptor
{
    std::string id;          ///< stable identifier, e.g. "epanet-2.3-owa"
    std::string name;        ///< display name
    std::string version;
    std::string provider;    ///< who maintains the upstream engine
    std::string license;
    std::string upstreamUrl;
    std::string description;
    core::Dialect dialect = core::Dialect::Epanet;
    std::string library;     ///< library stem, e.g. "anahita_epanet23_owa"
    std::string companion;   ///< second library the engine needs (MSX -> EPANET)
    bool multiSpecies = false;
    std::string baseEngineId; ///< for MSX variants: the EPANET engine underneath
    bool optional = false;    ///< distributed separately (SWMM5+)
    std::map<core::Feature, Support> support; ///< features not listed are fully supported

    Support supportFor(core::Feature f) const
    {
        auto it = support.find(f);
        return it == support.end() ? Support::Full : it->second;
    }
};

struct RunRequest
{
    std::filesystem::path input;      ///< engine-ready input file
    std::filesystem::path msxInput;   ///< EPANET-MSX file (MSX engines only)
    std::filesystem::path reportFile; ///< text report written by the engine
    std::filesystem::path workDir;    ///< scratch folder for engine byproducts
};

enum class LogLevel
{
    Info,
    Warning,
    Error
};

struct RunCallbacks
{
    std::function<void(double fraction, const std::string& stage)> progress;
    std::function<void(LogLevel level, const std::string& message)> log;
    std::function<bool()> cancelRequested;

    void report(double f, const std::string& stage) const
    {
        if (progress)
            progress(f, stage);
    }
    void message(LogLevel level, const std::string& m) const
    {
        if (log)
            log(level, m);
    }
    bool cancelled() const { return cancelRequested && cancelRequested(); }
};

struct RunOutcome
{
    bool success = false;
    bool cancelled = false;
    int code = 0;
    std::string message;
    core::ResultSet results;
};

class Engine
{
public:
    Engine(EngineDescriptor descriptor, std::filesystem::path libraryPath)
        : descriptor_(std::move(descriptor)), libraryPath_(std::move(libraryPath))
    {
    }
    virtual ~Engine() = default;

    const EngineDescriptor& descriptor() const { return descriptor_; }
    const std::filesystem::path& libraryPath() const { return libraryPath_; }

    /// Loads the engine library and resolves its entry points.
    virtual bool load(std::string* error) = 0;
    /// Version reported by the engine library itself.
    virtual std::string engineVersion() const { return descriptor_.version; }
    /// Runs a simulation. Engines are not reentrant: run one at a time per process.
    virtual RunOutcome run(const RunRequest& request, const RunCallbacks& callbacks) = 0;

protected:
    EngineDescriptor descriptor_;
    std::filesystem::path libraryPath_;
};

} // namespace anahita::engine
