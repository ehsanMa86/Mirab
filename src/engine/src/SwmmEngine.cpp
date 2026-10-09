/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/SwmmEngine.cpp
 * Purpose: Adapter that runs EPA SWMM 5.2 step by step and reads its binary output.
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
#include "Adapters.h"

#include "mirab/core/Model.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mirab::engine::detail
{

namespace
{
struct SwmmApi
{
    int (*open)(const char*, const char*, const char*) = nullptr;
    int (*start)(int) = nullptr;
    int (*step)(double*) = nullptr;
    int (*end)() = nullptr;
    int (*report)() = nullptr;
    int (*close)() = nullptr;
    int (*getMassBalErr)(float*, float*, float*) = nullptr;
    int (*getVersion)() = nullptr;
    int (*getError)(char*, int) = nullptr;
    int (*getWarnings)() = nullptr;
};

class SwmmEngine : public Engine
{
public:
    using Engine::Engine;

    bool load(std::string* error) override
    {
        if (!lib_.open(libraryPath_, error))
            return false;
        std::string missing;
        bool ok = true;
        ok &= lib_.bind(api_.open, "swmm_open", &missing);
        ok &= lib_.bind(api_.start, "swmm_start", &missing);
        ok &= lib_.bind(api_.step, "swmm_step", &missing);
        ok &= lib_.bind(api_.end, "swmm_end", &missing);
        ok &= lib_.bind(api_.report, "swmm_report", &missing);
        ok &= lib_.bind(api_.close, "swmm_close", &missing);
        ok &= lib_.bind(api_.getMassBalErr, "swmm_getMassBalErr", &missing);
        ok &= lib_.bind(api_.getVersion, "swmm_getVersion", &missing);
        ok &= lib_.bind(api_.getError, "swmm_getError", &missing);
        ok &= lib_.bind(api_.getWarnings, "swmm_getWarnings", &missing);
        if (!ok && error)
            *error = "SWMM library lacks functions: " + missing;
        return ok;
    }

    std::string engineVersion() const override
    {
        const int v = api_.getVersion ? api_.getVersion() : 0;
        // 52004 -> 5.2.4
        return std::to_string(v / 10000) + "." + std::to_string((v / 1000) % 10) + "." +
               std::to_string(v % 1000);
    }

    RunOutcome run(const RunRequest& req, const RunCallbacks& cb) override
    {
        RunOutcome out;
        double totalDays = 0;
        std::string title;
        try
        {
            const auto model = core::Model::fromFile(req.input);
            totalDays = model.durationSeconds() / 86400.0;
            title = model.title();
        }
        catch (const std::exception&)
        {
        }

        std::filesystem::path report = req.reportFile;
        if (report.empty())
            report = req.workDir / "swmm.rpt";
        const std::filesystem::path binary = req.workDir / "swmm.out";
        const std::string inp = req.input.u8string(), rpt = report.u8string(), bin = binary.u8string();

        cb.report(0.0, "Reading the project");
        int code = api_.open(inp.c_str(), rpt.c_str(), bin.c_str());
        if (code)
            return failure(out, code);
        code = api_.start(1);
        if (code)
        {
            api_.close();
            return failure(out, code);
        }
        double elapsed = 0;
        bool cancelled = false;
        do
        {
            code = api_.step(&elapsed);
            if (code)
                break;
            if (totalDays > 0)
                cb.report(0.02 + 0.93 * std::min(1.0, elapsed / totalDays), "Routing flows");
            if (cb.cancelled())
            {
                cancelled = true;
                break;
            }
        } while (elapsed > 0);
        const int endCode = api_.end();
        if (!code)
            code = endCode;
        if (!code && !cancelled)
        {
            cb.report(0.96, "Writing the report");
            api_.report();
        }
        float runoffErr = 0, flowErr = 0, qualErr = 0;
        api_.getMassBalErr(&runoffErr, &flowErr, &qualErr);
        const int warnings = api_.getWarnings();
        if (code)
        {
            failure(out, code);
            api_.close();
            return out;
        }
        api_.close();
        if (cancelled)
        {
            out.cancelled = true;
            out.message = "simulation cancelled";
            return out;
        }
        try
        {
            cb.report(0.98, "Reading results");
            out.results = core::ResultSet::readSwmmOutput(binary);
        }
        catch (const std::exception& e)
        {
            out.code = -1;
            out.message = e.what();
            return out;
        }
        out.results.engineId = descriptor_.id;
        out.results.engineName = descriptor_.name + " (" + engineVersion() + ")";
        out.results.title = title;
        out.results.metadata["runoffContinuityErrorPct"] = std::to_string(runoffErr);
        out.results.metadata["flowContinuityErrorPct"] = std::to_string(flowErr);
        out.results.metadata["qualityContinuityErrorPct"] = std::to_string(qualErr);
        if (warnings > 0)
            out.results.messages.push_back(std::to_string(warnings) +
                                           " warning(s) were written to the SWMM report");
        out.success = true;
        out.message = "simulation completed";
        cb.report(1.0, "Done");
        return out;
    }

private:
    RunOutcome& failure(RunOutcome& out, int code)
    {
        std::array<char, 512> msg{};
        api_.getError(msg.data(), static_cast<int>(msg.size()) - 1);
        out.code = code;
        out.message = msg[0] ? std::string(msg.data()) : "SWMM error " + std::to_string(code);
        return out;
    }

    DynamicLibrary lib_;
    SwmmApi api_;
};
} // namespace

std::unique_ptr<Engine> makeSwmmEngine(EngineDescriptor d, std::filesystem::path library)
{
    return std::make_unique<SwmmEngine>(std::move(d), std::move(library));
}

} // namespace mirab::engine::detail
