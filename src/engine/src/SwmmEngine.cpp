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
#include "mirab/core/Text.h"
#include "mirab/core/Units.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

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
    // Optional (SWMM 5.2 "getter" API): current values for live results.
    int (*getCount)(int) = nullptr;
    void (*getName)(int, int, char*, int) = nullptr;
    double (*getValue)(int, int) = nullptr;
};

// Object and property codes of swmm5.h (SWMM 5.2).
constexpr int kSubcatch = 1, kNode = 2, kLink = 3;
constexpr int kSubArea = 200, kSubRainfall = 202, kSubEvap = 203, kSubInfil = 204,
              kSubRunoff = 205, kSubReported = 206;
constexpr int kNodeType = 300, kNodeDepth = 303, kNodeHead = 304, kNodeVolume = 305,
              kNodeLatFlow = 306, kNodeInflow = 307, kNodeOverflow = 308, kNodeReported = 309;
constexpr int kLinkType = 400, kLinkFullDepth = 405, kLinkSetting = 407, kLinkFlow = 410,
              kLinkDepth = 411, kLinkVelocity = 412, kLinkReported = 414;
constexpr int kReportStep = 5, kSavedPeriods = 6, kFlowUnits = 8;
constexpr int kOutfall = 1, kConduit = 0;

/// Reads the current state of a running SWMM 5.2 simulation into a
/// one-period result set laid out like the binary output file.
class LiveState
{
public:
    bool setup(const SwmmApi& api, const core::Model* model, const std::string& engineId,
               const std::string& engineName, const std::string& title)
    {
        api_ = &api;
        if (!api.getCount || !api.getName || !api.getValue)
            return false;
        static const char* flowNames[] = {"CFS", "GPM", "MGD", "CMS", "LPS", "MLD"};
        const int code = static_cast<int>(api.getValue(kFlowUnits, 0));
        r_.flowUnits = (code >= 0 && code <= 5) ? flowNames[code] : "CFS";
        const auto units = core::ProjectUnits::fromFlowUnits(core::Dialect::Swmm, r_.flowUnits);
        r_.unitSystem = units.system == core::UnitSystem::SI ? "SI" : "US";
        r_.engineId = engineId;
        r_.engineName = engineName;
        r_.title = title;
        step_ = api.getValue(kReportStep, 0);
        if (step_ <= 0)
            return false;
        const double offset = model ? model->reportStartSeconds() : 0.0;
        first_ = step_ * std::max(1.0, std::ceil((offset - 1e-3) / step_));
        r_.reportStart = first_;
        r_.reportStep = step_;

        std::vector<std::string> pollutants, pollutantUnits;
        if (model && text::toUpper(text::trim(model->option("IGNORE_QUALITY"))) != "YES")
        {
            if (const auto* s = model->document().find("POLLUTANTS"))
            {
                for (const auto& row : s->allRowTokens())
                {
                    if (row.empty())
                        continue;
                    pollutants.push_back(row[0]);
                    const std::string u = row.size() > 1 ? text::toUpper(row[1]) : std::string();
                    pollutantUnits.push_back(u == "UG/L" ? "ug/L" : u == "#/L" ? "count/L" : "mg/L");
                }
            }
        }
        const auto vars = core::swmmVariables(r_.flowUnits, pollutants, pollutantUnits);
        subs_ = reported(kSubcatch, kSubReported);
        nodes_ = reported(kNode, kNodeReported);
        links_ = reported(kLink, kLinkReported);
        if (!subs_.empty())
            r_.defineTable("subcatchment", names(kSubcatch, subs_), vars.subcatchment);
        r_.defineTable("node", names(kNode, nodes_), vars.node);
        r_.defineTable("link", names(kLink, links_), vars.link);
        r_.defineTable("system", {"System"}, vars.system);
        r_.addPeriod(0.0);

        // Properties that do not change during the run.
        const int nSub = api.getCount(kSubcatch);
        for (int i = 0; i < nSub; ++i)
            areas_.push_back(api.getValue(kSubArea, i));
        const int nNodes = api.getCount(kNode);
        for (int i = 0; i < nNodes; ++i)
            nodeTypes_.push_back(static_cast<int>(api.getValue(kNodeType, i)));
        const int nLinks = api.getCount(kLink);
        for (int i = 0; i < nLinks; ++i)
        {
            linkTypes_.push_back(static_cast<int>(api.getValue(kLinkType, i)));
            fullDepths_.push_back(api.getValue(kLinkFullDepth, i));
        }
        return true;
    }

    /// Number of reporting periods SWMM has written so far.
    int savedPeriods() const { return static_cast<int>(api_->getValue(kSavedPeriods, 0)); }

    /// Samples the current state as reporting period @p period.
    ResultFrame sample(int period)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto& api = *api_;
        r_.times[0] = first_ + step_ * period;
        if (auto* t = r_.table("subcatchment"))
        {
            for (int v = 0; v < t->variableCount(); ++v)
                fill(*t, v, nan);
            for (std::size_t k = 0; k < subs_.size(); ++k)
            {
                const int i = subs_[k], e = static_cast<int>(k);
                t->set(0, 0, e, static_cast<float>(api.getValue(kSubRainfall, i)));
                t->set(0, 2, e, static_cast<float>(api.getValue(kSubEvap, i)));
                t->set(0, 3, e, static_cast<float>(api.getValue(kSubInfil, i)));
                t->set(0, 4, e, static_cast<float>(api.getValue(kSubRunoff, i)));
            }
        }
        if (auto* t = r_.table("node"))
        {
            static const int props[] = {kNodeDepth,   kNodeHead,   kNodeVolume,
                                        kNodeLatFlow, kNodeInflow, kNodeOverflow};
            for (int v = 6; v < t->variableCount(); ++v)
                fill(*t, v, nan);
            for (std::size_t k = 0; k < nodes_.size(); ++k)
            {
                for (int v = 0; v < 6; ++v)
                    t->set(0, v, static_cast<int>(k),
                           static_cast<float>(api.getValue(props[v], nodes_[k])));
            }
        }
        if (auto* t = r_.table("link"))
        {
            for (int v = 3; v < t->variableCount(); ++v)
                fill(*t, v, nan);
            for (std::size_t k = 0; k < links_.size(); ++k)
            {
                const int i = links_[k], e = static_cast<int>(k);
                const double q = api.getValue(kLinkFlow, i);
                const double y = api.getValue(kLinkDepth, i);
                t->set(0, 0, e, static_cast<float>(q));
                t->set(0, 1, e, static_cast<float>(y));
                // SWMM reports velocity with the sign of the flow.
                const double u = std::copysign(api.getValue(kLinkVelocity, i), q);
                t->set(0, 2, e, static_cast<float>(u));
                // Capacity: the fraction of the full depth approximates SWMM's area fraction.
                const auto ui = static_cast<std::size_t>(i);
                const double c = linkTypes_[ui] == kConduit
                                     ? (fullDepths_[ui] > 0 ? y / fullDepths_[ui] : 0.0)
                                     : api.getValue(kLinkSetting, i);
                t->set(0, 4, e, static_cast<float>(c));
            }
        }
        if (auto* t = r_.table("system"))
        {
            for (int v = 0; v < t->variableCount(); ++v)
                fill(*t, v, nan);
            double rain = 0, area = 0, runoff = 0;
            for (std::size_t i = 0; i < areas_.size(); ++i)
            {
                rain += api.getValue(kSubRainfall, static_cast<int>(i)) * areas_[i];
                area += areas_[i];
                runoff += api.getValue(kSubRunoff, static_cast<int>(i));
            }
            double lateral = 0, flooding = 0, outflow = 0;
            for (std::size_t i = 0; i < nodeTypes_.size(); ++i)
            {
                const int n = static_cast<int>(i);
                lateral += api.getValue(kNodeLatFlow, n);
                flooding += api.getValue(kNodeOverflow, n);
                if (nodeTypes_[i] == kOutfall)
                    outflow += api.getValue(kNodeInflow, n);
            }
            set(*t, "Rainfall", area > 0 ? rain / area : 0.0);
            set(*t, "Runoff", runoff);
            set(*t, "Total Lateral Inflow", lateral);
            set(*t, "Flooding", flooding);
            set(*t, "Outflow", outflow);
        }
        ResultFrame f;
        f.results = &r_;
        f.slot = 0;
        f.period = period;
        f.time = r_.times[0];
        return f;
    }

private:
    std::vector<int> reported(int object, int flag) const
    {
        std::vector<int> out;
        const int n = api_->getCount(object);
        for (int i = 0; i < n; ++i)
        {
            if (api_->getValue(flag, i) > 0)
                out.push_back(i);
        }
        return out;
    }

    std::vector<std::string> names(int object, const std::vector<int>& indices) const
    {
        std::vector<std::string> out;
        out.reserve(indices.size());
        std::array<char, 128> buf{};
        for (int i : indices)
        {
            buf[0] = '\0';
            // swmm_getName writes a terminating zero after "size" characters.
            api_->getName(object, i, buf.data(), static_cast<int>(buf.size()) - 1);
            out.emplace_back(buf.data());
        }
        return out;
    }

    static void fill(core::ResultTable& t, int variable, float value)
    {
        for (int e = 0; e < t.elementCount(); ++e)
            t.set(0, variable, e, value);
    }

    static void set(core::ResultTable& t, const char* variable, double value)
    {
        const int v = t.variableIndex(variable);
        if (v >= 0)
            t.set(0, v, 0, static_cast<float>(value));
    }

    const SwmmApi* api_ = nullptr;
    core::ResultSet r_;
    double step_ = 0.0;
    double first_ = 0.0;
    std::vector<int> subs_, nodes_, links_;
    std::vector<double> areas_, fullDepths_;
    std::vector<int> nodeTypes_, linkTypes_;
};

ResultFrame lastFrame(const core::ResultSet& r)
{
    ResultFrame f;
    f.results = &r;
    f.slot = r.periodCount() - 1;
    f.period = f.slot;
    f.time = f.slot >= 0 ? r.times[static_cast<std::size_t>(f.slot)] : 0.0;
    return f;
}

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
        lib_.bind(api_.getCount, "swmm_getCount");
        lib_.bind(api_.getName, "swmm_getName");
        lib_.bind(api_.getValue, "swmm_getValue");
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
        std::unique_ptr<core::Model> model;
        try
        {
            model = std::make_unique<core::Model>(core::Model::fromFile(req.input));
            totalDays = model->durationSeconds() / 86400.0;
            title = model->title();
        }
        catch (const std::exception&)
        {
        }
        const std::string engineName = descriptor_.name + " (" + engineVersion() + ")";

        std::filesystem::path report = req.reportFile;
        if (report.empty())
            report = req.workDir / "swmm.rpt";
        const std::filesystem::path binary = req.workDir / "swmm.out";
        // SWMM resolves relative paths with a buffer that is too small on Linux.
        std::error_code ec;
        const auto absolute = [&ec](const std::filesystem::path& p) {
            const auto a = std::filesystem::absolute(p, ec);
            return (ec ? p : a).u8string();
        };
        const std::string inp = absolute(req.input), rpt = absolute(report), bin = absolute(binary);

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
        // Live results: the state at the routing step that reaches each
        // reporting time. The binary output file stays the reference.
        FramePacer pacer(cb);
        LiveState live;
        const bool liveOk =
            pacer.active() && live.setup(api_, model.get(), descriptor_.id, engineName, title);
        int saved = 0;
        double elapsed = 0;
        bool cancelled = false;
        do
        {
            code = api_.step(&elapsed);
            if (code)
                break;
            if (liveOk)
            {
                const int n = live.savedPeriods();
                if (n > saved)
                {
                    saved = n;
                    if (pacer.due())
                        pacer.send(live.sample(saved - 1));
                }
            }
            if (totalDays > 0)
                cb.report(0.02 + 0.93 * std::min(1.0, elapsed / totalDays), "Routing flows");
            if (!cb.proceed(elapsed * 86400.0))
            {
                cancelled = true;
                if (liveOk && saved > 0 && pacer.lastPeriod() != saved - 1)
                    pacer.send(live.sample(saved - 1));
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
            // The model's report start places the periods on the simulation's
            // time axis when reporting starts later than the simulation.
            out.results = core::ResultSet::readSwmmOutput(
                binary, model ? std::optional<double>(model->reportStartSeconds()) : std::nullopt);
        }
        catch (const std::exception& e)
        {
            out.code = -1;
            out.message = e.what();
            return out;
        }
        out.results.engineId = descriptor_.id;
        out.results.engineName = engineName;
        out.results.title = title;
        out.results.metadata["runoffContinuityErrorPct"] = std::to_string(runoffErr);
        out.results.metadata["flowContinuityErrorPct"] = std::to_string(flowErr);
        out.results.metadata["qualityContinuityErrorPct"] = std::to_string(qualErr);
        if (warnings > 0)
            out.results.messages.push_back(std::to_string(warnings) +
                                           " warning(s) were written to the SWMM report");
        // The last frame comes from the output file, so the live view ends
        // exactly on the saved results (it may repeat the last live period).
        pacer.send(lastFrame(out.results));
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
