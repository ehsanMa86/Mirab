/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/engine/src/Swmm5PlusEngine.cpp
 * Purpose: Adapter that runs SWMM5+ through the swmm5plus-capi C interface.
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
//
// The C interface is defined by the companion project swmm5plus-capi
// (include/swmm5plus.h). Functions are resolved at run time, so Anahita
// builds without a Fortran compiler and works whether or not SWMM5+ is
// installed.
#include "Adapters.h"

#include "anahita/core/Model.h"
#include "anahita/core/Units.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace anahita::engine::detail
{

namespace
{
constexpr int S5P_API_VERSION = 1;
constexpr int S5P_NODE = 0;
constexpr int S5P_LINK = 1;
constexpr int S5P_NODE_DEPTH = 0;
constexpr int S5P_NODE_HEAD = 1;
constexpr int S5P_LINK_FLOW = 0;
constexpr int S5P_LINK_DEPTH = 1;
constexpr int S5P_LINK_VELOCITY = 2;

using ProgressFn = int (*)(double nowSeconds, double endSeconds, void* user);

struct S5pApi
{
    int (*apiVersion)() = nullptr;
    int (*version)(char*, int) = nullptr;
    int (*open)(const char*, const char*, const char*, const char*) = nullptr;
    int (*setProgress)(ProgressFn, void*) = nullptr;
    int (*setQuiet)(int) = nullptr; ///< optional

    int (*initialize)() = nullptr;
    int (*run)() = nullptr;
    int (*finalize)() = nullptr;
    int (*close)() = nullptr;
    int (*getCount)(int, int*) = nullptr;
    int (*getName)(int, int, char*, int) = nullptr;
    int (*getValue)(int, int, int, double*) = nullptr;
    int (*getTimes)(double*, double*, double*) = nullptr;
    int (*lastError)(char*, int) = nullptr;
};

struct Sampler
{
    const S5pApi* api = nullptr;
    core::ResultSet* results = nullptr;
    const RunCallbacks* callbacks = nullptr;
    double reportStep = 300.0;
    double nextReport = 0.0;
    int nodes = 0;
    int links = 0;

    void sample(double now)
    {
        const int p = results->addPeriod(now);
        auto* nt = results->table("node");
        auto* lt = results->table("link");
        for (int i = 0; i < nodes; ++i)
        {
            double v = 0;
            api->getValue(S5P_NODE, i, S5P_NODE_DEPTH, &v);
            nt->set(p, 0, i, static_cast<float>(v));
            api->getValue(S5P_NODE, i, S5P_NODE_HEAD, &v);
            nt->set(p, 1, i, static_cast<float>(v));
        }
        for (int i = 0; i < links; ++i)
        {
            double v = 0;
            api->getValue(S5P_LINK, i, S5P_LINK_FLOW, &v);
            lt->set(p, 0, i, static_cast<float>(v));
            api->getValue(S5P_LINK, i, S5P_LINK_DEPTH, &v);
            lt->set(p, 1, i, static_cast<float>(v));
            api->getValue(S5P_LINK, i, S5P_LINK_VELOCITY, &v);
            lt->set(p, 2, i, static_cast<float>(v));
        }
    }

    static int onProgress(double now, double end, void* user)
    {
        auto* self = static_cast<Sampler*>(user);
        while (now + 1e-6 >= self->nextReport)
        {
            self->sample(self->nextReport);
            self->nextReport += self->reportStep;
        }
        if (end > 0)
            self->callbacks->report(0.05 + 0.85 * std::min(1.0, now / end), "Finite-volume routing");
        return self->callbacks->cancelled() ? 1 : 0;
    }
};

class Swmm5PlusEngine : public Engine
{
public:
    using Engine::Engine;

    bool load(std::string* error) override
    {
        if (!lib_.open(libraryPath_, error))
            return false;
        std::string missing;
        bool ok = true;
        ok &= lib_.bind(api_.apiVersion, "s5p_api_version", &missing);
        ok &= lib_.bind(api_.version, "s5p_version", &missing);
        ok &= lib_.bind(api_.open, "s5p_open", &missing);
        ok &= lib_.bind(api_.setProgress, "s5p_set_progress_callback", &missing);
        ok &= lib_.bind(api_.initialize, "s5p_initialize", &missing);
        ok &= lib_.bind(api_.run, "s5p_run", &missing);
        ok &= lib_.bind(api_.finalize, "s5p_finalize", &missing);
        ok &= lib_.bind(api_.close, "s5p_close", &missing);
        ok &= lib_.bind(api_.getCount, "s5p_get_count", &missing);
        ok &= lib_.bind(api_.getName, "s5p_get_name", &missing);
        ok &= lib_.bind(api_.getValue, "s5p_get_value", &missing);
        ok &= lib_.bind(api_.getTimes, "s5p_get_times", &missing);
        ok &= lib_.bind(api_.lastError, "s5p_last_error", &missing);
        lib_.bind(api_.setQuiet, "s5p_set_quiet"); // optional: newer interface builds
        if (!ok)
        {
            if (error)
                *error = "SWMM5+ C interface lacks functions: " + missing;
            return false;
        }
        if (api_.apiVersion() != S5P_API_VERSION)
        {
            if (error)
                *error = "SWMM5+ C interface version " + std::to_string(api_.apiVersion()) +
                         " is not supported (expected " + std::to_string(S5P_API_VERSION) + ")";
            return false;
        }
        return true;
    }

    std::string engineVersion() const override
    {
        std::array<char, 64> buf{};
        if (api_.version && api_.version(buf.data(), static_cast<int>(buf.size()) - 1) == 0)
            return buf.data();
        return descriptor_.version;
    }

    RunOutcome run(const RunRequest& req, const RunCallbacks& cb) override
    {
        RunOutcome out;
        out.results.engineId = descriptor_.id;
        out.results.engineName = descriptor_.name + " (" + engineVersion() + ")";
        core::ProjectUnits units = core::ProjectUnits::fromFlowUnits(core::Dialect::Swmm, "CFS");
        try
        {
            const auto model = core::Model::fromFile(req.input);
            units = model.units();
            out.results.title = model.title();
        }
        catch (const std::exception&)
        {
        }
        out.results.flowUnits = units.flow;
        out.results.unitSystem = units.system == core::UnitSystem::SI ? "SI" : "US";

        const std::filesystem::path outDir = req.workDir / "swmm5plus";
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        const std::string inp = req.input.u8string();
        const std::string od = outDir.u8string();
        const std::string libDir = libraryPath_.parent_path().u8string();

        cb.report(0.0, "Initializing SWMM5+");
        if (int code = api_.open(inp.c_str(), od.c_str(), nullptr, libDir.c_str()))
            return failure(out, code);
        // SWMM5+ writes to standard output, which carries the solver protocol.
        if (api_.setQuiet)
            api_.setQuiet(1);
        if (int code = api_.initialize())
        {
            api_.close();
            return failure(out, code);
        }

        Sampler sampler;
        sampler.api = &api_;
        sampler.results = &out.results;
        sampler.callbacks = &cb;
        double start = 0, end = 0, step = 0;
        api_.getTimes(&start, &end, &step);
        if (step > 0)
            sampler.reportStep = step;
        sampler.nextReport = start;
        out.results.reportStart = start;
        out.results.reportStep = sampler.reportStep;
        api_.getCount(S5P_NODE, &sampler.nodes);
        api_.getCount(S5P_LINK, &sampler.links);
        std::vector<std::string> nodeIds, linkIds;
        for (int i = 0; i < sampler.nodes; ++i)
            nodeIds.push_back(name(S5P_NODE, i));
        for (int i = 0; i < sampler.links; ++i)
            linkIds.push_back(name(S5P_LINK, i));
        out.results.defineTable("node", nodeIds,
                                {{"Depth", units.label("depth")}, {"Head", units.label("head")}});
        out.results.defineTable("link", linkIds,
                                {{"Flow", units.label("flow")},
                                 {"Depth", units.label("depth")},
                                 {"Velocity", units.label("velocity")}});

        api_.setProgress(&Sampler::onProgress, &sampler);
        const int runCode = api_.run();
        api_.setProgress(nullptr, nullptr);
        if (runCode && !cb.cancelled())
        {
            failure(out, runCode);
            api_.close();
            return out;
        }
        cb.report(0.92, "Finalizing SWMM5+");
        api_.finalize();
        api_.close();
        if (cb.cancelled())
        {
            out.cancelled = true;
            out.message = "simulation cancelled";
            return out;
        }
        out.results.metadata["swmm5plusOutput"] = od;
        out.success = true;
        out.message = "simulation completed";
        cb.report(1.0, "Done");
        return out;
    }

private:
    std::string name(int type, int index) const
    {
        std::array<char, 128> buf{};
        api_.getName(type, index, buf.data(), static_cast<int>(buf.size()) - 1);
        return buf.data();
    }

    RunOutcome& failure(RunOutcome& out, int code)
    {
        std::array<char, 512> msg{};
        api_.lastError(msg.data(), static_cast<int>(msg.size()) - 1);
        out.code = code;
        out.message = msg[0] ? std::string(msg.data()) : "SWMM5+ error " + std::to_string(code);
        return out;
    }

    DynamicLibrary lib_;
    S5pApi api_;
};
} // namespace

std::unique_ptr<Engine> makeSwmm5PlusEngine(EngineDescriptor d, std::filesystem::path library)
{
    return std::make_unique<Swmm5PlusEngine>(std::move(d), std::move(library));
}

} // namespace anahita::engine::detail
