/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/cli/Calibrate.cpp
 * Purpose: mirab-cli calibrate: goodness-of-fit metrics and automated calibration.
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
#include "Calibrate.h"

#include "mirab/core/Calibration.h"
#include "mirab/core/Json.h"
#include "mirab/core/Model.h"
#include "mirab/core/Optimizer.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Results.h"
#include "mirab/core/Text.h"
#include "mirab/engine/EngineRegistry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;
using mirab::json::Value;

namespace mirab::cli
{

namespace
{
constexpr double kInf = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// Arguments and output
// ---------------------------------------------------------------------------
struct Options
{
    std::vector<std::string> positional;
    std::map<std::string, std::string> values;
    std::vector<std::string> flags;

    bool flag(const std::string& f) const { return std::find(flags.begin(), flags.end(), f) != flags.end(); }
    bool has(const std::string& k) const { return values.count(k) > 0; }
    std::string get(const std::string& k, const std::string& fallback = {}) const
    {
        auto it = values.find(k);
        return it == values.end() ? fallback : it->second;
    }
    int integer(const std::string& k, int fallback) const
    {
        if (!has(k))
            return fallback;
        auto v = text::toInt(get(k));
        if (!v || *v < 0)
            throw std::runtime_error("--" + k + " expects a whole number");
        return static_cast<int>(*v);
    }
};

Options parseOptions(const std::vector<std::string>& args)
{
    static const std::vector<std::string> valued = {"config", "engine",   "msx",     "jobs",    "budget", "seed",
                                                     "method", "out-dir",  "observed", "results", "start"};
    Options o;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& s = args[i];
        if (s.rfind("--", 0) != 0)
        {
            o.positional.push_back(s);
            continue;
        }
        std::string key = s.substr(2);
        const auto eq = key.find('=');
        if (eq != std::string::npos)
        {
            o.values[key.substr(0, eq)] = key.substr(eq + 1);
            continue;
        }
        if (std::find(valued.begin(), valued.end(), key) != valued.end())
        {
            if (i + 1 >= args.size())
                throw std::runtime_error("--" + key + " needs a value");
            o.values[key] = args[++i];
        }
        else
        {
            o.flags.push_back(key);
        }
    }
    return o;
}

class Output
{
public:
    explicit Output(bool machine) : machine_(machine) {}
    bool machine() const { return machine_; }
    void event(const std::string& type, Value payload)
    {
        if (!machine_)
            return;
        payload.set("event", type);
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << payload.dump() << "\n" << std::flush;
    }
    void log(const std::string& level, const std::string& message)
    {
        if (machine_)
        {
            Value v = Value::object();
            v.set("level", level);
            v.set("message", message);
            event("log", v);
        }
        else
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cerr << (level == "info" ? "" : level + ": ") << message << "\n";
        }
    }
    void progress(double fraction, const std::string& stage)
    {
        Value v = Value::object();
        v.set("fraction", fraction);
        v.set("stage", stage);
        event("progress", v);
    }

private:
    bool machine_;
    std::mutex mutex_;
};

std::atomic<bool> g_cancel{false};

void watchStdin()
{
    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (text::trim(line) == "cancel")
            {
                g_cancel = true;
                return;
            }
        }
    }).detach();
}

Value number(double v)
{
    return std::isfinite(v) ? Value(v) : Value();
}

std::string fmt(double v, int digits = 5)
{
    return std::isfinite(v) ? text::formatDouble(std::fabs(v) < 1e-12 ? 0.0 : v, digits) : std::string("-");
}

// ---------------------------------------------------------------------------
// Child processes
// ---------------------------------------------------------------------------
fs::path selfPath()
{
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return fs::path(buf);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0)
        return fs::weakly_canonical(fs::path(buf.c_str()));
    return {};
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

#if defined(_WIN32)
std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

/// Quotes an argument for CommandLineToArgvW and the C runtime.
std::wstring quoteArg(const std::wstring& a)
{
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return a;
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : a)
    {
        if (c == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (c == L'"')
            out.append(backslashes * 2 + 1, L'\\');
        else
            out.append(backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

std::mutex g_spawnMutex;
#endif

/// Runs a program with its output in @p log. Returns the exit code, or -1
/// when it could not start or was stopped because of a cancel request.
int runProcess(const fs::path& exe, const std::vector<std::string>& args, const fs::path& log, std::string* error)
{
#if defined(_WIN32)
    std::wstring cmd = quoteArg(exe.wstring());
    for (const auto& a : args)
        cmd += L" " + quoteArg(widen(a));
    PROCESS_INFORMATION pi{};
    {
        // Only one inheritable handle exists at a time, so children never
        // inherit each other's log files.
        std::lock_guard<std::mutex> lock(g_spawnMutex);
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE out = CreateFileW(log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE)
        {
            if (error)
                *error = "cannot write " + log.u8string();
            return -1;
        }
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nullptr;
        si.hStdOutput = out;
        si.hStdError = out;
        const BOOL ok = CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                       nullptr, nullptr, &si, &pi);
        CloseHandle(out);
        if (!ok)
        {
            if (error)
                *error = "cannot start " + exe.u8string();
            return -1;
        }
    }
    CloseHandle(pi.hThread);
    while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT)
    {
        if (g_cancel)
        {
            TerminateProcess(pi.hProcess, 3);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            return -1;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    std::vector<std::string> all;
    all.push_back(exe.string());
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : all)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0)
    {
        if (error)
            *error = "cannot start " + exe.u8string();
        return -1;
    }
    int status = 0;
    for (;;)
    {
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        if (r < 0)
            return -1;
        if (g_cancel)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::string tail(const fs::path& file, std::size_t bytes = 600)
{
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string s = buf.str();
    if (s.size() > bytes)
        s = s.substr(s.size() - bytes);
    return text::trim(s);
}

// ---------------------------------------------------------------------------
// Set-up
// ---------------------------------------------------------------------------
struct Setup
{
    fs::path inp;
    core::Model model;
    core::CalibrationConfig config;
    fs::path baseDir; ///< folder that relative observed-data paths start from
};

/// Reads a calibration set-up: a file with the "calibration" member of a
/// project sidecar, that member alone, or the sidecar of the model.
core::CalibrationConfig readConfig(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + file.u8string());
    std::ostringstream buf;
    buf << in.rdbuf();
    const Value v = Value::parse(buf.str());
    return core::CalibrationConfig::fromJson(v.contains("calibration") ? v.get("calibration") : v);
}

std::vector<core::ObservedSeries> observedFor(const Setup& s, const core::Model* model, Output& out)
{
    std::vector<std::string> warnings;
    auto observed = core::loadObserved(s.config, s.baseDir, model, &warnings);
    for (const auto& w : warnings)
        out.log("warning", w);
    if (observed.empty())
        throw std::runtime_error("no observed data (add series in Mirab or list files under \"observed\")");
    return observed;
}

Value fitJson(const core::SeriesFit& f)
{
    Value v = Value::object();
    v.set("series", f.name);
    v.set("key", f.key);
    v.set("ok", f.ok);
    if (!f.ok)
    {
        v.set("error", f.error);
        return v;
    }
    v.set("count", f.metrics.count);
    v.set("unit", f.unit);
    for (core::Metric m : core::allMetrics())
        v.set(core::metricName(m), number(core::metricValue(m, f.metrics)));
    return v;
}

// ---------------------------------------------------------------------------
// calibrate metrics
// ---------------------------------------------------------------------------
int cmdMetrics(const Options& o)
{
    Output out(false);
    if (o.positional.size() < 2)
        throw std::runtime_error("usage: mirab-cli calibrate metrics <model.inp|results.anr> [--observed FILE]");
    const fs::path file = fs::u8path(o.positional[1]);
    const std::string ext = text::toLower(file.extension().u8string());
    fs::path inp, resultsFile;
    if (ext == ".anr" || ext == ".out")
    {
        resultsFile = file;
        inp = fs::path(file).replace_extension(".inp");
    }
    else
    {
        inp = file;
        resultsFile = o.has("results") ? fs::u8path(o.get("results")) : fs::path(file).replace_extension(".anr");
    }
    if (!fs::exists(resultsFile))
        throw std::runtime_error("no results " + resultsFile.u8string() + " (run the model first or use --results)");
    const auto results = core::ResultSet::loadAny(resultsFile);
    std::optional<core::Model> model;
    if (fs::exists(inp))
        model = core::Model::fromFile(inp);

    Setup s;
    s.inp = inp;
    if (o.has("observed"))
    {
        const fs::path obs = fs::u8path(o.get("observed"));
        const std::string oext = text::toLower(obs.extension().u8string());
        s.baseDir = obs.parent_path();
        if (oext == ".json")
        {
            s.config = readConfig(obs);
        }
        else
        {
            core::ObservedSource src;
            src.file = obs.filename().u8string();
            s.config.observed.push_back(src);
        }
    }
    else
    {
        s.config = core::CalibrationConfig::fromJson(core::ProjectSettings::loadFor(inp).calibration);
        s.baseDir = inp.parent_path();
    }
    if (o.has("start"))
        s.config.simulationStart = o.get("start");
    const auto observed = observedFor(s, model ? &*model : nullptr, out);
    const auto base = core::timeBaseFor(model ? &*model : nullptr, &results, s.config.simulationStart);
    const auto fits = core::evaluateFit(observed, results, base);
    const double objective = core::objectiveValue(s.config.objective, observed, fits);

    if (o.flag("json"))
    {
        Value v = Value::object();
        Value list = Value::array();
        for (const auto& f : fits)
            list.push(fitJson(f));
        v.set("series", list);
        v.set("objective", number(objective));
        std::cout << v.dump(2) << "\n";
        return 0;
    }
    const bool csv = o.flag("csv");
    auto cell = [csv](const std::string& v, std::size_t width) {
        if (csv)
            return "," + v;
        return std::string(v.size() < width ? width - v.size() : 1, ' ') + v;
    };
    std::cout << (csv ? "series" : "Series") << std::string(csv ? 0 : 26, ' ') << cell("count", 6);
    for (core::Metric m : core::allMetrics())
        std::cout << cell(core::metricName(m), 12);
    std::cout << "\n";
    for (const auto& f : fits)
    {
        std::string name = csv ? text::replaceAll(f.name, ",", ";") : f.name;
        if (!csv && name.size() < 32)
            name += std::string(32 - name.size(), ' ');
        std::cout << name;
        if (!f.ok)
        {
            std::cout << (csv ? "," : "  ") << "not evaluated: " << f.error << "\n";
            continue;
        }
        std::cout << cell(std::to_string(f.metrics.count), 6);
        for (core::Metric m : core::allMetrics())
            std::cout << cell(fmt(core::metricValue(m, f.metrics), 4), 12);
        std::cout << "\n";
    }
    if (!csv)
        std::cout << "Objective: " << fmt(objective, 6) << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// calibrate (optimisation)
// ---------------------------------------------------------------------------
struct Evaluation
{
    int index = 0;
    std::vector<double> values;
    double objective = kInf;
    bool ok = false;
    std::string error;
    std::vector<core::SeriesFit> fits;
};

class Evaluator
{
public:
    Evaluator(const Setup& setup, std::vector<core::ObservedSeries> observed, core::TimeBase base,
              std::string engine, std::string msx, fs::path work, bool keep)
        : setup_(setup), observed_(std::move(observed)), base_(base), engine_(std::move(engine)), msx_(std::move(msx)),
          work_(std::move(work)), keep_(keep), exe_(selfPath())
    {
        if (exe_.empty())
            throw std::runtime_error("cannot find the mirab-cli program");
    }

    /// Evaluates the points in parallel, one engine process each.
    std::vector<Evaluation> run(const std::vector<std::vector<double>>& points, int firstIndex)
    {
        std::vector<Evaluation> results(points.size());
        std::vector<std::thread> threads;
        for (std::size_t k = 0; k < points.size(); ++k)
        {
            results[k].index = firstIndex + static_cast<int>(k);
            results[k].values = points[k];
            threads.emplace_back([this, &results, k] { evaluate(results[k]); });
        }
        for (auto& t : threads)
            t.join();
        return results;
    }

    const std::vector<core::ObservedSeries>& observed() const { return observed_; }

private:
    void evaluate(Evaluation& e)
    {
        const std::string tag = "eval-" + std::to_string(e.index);
        // Next to the model, so that file references in the input stay valid.
        const fs::path inp = setup_.inp.parent_path() / ("." + setup_.inp.stem().u8string() + ".calib-" +
                                                         std::to_string(e.index) + ".inp");
        const fs::path anr = work_ / (tag + ".anr");
        const fs::path rpt = work_ / (tag + ".rpt");
        const fs::path log = work_ / (tag + ".log");
        const fs::path dir = work_ / tag;
        std::error_code ec;
        try
        {
            core::Model m = setup_.model;
            std::string error;
            if (!core::applyParameters(m, setup_.config.parameters, e.values, &error))
                throw std::runtime_error(error);
            m.save(inp);
            fs::create_directories(dir);
            std::vector<std::string> args = {"run",      inp.u8string(), "--engine",  engine_,
                                             "--out",    anr.u8string(), "--report",  rpt.u8string(),
                                             "--workdir", dir.u8string()};
            if (!msx_.empty())
            {
                args.push_back("--msx");
                args.push_back(msx_);
            }
            const int code = runProcess(exe_, args, log, &error);
            fs::remove(inp, ec);
            if (code != 0)
            {
                if (g_cancel)
                    throw std::runtime_error("cancelled");
                throw std::runtime_error(error.empty() ? "the engine run failed: " + tail(log) : error);
            }
            const auto results = core::ResultSet::load(anr);
            e.fits = core::evaluateFit(observed_, results, base_);
            e.objective = core::objectiveValue(setup_.config.objective, observed_, e.fits);
            e.ok = true;
            for (const auto& f : e.fits)
            {
                if (!f.ok && e.error.empty())
                    e.error = f.name + ": " + f.error;
            }
        }
        catch (const std::exception& ex)
        {
            e.ok = false;
            e.error = ex.what();
            e.objective = kInf;
        }
        fs::remove(inp, ec);
        if (!keep_)
        {
            fs::remove(anr, ec);
            fs::remove(rpt, ec);
            fs::remove(log, ec);
            fs::remove_all(dir, ec);
        }
    }

    const Setup& setup_;
    std::vector<core::ObservedSeries> observed_;
    core::TimeBase base_;
    std::string engine_;
    std::string msx_;
    fs::path work_;
    bool keep_;
    fs::path exe_;
};

int cmdCalibrate(const Options& o)
{
    Output out(o.flag("machine"));
    if (out.machine())
        watchStdin();
    if (o.positional.empty())
        throw std::runtime_error("usage: mirab-cli calibrate <model.inp> [--config calib.json]");
    Setup s;
    s.inp = fs::absolute(fs::u8path(o.positional[0])).lexically_normal().make_preferred();
    s.model = core::Model::fromFile(s.inp);
    const auto settings = core::ProjectSettings::loadFor(s.inp);
    if (o.has("config"))
    {
        const fs::path cfg = fs::absolute(fs::u8path(o.get("config")));
        s.config = readConfig(cfg);
        s.baseDir = cfg.parent_path();
    }
    else
    {
        s.config = core::CalibrationConfig::fromJson(settings.calibration);
        s.baseDir = s.inp.parent_path();
    }
    if (s.config.parameters.empty())
        throw std::runtime_error("no calibration parameters (list them under \"parameters\")");
    s.config.budget = o.integer("budget", s.config.budget);
    s.config.seed = static_cast<unsigned>(o.integer("seed", static_cast<int>(s.config.seed)));
    int jobs = o.integer("jobs", s.config.jobs);
    if (jobs <= 0)
        jobs = static_cast<int>(std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    if (s.config.budget < 1)
        throw std::runtime_error("the budget must be at least one evaluation");
    if (o.has("start"))
        s.config.simulationStart = o.get("start");
    const std::string method = text::toLower(o.get("method", "dds"));
    if (method != "dds" && method != "lhs")
        throw std::runtime_error("--method is dds or lhs");

    // Engine
    std::string engineId = o.get("engine", s.config.engine.empty() ? settings.engineId : s.config.engine);
    if (engineId == "swmm5plus" || engineId == "swmm5+")
        engineId = "swmm5plus-1.0";
    if (engineId.empty())
        engineId = engine::EngineRegistry::defaultEngineId(s.model.dialect());
    std::string msx = o.get("msx");
    if (msx.empty() && settings.useMsx && !settings.msxFile.empty())
        msx = (s.inp.parent_path() / fs::u8path(settings.msxFile)).u8string();

    // Parameters
    std::vector<double> lower, upper, initial;
    for (const auto& p : s.config.parameters)
    {
        if (core::parameterColumn(s.model, p) < 1)
            throw std::runtime_error(p.label() + ": unknown column " + p.column + " in [" + p.section + "]");
        if (core::parameterRowCount(s.model, p) == 0)
            throw std::runtime_error(p.label() + ": no rows of [" + p.section + "] match");
        lower.push_back(p.min);
        upper.push_back(p.max);
        initial.push_back(core::parameterInitialValue(s.model, p));
    }
    auto observed = observedFor(s, &s.model, out);
    const auto base = core::timeBaseFor(&s.model, nullptr, s.config.simulationStart);

    const fs::path outDir = (o.has("out-dir") ? fs::absolute(fs::u8path(o.get("out-dir")))
                                              : s.inp.parent_path() / (s.inp.stem().u8string() + "-calibration"))
                                .lexically_normal()
                                .make_preferred();
    const fs::path work = outDir / "runs";
    fs::create_directories(work);
    const bool keep = o.flag("keep-runs");
    Evaluator evaluator(s, std::move(observed), base, engineId, msx, work, keep);
    const auto& obs = evaluator.observed();
    const core::Metric firstMetric = s.config.objective.empty() ? core::Metric::Nse : s.config.objective[0].metric;

    out.log("info", "Calibrating " + std::to_string(s.config.parameters.size()) + " parameter(s) against " +
                        std::to_string(obs.size()) + " observed series with " + engineId + ", " +
                        std::to_string(s.config.budget) + " runs, " + std::to_string(jobs) + " at a time");

    // Log of every evaluation
    std::ofstream csv(outDir / "evaluations.csv", std::ios::binary | std::ios::trunc);
    if (!csv)
        throw std::runtime_error("cannot write " + (outDir / "evaluations.csv").u8string());
    csv << "evaluation,objective,best";
    for (const auto& p : s.config.parameters)
        csv << "," << text::replaceAll(p.label(), ",", ";");
    for (const auto& series : obs)
        csv << "," << text::replaceAll(series.name, ",", ";") << " " << core::metricName(firstMetric);
    csv << ",error\n";

    Evaluation best;
    std::vector<Evaluation> all;
    int done = 0;
    auto record = [&](std::vector<Evaluation>& batch) {
        for (auto& e : batch)
        {
            ++done;
            if (e.ok && (!best.ok || e.objective < best.objective))
                best = e;
            csv << e.index << "," << fmt(e.objective, 10) << "," << fmt(best.objective, 10);
            for (double v : e.values)
                csv << "," << text::formatDouble(v, 10);
            for (std::size_t i = 0; i < obs.size(); ++i)
                csv << "," << (i < e.fits.size() && e.fits[i].ok ? fmt(core::metricValue(firstMetric, e.fits[i].metrics), 8) : "");
            csv << "," << text::replaceAll(e.error, ",", ";") << "\n" << std::flush;
            if (out.machine())
            {
                Value v = Value::object();
                v.set("index", e.index);
                v.set("objective", number(e.objective));
                v.set("best", number(best.objective));
                Value vals = Value::array();
                for (double x : e.values)
                    vals.push(x);
                v.set("values", vals);
                v.set("success", e.ok);
                if (!e.error.empty())
                    v.set("message", e.error);
                out.event("evaluation", v);
                out.progress(static_cast<double>(done) / s.config.budget,
                             "Run " + std::to_string(done) + " of " + std::to_string(s.config.budget));
            }
            else
            {
                std::string vals;
                for (double x : e.values)
                    vals += (vals.empty() ? "" : ", ") + text::formatDouble(x, 5);
                std::cout << "  " << e.index << "  objective " << fmt(e.objective, 6) << "  best "
                          << fmt(best.objective, 6) << "  [" << vals << "]"
                          << (e.error.empty() ? std::string() : "  (" + e.error + ")") << "\n"
                          << std::flush;
            }
            all.push_back(e);
        }
    };

    Value sensitivity = Value::array();
    if (method == "dds")
    {
        core::DdsOptimizer dds(lower, upper, initial, s.config.budget, s.config.seed);
        while (!dds.finished() && !g_cancel)
        {
            const auto points = dds.propose(jobs);
            auto batch = evaluator.run(points, dds.evaluations() + 1);
            if (g_cancel)
                break;
            std::vector<double> values;
            for (const auto& e : batch)
                values.push_back(e.objective);
            dds.report(points, values);
            record(batch);
            if (dds.evaluations() == 1 && !batch.front().ok)
                throw std::runtime_error("the model with its current values could not be evaluated: " +
                                         batch.front().error);
        }
    }
    else
    {
        const auto points = core::latinHypercube(lower, upper, s.config.budget, s.config.seed);
        for (std::size_t i = 0; i < points.size() && !g_cancel; i += static_cast<std::size_t>(jobs))
        {
            const std::vector<std::vector<double>> chunk(
                points.begin() + static_cast<std::ptrdiff_t>(i),
                points.begin() + static_cast<std::ptrdiff_t>(std::min(points.size(), i + static_cast<std::size_t>(jobs))));
            auto batch = evaluator.run(chunk, static_cast<int>(i) + 1);
            if (g_cancel)
                break;
            record(batch);
        }
        // Rank correlation of each parameter with the objective.
        std::vector<double> f;
        for (const auto& e : all)
            f.push_back(e.ok ? e.objective : std::numeric_limits<double>::quiet_NaN());
        for (std::size_t j = 0; j < s.config.parameters.size(); ++j)
        {
            std::vector<double> x;
            for (const auto& e : all)
                x.push_back(e.values[j]);
            const double rho = core::spearman(x, f);
            Value v = Value::object();
            v.set("parameter", s.config.parameters[j].label());
            v.set("spearman", number(rho));
            sensitivity.push(v);
            if (!out.machine())
                std::cout << "Sensitivity of " << s.config.parameters[j].label() << ": rank correlation "
                          << fmt(rho, 3) << "\n";
        }
    }
    csv.close();
    std::error_code ec;
    if (!keep)
        fs::remove_all(work, ec);

    const bool cancelled = g_cancel.load();
    Value payload = Value::object();
    payload.set("method", method);
    payload.set("evaluations", static_cast<int>(all.size()));
    payload.set("outDir", outDir.u8string());
    payload.set("log", (outDir / "evaluations.csv").u8string());
    if (method == "lhs")
        payload.set("sensitivity", sensitivity);
    if (best.ok)
    {
        payload.set("objective", number(best.objective));
        Value params = Value::array();
        for (std::size_t j = 0; j < s.config.parameters.size(); ++j)
        {
            Value p = s.config.parameters[j].toJson();
            p.set("label", s.config.parameters[j].label());
            p.set("value", best.values[j]);
            params.push(p);
        }
        payload.set("parameters", params);
        Value values = Value::array();
        for (double x : best.values)
            values.push(x);
        payload.set("values", values);
        Value fits = Value::array();
        for (const auto& f : best.fits)
            fits.push(fitJson(f));
        payload.set("series", fits);
        core::Model bestModel = s.model;
        core::applyParameters(bestModel, s.config.parameters, best.values);
        const fs::path bestInp = outDir / (s.inp.stem().u8string() + "-best.inp");
        bestModel.save(bestInp);
        payload.set("bestInput", bestInp.u8string());
        std::ofstream(outDir / "best.json", std::ios::binary | std::ios::trunc) << payload.dump(2) << "\n";
    }

    if (out.machine())
    {
        Value doneEvent = Value::object();
        doneEvent.set("success", best.ok);
        doneEvent.set("cancelled", cancelled);
        doneEvent.set("message", best.ok ? std::string() : "no run succeeded" + (best.error.empty() ? "" : ": " + best.error));
        doneEvent.set("payload", payload);
        out.event("done", doneEvent);
    }
    else if (best.ok)
    {
        std::cout << (cancelled ? "Calibration cancelled" : "Calibration finished") << " after " << all.size()
                  << " runs. Best objective " << fmt(best.objective, 6) << "\n";
        for (std::size_t j = 0; j < s.config.parameters.size(); ++j)
            std::cout << "  " << s.config.parameters[j].label() << " = " << text::formatDouble(best.values[j], 6)
                      << (s.config.parameters[j].mode == core::CalibrationParameter::Mode::Multiply ? " (multiplier)" : "")
                      << "\n";
        std::cout << "Results: " << (outDir / "best.json").u8string() << "\nLog:     "
                  << (outDir / "evaluations.csv").u8string() << "\n";
    }
    if (!best.ok)
    {
        if (!out.machine())
            std::cerr << "error: no run succeeded" << (all.empty() ? "" : ": " + all.front().error) << "\n";
        return 1;
    }
    return cancelled ? 3 : 0;
}
} // namespace

const char* calibrateUsage()
{
    return R"(  mirab-cli calibrate <model.inp> [--config calib.json] [--engine ID] [--jobs N]
                  [--budget N] [--seed N] [--method dds|lhs] [--out-dir DIR]
                  [--start "YYYY-MM-DD HH:MM"] [--keep-runs] [--machine]
  mirab-cli calibrate metrics <model.inp|results.anr> [--observed calib.json|data.csv|data.dat]
                  [--results FILE] [--start "YYYY-MM-DD HH:MM"] [--json|--csv]
)";
}

int runCalibrate(const std::vector<std::string>& args)
{
    const Options o = parseOptions(args);
    const bool machine = o.flag("machine");
    int rc = 0;
    try
    {
        if (!o.positional.empty() && o.positional[0] == "metrics")
            rc = cmdMetrics(o);
        else
            rc = cmdCalibrate(o);
    }
    catch (const std::exception& e)
    {
        if (machine)
        {
            Value v = Value::object();
            v.set("event", "done");
            v.set("success", false);
            v.set("message", e.what());
            std::cout << v.dump() << "\n" << std::flush;
        }
        else
        {
            std::cerr << "error: " << e.what() << "\n";
        }
        rc = 1;
    }
    if (machine)
    {
        // The cancel watcher may still be reading standard input.
        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(rc);
    }
    return rc;
}

} // namespace mirab::cli
