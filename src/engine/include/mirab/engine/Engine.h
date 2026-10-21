/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/include/mirab/engine/Engine.h
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

#include "mirab/core/Features.h"
#include "mirab/core/Results.h"
#include "mirab/core/Schema.h"
#include "mirab/engine/DynamicLibrary.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace mirab::engine
{

enum class Support
{
    Full,        ///< the engine models the feature
    Ignored,     ///< Mirab removes or ignores it before the run, with a warning
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
    std::string library;     ///< library stem, e.g. "mirab_epanet23_owa"
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

/// Values of one reporting period, delivered while a run is in progress.
/// The tables of @c results have the same kinds, elements and variables as
/// the final results; values an engine cannot read during the run are NaN.
struct ResultFrame
{
    const core::ResultSet* results = nullptr; ///< layout and values
    int slot = 0;      ///< period of @c results that holds this frame's values
    int period = 0;    ///< index of the reporting period in the final results
    double time = 0.0; ///< seconds from the start of the simulation
    int layout = 1;    ///< changes when the tables or their variables change
};

struct RunCallbacks
{
    std::function<void(double fraction, const std::string& stage)> progress;
    std::function<void(LogLevel level, const std::string& message)> log;
    std::function<bool()> cancelRequested;
    /// Receives reporting periods during the run (optional).
    std::function<void(const ResultFrame& frame)> frame;
    /// Minimum wall-clock seconds between two frames; 0 delivers every period.
    double frameInterval = 0.25;
    /// Called between time steps with the simulation time reached, in
    /// seconds. It may block, which pauses the run.
    std::function<void(double seconds)> checkpoint;

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
    /// Engines call this between time steps: waits while the run is paused
    /// and returns false when the run should stop.
    bool proceed(double seconds) const
    {
        if (checkpoint)
            checkpoint(seconds);
        return !cancelled();
    }
};

/// Paces the frames of one run: at most one per RunCallbacks::frameInterval,
/// and the final state is always delivered by finish().
class FramePacer
{
public:
    explicit FramePacer(const RunCallbacks& callbacks) : cb_(callbacks) {}

    bool active() const { return static_cast<bool>(cb_.frame); }
    /// True when a newly computed period should be delivered now.
    bool due() const;
    void send(const ResultFrame& frame);
    /// Delivers @p frame unless that period was the last one delivered.
    void finish(const ResultFrame& frame);
    /// Forgets the last delivered period (a new layout starts over).
    void restart();
    int lastPeriod() const { return lastPeriod_; }

private:
    const RunCallbacks& cb_;
    int lastPeriod_ = -1;
    int lastLayout_ = 0;
    std::chrono::steady_clock::time_point last_{};
    bool sentAny_ = false;
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

} // namespace mirab::engine
