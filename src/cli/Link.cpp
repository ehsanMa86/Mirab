/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/cli/Link.cpp
 * Purpose: The "link" command of mirab-cli: dry-weather flow of a sewer model from a water model.
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
#include "Link.h"

#include "mirab/core/Json.h"
#include "mirab/core/Linkage.h"
#include "mirab/core/Model.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Text.h"
#include "mirab/interop/WaterLink.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace mirab::cli
{

namespace
{

using json::Value;

struct LinkArgs
{
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::set<std::string> flags;
    bool has(const std::string& k) const { return options.count(k) > 0; }
    std::string opt(const std::string& k) const
    {
        const auto it = options.find(k);
        return it == options.end() ? std::string() : it->second;
    }
    double num(const std::string& k) const
    {
        const auto v = text::toDouble(opt(k));
        if (!v)
            throw std::runtime_error("--" + k + " expects a number");
        return *v;
    }
};

LinkArgs parse(const std::vector<std::string>& args)
{
    static const std::set<std::string> kFlags = {"json", "weekend", "no-storage", "storage"};
    LinkArgs a;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& s = args[i];
        if (s.rfind("--", 0) != 0)
        {
            a.positional.push_back(s);
            continue;
        }
        std::string key = s.substr(2);
        if (const auto eq = key.find('='); eq != std::string::npos)
        {
            a.options[key.substr(0, eq)] = key.substr(eq + 1);
            continue;
        }
        if (kFlags.count(key))
        {
            a.flags.insert(key);
            continue;
        }
        if (i + 1 >= args.size())
            throw std::runtime_error("--" + key + " needs a value");
        a.options[key] = args[++i];
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

/// "none" names the demands without a category.
std::string categoryName(const std::string& s)
{
    return text::iequals(s, "none") ? std::string() : s;
}

/// Stored path of a file given on the command line (relative to the working directory).
std::string storedArg(const fs::path& swmm, const std::string& arg)
{
    return arg.empty() ? arg : interop::storedLinkPath(swmm, fs::u8path(arg));
}

/// A path stored relative to @p from, stored again relative to @p to.
std::string rebase(const fs::path& from, const fs::path& to, const std::string& stored)
{
    if (stored.empty() || text::iequals(stored, "subcatchments"))
        return stored;
    return interop::storedLinkPath(to, interop::resolveLinkPath(from, stored));
}

std::string fmt(double v)
{
    return text::formatDouble(v, 6);
}

const char* const kUsage = "usage: mirab-cli link dwf <swmm.inp> --water <epanet.inp> [options]";

int cmdDwf(const LinkArgs& a)
{
    if (a.positional.size() < 2)
        throw std::runtime_error(kUsage);
    const fs::path inp = fs::u8path(a.positional[1]);
    core::Model swmm = core::Model::fromFile(inp);
    if (swmm.dialect() != core::Dialect::Swmm)
        throw std::runtime_error(a.positional[1] + " is not a SWMM model");
    const auto project = core::ProjectSettings::loadFor(inp);
    core::WaterLinkSettings s = core::WaterLinkSettings::fromJson(project.waterLink);

    if (a.has("water"))
        s.waterModel = storedArg(inp, a.opt("water"));
    if (a.has("distance"))
        s.maxDistance = a.num("distance");
    if (a.has("return"))
    {
        s.categoryFactors.clear();
        for (const auto& item : splitList(a.opt("return")))
        {
            if (item.empty())
                continue;
            const auto eq = item.find('=');
            const auto value = text::toDouble(eq == std::string::npos ? item : item.substr(eq + 1));
            if (!value || *value < 0)
                throw std::runtime_error("--return expects a factor or CATEGORY=factor values");
            if (eq == std::string::npos)
                s.returnFactor = *value;
            else
                s.categoryFactors[categoryName(text::trim(item.substr(0, eq)))] = *value;
        }
    }
    if (a.has("categories"))
    {
        s.categories.clear();
        for (const auto& c : splitList(a.opt("categories")))
        {
            if (!c.empty())
                s.categories.push_back(categoryName(c));
        }
    }
    if (a.has("use-results"))
        s.results = storedArg(inp, a.opt("use-results"));
    if (a.has("tag"))
        s.nodeTag = a.opt("tag");
    if (a.has("areas"))
        s.serviceAreas = text::iequals(a.opt("areas"), "subcatchments")
                             ? std::string("subcatchments")
                             : storedArg(inp, a.opt("areas"));
    if (a.has("lag"))
        s.lagHours = a.num("lag");
    if (a.has("prefix"))
        s.patternPrefix = a.opt("prefix");
    if (a.flags.count("weekend"))
        s.weekend = true;
    if (a.has("week-start"))
    {
        Value v = s.toJson();
        v.set("weekStart", a.opt("week-start"));
        s.weekStart = core::WaterLinkSettings::fromJson(v).weekStart;
    }
    if (a.flags.count("no-storage"))
        s.storage = false;
    if (a.has("override"))
    {
        for (const auto& item : splitList(a.opt("override")))
        {
            const auto eq = item.find('=');
            if (item.empty())
                continue;
            if (eq == std::string::npos)
                throw std::runtime_error("--override expects WATER_NODE=SWMM_NODE values");
            s.overrides[text::trim(item.substr(0, eq))] = text::trim(item.substr(eq + 1));
        }
    }
    if (s.waterModel.empty())
        throw std::runtime_error("choose the water model with --water <epanet.inp>");

    const std::string swmmCrs = a.has("crs") ? a.opt("crs") : project.crs;
    auto linked = interop::loadLinkedModels(inp, swmm, swmmCrs, s);
    if (a.has("water-crs"))
        linked->context.waterCrs = a.opt("water-crs");
    core::DwfPlan plan = core::planDwf(linked->water, swmm, s, linked->context);
    plan.notes.insert(plan.notes.begin(), linked->notes.begin(), linked->notes.end());

    int changed = -1;
    if (a.has("out"))
    {
        const fs::path out = fs::u8path(a.opt("out"));
        changed = core::applyDwf(swmm, plan, s.patternPrefix);
        swmm.save(out);
        s.links.clear();
        for (const auto& as : plan.assignments)
        {
            if (!as.swmmNode.empty())
                s.links[as.waterNode] = as.swmmNode;
        }
        s.waterModel = rebase(inp, out, s.waterModel);
        s.results = rebase(inp, out, s.results);
        s.serviceAreas = rebase(inp, out, s.serviceAreas);
        const bool own = fs::exists(core::ProjectSettings::sidecarPath(out));
        auto settings = core::ProjectSettings::loadFor(own ? out : inp);
        if (settings.crs.empty())
            settings.crs = swmmCrs;
        settings.waterLink = s.toJson();
        settings.saveFor(out);
    }

    const std::string units = plan.flowUnits;
    if (a.flags.count("json"))
    {
        Value v = Value::object();
        v.set("model", a.positional[1]);
        v.set("water", linked->waterPath.u8string());
        v.set("flowUnits", units);
        v.set("source", s.results.empty() ? "demands" : "results");
        v.set("days", plan.days);
        Value totals = Value::object();
        totals.set("waterDemand", plan.totalDemand);
        totals.set("includedDemand", plan.includedDemand);
        totals.set("returned", plan.returned);
        totals.set("unassignedFlow", plan.unassignedFlow);
        totals.set("demandNodes", static_cast<int>(plan.assignments.size()));
        totals.set("unassigned", plan.unassigned);
        totals.set("tooFar", plan.tooFar);
        totals.set("swmmNodes", static_cast<int>(plan.inflows.size()));
        v.set("totals", totals);
        Value inflows = Value::array();
        for (const auto& in : plan.inflows)
        {
            Value r = Value::object();
            r.set("node", in.node);
            r.set("average", in.average);
            if (!in.hourly.empty())
                r.set("hourly", in.hourly);
            if (!in.weekend.empty())
                r.set("weekend", in.weekend);
            if (!in.daily.empty())
                r.set("daily", in.daily);
            Value src = Value::array();
            for (const auto& id : in.sources)
                src.push(id);
            r.set("sources", src);
            inflows.push(r);
        }
        v.set("inflows", inflows);
        Value patterns = Value::array();
        for (const auto& p : plan.patterns)
        {
            Value r = Value::object();
            r.set("name", p.name);
            r.set("type", p.type);
            Value f = Value::array();
            for (double x : p.factors)
                f.push(x);
            r.set("factors", f);
            patterns.push(r);
        }
        v.set("patterns", patterns);
        Value nodes = Value::array();
        for (const auto& as : plan.assignments)
        {
            Value r = Value::object();
            r.set("water", as.waterNode);
            r.set("swmm", as.swmmNode.empty() ? Value() : Value(as.swmmNode));
            r.set("status", core::statusName(as.status));
            if (as.distance >= 0)
                r.set("distance", as.distance);
            if (!as.nearest.empty())
                r.set("nearest", as.nearest);
            if (!as.area.empty())
                r.set("area", as.area);
            r.set("demand", as.demand);
            r.set("returned", as.returned);
            nodes.push(r);
        }
        v.set("assignments", nodes);
        Value notes = Value::array();
        for (const auto& n : plan.notes)
            notes.push(n);
        v.set("notes", notes);
        if (changed >= 0)
        {
            v.set("written", a.opt("out"));
            v.set("changed", changed);
        }
        std::cout << v.dump(2) << "\n";
        return 0;
    }

    std::cout << "Water model: " << linked->waterPath.u8string() << " ("
              << linked->water.units().flow << "), sewer flow units " << units << "\n";
    for (const auto& n : plan.notes)
        std::cout << "note: " << n << "\n";
    std::cout << "\n  Node             Average      Hourly pattern   From\n";
    for (const auto& in : plan.inflows)
    {
        std::string line = "  " + in.node;
        line.resize(std::max<std::size_t>(line.size() + 1, 19), ' ');
        std::string avg = fmt(in.average);
        avg.resize(std::max<std::size_t>(avg.size() + 1, 13), ' ');
        std::string pat = in.hourly.empty() ? "-" : in.hourly;
        pat.resize(std::max<std::size_t>(pat.size() + 1, 17), ' ');
        std::cout << line << avg << pat << text::join(in.sources, ", ") << "\n";
    }
    std::cout << "\nWater demand " << fmt(plan.totalDemand) << " " << units
              << " (returning categories "
              << fmt(plan.includedDemand) << "); dry-weather inflow " << fmt(plan.returned) << " "
              << units << " at " << plan.inflows.size() << " SWMM node(s)\n";
    std::cout << plan.assignments.size() << " demand node(s), " << plan.unassigned
              << " not assigned";
    if (plan.unassigned > 0)
        std::cout << " (" << fmt(plan.unassignedFlow) << " " << units << " not returned)";
    if (plan.tooFar > 0)
        std::cout << ", " << plan.tooFar << " farther than " << fmt(s.maxDistance);
    std::cout << "\n";
    for (const auto& as : plan.assignments)
    {
        if (!as.swmmNode.empty())
            continue;
        std::cout << "  " << as.waterNode << ": " << core::statusName(as.status);
        if (!as.nearest.empty())
            std::cout << " (nearest " << as.nearest << " at " << fmt(as.distance) << ")";
        std::cout << "\n";
    }
    if (changed >= 0)
        std::cerr << "Wrote " << a.opt("out") << " (" << changed << " rows changed)\n";
    else
        std::cout << "Preview only: add --out <model.inp> to write [DWF] and [PATTERNS].\n";
    return 0;
}

} // namespace

int runLink(const std::vector<std::string>& args)
{
    const LinkArgs a = parse(args);
    if (a.positional.empty() || a.positional[0] != "dwf")
        throw std::runtime_error(kUsage);
    return cmdDwf(a);
}

const char* linkUsage()
{
    return R"(  mirab-cli link dwf <swmm.inp> [--water epanet.inp] [--distance D] [--return F[,CAT=F...]]
                  [--categories CAT,CAT] [--use-results epanet.anr] [--tag TAG]
                  [--areas subcatchments|FILE.shp] [--override WNODE=SNODE,...] [--lag HOURS]
                  [--weekend [--week-start DAY]] [--prefix WW_] [--no-storage] [--crs EPSG:n]
                  [--water-crs EPSG:n] [--out model.inp] [--json])";
}

} // namespace mirab::cli
