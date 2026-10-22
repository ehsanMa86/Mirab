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
#include "Calibrate.h"
#include "Link.h"
#include "mirab/MirabConfig.h"
#include "mirab/core/Elevation.h"
#include "mirab/core/Features.h"
#include "mirab/core/Json.h"
#include "mirab/core/LiveResults.h"
#include "mirab/core/Model.h"
#include "mirab/core/ModelJson.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Raster.h"
#include "mirab/core/Results.h"
#include "mirab/core/Scenario.h"
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
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
    "frame-interval", "section", "id", "column", "value", "row", "option", "edits", "format",
    "category", "scenario", "scenarios", "jobs"};

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

/// Loads the model of the first argument; with --scenario, that scenario of it.
/// Conflicts of the scenario go to @p conflicts, or to standard error.
core::Model loadModel(const Args& a, std::vector<core::ScenarioConflict>* conflicts = nullptr)
{
    if (a.positional.empty())
        throw std::runtime_error("missing input file");
    const fs::path inp = pathArg(a.positional[0]);
    core::Model model = core::Model::fromFile(inp);
    const std::string scenario = a.opt("scenario");
    if (scenario.empty() || text::iequals(scenario, "base"))
        return model;
    const auto settings = core::ProjectSettings::loadFor(inp);
    std::vector<core::ScenarioConflict> found;
    model = core::scenarioModel(model, settings.scenarios, scenario, &found);
    if (conflicts)
        *conflicts = std::move(found);
    else
    {
        for (const auto& c : found)
            std::cerr << "warning: scenario " << c.scenario << ": " << c.message << "\n";
    }
    return model;
}

/// Name of the scenario chosen with --scenario, empty for the base model.
std::string scenarioArg(const Args& a)
{
    const std::string s = a.opt("scenario");
    return text::iequals(s, "base") ? std::string() : s;
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
    const auto chosen = core::scenarioEngine(settings.scenarios, scenarioArg(a),
                                             {settings.engineId, settings.useMsx, settings.msxFile});
    std::string id = canonicalEngineId(a.opt("engine", chosen.engineId));
    if (id.empty())
        id = engine::EngineRegistry::defaultEngineId(model.dialect());
    const bool msx = a.has("msx") || (chosen.useMsx && !chosen.msxFile.empty());
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
int cmdVersion(const Args& a)
{
    if (a.flag("json"))
    {
        Value v = Value::object();
        v.set("name", "Mirab");
        v.set("version", MIRAB_VERSION_STRING);
        v.set("copyright", MIRAB_COPYRIGHT);
        v.set("license", "GPL-3.0-or-later");
        v.set("homepage", MIRAB_HOMEPAGE);
        std::cout << v.dump(2) << "\n";
        return 0;
    }
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
        if (a.flag("json"))
        {
            Value v = Value::object();
            v.set("engine", manifest.engine);
            v.set("name", manifest.name);
            v.set("version", manifest.version);
            v.set("packageVersion", manifest.packageVersion);
            v.set("target", target.u8string());
            std::cout << v.dump(2) << "\n";
            return 0;
        }
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
        if (a.flag("json"))
        {
            Value v = Value::object();
            v.set("engine", id);
            v.set("target", target.u8string());
            std::cout << v.dump(2) << "\n";
            return 0;
        }
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
        if (a.flag("json"))
        {
            Value v = Value::object();
            v.set("engine", id);
            v.set("name", eng->descriptor().name);
            v.set("version", eng->engineVersion());
            v.set("library", eng->libraryPath().u8string());
            std::cout << v.dump(2) << "\n";
            return 0;
        }
        std::cout << eng->descriptor().name << " loads correctly: version " << eng->engineVersion() << " from "
                  << eng->libraryPath().u8string() << "\n";
        return 0;
    }
    if (action == "where")
    {
        if (a.flag("json"))
        {
            Value v = Value::object();
            v.set("path", engine::userEngineDirectory().u8string());
            std::cout << v.dump(2) << "\n";
            return 0;
        }
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

Value issueJson(const core::ModelIssue& issue)
{
    Value v = Value::object();
    v.set("severity", issue.severity == core::ModelIssue::Severity::Error ? "error" : "warning");
    v.set("message", issue.message);
    v.set("section", issue.section);
    v.set("row", issue.row);
    return v;
}

int cmdInfo(const Args& a)
{
    const auto model = loadModel(a);
    const auto units = model.units();
    if (a.flag("json"))
    {
        Value v = Value::object();
        v.set("path", a.positional[0]);
        v.set("dialect", core::dialectName(model.dialect()));
        v.set("title", model.title());
        Value u = Value::object();
        u.set("flow", units.flow);
        u.set("system", units.system == core::UnitSystem::SI ? "SI" : "US");
        v.set("units", u);
        v.set("duration", model.durationSeconds());
        v.set("reportStep", model.reportStepSeconds());
        Value counts = Value::object();
        counts.set("nodes", model.nodes().size());
        counts.set("links", model.links().size());
        counts.set("subcatchments", model.areas().size());
        counts.set("gages", model.gages().size());
        v.set("counts", counts);
        std::map<std::string, int> kinds;
        for (const auto& n : model.nodes())
            ++kinds[n.kind];
        for (const auto& l : model.links())
            ++kinds[l.kind];
        Value k = Value::object();
        for (const auto& [name, n] : kinds)
            k.set(name, n);
        v.set("kinds", k);
        Value features = Value::array();
        for (const auto& f : core::detectFeatures(model))
        {
            Value fv = Value::object();
            fv.set("feature", core::featureName(f.feature));
            fv.set("detail", f.detail);
            fv.set("section", f.section);
            fv.set("row", f.row);
            features.push(fv);
        }
        v.set("features", features);
        Value issues = Value::array();
        for (const auto& issue : model.validate())
            issues.push(issueJson(issue));
        for (const auto& issue : core::validateObjectSections(model))
            issues.push(issueJson(issue));
        v.set("issues", issues);
        std::cout << v.dump(2) << "\n";
        return 0;
    }
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
    out.set("engineName", d->name);
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
    std::vector<core::ScenarioConflict> conflicts;
    const auto model = loadModel(a, &conflicts);
    engine::EngineRegistry registry;
    bool useMsx = false;
    const std::string id = engineFor(a, model, inp, registry, &useMsx);
    const auto settings = core::ProjectSettings::loadFor(inp);
    const std::string scenario = scenarioArg(a);
    const auto chosen = core::scenarioEngine(settings.scenarios, scenario,
                                             {settings.engineId, settings.useMsx, settings.msxFile});
    // A scenario's results and report go beside the project's: <name>.<scenario>.anr
    const fs::path outBase =
        scenario.empty() ? fs::path(inp).replace_extension()
                         : inp.parent_path() / (inp.stem().u8string() + "." + core::scenarioFileStem(scenario));

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
    if (!scenario.empty())
        out.log("info", "Scenario: " + scenario);
    for (const auto& c : conflicts)
        out.log("warning", "scenario " + c.scenario + ": " + c.message);

    std::vector<std::string> notes;
    const std::string prepared = engine::prepareInput(model, d, &notes);
    fs::path runInput = inp;
    fs::path scratchInput;
    if (!notes.empty() || !scenario.empty())
    {
        // Next to the project, so that file references relative to it keep working.
        scratchInput = inp.parent_path() / ("." + outBase.filename().u8string() + ".mirab-run.inp");
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
        if (msx.empty() && !chosen.msxFile.empty())
            msx = (inp.parent_path() / fs::u8path(chosen.msxFile)).u8string();
        if (msx.empty())
            throw std::runtime_error("EPANET-MSX needs an .msx file (use --msx)");
        req.msxInput = pathArg(msx);
    }
    fs::path results = a.has("out") ? pathArg(a.opt("out")) : fs::path(outBase.u8string() + ".anr");
    req.reportFile = a.has("report") ? pathArg(a.opt("report")) : fs::path(outBase.u8string() + ".rpt");
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
    if (!scenario.empty())
        done.set("scenario", scenario);
    done.set("seconds", seconds);
    if (outcome.success)
    {
        outcome.results.metadata["input"] = inp.filename().u8string();
        outcome.results.metadata["mirabVersion"] = MIRAB_VERSION_STRING;
        if (!scenario.empty())
            outcome.results.metadata["scenario"] = scenario;
        outcome.results.save(results);
        done.set("results", results.u8string());
        done.set("report", req.reportFile.u8string());
        done.set("periods", outcome.results.periodCount());
    }
    if (out.machine())
    {
        out.event("done", done);
    }
    else if (a.flag("json"))
    {
        std::cout << done.dump(2) << "\n";
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

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------
int cmdScenarios(const Args& a)
{
    if (a.positional.size() < 2)
        throw std::runtime_error("usage: mirab-cli scenarios list|export <model.inp> [--scenario NAME --out FILE]");
    const std::string sub = a.positional[0];
    const fs::path inp = pathArg(a.positional[1]);
    const core::Model base = core::Model::fromFile(inp);
    const auto settings = core::ProjectSettings::loadFor(inp);
    if (sub == "list")
    {
        Value list = Value::array();
        for (const auto& s : settings.scenarios)
        {
            std::vector<core::ScenarioConflict> conflicts;
            std::string error;
            try
            {
                core::scenarioModel(base, settings.scenarios, s.name, &conflicts);
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
            const auto engine = core::scenarioEngine(settings.scenarios, s.name,
                                                     {settings.engineId, settings.useMsx, settings.msxFile});
            const fs::path results =
                inp.parent_path() / (inp.stem().u8string() + "." + core::scenarioFileStem(s.name) + ".anr");
            Value v = Value::object();
            v.set("name", s.name);
            v.set("parent", s.parent);
            v.set("description", s.description);
            v.set("changes", static_cast<int>(s.changes.size()));
            v.set("conflicts", static_cast<int>(conflicts.size()));
            v.set("engine", engine.engineId);
            v.set("results", fs::exists(results) ? results.u8string() : std::string());
            if (!error.empty())
                v.set("error", error);
            list.push(v);
        }
        if (a.flag("json"))
        {
            std::cout << list.dump(2) << "\n";
            return 0;
        }
        if (settings.scenarios.empty())
        {
            std::cout << "No scenarios in " << core::ProjectSettings::sidecarPath(inp).u8string() << "\n";
            return 0;
        }
        for (const auto& v : list.items())
        {
            std::cout << v.get("name").asString();
            if (!v.get("parent").asString().empty())
                std::cout << "  (based on " << v.get("parent").asString() << ")";
            std::cout << "\n  " << v.get("changes").asNumber() << " change(s)";
            if (v.get("conflicts").asNumber() > 0)
                std::cout << ", " << v.get("conflicts").asNumber() << " conflict(s)";
            if (!v.get("engine").asString().empty())
                std::cout << ", engine " << v.get("engine").asString();
            std::cout << (v.get("results").asString().empty() ? std::string(", not run")
                                                              : ", results " + v.get("results").asString())
                      << "\n";
            if (!v.get("description").asString().empty())
                std::cout << "  " << v.get("description").asString() << "\n";
            if (!v.get("error").asString().empty())
                std::cout << "  error: " << v.get("error").asString() << "\n";
        }
        return 0;
    }
    if (sub == "export")
    {
        if (!a.has("scenario") || !a.has("out"))
            throw std::runtime_error("usage: mirab-cli scenarios export <model.inp> --scenario NAME --out FILE");
        std::vector<core::ScenarioConflict> conflicts;
        const core::Model m = core::scenarioModel(base, settings.scenarios, scenarioArg(a), &conflicts);
        for (const auto& c : conflicts)
            std::cerr << "warning: scenario " << c.scenario << ": " << c.message << "\n";
        m.save(pathArg(a.opt("out")));
        if (a.flag("json"))
        {
            Value v = Value::object();
            v.set("scenario", scenarioArg(a));
            v.set("path", a.opt("out"));
            Value list = Value::array();
            for (const auto& c : conflicts)
            {
                Value k = Value::object();
                k.set("scenario", c.scenario);
                k.set("section", c.section);
                k.set("key", c.key);
                k.set("missing", c.missing);
                k.set("message", c.message);
                list.push(k);
            }
            v.set("conflicts", list);
            std::cout << v.dump(2) << "\n";
        }
        else
        {
            std::cout << "Wrote " << a.opt("out") << "\n";
        }
        return 0;
    }
    throw std::runtime_error("unknown scenarios command " + sub + " (list or export)");
}

// ---------------------------------------------------------------------------
// Batch runs: one solver process per scenario, several at a time
// ---------------------------------------------------------------------------
int cmdBatch(const Args& a)
{
    if (a.positional.empty())
        throw std::runtime_error("usage: mirab-cli batch <model.inp> [--scenarios A,B|--all] [--jobs N]");
    const fs::path inp = fs::absolute(pathArg(a.positional[0]));
    const core::Model base = core::Model::fromFile(inp);
    const auto settings = core::ProjectSettings::loadFor(inp);
    std::vector<std::string> names; // "" is the base model
    if (a.has("scenarios") && !a.flag("all"))
    {
        for (const auto& n : splitList(a.opt("scenarios")))
        {
            const bool isBase = text::iequals(n, "base") || text::iequals(n, "base model");
            if (!isBase && !core::findScenario(settings.scenarios, n))
                throw std::runtime_error("no scenario named \"" + n + "\" (see mirab-cli scenarios list)");
            names.push_back(isBase ? std::string() : n);
        }
    }
    else
    {
        names.push_back({});
        for (const auto& s : settings.scenarios)
            names.push_back(s.name);
    }
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const int jobs = std::max(1, static_cast<int>(a.num("jobs", std::min(4u, cores))));
    const fs::path program = cli::programPath();
    if (program.empty())
        throw std::runtime_error("cannot find the mirab-cli program");
    const bool asJson = a.flag("json");
    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto label = [](const std::string& n) { return n.empty() ? std::string("Base model") : n; };
    auto stemOf = [&](const std::string& n) {
        return inp.parent_path() /
               (inp.stem().u8string() + (n.empty() ? std::string() : "." + core::scenarioFileStem(n)));
    };

    struct Job
    {
        int code = -1;
        double seconds = 0;
        std::string log;
    };
    std::vector<Job> done(names.size());
    std::atomic<std::size_t> next{0};
    std::mutex print;
    std::size_t finished = 0;
    if (!asJson)
        std::cout << "Running " << names.size() << " simulation(s), "
                  << std::min(static_cast<std::size_t>(jobs), names.size()) << " at a time.\n";
    auto worker = [&] {
        for (std::size_t i = next++; i < names.size(); i = next++)
        {
            const std::string& name = names[i];
            const fs::path log = fs::temp_directory_path() / ("mirab-batch-" + stamp + "-" + std::to_string(i) + ".log");
            std::vector<std::string> args = {"run", inp.u8string()};
            if (!name.empty())
                args.insert(args.end(), {"--scenario", name});
            if (a.has("engine"))
                args.insert(args.end(), {"--engine", a.opt("engine")});
            const auto started = std::chrono::steady_clock::now();
            std::string error;
            Job& j = done[i];
            j.code = cli::runChildProcess(program, args, log, &error);
            j.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            {
                std::ifstream in(log, std::ios::binary);
                std::ostringstream text;
                text << in.rdbuf();
                j.log = error.empty() ? text.str() : error;
            }
            std::error_code ec;
            fs::remove(log, ec);
            std::lock_guard<std::mutex> lock(print);
            ++finished;
            if (asJson)
                continue;
            std::cout << "[" << finished << "/" << names.size() << "] " << label(name) << ": "
                      << (j.code == 0 ? "finished" : "failed") << " in " << text::formatDouble(j.seconds, 3)
                      << " s\n";
            std::istringstream lines(j.log);
            std::string line;
            while (std::getline(lines, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (j.code != 0 || line.rfind("Results:", 0) == 0 || line.rfind("warning", 0) == 0)
                    std::cout << "    " << line << "\n";
            }
            std::cout << std::flush;
        }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < std::min<int>(jobs, static_cast<int>(names.size())); ++t)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    int failed = 0;
    Value runs = Value::array();
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        failed += done[i].code != 0 ? 1 : 0;
        Value v = Value::object();
        v.set("scenario", names[i]);
        v.set("success", done[i].code == 0);
        v.set("seconds", done[i].seconds);
        v.set("results", done[i].code == 0 ? stemOf(names[i]).u8string() + ".anr" : std::string());
        v.set("report", stemOf(names[i]).u8string() + ".rpt");
        if (done[i].code != 0)
            v.set("log", done[i].log);
        runs.push(v);
    }
    if (asJson)
    {
        Value out = Value::object();
        out.set("model", inp.u8string());
        out.set("runs", runs);
        out.set("failed", failed);
        std::cout << out.dump(2) << "\n";
    }
    else if (failed)
    {
        std::cout << failed << " of " << names.size() << " simulation(s) failed.\n";
    }
    return failed ? 1 : 0;
}

Value variablesJson(const std::vector<core::Variable>& vars)
{
    Value list = Value::array();
    for (const auto& v : vars)
    {
        Value e = Value::object();
        e.set("name", v.name);
        e.set("unit", v.unit);
        list.push(e);
    }
    return list;
}

/// Same members as the header of an .anr file, plus the period count.
Value resultsSummary(const core::ResultSet& r)
{
    Value v = Value::object();
    v.set("engineId", r.engineId);
    v.set("engineName", r.engineName);
    v.set("title", r.title);
    v.set("flowUnits", r.flowUnits);
    v.set("unitSystem", r.unitSystem);
    v.set("reportStart", r.reportStart);
    v.set("reportStep", r.reportStep);
    v.set("periods", r.periodCount());
    Value times = Value::array();
    for (double t : r.times)
        times.push(t);
    v.set("times", times);
    Value meta = Value::object();
    for (const auto& [k, m] : r.metadata)
        meta.set(k, m);
    v.set("metadata", meta);
    Value messages = Value::array();
    for (const auto& m : r.messages)
        messages.push(m);
    v.set("messages", messages);
    Value tables = Value::array();
    for (const auto& t : r.tables)
    {
        Value tv = Value::object();
        tv.set("kind", t.kind);
        Value ids = Value::array();
        for (const auto& id : t.ids)
            ids.push(id);
        tv.set("ids", ids);
        tv.set("variables", variablesJson(t.variables));
        tables.push(tv);
    }
    v.set("tables", tables);
    return v;
}

/// Writes a NumPy .npy file (format 1.0) of little-endian float32 values.
void writeNpy(const fs::path& path, const std::vector<std::size_t>& shape,
              const std::vector<float>& values)
{
    std::vector<std::string> dims;
    for (std::size_t d : shape)
        dims.push_back(std::to_string(d));
    std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': (" +
                         text::join(dims, ", ") + (shape.size() == 1 ? "," : "") + "), }";
    const std::size_t total = 10 + header.size() + 1;
    header.append((64 - total % 64) % 64, ' ');
    header += '\n';
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("cannot write " + path.u8string());
    const unsigned char magic[8] = {0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0};
    f.write(reinterpret_cast<const char*>(magic), 8);
    const auto len = static_cast<std::uint16_t>(header.size());
    const unsigned char lenBytes[2] = {static_cast<unsigned char>(len & 0xff),
                                       static_cast<unsigned char>(len >> 8)};
    f.write(reinterpret_cast<const char*>(lenBytes), 2);
    f << header;
    std::string block(values.size() * 4, '\0');
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &values[i], 4);
        for (int b = 0; b < 4; ++b)
            block[i * 4 + static_cast<std::size_t>(b)] =
                static_cast<char>((bits >> (8 * b)) & 0xff);
    }
    f.write(block.data(), static_cast<std::streamsize>(block.size()));
    if (!f)
        throw std::runtime_error("cannot write " + path.u8string());
}

int cmdResults(const Args& a)
{
    if (a.positional.empty())
        throw std::runtime_error("missing results file");
    const auto r = core::ResultSet::loadAny(pathArg(a.positional[0]));
    const std::string format = text::toLower(a.opt("format", a.flag("json") ? "json" : "csv"));
    if (format != "csv" && format != "json" && format != "npy" && format != "anr")
        throw std::runtime_error("--format expects csv, json, npy or anr");
    if ((format == "npy" || format == "anr") && !a.has("out"))
        throw std::runtime_error("--format " + format + " needs --out <file>");
    if (format == "anr")
    {
        r.save(pathArg(a.opt("out")));
        std::cerr << "Wrote " << a.opt("out") << "\n";
        return 0;
    }
    const std::string tableName = a.opt("table");
    if (tableName.empty())
    {
        if (format == "json")
        {
            std::cout << resultsSummary(r).dump(a.flag("compact") ? -1 : 2) << "\n";
            return 0;
        }
        if (format == "npy")
            throw std::runtime_error("--format npy needs --table");
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

    // Variables: those named by --variable, else the first one for CSV and
    // every one for JSON and NumPy.
    std::vector<int> vars;
    if (a.has("variable"))
    {
        for (const auto& name : splitList(a.opt("variable")))
        {
            const int v = t->variableIndex(name);
            if (v < 0)
                throw std::runtime_error("unknown variable " + name);
            vars.push_back(v);
        }
    }
    else if (format == "csv")
    {
        if (t->variables.empty())
            throw std::runtime_error("table " + tableName + " has no variables");
        vars.push_back(0);
    }
    else
    {
        for (int v = 0; v < t->variableCount(); ++v)
            vars.push_back(v);
    }
    if (format == "csv" && vars.size() != 1)
        throw std::runtime_error("CSV output takes one --variable");
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
    std::vector<int> periods;
    if (a.has("period"))
    {
        const int p = static_cast<int>(a.num("period", 0));
        if (p < 0 || p >= r.periodCount())
            throw std::runtime_error("--period must be between 0 and " +
                                     std::to_string(r.periodCount() - 1));
        periods.push_back(p);
    }
    else
    {
        for (int p = 0; p < r.periodCount(); ++p)
            periods.push_back(p);
    }

    if (format == "npy")
    {
        // Shape (periods, elements) for one --variable, else (periods, variables, elements).
        std::vector<float> values;
        values.reserve(periods.size() * vars.size() * elements.size());
        for (int p : periods)
            for (int v : vars)
                for (int e : elements)
                    values.push_back(t->value(p, v, e));
        std::vector<std::size_t> shape = {periods.size()};
        if (vars.size() != 1 || !a.has("variable"))
            shape.push_back(vars.size());
        shape.push_back(elements.size());
        writeNpy(pathArg(a.opt("out")), shape, values);
        std::cerr << "Wrote " << a.opt("out") << "\n";
        return 0;
    }
    if (format == "json")
    {
        Value out = Value::object();
        out.set("table", t->kind);
        Value pv = Value::array();
        Value times = Value::array();
        for (int p : periods)
        {
            pv.push(p);
            times.push(r.times[static_cast<std::size_t>(p)]);
        }
        out.set("periods", pv);
        out.set("times", times);
        Value ids = Value::array();
        for (int e : elements)
            ids.push(t->ids[static_cast<std::size_t>(e)]);
        out.set("elements", ids);
        Value variables = Value::array();
        for (int v : vars)
        {
            Value vv = Value::object();
            vv.set("name", t->variables[static_cast<std::size_t>(v)].name);
            vv.set("unit", t->variables[static_cast<std::size_t>(v)].unit);
            Value rows = Value::array();
            for (int p : periods)
            {
                Value row = Value::array();
                for (int e : elements)
                    row.push(static_cast<double>(t->value(p, v, e)));
                rows.push(row);
            }
            vv.set("values", rows);
            variables.push(vv);
        }
        out.set("variables", variables);
        std::cout << out.dump(a.flag("compact") ? -1 : 2) << "\n";
        return 0;
    }
    std::ofstream file;
    if (a.has("out"))
    {
        file.open(pathArg(a.opt("out")), std::ios::binary | std::ios::trunc);
        if (!file)
            throw std::runtime_error("cannot write " + a.opt("out"));
    }
    std::ostream& os = a.has("out") ? static_cast<std::ostream&>(file) : std::cout;
    const int var = vars.front();
    os << "time_s";
    for (int e : elements)
        os << "," << t->ids[static_cast<std::size_t>(e)];
    os << "\n";
    for (int p : periods)
    {
        os << text::formatDouble(r.times[static_cast<std::size_t>(p)]);
        for (int e : elements)
            os << "," << text::formatDouble(t->value(p, var, e), 7);
        os << "\n";
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
    if (a.flag("json"))
    {
        Value v = Value::object();
        v.set("format", format);
        v.set("path", out.u8string());
        Value list = Value::array();
        for (const auto& n : notes)
            list.push(n);
        v.set("notes", list);
        std::cout << v.dump(2) << "\n";
        return 0;
    }
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
    if (a.flag("json"))
    {
        Value v = Value::object();
        v.set("path", a.opt("out"));
        v.set("nodes", model.nodes().size());
        v.set("links", model.links().size());
        Value list = Value::array();
        for (const auto& n : notes)
            list.push(n);
        v.set("notes", list);
        std::cout << v.dump(2) << "\n";
        return 0;
    }
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
    int written = -1;
    if (a.has("out"))
    {
        written = core::applyElevations(model, plan, o.decimals);
        model.save(pathArg(a.opt("out")));
    }
    if (a.flag("json"))
    {
        Value out = Value::object();
        const auto& info = dem->info();
        Value dv = Value::object();
        dv.set("path", a.opt("dem"));
        dv.set("format", info.format);
        dv.set("width", info.width);
        dv.set("height", info.height);
        dv.set("crs", info.crsDescription);
        out.set("dem", dv);
        Value cv = Value::object();
        for (const auto& [status, n] : counts)
            cv.set(core::statusName(status), n);
        out.set("counts", cv);
        if (written >= 0)
        {
            out.set("written", a.opt("out"));
            out.set("changed", written);
        }
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
    if (written >= 0)
    {
        std::cerr << "Wrote " << a.opt("out") << " (" << written << " values changed)\n";
    }
    else if (!a.flag("json"))
    {
        std::cout << "Preview only: add --out <model.inp> to write the changes.\n";
    }
    return 0;
}

std::string readTextFile(const std::string& name)
{
    std::ostringstream text;
    if (name == "-")
    {
        text << std::cin.rdbuf();
        return text.str();
    }
    std::ifstream f(pathArg(name), std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot read " + name);
    text << f.rdbuf();
    return text.str();
}

int cmdModel(const Args& a)
{
    if (a.positional.size() < 2 || (a.positional[0] != "get" && a.positional[0] != "set"))
        throw std::runtime_error("usage: mirab-cli model get|set <model.inp> ... "
                                 "(see mirab-cli help)");
    const std::string action = a.positional[0];
    Args rest = a;
    rest.positional.erase(rest.positional.begin());
    auto model = loadModel(rest);
    if (action == "get")
    {
        Value v = core::modelToJson(model);
        v.set("path", rest.positional[0]);
        if (!scenarioArg(a).empty())
            v.set("scenario", scenarioArg(a));
        const std::string text = v.dump(a.flag("compact") ? -1 : 2);
        if (a.has("out"))
        {
            std::ofstream f(pathArg(a.opt("out")), std::ios::binary | std::ios::trunc);
            if (!f || !(f << text << "\n"))
                throw std::runtime_error("cannot write " + a.opt("out"));
        }
        else
        {
            std::cout << text << "\n";
        }
        return 0;
    }

    Value edits = Value::array();
    if (a.has("edits"))
    {
        Value file;
        try
        {
            file = Value::parse(readTextFile(a.opt("edits")));
        }
        catch (const json::ParseError& e)
        {
            throw std::runtime_error(a.opt("edits") + ": " + e.what());
        }
        const Value* list = file.isObject() ? file.find("edits") : &file;
        if (!list || !list->isArray())
            throw std::runtime_error(a.opt("edits") +
                                     " must hold a JSON array or an object with \"edits\"");
        for (const auto& e : list->items())
            edits.push(e);
    }
    if (a.has("option") || a.has("id"))
    {
        if (!a.has("value"))
            throw std::runtime_error("--value is required");
        Value e = Value::object();
        if (a.has("option"))
            e.set("option", a.opt("option"));
        else
        {
            e.set("id", a.opt("id"));
            const std::string column = a.opt("column");
            if (column.empty())
                throw std::runtime_error("--column is required with --id");
            if (auto n = text::toInt(column))
                e.set("column", static_cast<double>(*n));
            else
                e.set("column", column);
        }
        if (a.has("section"))
            e.set("section", a.opt("section"));
        if (a.has("row"))
            e.set("row", a.num("row", 0));
        if (a.has("category"))
            e.set("category", a.opt("category"));
        e.set("value", a.opt("value"));
        edits.push(e);
    }
    if (edits.size() == 0)
        throw std::runtime_error(
            "nothing to change: use --id/--column/--value, --option/--value or --edits FILE");
    const auto summary = core::applyEdits(model, edits);
    const std::string scenario = scenarioArg(a);
    const fs::path inp = pathArg(rest.positional[0]);
    // With --scenario the edits become changes of that scenario in the
    // sidecar (the input file is unchanged), unless --out writes its model.
    const bool intoScenario = !scenario.empty() && !a.has("out");
    const fs::path out = intoScenario ? core::ProjectSettings::sidecarPath(inp) : pathArg(a.opt("out", rest.positional[0]));
    const bool write = summary.changed > 0 || a.has("out");
    if (write && intoScenario)
    {
        auto settings = core::ProjectSettings::loadFor(inp);
        core::Scenario* s = core::findScenario(settings.scenarios, scenario);
        const core::Model base = core::Model::fromFile(inp);
        const auto parent = core::scenarioDocument(base.document(), base.dialect(), settings.scenarios, s->parent);
        s->changes = core::diffDocuments(parent, model.document(), model.dialect());
        settings.saveFor(inp);
    }
    else if (write)
    {
        model.save(out);
    }
    if (a.flag("json"))
    {
        Value v = Value::object();
        v.set("path", out.u8string());
        if (!scenario.empty())
            v.set("scenario", scenario);
        v.set("applied", summary.applied);
        v.set("changed", summary.changed);
        v.set("written", write);
        Value issues = Value::array();
        for (const auto& issue : model.validate())
            issues.push(issueJson(issue));
        v.set("issues", issues);
        std::cout << v.dump(2) << "\n";
    }
    else
    {
        std::cout << summary.applied << " edit(s), " << summary.changed << " changed"
                  << (write ? "; wrote " + out.u8string() : "; the file is unchanged") << "\n";
    }
    return 0;
}

void usage()
{
    std::cout <<
        R"(Mirab command-line interface

Usage:
  mirab-cli version [--json]
  mirab-cli engines [--json]
  mirab-cli engines install <manifest.json> [--target DIR] [--json]
  mirab-cli engines uninstall <engine> [--target DIR] [--json]
  mirab-cli engines verify <engine> [--json]
  mirab-cli engines where [--json]
  mirab-cli info <model.inp> [--json]
  mirab-cli check <model.inp> [--engine ID] [--msx file.msx] [--json]
  mirab-cli model get <model.inp> [--scenario NAME] [--out FILE.json] [--compact]
  mirab-cli model set <model.inp> [--scenario NAME] [--id ID --column NAME --value V [--section S]
                  [--row N] [--category node|link|subcatchment|gage|KIND]]
                  [--option KEY --value V [--section S]] [--edits FILE.json|-]
                  [--out model.inp] [--json]
  mirab-cli run <model.inp> [--scenario NAME] [--engine ID] [--msx file.msx] [--out results.anr]
                  [--report file.rpt] [--workdir DIR] [--json | --machine [--live]
                  [--frame-interval SECONDS]]
  mirab-cli scenarios list <model.inp> [--json]
  mirab-cli scenarios export <model.inp> --scenario NAME --out FILE [--json]
  mirab-cli batch <model.inp> [--scenarios NAME,NAME|--all] [--jobs N] [--engine ID] [--json]
  mirab-cli results <results.anr|.out> [--json] [--table node|link|subcatchment|system]
                  [--variable NAME[,NAME...]] [--element ID[,ID...]] [--period N]
                  [--format csv|json|npy|anr] [--out FILE] [--compact]
  mirab-cli resilience todini <model.inp> --results FILE [--required-pressure P] [--json]
  mirab-cli resilience service --results FILE [--json]
  mirab-cli resilience topology <model.inp> [--json]
  mirab-cli resilience criticality <model.inp> [--engine ID] [--threshold P] [--dda]
                  [--links ID,ID] [--json]
  mirab-cli resilience fireflow <model.inp> --flow Q [--residual P] [--time HOURS]
                  [--nodes ID,ID] [--max-flow Q] [--no-available] [--json]
  mirab-cli export geojson|gpkg|wntr <model.inp> --out FILE [--crs EPSG:n]
                  [--results FILE [--period N]] [--json]
  mirab-cli import wntr <network.json> --out model.inp [--json]
  mirab-cli elevations <model.inp> --dem FILE.tif|FILE.asc [--mode all|missing|selected]
                  [--nodes ID,ID] [--reservoirs] [--no-tanks] [--swmm invert|depth]
                  [--dem-units auto|m|ft|usft] [--crs EPSG:n] [--decimals N]
                  [--out model.inp] [--json]
)" << cli::calibrateUsage() << cli::linkUsage() << R"(

Engines: epanet-2.2-epa, epanet-2.3-owa, epanet-msx-2.0-epa22, epanet-msx-2.0-owa23,
         swmm-5.2-epa, swmm5plus-1.0 (when installed). Run "mirab-cli engines".
Pressures and flows are in the units of the model. Calibration settings live in the
project sidecar (model.inp.mirab.json) or a --config file; see docs/user-guide.md.
--scenario NAME works with info, check, model, run, calibrate, export and the
analyses: the scenario's changes (from the sidecar) are applied to the model
first. A scenario's results are written to model.<scenario>.anr and .rpt.
"batch" runs the base model ("Base") and the scenarios in separate processes,
--jobs at a time (default: up to 4).
With --machine, events are written as JSON lines and standard input accepts
"cancel", "pause" and "resume"; --live adds the results of each reporting
period while the run is in progress (at most one every --frame-interval
seconds, default 0.25). --json writes the result as one JSON document;
docs/cli.md describes every JSON layout.
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
            return cmdVersion(a);
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
        if (cmd == "scenarios")
            return cmdScenarios(a);
        if (cmd == "batch")
            return cmdBatch(a);
        if (cmd == "resilience")
            return cmdResilience(a);
        if (cmd == "export")
            return cmdExport(a);
        if (cmd == "import")
            return cmdImport(a);
        if (cmd == "elevations")
            return cmdElevations(a);
        if (cmd == "calibrate")
            return cli::runCalibrate(std::vector<std::string>(argsv.begin() + 2, argsv.end()));
        if (cmd == "model")
            return cmdModel(a);
        if (cmd == "link")
            return cli::runLink(std::vector<std::string>(argsv.begin() + 2, argsv.end()));
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
