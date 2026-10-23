/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
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
// (include/swmm5plus.h). Functions are resolved at run time, so Mirab
// builds without a Fortran compiler and works whether or not SWMM5+ is
// installed. Interface version 2 delivers every reporting period through a
// callback, reads whole tables at once, adds subcatchments and system totals,
// and lets Mirab advance the solver step by step; with version 1 Mirab
// samples nodes and links from the per-step progress callback.
#include "Adapters.h"

#include "mirab/core/Model.h"
#include "mirab/core/Units.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mirab::engine::detail
{

namespace
{
constexpr int S5P_OK = 0;
constexpr int S5P_END = 5;
constexpr int S5P_NODE = 0;
constexpr int S5P_LINK = 1;
constexpr int S5P_SUBCATCH = 2;
constexpr int S5P_SYSTEM = 3;
constexpr int S5P_NODE_DEPTH = 0;
constexpr int S5P_NODE_HEAD = 1;
constexpr int S5P_LINK_FLOW = 0;
constexpr int S5P_LINK_DEPTH = 1;
constexpr int S5P_LINK_VELOCITY = 2;
constexpr int S5P_CONSOLE_STDERR = 1;

using ProgressFn = int (*)(double nowSeconds, double endSeconds, void* user);
using ReportFn = int (*)(int period, double seconds, void* user);

struct S5pApi
{
    int version = 1;
    int (*apiVersion)() = nullptr;
    int (*version_)(char*, int) = nullptr;
    int (*open)(const char*, const char*, const char*, const char*) = nullptr;
    int (*setProgress)(ProgressFn, void*) = nullptr;
    int (*setQuiet)(int) = nullptr; ///< optional in version 1
    int (*initialize)() = nullptr;
    int (*run)() = nullptr;
    int (*finalize)() = nullptr;
    int (*close)() = nullptr;
    int (*getCount)(int, int*) = nullptr;
    int (*getName)(int, int, char*, int) = nullptr;
    int (*getValue)(int, int, int, double*) = nullptr;
    int (*getTimes)(double*, double*, double*) = nullptr;
    int (*lastError)(char*, int) = nullptr;
    // Version 2
    int (*setReport)(ReportFn, void*) = nullptr;
    int (*setConsole)(int) = nullptr;
    int (*step)(double*) = nullptr;
    int (*getValues)(int, int, double*, int) = nullptr;
};

// At its crash points SWMM5+ ends the whole process with a Fortran STOP,
// whose exit code is 0. While a run is in progress an exit handler turns
// that into a failure, so that callers do not take it for a finished run.
std::atomic<bool> g_running{false};

void exitDuringRun()
{
    if (!g_running.load())
        return;
    // SWMM5+'s own messages may still be buffered and follow this line.
    std::fputs("\nerror: SWMM5+ stopped the program during the simulation; its messages give the cause\n",
               stderr);
    std::fflush(stderr);
    std::_Exit(3);
}

struct RunGuard
{
    RunGuard()
    {
        static const bool registered = std::atexit(exitDuringRun) == 0;
        (void)registered;
        g_running = true;
    }
    ~RunGuard() { g_running = false; }
    RunGuard(const RunGuard&) = delete;
    RunGuard& operator=(const RunGuard&) = delete;
};

ResultFrame frameOf(const core::ResultSet& results, int p)
{
    ResultFrame f;
    f.results = &results;
    f.slot = p;
    f.period = p;
    f.time = p >= 0 ? results.times[static_cast<std::size_t>(p)] : 0.0;
    return f;
}

/// Version 1: samples node and link values at the first time step that
/// reaches each reporting time. Version 1 libraries report SI units.
struct Sampler
{
    const S5pApi* api = nullptr;
    core::ResultSet* results = nullptr;
    const RunCallbacks* callbacks = nullptr;
    FramePacer* pacer = nullptr;
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
        if (pacer->due())
            pacer->send(frameOf(*results, p));
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
        // Blocking here pauses SWMM5+ between two time steps.
        return self->callbacks->proceed(now) ? 0 : 1;
    }
};

/// Version 2: records every reporting period SWMM5+ stores in its own output,
/// reading each variable of a table with one call.
class Recorder
{
public:
    struct Column
    {
        int variable; ///< column of the result table
        int property; ///< swmm5plus-capi property code
    };
    struct Table
    {
        std::string kind;
        int object = 0;
        int count = 0;
        std::vector<Column> columns;
    };

    const S5pApi* api = nullptr;
    core::ResultSet* results = nullptr;
    FramePacer* pacer = nullptr;
    double reportStep = 0.0;
    bool failed = false;

    /// Defines a result table with the variables of the SWMM 5.2 table
    /// @p all (names and units) that SWMM5+ provides, listed in @p provided.
    void addTable(const std::string& kind, int object, std::vector<std::string> ids,
                  const std::vector<core::Variable>& all,
                  const std::vector<std::pair<const char*, int>>& provided)
    {
        Table t;
        t.kind = kind;
        t.object = object;
        t.count = static_cast<int>(ids.size());
        std::vector<core::Variable> vars;
        for (const auto& [name, property] : provided)
        {
            const auto it = std::find_if(all.begin(), all.end(),
                                         [&](const core::Variable& v) { return v.name == name; });
            t.columns.push_back({static_cast<int>(vars.size()), property});
            vars.push_back(it != all.end() ? *it : core::Variable{name, ""});
        }
        results->defineTable(kind, std::move(ids), std::move(vars));
        tables_.push_back(std::move(t));
    }

    static int onReport(int /*period*/, double seconds, void* user)
    {
        auto* self = static_cast<Recorder*>(user);
        const int p = self->record(seconds);
        if (p >= 0 && self->pacer->due())
            self->pacer->send(frameOf(*self->results, p));
        return 0;
    }

private:
    int record(double seconds)
    {
        // SWMM5+ stores a level at the first time step that reaches a
        // reporting time; label it with that reporting time.
        double t = seconds;
        if (reportStep > 0)
            t = std::round(seconds / reportStep) * reportStep;
        if (!results->times.empty() && t <= results->times.back())
            t = seconds;
        const int p = results->addPeriod(t);
        if (p == 0)
            results->reportStart = t;
        for (const auto& tab : tables_)
        {
            auto* table = results->table(tab.kind);
            buffer_.resize(static_cast<std::size_t>(std::max(1, tab.count)));
            const int capacity = static_cast<int>(buffer_.size());
            for (const auto& c : tab.columns)
            {
                if (api->getValues(tab.object, c.property, buffer_.data(), capacity) != S5P_OK)
                {
                    failed = true;
                    continue;
                }
                for (int e = 0; e < tab.count; ++e)
                {
                    const double v = buffer_[static_cast<std::size_t>(e)];
                    table->set(p, c.variable, e, static_cast<float>(v));
                }
            }
        }
        return p;
    }

    std::vector<Table> tables_;
    std::vector<double> buffer_;
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
        ok &= lib_.bind(api_.version_, "s5p_version", &missing);
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
        lib_.bind(api_.setQuiet, "s5p_set_quiet"); // optional in version 1
        if (ok)
        {
            api_.version = api_.apiVersion();
            if (api_.version == 2)
            {
                ok &= lib_.bind(api_.setReport, "s5p_set_report_callback", &missing);
                ok &= lib_.bind(api_.setConsole, "s5p_set_console", &missing);
                ok &= lib_.bind(api_.step, "s5p_step", &missing);
                ok &= lib_.bind(api_.getValues, "s5p_get_values", &missing);
                ok &= lib_.bind(api_.setQuiet, "s5p_set_quiet", &missing);
            }
        }
        if (!ok)
        {
            if (error)
                *error = "SWMM5+ C interface lacks functions: " + missing;
            return false;
        }
        if (api_.version != 1 && api_.version != 2)
        {
            if (error)
                *error = "SWMM5+ C interface version " + std::to_string(api_.version) +
                         " is not supported (expected 1 or 2)";
            return false;
        }
        return true;
    }

    std::string engineVersion() const override
    {
        std::array<char, 64> buf{};
        if (api_.version_ && api_.version_(buf.data(), static_cast<int>(buf.size()) - 1) == 0)
            return buf.data();
        return descriptor_.version;
    }

    RunOutcome run(const RunRequest& req, const RunCallbacks& cb) override
    {
        RunOutcome out;
        out.results.engineId = descriptor_.id;
        out.results.engineName = descriptor_.name + " (" + engineVersion() + ")";
        std::string flowUnits = "CFS";
        try
        {
            const auto model = core::Model::fromFile(req.input);
            flowUnits = model.units().flow;
            out.results.title = model.title();
        }
        catch (const std::exception&)
        {
        }
        // Version 1 reports SWMM5+'s SI values; version 2 the input file's units.
        if (api_.version < 2)
            flowUnits = "CMS";
        const auto units = core::ProjectUnits::fromFlowUnits(core::Dialect::Swmm, flowUnits);
        out.results.flowUnits = units.flow;
        out.results.unitSystem = units.system == core::UnitSystem::SI ? "SI" : "US";

        const std::filesystem::path outDir = req.workDir / "swmm5plus";
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        const std::string inp = req.input.u8string();
        const std::string od = outDir.u8string();
        const std::string libDir = libraryPath_.parent_path().u8string();

        const RunGuard guard;
        cb.report(0.0, "Initializing SWMM5+");
        // Standard output carries the solver protocol: SWMM5+'s own text goes
        // to standard error (version 2) or is reduced (version 1).
        if (api_.setConsole)
            api_.setConsole(S5P_CONSOLE_STDERR);
        if (int code = api_.open(inp.c_str(), od.c_str(), nullptr, libDir.c_str()))
            return failure(out, code);
        if (api_.setQuiet)
            api_.setQuiet(1);
        if (int code = api_.initialize())
        {
            api_.close();
            return failure(out, code);
        }
        double start = 0, end = 0, step = 0;
        api_.getTimes(&start, &end, &step);
        out.results.reportStep = step > 0 ? step : 300.0;

        const bool ok = api_.version >= 2 ? runStepwise(out, cb, units, end)
                                          : runSampled(out, cb, units, start);
        if (!ok)
        {
            api_.close();
            return out;
        }
        cb.report(0.92, "Finalizing SWMM5+");
        api_.finalize();
        api_.close();
        if (cb.cancelled() || out.cancelled)
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
    /// Version 2: Mirab advances SWMM5+ one step at a time, so a pause blocks
    /// here rather than inside the solver, and every reporting period comes
    /// from the reporting callback with the values SWMM5+ writes to its output.
    bool runStepwise(RunOutcome& out, const RunCallbacks& cb, const core::ProjectUnits& units,
                     double end)
    {
        FramePacer pacer(cb);
        Recorder recorder;
        recorder.api = &api_;
        recorder.results = &out.results;
        recorder.pacer = &pacer;
        recorder.reportStep = out.results.reportStep;

        const auto vars = core::swmmVariables(units.flow, {}, {});
        int subcatchments = 0, nodes = 0, links = 0;
        api_.getCount(S5P_SUBCATCH, &subcatchments);
        api_.getCount(S5P_NODE, &nodes);
        api_.getCount(S5P_LINK, &links);
        if (subcatchments > 0)
            recorder.addTable("subcatchment", S5P_SUBCATCH, names(S5P_SUBCATCH, subcatchments),
                              vars.subcatchment,
                              {{"Rainfall", 0},
                               {"Evaporation Loss", 1},
                               {"Infiltration Loss", 2},
                               {"Runoff", 3}});
        recorder.addTable("node", S5P_NODE, names(S5P_NODE, nodes), vars.node,
                          {{"Depth", 0},
                           {"Head", 1},
                           {"Volume", 2},
                           {"Lateral Inflow", 3},
                           {"Total Inflow", 4},
                           {"Flooding", 5}});
        recorder.addTable("link", S5P_LINK, names(S5P_LINK, links), vars.link,
                          {{"Flow", 0},
                           {"Depth", 1},
                           {"Velocity", 2},
                           {"Volume", 4},
                           {"Capacity", 5}});
        recorder.addTable("system", S5P_SYSTEM, {"System"}, vars.system,
                          {{"Rainfall", 0},
                           {"Evaporation", 1},
                           {"Infiltration", 2},
                           {"Runoff", 3},
                           {"Total Lateral Inflow", 4},
                           {"Flooding", 5},
                           {"Outflow", 6},
                           {"Storage Volume", 7}});

        api_.setReport(&Recorder::onReport, &recorder);
        double now = 0.0;
        int code = S5P_OK;
        bool cancelled = false;
        while ((code = api_.step(&now)) == S5P_OK)
        {
            if (end > 0)
                cb.report(0.05 + 0.85 * std::min(1.0, now / end), "Finite-volume routing");
            if (!cb.proceed(now))
            {
                cancelled = true;
                break;
            }
        }
        api_.setReport(nullptr, nullptr);
        pacer.finish(frameOf(out.results, out.results.periodCount() - 1));
        if (cancelled)
        {
            out.cancelled = true;
            return true;
        }
        if (code != S5P_END)
        {
            failure(out, code);
            return false;
        }
        if (recorder.failed)
            out.results.messages.push_back("some SWMM5+ results could not be read");
        return true;
    }

    /// Version 1: samples from the progress callback; a pause blocks inside it.
    bool runSampled(RunOutcome& out, const RunCallbacks& cb, const core::ProjectUnits& units,
                    double start)
    {
        FramePacer pacer(cb);
        Sampler sampler;
        sampler.api = &api_;
        sampler.results = &out.results;
        sampler.callbacks = &cb;
        sampler.pacer = &pacer;
        sampler.reportStep = out.results.reportStep;
        sampler.nextReport = start;
        out.results.reportStart = start;
        api_.getCount(S5P_NODE, &sampler.nodes);
        api_.getCount(S5P_LINK, &sampler.links);
        out.results.defineTable("node", names(S5P_NODE, sampler.nodes),
                                {{"Depth", units.label("depth")}, {"Head", units.label("head")}});
        out.results.defineTable("link", names(S5P_LINK, sampler.links),
                                {{"Flow", units.label("flow")},
                                 {"Depth", units.label("depth")},
                                 {"Velocity", units.label("velocity")}});

        api_.setProgress(&Sampler::onProgress, &sampler);
        const int runCode = api_.run();
        api_.setProgress(nullptr, nullptr);
        pacer.finish(frameOf(out.results, out.results.periodCount() - 1));
        if (runCode && !cb.cancelled())
        {
            failure(out, runCode);
            return false;
        }
        return true;
    }

    std::vector<std::string> names(int type, int count) const
    {
        std::vector<std::string> ids;
        ids.reserve(static_cast<std::size_t>(std::max(0, count)));
        std::array<char, 128> buf{};
        for (int i = 0; i < count; ++i)
        {
            buf[0] = '\0';
            api_.getName(type, i, buf.data(), static_cast<int>(buf.size()) - 1);
            ids.emplace_back(buf.data());
        }
        return ids;
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

} // namespace mirab::engine::detail
