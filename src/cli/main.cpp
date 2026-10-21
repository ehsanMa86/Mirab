/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/cli/main.cpp
 * Purpose: mirab-cli: command-line runner and the isolated solver process used by the desktop application.
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
#include "mirab/MirabConfig.h"
#include "mirab/core/Elevation.h"
#include "mirab/core/Features.h"
#include "mirab/core/Json.h"
#include "mirab/core/LiveResults.h"
#include "mirab/core/Model.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Raster.h"
#include "mirab/core/Results.h"
#include "mirab/core/SwmmObjects.h"
#include "mirab/core/Text.h"
#include "mirab/engine/Compatibility.h"
#include "mirab/engine/EngineInstall.h"
#include "mirab/engine/EngineRegistry.h"
#include "mirab/engine/EpanetToolkit.h"
#include "mirab/interop/Interop.h"
#include "mirab/resilience/Resilience.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
using namespace mirab;
using json::Value;

namespace
{

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------
struct Args
{
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::vector<std::string> flags;

    bool flag(const std::string& f) const
    {
        for (const auto& x : flags)
        {
            if (x == f)
                return true;
        }
        return false;
    }
    std::string opt(const std::string& k, const std::string& fallback = {}) const
    {
        auto it = options.find(k);
        return it == options.end() ? fallback : it->second;
    }
    bool has(const std::string& k) const { return options.count(k) > 0; }
    double num(const std::string& k, double fallback) const
    {
        auto it = options.find(k);
        if (it == options.end())
            return fallback;
        auto v = text::toDouble(it->second);
        if (!v)
            throw std::runtime_error("--" + k + " expects a number");
        return *v;
    }
};

const std::vector<std::string> kValueOptions = {
    "target", "engine", "msx", "out", "report", "workdir", "table", "variable", "element", "results",
    "period", "crs", "required-pressure", "threshold", "links", "nodes", "flow", "residual",
    "time", "max-flow", "minimum-pressure", "dem", "mode", "swmm", "dem-units", "decimals",
    "frame-interval"};

Args parseArgs(const std::vector<std::string>& argv, std::size_t from)
{
    Args a;
    for (std::size_t i = from; i < argv.size(); ++i)
    {
        const std::string& s = argv[i];
        if (s.rfind("--", 0) == 0)
        {
            std::string key = s.substr(2);
            std::string value;
            const auto eq = key.find('=');
            if (eq != std::string::npos)
            {
                value = key.substr(eq + 1);
                key = key.substr(0, eq);
                a.options[key] = value;
                continue;
            }
            bool takesValue = false;
            for (const auto& k : kValueOptions)
                takesValue = takesValue || k == key;
            if (takesValue)
            {
                if (i + 1 >= argv.size())
                    throw std::runtime_error("--" + key + " needs a value");
                a.options[key] = argv[++i];
            }
            else
            {
                a.flags.push_back(key);
            }
        }
        else
        {
            a.positional.push_back(s);
        }
    }
    return a;
}

std::vector<std::string> splitList(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + ",")
    {
        if (c == ',')
        {
            if (!text::trim(cur).empty())
                out.push_back(text::trim(cur));
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Output: human-readable text or JSON lines for the desktop application.
// ---------------------------------------------------------------------------
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
        line(payload.dump());
    }
    /// Writes one prepared JSON line (machine mode only).
    void line(const std::string& text)
    {
        if (!machine_)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << text << "\n" << std::flush;
    }
    void progress(double fraction, const std::string& stage)
    {
        const auto now = std::chrono::steady_clock::now();
        if (machine_)
        {
            if (fraction < 1.0 && now - last_ < std::chrono::milliseconds(100) && stage == lastStage_)
                return;
            last_ = now;
            lastStage_ = stage;
            Value v = Value::object();
            v.set("fraction", fraction);
            v.set("stage", stage);
            event("progress", v);
        }
        else if (stage != lastStage_)
        {
            lastStage_ = stage;
            std::cerr << "  " << stage << "...\n";
        }
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
            std::cerr << (level == "info" ? "" : level + ": ") << message << "\n";
        }
    }

private:
    bool machine_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point last_{};
    std::string lastStage_;
};

std::atomic<bool> g_cancel{false};
std::atomic<bool> g_pause{false};
std::atomic<bool> g_stdinWatcher{false};
std::mutex g_controlMutex;
std::condition_variable g_control;

void setControl(std::atomic<bool>& flag, bool value)
{
    {
        std::lock_guard<std::mutex> lock(g_controlMutex);
        flag = value;
    }
    g_control.notify_all();
}

/// Reads "cancel", "pause" and "resume" commands from standard input.
void watchStdinForCommands()
{
    g_stdinWatcher = true;
    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line))
        {
            const std::string cmd = text::trim(line);
            if (cmd == "cancel")
                setControl(g_cancel, true);
            else if (cmd == "pause")
                setControl(g_pause, true);
            else if (cmd == "resume")
                setControl(g_pause, false);
        }
        // Nobody can resume once standard input is closed.
        setControl(g_pause, false);
    }).detach();
}

/// Blocks the engine between two time steps while a pause is requested.
void waitWhilePaused(Output& out, double seconds)
{
    if (!g_pause)
        return;
    std::unique_lock<std::mutex> lock(g_controlMutex);
    if (!g_pause || g_cancel)
        return;
    json::Value v = json::Value::object();
    v.set("time", seconds);
    out.event("paused", v);
    g_control.wait(lock, [] { return !g_pause || g_cancel; });
    if (!g_cancel)
    {
        json::Value r = json::Value::object();
        r.set("time", seconds);
        out.event("resumed", r);
    }
}

/// Turns the engine's result frames into "layout" and "frame" events.
class FrameStream
{
public:
    explicit FrameStream(Output& out) : out_(out) {}

    void send(const engine::ResultFrame& f)
    {
        if (!f.results)
            return;
        const bool known = announced_ > 0 && f.layout == engineLayout_ && f.results == source_;
        if (!known && (announced_ == 0 || !core::live::sameLayout(shape_, *f.results)))
        {
            shape_ = core::ResultSet();
            for (const auto& t : f.results->tables)
                shape_.defineTable(t.kind, t.ids, t.variables);
            json::Value v = core::live::layoutPayload(*f.results, ++announced_);
            v.set("event", "layout");
            out_.line(v.dump());
        }
        engineLayout_ = f.layout;
        source_ = f.results;
        out_.line(core::live::frameLine(*f.results, f.slot, announced_, f.period, f.time));
    }

private:
    Output& out_;
    int announced_ = 0;
    int engineLayout_ = 0;
    const core::ResultSet* source_ = nullptr;
    core::ResultSet shape_;
};

fs::path pathArg(const std::string& s)
{
    return fs::u8path(s);
}

core::Model loadModel(const Args& a)
{
    if (a.positional.empty())
        throw std::runtime_error("missing input file");
    return core::Model::fromFile(pathArg(a.positional[0]));
}

/// Accepts short names for engines whose identifiers are long.
std::string canonicalEngineId(std::string id)
{
    if (id == "swmm5plus" || id == "swmm5+")
        id = "swmm5plus-1.0";
    return id;
}

std::string engineFor(const Args& a, const core::Model& model, const fs::path& inp,
                      const engine::EngineRegistry& registry, bool* useMsx = nullptr)
{
    const auto settings = core::ProjectSettings::loadFor(inp);
    std::string id = canonicalEngineId(a.opt("engine", settings.engineId));
    if (id.empty())
        id = engine::EngineRegistry::defaultEngineId(model.dialect());
    const bool msx = a.has("msx") || (settings.useMsx && !settings.msxFile.empty());
    if (useMsx)
        *useMsx = msx;
    if (msx)
    {
        const auto* d = registry.descriptor(id);
        if (d && !d->multiSpecies)
        {
            if (const auto* v = registry.msxVariantOf(id))
                id = v->id;
        }
    }
    return id;
}

const char* supportName(engine::Support s)
{
    switch (s)
    {
    case engine::Support::Full:
        return "supported";
    case engine::Support::Ignored:
        return "ignored";
    case engine::Support::Unsupported:
        return "unsupported";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
int cmdVersion()
{
    std::cout << "Mirab " << MIRAB_VERSION_STRING << "\n" << MIRAB_COPYRIGHT << "\n"
              << "License GPL-3.0-or-later: <https://www.gnu.org/licenses/gpl-3.0.html>\n"
              << MIRAB_HOMEPAGE << "\n";
    return 0;
}

std::string resolveEngineId(const engine::EngineRegistry& registry, std::string id)
{
    id = canonicalEngineId(id);
    if (!registry.descriptor(id))
        throw std::runtime_error("unknown engine " + id + " (see mirab-cli engines)");
    return id;
}

int cmdEnginesManage(const Args& a)
{
    const std::string action = a.positional[0];
    engine::EngineRegistry registry;
    const fs::path target = a.has("target") ? pathArg(a.opt("target")) : engine::userEngineDirectory();
    if (action == "install")
    {
        if (a.positional.size() < 2)
            throw std::runtime_error("usage: mirab-cli engines install <manifest.json> [--target DIR]");
        const fs::path manifestFile = pathArg(a.positional[1]);
        const auto manifest = engine::EngineManifest::load(manifestFile);
        resolveEngineId(registry, manifest.engine);
        engine::installFromManifest(manifest, manifestFile.parent_path(), target);
        std::cout << "Installed " << manifest.name << " " << manifest.version << " (package "
                  << manifest.packageVersion << ") into " << target.u8string() << "\n";
        return 0;
    }
    if (action == "uninstall")
    {
        if (a.positional.size() < 2)
            throw std::runtime_error("usage: mirab-cli engines uninstall <engine> [--target DIR]");
        const std::string id = resolveEngineId(registry, a.positional[1]);
        if (!engine::uninstallEngine(id, target))
            throw std::runtime_error(id + " was not installed in " + target.u8string());
        std::cout << "Removed " << id << " from " << target.u8string() << "\n";
        return 0;
    }
    if (action == "verify")
    {
        if (a.positional.size() < 2)
            throw std::runtime_error("usage: mirab-cli engines verify <engine>");
        const std::string id = resolveEngineId(registry, a.positional[1]);
        std::string error;
        auto eng = registry.create(id, &error);
        if (!eng)
            throw std::runtime_error(error);
        std::cout << eng->descriptor().name << " loads correctly: version " << eng->engineVersion() << " from "
                  << eng->libraryPath().u8string() << "\n";
        return 0;
    }
    if (action == "where")
    {
        std::cout << engine::userEngineDirectory().u8string() << "\n";
        return 0;
    }
    throw std::runtime_error("unknown engines command " + action + " (install, uninstall, verify, where)");
}

int cmdEngines(const Args& a)
{
    if (!a.positional.empty())
        return cmdEnginesManage(a);
    engine::EngineRegistry registry;
    Value list = Value::array();
    for (const auto& d : registry.descriptors())
    {
        std::string reason;
        const bool ok = registry.isAvailable(d.id, &reason);
        Value e = Value::object();
        e.set("id", d.id);
        e.set("name", d.name);
        e.set("version", d.version);
        e.set("dialect", core::dialectName(d.dialect));
        e.set("multiSpecies", d.multiSpecies);
        e.set("baseEngine", d.baseEngineId);
        e.set("provider", d.provider);
        e.set("license", d.license);
        e.set("url", d.upstreamUrl);
        e.set("description", d.description);
        e.set("optional", d.optional);
        e.set("available", ok);
        if (ok)
            e.set("library", registry.locate(d)->u8string());
        else
            e.set("reason", reason);
        Value support = Value::object();
        for (const auto& [f, s] : d.support)
            support.set(core::featureName(f), supportName(s));
        e.set("limitations", support);
        list.push(e);
    }
    if (a.flag("json"))
    {
        std::cout << list.dump(2) << "\n";
        return 0;
    }
    for (const auto& e : list.items())
    {
        std::cout << (e.get("available").asBool() ? "[x] " : "[ ] ") << e.get("id").asString() << "  "
                  << e.get("name").asString() << "\n";
        if (!e.get("available").asBool())
            std::cout << "      not installed: " << e.get("reason").asString() << "\n";
    }
    std::cout << "\nEngine folders searched:\n";
    for (const auto& d : registry.searchDirectories())
        std::cout << "  " << d.u8string() << "\n";
    return 0;
}

int cmdInfo(const Args& a)
{
    const auto model = loadModel(a);
    const auto units = model.units();
    std::cout << "Model:      " << a.positional[0] << "\n"
              << "Format:     " << core::dialectName(model.dialect()) << "\n"
              << "Title:      " << model.title() << "\n"
              << "Flow units: " << units.flow << " (" << (units.system == core::UnitSystem::SI ? "SI" : "US") << ")\n"
              << "Duration:   " << text::formatDuration(model.durationSeconds()) << "\n"
              << "Nodes:      " << model.nodes().size() << "\n"
              << "Links:      " << model.links().size() << "\n";
    if (model.dialect() == core::Dialect::Swmm)
        std::cout << "Subcatch.:  " << model.areas().size() << "\n"
                  << "Rain gages: " << model.gages().size() << "\n";
    std::map<std::string, int> kinds;
    for (const auto& n : model.nodes())
        ++kinds[n.kind];
    for (const auto& l : model.links())
        ++kinds[l.kind];
    for (const auto& [k, n] : kinds)
        std::cout << "  " << k << ": " << n << "\n";
    const auto features = core::detectFeatures(model);
    if (!features.empty())
    {
        std::cout << "Features:\n";
        for (const auto& f : features)
            std::cout << "  " << core::featureName(f.feature) << " (" << f.detail << ")\n";
    }
    for (const auto& issue : model.validate())
        std::cout << (issue.severity == core::ModelIssue::Severity::Error ? "error: " : "warning: ")
                  << issue.message << "\n";
    for (const auto& issue : core::validateObjectSections(model))
        std::cout << (issue.severity == core::ModelIssue::Severity::Error ? "error: " : "warning: ")
                  << issue.message << "\n";
    return 0;
}

int cmdCheck(const Args& a)
{
    const auto model = loadModel(a);
    engine::EngineRegistry registry;
    const std::string id = engineFor(a, model, pathArg(a.positional[0]), registry);
    const auto* d = registry.descriptor(id);
    if (!d)
        throw std::runtime_error("unknown engine " + id);
    const auto report = engine::checkCompatibility(model, *d);
    Value out = Value::object();
    out.set("engine", id);
    out.set("runnable", report.runnable());
    Value issues = Value::array();
    for (const auto& i : report.issues)
    {
        Value v = Value::object();
        v.set("level", supportName(i.level));
        v.set("message", i.message);
        v.set("section", i.section);
        v.set("row", i.row);
        issues.push(v);
    }
    out.set("issues", issues);
    if (a.flag("json"))
    {
        std::cout << out.dump(2) << "\n";
    }
    else
    {
        std::cout << d->name << ": " << (report.runnable() ? "the project can run" : "the project cannot run") << "\n";
        for (const auto& i : report.issues)
            std::cout << "  " << supportName(i.level) << ": " << i.message << "\n";
    }
    return report.runnable() ? 0 : 2;
}

int cmdRun(const Args& a)
{
    Output out(a.flag("machine"));
    if (out.machine())
        watchStdinForCommands();
    const fs::path inp = pathArg(a.positional.empty() ? std::string() : a.positional[0]);
    const auto model = loadModel(a);
    engine::EngineRegistry registry;
    bool useMsx = false;
    const std::string id = engineFor(a, model, inp, registry, &useMsx);
    const auto settings = core::ProjectSettings::loadFor(inp);

    std::string error;
    auto eng = registry.create(id, &error);
    if (!eng)
        throw std::runtime_error(error);
    const auto& d = eng->descriptor();
    out.log("info", "Engine: " + d.name + " (" + eng->engineVersion() + ")");

    const auto compat = engine::checkCompatibility(model, d);
    for (const auto& i : compat.issues)
        out.log(i.level == engine::Support::Unsupported ? "error" : "warning", i.message);
    if (!compat.runnable())
        throw std::runtime_error("the project uses features that " + d.name + " cannot run");

    std::vector<std::string> notes;
    const std::string prepared = engine::prepareInput(model, d, &notes);
    fs::path runInput = inp;
    fs::path scratchInput;
    if (!notes.empty())
    {
        scratchInput = inp.parent_path() / ("." + inp.stem().u8string() + ".mirab-run.inp");
        std::ofstream f(scratchInput, std::ios::binary | std::ios::trunc);
        f << prepared;
        runInput = scratchInput;
        for (const auto& n : notes)
            out.log("warning", "for this engine Mirab " + n);
    }

    engine::RunRequest req;
    req.input = runInput;
    if (d.multiSpecies)
    {
        std::string msx = a.opt("msx");
        if (msx.empty() && !settings.msxFile.empty())
            msx = (inp.parent_path() / fs::u8path(settings.msxFile)).u8string();
        if (msx.empty())
            throw std::runtime_error("EPANET-MSX needs an .msx file (use --msx)");
        req.msxInput = pathArg(msx);
    }
    fs::path results = a.has("out") ? pathArg(a.opt("out")) : fs::path(inp).replace_extension(".anr");
    req.reportFile = a.has("report") ? pathArg(a.opt("report")) : fs::path(inp).replace_extension(".rpt");
    req.workDir = a.has("workdir") ? pathArg(a.opt("workdir"))
                                   : fs::temp_directory_path() /
                                         ("mirab-" + std::to_string(std::chrono::steady_clock::now()
                                                                          .time_since_epoch()
                                                                          .count()));
    fs::create_directories(req.workDir);

    engine::RunCallbacks cb;
    cb.progress = [&](double f, const std::string& s) { out.progress(f, s); };
    cb.log = [&](engine::LogLevel l, const std::string& m) {
        out.log(l == engine::LogLevel::Error ? "error" : l == engine::LogLevel::Warning ? "warning" : "info", m);
    };
    cb.cancelRequested = [] { return g_cancel.load(); };
    cb.checkpoint = [&](double seconds) { waitWhilePaused(out, seconds); };
    FrameStream frames(out);
    if (out.machine() && a.flag("live"))
    {
        cb.frame = [&](const engine::ResultFrame& f) { frames.send(f); };
        if (a.has("frame-interval"))
            cb.frameInterval = std::max(0.0, std::stod(a.opt("frame-interval")));
    }

    const auto started = std::chrono::steady_clock::now();
    auto outcome = eng->run(req, cb);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::error_code ec;
    if (!scratchInput.empty())
        fs::remove(scratchInput, ec);
    if (!a.has("workdir"))
        fs::remove_all(req.workDir, ec);

    Value done = Value::object();
    done.set("success", outcome.success);
    done.set("cancelled", outcome.cancelled);
    done.set("code", outcome.code);
    done.set("message", outcome.message);
    done.set("engine", d.id);
    done.set("seconds", seconds);
    if (outcome.success)
    {
        outcome.results.metadata["input"] = inp.filename().u8string();
        outcome.results.metadata["mirabVersion"] = MIRAB_VERSION_STRING;
        outcome.results.save(results);
        done.set("results", results.u8string());
        done.set("report", req.reportFile.u8string());
        done.set("periods", outcome.results.periodCount());
    }
    if (out.machine())
    {
        out.event("done", done);
    }
    else
    {
        if (outcome.success)
            std::cout << "Simulation completed in " << text::formatDouble(seconds, 3) << " s with "
                      << outcome.results.periodCount() << " reporting periods.\n"
                      << "Results: " << results.u8string() << "\nReport:  " << req.reportFile.u8string()
                      << "\n";
        else
            std::cerr << (outcome.cancelled ? "Simulation cancelled." : "Simulation failed: " + outcome.message)
                      << "\n";
    }
    return outcome.success ? 0 : (outcome.cancelled ? 3 : 1);
}

int cmdResults(const Args& a)
{
    if (a.positional.empty())
        throw std::runtime_error("missing results file");
    const auto r = core::ResultSet::loadAny(pathArg(a.positional[0]));
    const std::string tableName = a.opt("table");
    if (tableName.empty())
    {
        std::cout << "Engine:  " << r.engineName << "\nPeriods: " << r.periodCount()
                  << "\nUnits:   " << r.flowUnits << " (" << r.unitSystem << ")\n";
        for (const auto& t : r.tables)
        {
            std::cout << "Table " << t.kind << ": " << t.elementCount() << " elements; variables:";
            for (const auto& v : t.variables)
                std::cout << " " << v.name << (v.unit.empty() ? "" : " [" + v.unit + "]") << ";";
            std::cout << "\n";
        }
        for (const auto& m : r.messages)
            std::cout << "note: " << m << "\n";
        return 0;
    }
    const auto* t = r.table(tableName);
    if (!t)
        throw std::runtime_error("no table " + tableName);
    const int var = t->variableIndex(a.opt("variable", t->variables.empty() ? "" : t->variables[0].name));
    if (var < 0)
        throw std::runtime_error("unknown variable " + a.opt("variable"));
    std::vector<int> elements;
    if (a.has("element"))
    {
        for (const auto& id : splitList(a.opt("element")))
        {
            const int e = t->elementIndex(id);
            if (e < 0)
                throw std::runtime_error("unknown element " + id);
            elements.push_back(e);
        }
    }
    else
    {
        for (int e = 0; e < t->elementCount(); ++e)
            elements.push_back(e);
    }
    std::cout << "time_s";
    for (int e : elements)
        std::cout << "," << t->ids[static_cast<std::size_t>(e)];
    std::cout << "\n";
    for (int p = 0; p < r.periodCount(); ++p)
    {
        std::cout << text::formatDouble(r.times[static_cast<std::size_t>(p)]);
        for (int e : elements)
            std::cout << "," << text::formatDouble(t->value(p, var, e), 7);
        std::cout << "\n";
    }
    return 0;
}

std::unique_ptr<engine::EpanetLibrary> loadEpanet(const Args& a, const core::Model& model,
                                                  const fs::path& inp)
{
    if (model.dialect() != core::Dialect::Epanet)
        throw std::runtime_error("this analysis applies to EPANET models");
    engine::EngineRegistry registry;
    std::string id = engineFor(a, model, inp, registry);
    const auto* d = registry.descriptor(id);
    if (d && d->multiSpecies)
        d = registry.descriptor(d->baseEngineId);
    if (!d || d->dialect != core::Dialect::Epanet)
        throw std::runtime_error("choose an EPANET engine with --engine");
    const auto lib = registry.locate(*d);
    if (!lib)
        throw std::runtime_error(d->name + " is not installed");
    auto epanet = std::make_unique<engine::EpanetLibrary>();
    std::string error;
    if (!epanet->load(*lib, &error))
        throw std::runtime_error(error);
    return epanet;
}

int cmdResilience(const Args& a)
{
    if (a.positional.empty())
        throw std::runtime_error("choose an analysis: todini, service, topology, criticality or fireflow");
    const std::string what = a.positional[0];
    Args rest = a;
    rest.positional.erase(rest.positional.begin());
    Output out(a.flag("machine"));
    if (out.machine())
        watchStdinForCommands();
    const bool asJson = a.flag("json") || out.machine();
    Value payload = Value::object();
    payload.set("analysis", what);
    resilience::Progress progress = [&](double f, const std::string& s) {
        out.progress(f, s);
        return !g_cancel.load();
    };

    if (what == "todini" || what == "service")
    {
        if (!a.has("results"))
            throw std::runtime_error("--results <file.anr> is required");
        const auto results = core::ResultSet::loadAny(pathArg(a.opt("results")));
        if (what == "todini")
        {
            const auto model = loadModel(rest);
            const auto r = resilience::todiniIndex(model, results, a.num("required-pressure", 0.0));
            payload.set("minimum", r.minimum);
            payload.set("mean", r.mean);
            payload.set("note", r.note);
            Value series = Value::array();
            for (std::size_t i = 0; i < r.index.size(); ++i)
            {
                Value p = Value::array();
                p.push(r.times[i]);
                p.push(r.index[i]);
                series.push(p);
            }
            payload.set("series", series);
            if (!asJson)
            {
                if (!r.note.empty())
                    std::cout << r.note << "\n";
                std::cout << "Todini resilience index: minimum " << text::formatDouble(r.minimum, 4) << ", mean "
                          << text::formatDouble(r.mean, 4) << "\n";
                for (std::size_t i = 0; i < r.index.size(); ++i)
                    std::cout << "  " << text::formatDuration(r.times[i]) << "  " << text::formatDouble(r.index[i], 4)
                              << "\n";
            }
        }
        else
        {
            const auto r = resilience::waterServiceAvailability(results);
            payload.set("overall", r.overall);
            payload.set("note", r.note);
            Value nodes = Value::array();
            for (const auto& [id, v] : r.nodes)
            {
                Value n = Value::array();
                n.push(id);
                n.push(v);
                nodes.push(n);
            }
            payload.set("nodes", nodes);
            Value series = Value::array();
            for (std::size_t i = 0; i < r.system.size(); ++i)
            {
                Value p = Value::array();
                p.push(r.times[i]);
                p.push(r.system[i]);
                series.push(p);
            }
            payload.set("series", series);
            if (!asJson)
            {
                if (!r.note.empty())
                    std::cout << r.note << "\n";
                std::cout << "Water service availability: " << text::formatDouble(100.0 * r.overall, 4) << " %\n";
                for (const auto& [id, v] : r.nodes)
                {
                    if (v < 0.999)
                        std::cout << "  " << id << ": " << text::formatDouble(100.0 * v, 4) << " %\n";
                }
            }
        }
    }
    else if (what == "topology")
    {
        const auto model = loadModel(rest);
        const auto t = resilience::topology(model);
        payload.set("nodes", t.nodes);
        payload.set("links", t.links);
        payload.set("components", t.components);
        payload.set("averageDegree", t.averageDegree);
        payload.set("linkDensity", t.linkDensity);
        payload.set("meshedness", t.meshedness);
        auto arr = [](const std::vector<std::string>& v) {
            Value a2 = Value::array();
            for (const auto& s : v)
                a2.push(s);
            return a2;
        };
        payload.set("articulationNodes", arr(t.articulationNodes));
        payload.set("bridgeLinks", arr(t.bridgeLinks));
        payload.set("deadEnds", arr(t.deadEnds));
        payload.set("isolatedNodes", arr(t.isolatedNodes));
        if (!asJson)
        {
            std::cout << "Nodes " << t.nodes << ", links " << t.links << ", connected components " << t.components
                      << "\nAverage degree " << text::formatDouble(t.averageDegree, 4) << ", meshedness "
                      << text::formatDouble(t.meshedness, 4) << "\nCritical nodes (articulation points): "
                      << text::join(t.articulationNodes, ", ") << "\nCritical links (bridges): "
                      << text::join(t.bridgeLinks, ", ") << "\nDead ends: " << text::join(t.deadEnds, ", ") << "\n";
        }
    }
    else if (what == "criticality" || what == "fireflow")
    {
        const fs::path inp = pathArg(rest.positional.empty() ? std::string() : rest.positional[0]);
        const auto model = loadModel(rest);
        auto lib = loadEpanet(a, model, inp);
        if (what == "criticality")
        {
            resilience::CriticalityOptions o;
            o.pressureThreshold = a.num("threshold", model.units().system == core::UnitSystem::SI ? 14.0 : 20.0);
            o.pressureDriven = !a.flag("dda");
            o.minimumPressure = a.num("minimum-pressure", 0.0);
            o.requiredPressure = a.num("required-pressure", 0.0);
            o.links = splitList(a.opt("links"));
            const auto entries = resilience::pipeCriticality(lib->api(), inp, o, progress);
            Value list = Value::array();
            for (const auto& e : entries)
            {
                Value v = Value::object();
                v.set("link", e.link);
                v.set("nodesBelowThreshold", e.nodesBelowThreshold);
                v.set("unmetDemandFraction", e.unmetDemandFraction);
                v.set("minimumPressure", e.minimumPressure);
                v.set("worstNode", e.worstNode);
                v.set("failed", e.failed);
                v.set("error", e.error);
                list.push(v);
            }
            payload.set("threshold", o.pressureThreshold);
            payload.set("entries", list);
            if (!asJson)
            {
                std::cout << "Pipe criticality (pressure threshold " << text::formatDouble(o.pressureThreshold) << ")\n"
                          << "link,nodes_below_threshold,unmet_demand_pct,min_pressure,worst_node\n";
                for (const auto& e : entries)
                    std::cout << e.link << "," << e.nodesBelowThreshold << ","
                              << text::formatDouble(100.0 * e.unmetDemandFraction, 4) << ","
                              << text::formatDouble(e.minimumPressure, 4) << "," << e.worstNode
                              << (e.failed ? ",failed: " + e.error : "") << "\n";
            }
        }
        else
        {
            resilience::FireFlowOptions o;
            if (!a.has("flow"))
                throw std::runtime_error("--flow <fire flow in project flow units> is required");
            o.fireFlow = a.num("flow", 0.0);
            o.residualPressure = a.num("residual", model.units().system == core::UnitSystem::SI ? 14.0 : 20.0);
            o.timeOfDay = a.num("time", 0.0) * 3600.0;
            o.maximumFlow = a.num("max-flow", 0.0);
            o.computeAvailableFlow = !a.flag("no-available");
            o.nodes = splitList(a.opt("nodes"));
            const auto entries = resilience::fireFlow(lib->api(), inp, o, progress);
            Value list = Value::array();
            for (const auto& e : entries)
            {
                Value v = Value::object();
                v.set("node", e.node);
                v.set("residualPressure", e.residualPressure);
                v.set("minimumPressure", e.minimumPressure);
                v.set("criticalNode", e.criticalNode);
                v.set("adequate", e.adequate);
                v.set("availableFlow", e.availableFlow);
                v.set("failed", e.failed);
                v.set("error", e.error);
                list.push(v);
            }
            payload.set("fireFlow", o.fireFlow);
            payload.set("residual", o.residualPressure);
            payload.set("entries", list);
            if (!asJson)
            {
                std::cout << "Fire flow " << text::formatDouble(o.fireFlow) << " " << model.units().flow
                          << ", residual pressure " << text::formatDouble(o.residualPressure) << "\n"
                          << "node,adequate,pressure_at_node,min_pressure,critical_node,available_flow\n";
                for (const auto& e : entries)
                    std::cout << e.node << "," << (e.adequate ? "yes" : "no") << ","
                              << text::formatDouble(e.residualPressure, 4) << ","
                              << text::formatDouble(e.minimumPressure, 4) << "," << e.criticalNode << ","
                              << text::formatDouble(e.availableFlow, 4) << (e.failed ? ",failed: " + e.error : "")
                              << "\n";
            }
        }
    }
    else
    {
        throw std::runtime_error("unknown analysis " + what);
    }
    if (out.machine())
    {
        Value done = Value::object();
        done.set("success", true);
        done.set("payload", payload);
        out.event("done", done);
    }
    else if (asJson)
    {
        std::cout << payload.dump(2) << "\n";
    }
    return 0;
}

int cmdExport(const Args& a)
{
    if (a.positional.size() < 2)
        throw std::runtime_error("usage: mirab-cli export geojson|gpkg|wntr <model.inp> --out <file>");
    const std::string format = a.positional[0];
    Args rest = a;
    rest.positional.erase(rest.positional.begin());
    const auto model = loadModel(rest);
    if (!a.has("out"))
        throw std::runtime_error("--out <file> is required");
    const fs::path out = pathArg(a.opt("out"));
    interop::GisOptions g;
    const auto settings = core::ProjectSettings::loadFor(pathArg(rest.positional[0]));
    g.crs = a.opt("crs", settings.crs);
    core::ResultSet results;
    if (a.has("results"))
    {
        results = core::ResultSet::loadAny(pathArg(a.opt("results")));
        g.results = &results;
        g.period = static_cast<int>(a.num("period", -1));
    }
    std::vector<std::string> notes;
    if (format == "geojson")
        interop::writeGeoJson(model, out, g);
    else if (format == "gpkg")
        interop::writeGeoPackage(model, out, g);
    else if (format == "wntr")
        interop::writeWntrJson(model, out, &notes);
    else
        throw std::runtime_error("unknown export format " + format);
    for (const auto& n : notes)
        std::cerr << "note: " << n << "\n";
    std::cout << "Wrote " << out.u8string() << "\n";
    return 0;
}

int cmdImport(const Args& a)
{
    if (a.positional.size() < 2 || a.positional[0] != "wntr")
        throw std::runtime_error("usage: mirab-cli import wntr <network.json> --out <model.inp>");
    if (!a.has("out"))
        throw std::runtime_error("--out <model.inp> is required");
    std::vector<std::string> notes;
    const auto model = interop::readWntrJson(pathArg(a.positional[1]), &notes);
    model.save(pathArg(a.opt("out")));
    for (const auto& n : notes)
        std::cerr << "note: " << n << "\n";
    std::cout << "Wrote " << a.opt("out") << " (" << model.nodes().size() << " nodes, " << model.links().size()
              << " links)\n";
    return 0;
}

int cmdElevations(const Args& a)
{
    if (a.positional.empty() || !a.has("dem"))
        throw std::runtime_error("usage: mirab-cli elevations <model.inp> --dem <file.tif|file.asc> [--out model.inp]");
    const fs::path inp = pathArg(a.positional[0]);
    auto model = core::Model::fromFile(inp);
    std::string err;
    std::shared_ptr<raster::Raster> dem(raster::Raster::open(pathArg(a.opt("dem")), &err).release());
    if (!dem)
        throw std::runtime_error(err);
    if (dem->isImagery())
        throw std::runtime_error(a.opt("dem") + " holds colour imagery, not elevations");
    const auto settings = core::ProjectSettings::loadFor(inp);
    double demUnit = 0.0;
    const std::string units = text::toLower(a.opt("dem-units", "auto"));
    if (units == "m" || units == "metre" || units == "meter" || units == "metres" || units == "meters")
        demUnit = 1.0;
    else if (units == "ft" || units == "feet")
        demUnit = crs::kInternationalFoot;
    else if (units == "usft" || units == "us-ft")
        demUnit = crs::kUsSurveyFoot;
    else if (units != "auto")
        throw std::runtime_error("--dem-units expects m, ft, usft or auto");
    const core::TerrainSampler terrain(dem, a.opt("crs", settings.crs), model.units(), demUnit);

    core::ElevationOptions o;
    const std::string mode = text::toLower(a.opt("mode", "all"));
    if (mode == "missing")
        o.scope = core::ElevationScope::Missing;
    else if (mode == "selected")
        o.scope = core::ElevationScope::Selected;
    else if (mode != "all")
        throw std::runtime_error("--mode expects all, missing or selected");
    if (o.scope == core::ElevationScope::Selected)
    {
        o.selected = splitList(a.opt("nodes"));
        if (o.selected.empty())
            throw std::runtime_error("--mode selected needs --nodes ID,ID,...");
    }
    o.reservoirs = a.flag("reservoirs");
    o.tanks = !a.flag("no-tanks");
    o.junctions = !a.flag("no-junctions");
    const std::string swmm = text::toLower(a.opt("swmm", "invert"));
    if (swmm == "depth" || swmm == "maxdepth")
        o.swmmMode = core::SwmmElevationMode::MaxDepth;
    else if (swmm != "invert")
        throw std::runtime_error("--swmm expects invert or depth");
    o.decimals = static_cast<int>(a.num("decimals", 2));

    const auto plan = core::planElevations(model, terrain, o);
    std::map<core::ElevationChange::Status, int> counts;
    for (const auto& c : plan)
        ++counts[c.status];
    auto fmt = [](const std::optional<double>& v) { return v ? text::formatDouble(*v, 10) : std::string("-"); };
    if (a.flag("json"))
    {
        Value out = Value::object();
        Value rows = Value::array();
        for (const auto& c : plan)
        {
            Value r = Value::object();
            r.set("id", c.id);
            r.set("kind", c.kind);
            r.set("field", c.field);
            r.set("status", core::statusName(c.status));
            if (c.ground)
                r.set("ground", *c.ground);
            if (c.oldValue)
                r.set("old", *c.oldValue);
            if (c.newValue)
                r.set("new", *c.newValue);
            if (!c.note.empty())
                r.set("note", c.note);
            rows.push(r);
        }
        out.set("nodes", rows);
        Value notes = Value::array();
        for (const auto& n : terrain.notes())
            notes.push(n);
        out.set("notes", notes);
        std::cout << out.dump(2) << "\n";
    }
    else
    {
        const auto& info = dem->info();
        std::cout << "DEM: " << info.width << " x " << info.height << " " << info.format
                  << (info.crsDescription.empty() ? "" : ", " + info.crsDescription) << "\n";
        for (const auto& n : terrain.notes())
            std::cout << "note: " << n << "\n";
        for (const auto& c : plan)
        {
            if (c.status == core::ElevationChange::Status::Same)
                continue;
            std::cout << "  " << c.id << "  " << c.field << "  " << fmt(c.oldValue) << " -> " << fmt(c.newValue)
                      << "  (" << core::statusName(c.status) << (c.note.empty() ? "" : "; " + c.note) << ")\n";
        }
        using S = core::ElevationChange::Status;
        std::cout << counts[S::Update] << " to update, " << counts[S::Same] << " unchanged, " << counts[S::Outside]
                  << " outside the DEM, " << counts[S::NoData] << " on NoData, " << counts[S::Unplaced]
                  << " without coordinates, " << counts[S::Skipped] << " skipped\n";
    }
    if (a.has("out"))
    {
        const int n = core::applyElevations(model, plan, o.decimals);
        model.save(pathArg(a.opt("out")));
        std::cerr << "Wrote " << a.opt("out") << " (" << n << " values changed)\n";
    }
    else if (!a.flag("json"))
    {
        std::cout << "Preview only: add --out <model.inp> to write the changes.\n";
    }
    return 0;
}

void usage()
{
    std::cout <<
        R"(Mirab command-line interface

Usage:
  mirab-cli version
  mirab-cli engines [--json]
  mirab-cli engines install <manifest.json> [--target DIR]
  mirab-cli engines uninstall <engine> [--target DIR]
  mirab-cli engines verify <engine>
  mirab-cli engines where
  mirab-cli info <model.inp>
  mirab-cli check <model.inp> [--engine ID] [--msx file.msx] [--json]
  mirab-cli run <model.inp> [--engine ID] [--msx file.msx] [--out results.anr]
                  [--report file.rpt] [--workdir DIR] [--machine [--live]
                  [--frame-interval SECONDS]]
  mirab-cli results <results.anr|.out> [--table node|link|subcatchment|system]
                  [--variable NAME] [--element ID[,ID...]]
  mirab-cli resilience todini <model.inp> --results FILE [--required-pressure P] [--json]
  mirab-cli resilience service --results FILE [--json]
  mirab-cli resilience topology <model.inp> [--json]
  mirab-cli resilience criticality <model.inp> [--engine ID] [--threshold P] [--dda]
                  [--links ID,ID] [--json]
  mirab-cli resilience fireflow <model.inp> --flow Q [--residual P] [--time HOURS]
                  [--nodes ID,ID] [--max-flow Q] [--no-available] [--json]
  mirab-cli export geojson|gpkg|wntr <model.inp> --out FILE [--crs EPSG:n]
                  [--results FILE [--period N]]
  mirab-cli import wntr <network.json> --out model.inp
  mirab-cli elevations <model.inp> --dem FILE.tif|FILE.asc [--mode all|missing|selected]
                  [--nodes ID,ID] [--reservoirs] [--no-tanks] [--swmm invert|depth]
                  [--dem-units auto|m|ft|usft] [--crs EPSG:n] [--decimals N]
                  [--out model.inp] [--json]

Engines: epanet-2.2-epa, epanet-2.3-owa, epanet-msx-2.0-epa22, epanet-msx-2.0-owa23,
         swmm-5.2-epa, swmm5plus-1.0 (when installed). Run "mirab-cli engines".
Pressures and flows are in the units of the model.
With --machine, events are written as JSON lines and standard input accepts
"cancel", "pause" and "resume"; --live adds the results of each reporting
period while the run is in progress (at most one every --frame-interval
seconds, default 0.25).
)";
}

std::vector<std::string> utf8Arguments(int argc, char** argv)
{
    std::vector<std::string> out;
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* wargs = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i)
    {
        const int len = WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 1)
            WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(s);
    }
    LocalFree(wargs);
#else
    for (int i = 0; i < argc; ++i)
        out.emplace_back(argv[i]);
#endif
    return out;
}

int runMain(int argc, char** argv)
{
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    const auto argsv = utf8Arguments(argc, argv);
    if (argsv.size() < 2 || argsv[1] == "--help" || argsv[1] == "-h" || argsv[1] == "help")
    {
        usage();
        return argsv.size() < 2 ? 1 : 0;
    }
    const std::string cmd = argsv[1];
    const bool machine = [&] {
        for (const auto& s : argsv)
        {
            if (s == "--machine")
                return true;
        }
        return false;
    }();
    try
    {
        const Args a = parseArgs(argsv, 2);
        if (cmd == "version" || cmd == "--version")
            return cmdVersion();
        if (cmd == "engines")
            return cmdEngines(a);
        if (cmd == "info")
            return cmdInfo(a);
        if (cmd == "check")
            return cmdCheck(a);
        if (cmd == "run")
            return cmdRun(a);
        if (cmd == "results")
            return cmdResults(a);
        if (cmd == "resilience")
            return cmdResilience(a);
        if (cmd == "export")
            return cmdExport(a);
        if (cmd == "import")
            return cmdImport(a);
        if (cmd == "elevations")
            return cmdElevations(a);
        std::cerr << "unknown command " << cmd << "\n";
        usage();
        return 1;
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
        return 1;
    }
}

} // namespace

int main(int argc, char** argv)
{
    const int rc = runMain(argc, argv);
    if (g_stdinWatcher)
    {
        // The cancel watcher may be blocked reading standard input. On glibc,
        // normal exit would wait for that stream's lock forever, so flush the
        // output streams and end the process directly.
        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(rc);
    }
    return rc;
}
