/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/interop/src/Wntr.cpp
 * Purpose: Conversion between EPANET models and the WNTR JSON network format (SI units).
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
#include "mirab/interop/Interop.h"

#include "mirab/core/Text.h"
#include "mirab/core/Units.h"

#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mirab::interop
{

using json::Value;

namespace
{
double num(const std::string& s, double fallback = 0.0)
{
    return text::toDouble(s).value_or(fallback);
}

std::string tok(const std::vector<std::string>& t, std::size_t i)
{
    return i < t.size() ? text::unquote(t[i]) : std::string();
}

Value nullable(const std::string& s)
{
    return (s.empty() || s == "*") ? Value() : Value(s);
}

double timeOption(const core::InpDocument& doc, const char* key, double fallback)
{
    auto v = doc.keyValue("TIMES", key);
    if (!v)
        return fallback;
    const auto t = text::splitInpLine(*v).tokens;
    if (t.empty())
        return fallback;
    return text::parseClockOrDuration(t[0], t.size() > 1 ? t[1] : std::string()).value_or(fallback);
}

std::string clock(double seconds)
{
    const long long s = static_cast<long long>(std::llround(seconds));
    std::ostringstream o;
    o << s / 3600 << ':' << (s % 3600) / 600 << ((s % 3600) / 60) % 10;
    if (s % 60)
        o << ':' << (s % 60) / 10 << s % 10;
    return o.str();
}

std::string fmt(double v)
{
    return text::formatDouble(v, 8);
}
} // namespace

Value toWntrJson(const core::Model& model, std::vector<std::string>* notes)
{
    if (model.dialect() != core::Dialect::Epanet)
        throw std::runtime_error("WNTR exchange is available for EPANET models only");
    auto note = [&](const std::string& n) {
        if (notes)
            notes->push_back(n);
    };
    const auto& doc = model.document();
    const auto u = model.units();
    const double qf = u.toSI("flow");
    const double lf = u.toSI("length");
    const double df = u.toSI("diameter");
    const double pf = u.toSI("pressure"); // pressure -> metres of head
    const double vf = u.toSI("volume");
    const std::string headloss = text::toUpper(text::splitInpLine(model.option("HEADLOSS") + " H-W").tokens[0]);
    const double roughFactor = headloss == "D-W" ? (u.system == core::UnitSystem::SI ? 0.001 : 0.0003048) : 1.0;

    auto rows = [&](const char* section) {
        std::vector<std::vector<std::string>> out;
        if (const auto* s = doc.find(section))
        {
            for (int r = 0; r < s->rowCount(); ++r)
                out.push_back(s->rowTokens(r));
        }
        return out;
    };

    Value root = Value::object();
    root.set("version", "wntr-1.5.0");
    root.set("comment", "WaterNetworkModel - all values given in SI units");
    root.set("name", model.title());
    root.set("references", Value::array());

    // Options
    Value options = Value::object();
    Value time = Value::object();
    time.set("duration", model.durationSeconds());
    time.set("hydraulic_timestep", timeOption(doc, "HYDRAULIC TIMESTEP", 3600));
    time.set("quality_timestep", timeOption(doc, "QUALITY TIMESTEP", 300));
    time.set("pattern_timestep", timeOption(doc, "PATTERN TIMESTEP", 3600));
    time.set("pattern_start", timeOption(doc, "PATTERN START", 0));
    time.set("report_timestep", timeOption(doc, "REPORT TIMESTEP", 3600));
    time.set("report_start", timeOption(doc, "REPORT START", 0));
    time.set("start_clocktime", timeOption(doc, "START CLOCKTIME", 0));
    options.set("time", time);
    Value hyd = Value::object();
    hyd.set("headloss", headloss);
    hyd.set("demand_model", model.demandModel() == "PDA" ? "PDA" : "DD");
    auto optNum = [&](const char* key, double fallback) {
        const auto t = text::splitInpLine(model.option(key)).tokens;
        return t.empty() ? fallback : num(t[0], fallback);
    };
    hyd.set("minimum_pressure", optNum("MINIMUM PRESSURE", 0.0) * pf);
    hyd.set("required_pressure", optNum("REQUIRED PRESSURE", 0.1) * pf);
    hyd.set("pressure_exponent", optNum("PRESSURE EXPONENT", 0.5));
    hyd.set("emitter_exponent", optNum("EMITTER EXPONENT", 0.5));
    hyd.set("demand_multiplier", optNum("DEMAND MULTIPLIER", 1.0));
    hyd.set("trials", optNum("TRIALS", 200));
    hyd.set("accuracy", optNum("ACCURACY", 0.001));
    hyd.set("inpfile_units", u.flow);
    const auto defaultPattern = text::splitInpLine(model.option("PATTERN")).tokens;
    hyd.set("pattern", defaultPattern.empty() ? Value("1") : Value(defaultPattern[0]));
    options.set("hydraulic", hyd);
    root.set("options", options);

    // Patterns
    std::map<std::string, std::vector<double>> patterns;
    std::vector<std::string> patternOrder;
    for (const auto& t : rows("PATTERNS"))
    {
        if (t.empty())
            continue;
        if (!patterns.count(t[0]))
            patternOrder.push_back(t[0]);
        auto& p = patterns[t[0]];
        for (std::size_t i = 1; i < t.size(); ++i)
            p.push_back(num(t[i]));
    }
    Value pats = Value::array();
    for (const auto& id : patternOrder)
    {
        Value p = Value::object();
        p.set("name", id);
        Value m = Value::array();
        for (double v : patterns[id])
            m.push(v);
        p.set("multipliers", m);
        pats.push(p);
    }
    root.set("patterns", pats);

    // Curve usage determines units.
    std::map<std::string, std::string> curveType;
    for (const auto& t : rows("PUMPS"))
    {
        for (std::size_t i = 3; i + 1 < t.size(); ++i)
        {
            if (text::iequals(t[i], "HEAD"))
                curveType[t[i + 1]] = "HEAD";
        }
    }
    for (const auto& t : rows("TANKS"))
    {
        if (t.size() > 7 && t[7] != "*")
            curveType[t[7]] = "VOLUME";
    }
    for (const auto& t : rows("VALVES"))
    {
        if (t.size() > 5 && text::iequals(t[4], "GPV"))
            curveType[t[5]] = "HEADLOSS";
    }
    for (const auto& t : rows("ENERGY"))
    {
        if (t.size() >= 4 && text::iequals(t[0], "PUMP") && text::istartsWith(t[2], "EFFIC"))
            curveType[t[3]] = "EFFICIENCY";
    }
    std::map<std::string, std::vector<std::pair<double, double>>> curves;
    std::vector<std::string> curveOrder;
    for (const auto& t : rows("CURVES"))
    {
        if (t.size() < 3)
            continue;
        if (!curves.count(t[0]))
            curveOrder.push_back(t[0]);
        curves[t[0]].emplace_back(num(t[1]), num(t[2]));
    }
    Value cs = Value::array();
    for (const auto& id : curveOrder)
    {
        auto it = curveType.find(id);
        if (it == curveType.end())
        {
            note("curve " + id + " is not used by a pump, tank or valve and was not exported");
            continue;
        }
        double fx = 1.0, fy = 1.0;
        if (it->second == "HEAD" || it->second == "HEADLOSS")
        {
            fx = qf;
            fy = lf;
        }
        else if (it->second == "VOLUME")
        {
            fx = lf;
            fy = vf;
        }
        else if (it->second == "EFFICIENCY")
        {
            fx = qf;
        }
        Value c = Value::object();
        c.set("name", id);
        c.set("curve_type", it->second);
        Value pts = Value::array();
        for (const auto& [x, y] : curves[id])
        {
            Value p = Value::array();
            p.push(x * fx);
            p.push(y * fy);
            pts.push(p);
        }
        c.set("points", pts);
        cs.push(c);
    }
    root.set("curves", cs);

    // Nodes
    std::map<std::string, std::vector<std::vector<std::string>>> demands;
    for (const auto& t : rows("DEMANDS"))
    {
        if (!t.empty())
            demands[t[0]].push_back(t);
    }
    std::map<std::string, double> emitters;
    for (const auto& t : rows("EMITTERS"))
    {
        if (t.size() >= 2)
            emitters[t[0]] = num(t[1]);
    }
    std::map<std::string, double> initQual;
    for (const auto& t : rows("QUALITY"))
    {
        if (t.size() >= 2)
            initQual[t[0]] = num(t[1]);
    }
    const double emitterExp = optNum("EMITTER EXPONENT", 0.5);
    auto coords = [&](const std::string& id) {
        Value c = Value::array();
        if (const auto* n = model.findNode(id); n && n->placed)
        {
            c.push(n->pos.x);
            c.push(n->pos.y);
        }
        return c;
    };
    Value nodes = Value::array();
    for (const auto& t : rows("JUNCTIONS"))
    {
        if (t.empty())
            continue;
        Value n = Value::object();
        n.set("name", t[0]);
        n.set("node_type", "Junction");
        n.set("elevation", num(tok(t, 1)) * lf);
        n.set("coordinates", coords(t[0]));
        Value dl = Value::array();
        auto addDemand = [&](double base, const std::string& pat, const std::string& cat) {
            Value d = Value::object();
            d.set("base_val", base * qf);
            d.set("pattern_name", nullable(pat));
            d.set("category", nullable(cat));
            dl.push(d);
        };
        auto it = demands.find(t[0]);
        if (it != demands.end())
        {
            for (const auto& d : it->second)
                addDemand(num(tok(d, 1)), tok(d, 2), tok(d, 3));
        }
        else
        {
            addDemand(num(tok(t, 2)), tok(t, 3), std::string());
        }
        n.set("demand_timeseries_list", dl);
        if (auto e = emitters.find(t[0]); e != emitters.end())
            n.set("emitter_coefficient", e->second * qf / std::pow(pf, emitterExp));
        else
            n.set("emitter_coefficient", Value());
        n.set("initial_quality", initQual.count(t[0]) ? initQual[t[0]] : 0.0);
        nodes.push(n);
    }
    for (const auto& t : rows("TANKS"))
    {
        if (t.empty())
            continue;
        Value n = Value::object();
        n.set("name", t[0]);
        n.set("node_type", "Tank");
        n.set("elevation", num(tok(t, 1)) * lf);
        n.set("init_level", num(tok(t, 2)) * lf);
        n.set("min_level", num(tok(t, 3)) * lf);
        n.set("max_level", num(tok(t, 4)) * lf);
        n.set("diameter", num(tok(t, 5)) * lf);
        n.set("min_vol", num(tok(t, 6)) * vf);
        n.set("vol_curve_name", nullable(tok(t, 7)));
        n.set("overflow", text::iequals(tok(t, 8), "YES"));
        n.set("coordinates", coords(t[0]));
        n.set("initial_quality", initQual.count(t[0]) ? initQual[t[0]] : 0.0);
        nodes.push(n);
    }
    for (const auto& t : rows("RESERVOIRS"))
    {
        if (t.empty())
            continue;
        Value n = Value::object();
        n.set("name", t[0]);
        n.set("node_type", "Reservoir");
        n.set("base_head", num(tok(t, 1)) * lf);
        n.set("head_pattern_name", nullable(tok(t, 2)));
        n.set("coordinates", coords(t[0]));
        n.set("initial_quality", initQual.count(t[0]) ? initQual[t[0]] : 0.0);
        nodes.push(n);
    }
    root.set("nodes", nodes);

    // Links
    std::map<std::string, std::string> status;
    for (const auto& t : rows("STATUS"))
    {
        if (t.size() >= 2)
            status[t[0]] = text::toUpper(t[1]);
    }
    auto vertices = [&](const std::string& id) {
        Value v = Value::array();
        if (const auto* l = model.findLink(id))
        {
            for (const auto& p : l->vertices)
            {
                Value a = Value::array();
                a.push(p.x);
                a.push(p.y);
                v.push(a);
            }
        }
        return v;
    };
    Value links = Value::array();
    for (const auto& t : rows("PIPES"))
    {
        if (t.size() < 6)
            continue;
        Value l = Value::object();
        l.set("name", t[0]);
        l.set("link_type", "Pipe");
        l.set("start_node_name", t[1]);
        l.set("end_node_name", t[2]);
        l.set("length", num(t[3]) * lf);
        l.set("diameter", num(t[4]) * df);
        l.set("roughness", num(t[5]) * roughFactor);
        l.set("minor_loss", num(tok(t, 6)));
        std::string st = text::toUpper(tok(t, 7));
        if (status.count(t[0]))
            st = status[t[0]];
        l.set("check_valve", st == "CV");
        l.set("initial_status", st == "CLOSED" ? "CLOSED" : "OPEN");
        l.set("vertices", vertices(t[0]));
        links.push(l);
    }
    for (const auto& t : rows("PUMPS"))
    {
        if (t.size() < 3)
            continue;
        Value l = Value::object();
        l.set("name", t[0]);
        l.set("link_type", "Pump");
        l.set("start_node_name", t[1]);
        l.set("end_node_name", t[2]);
        std::string type = "POWER";
        double speed = 1.0;
        std::string curve, pattern;
        double power = 0.0;
        for (std::size_t i = 3; i + 1 < t.size(); i += 2)
        {
            const std::string k = text::toUpper(t[i]);
            if (k == "HEAD")
            {
                type = "HEAD";
                curve = t[i + 1];
            }
            else if (k == "POWER")
                power = num(t[i + 1]) * (u.system == core::UnitSystem::SI ? 1000.0 : 745.699872);
            else if (k == "SPEED")
                speed = num(t[i + 1], 1.0);
            else if (k == "PATTERN")
                pattern = t[i + 1];
        }
        l.set("pump_type", type);
        if (type == "HEAD")
            l.set("pump_curve_name", curve);
        else
            l.set("power", power);
        l.set("base_speed", speed);
        l.set("speed_pattern_name", nullable(pattern));
        l.set("initial_status", status.count(t[0]) && status[t[0]] == "CLOSED" ? "CLOSED" : "OPEN");
        l.set("vertices", vertices(t[0]));
        links.push(l);
    }
    for (const auto& t : rows("VALVES"))
    {
        if (t.size() < 6)
            continue;
        const std::string type = text::toUpper(t[4]);
        Value l = Value::object();
        l.set("name", t[0]);
        l.set("link_type", "Valve");
        l.set("start_node_name", t[1]);
        l.set("end_node_name", t[2]);
        l.set("diameter", num(t[3]) * df);
        l.set("valve_type", type);
        l.set("minor_loss", num(tok(t, 6)));
        if (type == "GPV")
        {
            l.set("headloss_curve_name", t[5]);
            l.set("initial_setting", 0.0);
        }
        else if (type == "FCV")
            l.set("initial_setting", num(t[5]) * qf);
        else if (type == "TCV" || type == "PCV")
            l.set("initial_setting", num(t[5]));
        else
            l.set("initial_setting", num(t[5]) * pf);
        if (type == "PCV")
            note("valve " + t[0] + " is a PCV (EPANET 2.3), which WNTR may not support");
        // A valve is active unless [STATUS] opens or closes it (a setting there keeps it active).
        const std::string vs = status.count(t[0]) ? status[t[0]] : std::string();
        l.set("initial_status", vs == "CLOSED" ? "CLOSED" : vs == "OPEN" ? "OPEN" : "ACTIVE");
        l.set("vertices", vertices(t[0]));
        links.push(l);
    }
    root.set("links", links);
    root.set("sources", Value::array());
    root.set("controls", Value::array());
    if (doc.find("CONTROLS") && doc.find("CONTROLS")->rowCount() > 0)
        note("simple controls were not exported");
    if (doc.find("RULES") && doc.find("RULES")->rowCount() > 0)
        note("rule-based controls were not exported");
    if (doc.find("SOURCES") && doc.find("SOURCES")->rowCount() > 0)
        note("water quality sources were not exported");
    if (doc.find("LEAKAGE") && doc.find("LEAKAGE")->rowCount() > 0)
        note("FAVAD pipe leakage (EPANET 2.3) has no WNTR equivalent and was not exported");
    return root;
}

void writeWntrJson(const core::Model& model, const std::filesystem::path& path,
                   std::vector<std::string>* notes)
{
    const Value v = toWntrJson(model, notes);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + path.u8string());
    out << v.dump(2) << "\n";
}

core::Model fromWntrJson(const Value& w, std::vector<std::string>* notes)
{
    auto note = [&](const std::string& n) {
        if (notes)
            notes->push_back(n);
    };
    if (!w.isObject() || !w.contains("nodes"))
        throw std::runtime_error("the document is not a WNTR network (no \"nodes\")");
    const std::string version = w.get("version").asString();
    if (!text::istartsWith(version, "wntr"))
        note("unexpected version string \"" + version + "\"; reading anyway");

    core::Model model = core::Model::newProject(core::Dialect::Epanet, "LPS");
    core::InpDocument& doc = model.document();
    const auto& hyd = w.get("options").get("hydraulic");
    const auto& time = w.get("options").get("time");
    const std::string headloss = hyd.get("headloss").asString("H-W");
    doc.setKeyValue("OPTIONS", "HEADLOSS", headloss);
    if (hyd.get("demand_model").asString() == "PDA")
    {
        doc.setKeyValue("OPTIONS", "DEMAND MODEL", "PDA");
        doc.setKeyValue("OPTIONS", "MINIMUM PRESSURE", fmt(hyd.get("minimum_pressure").asNumber()));
        doc.setKeyValue("OPTIONS", "REQUIRED PRESSURE", fmt(hyd.get("required_pressure").asNumber(0.1)));
        doc.setKeyValue("OPTIONS", "PRESSURE EXPONENT", fmt(hyd.get("pressure_exponent").asNumber(0.5)));
    }
    if (hyd.contains("pattern") && !hyd.get("pattern").isNull())
        doc.setKeyValue("OPTIONS", "PATTERN", hyd.get("pattern").asString());
    auto hydOption = [&](const char* key, const char* json) {
        if (hyd.contains(json) && !hyd.get(json).isNull())
            doc.setKeyValue("OPTIONS", key, fmt(hyd.get(json).asNumber()));
    };
    hydOption("DEMAND MULTIPLIER", "demand_multiplier");
    hydOption("EMITTER EXPONENT", "emitter_exponent");
    hydOption("TRIALS", "trials");
    hydOption("ACCURACY", "accuracy");
    doc.setKeyValue("TIMES", "DURATION", clock(time.get("duration").asNumber(0)));
    doc.setKeyValue("TIMES", "HYDRAULIC TIMESTEP", clock(time.get("hydraulic_timestep").asNumber(3600)));
    doc.setKeyValue("TIMES", "PATTERN TIMESTEP", clock(time.get("pattern_timestep").asNumber(3600)));
    doc.setKeyValue("TIMES", "REPORT TIMESTEP", clock(time.get("report_timestep").asNumber(3600)));
    auto timeOptionIn = [&](const char* key, const char* json) {
        if (time.contains(json) && !time.get(json).isNull())
            doc.setKeyValue("TIMES", key, clock(time.get(json).asNumber()));
    };
    timeOptionIn("QUALITY TIMESTEP", "quality_timestep");
    timeOptionIn("PATTERN START", "pattern_start");
    timeOptionIn("REPORT START", "report_start");
    timeOptionIn("START CLOCKTIME", "start_clocktime");
    if (!w.get("name").asString().empty())
    {
        auto& title = doc.ensure("TITLE");
        title.lines = {w.get("name").asString(), ""};
    }

    const double q = 1000.0; // m3/s -> LPS
    const bool dw = text::iequals(headloss, "D-W");
    std::map<std::string, std::string> curveTypes;
    for (const auto& c : w.get("curves").items())
    {
        const std::string name = c.get("name").asString();
        const std::string type = c.get("curve_type").asString();
        curveTypes[name] = type;
        double fx = 1.0;
        if (type == "HEAD" || type == "HEADLOSS" || type == "EFFICIENCY")
            fx = q;
        auto& s = doc.ensure("CURVES");
        for (const auto& p : c.get("points").items())
            s.appendRow({name, fmt(p[0].asNumber() * fx), fmt(p[1].asNumber())});
    }
    for (const auto& p : w.get("patterns").items())
    {
        const std::string name = p.get("name").asString();
        std::vector<std::string> row = {name};
        auto& s = doc.ensure("PATTERNS");
        for (const auto& m : p.get("multipliers").items())
        {
            row.push_back(fmt(m.asNumber()));
            if (row.size() == 7)
            {
                s.appendRow(row);
                row = {name};
            }
        }
        if (row.size() > 1)
            s.appendRow(row);
    }
    auto place = [&](const Value& n) {
        const auto& c = n.get("coordinates");
        if (c.size() >= 2)
            doc.ensure("COORDINATES").appendRow({n.get("name").asString(), fmt(c[0].asNumber()), fmt(c[1].asNumber())});
    };
    auto patternOf = [](const Value& v) { return v.isNull() ? std::string() : v.asString(); };
    for (const auto& n : w.get("nodes").items())
    {
        const std::string name = n.get("name").asString();
        const std::string type = n.get("node_type").asString();
        if (type == "Junction")
        {
            const auto& dl = n.get("demand_timeseries_list");
            double base = 0.0;
            std::string pattern;
            if (dl.size() > 0)
            {
                base = dl[0].get("base_val").asNumber() * q;
                pattern = patternOf(dl[0].get("pattern_name"));
            }
            std::vector<std::string> row = {name, fmt(n.get("elevation").asNumber()), fmt(base)};
            if (!pattern.empty())
                row.push_back(pattern);
            doc.ensure("JUNCTIONS").appendRow(row);
            if (dl.size() > 1)
            {
                for (const auto& d : dl.items())
                {
                    // EPANET has no placeholder for a missing pattern: the row ends after the base demand.
                    std::vector<std::string> r = {name, fmt(d.get("base_val").asNumber() * q)};
                    const std::string pat = patternOf(d.get("pattern_name"));
                    if (!pat.empty())
                        r.push_back(pat);
                    if (!d.get("category").isNull())
                        r.push_back(";" + d.get("category").asString());
                    doc.ensure("DEMANDS").appendRow(r);
                }
            }
            if (!n.get("emitter_coefficient").isNull() && n.get("emitter_coefficient").asNumber() > 0)
                doc.ensure("EMITTERS").appendRow({name, fmt(n.get("emitter_coefficient").asNumber() * q)});
        }
        else if (type == "Tank")
        {
            std::vector<std::string> row = {name,
                                            fmt(n.get("elevation").asNumber()),
                                            fmt(n.get("init_level").asNumber()),
                                            fmt(n.get("min_level").asNumber()),
                                            fmt(n.get("max_level").asNumber()),
                                            fmt(n.get("diameter").asNumber()),
                                            fmt(n.get("min_vol").asNumber()),
                                            n.get("vol_curve_name").isNull() ? "*" : n.get("vol_curve_name").asString()};
            if (n.get("overflow").asBool())
                row.push_back("YES");
            doc.ensure("TANKS").appendRow(row);
        }
        else if (type == "Reservoir")
        {
            std::vector<std::string> row = {name, fmt(n.get("base_head").asNumber())};
            const std::string pat = patternOf(n.get("head_pattern_name"));
            if (!pat.empty())
                row.push_back(pat);
            doc.ensure("RESERVOIRS").appendRow(row);
        }
        else
        {
            note("skipped node " + name + " of unknown type " + type);
            continue;
        }
        place(n);
        if (n.get("initial_quality").asNumber() != 0.0)
            doc.ensure("QUALITY").appendRow({name, fmt(n.get("initial_quality").asNumber())});
    }
    for (const auto& l : w.get("links").items())
    {
        const std::string name = l.get("name").asString();
        const std::string type = l.get("link_type").asString();
        const std::string a = l.get("start_node_name").asString();
        const std::string b = l.get("end_node_name").asString();
        if (type == "Pipe")
        {
            const double rough = l.get("roughness").asNumber(100) * (dw ? 1000.0 : 1.0);
            // WNTR writes the status as "Closed"/"Open"; any case is accepted.
            const bool closed = text::iequals(l.get("initial_status").asString(), "CLOSED");
            std::string st = l.get("check_valve").asBool() ? "CV" : (closed ? "Closed" : "Open");
            doc.ensure("PIPES").appendRow({name, a, b, fmt(l.get("length").asNumber()),
                                           fmt(l.get("diameter").asNumber() * 1000.0), fmt(rough),
                                           fmt(l.get("minor_loss").asNumber()), st});
        }
        else if (type == "Pump")
        {
            std::vector<std::string> row = {name, a, b};
            if (l.get("pump_type").asString() == "HEAD")
            {
                row.push_back("HEAD");
                row.push_back(l.get("pump_curve_name").asString());
            }
            else
            {
                row.push_back("POWER");
                row.push_back(fmt(l.get("power").asNumber() / 1000.0));
            }
            if (l.get("base_speed").asNumber(1.0) != 1.0)
            {
                row.push_back("SPEED");
                row.push_back(fmt(l.get("base_speed").asNumber()));
            }
            const std::string pat = patternOf(l.get("speed_pattern_name"));
            if (!pat.empty())
            {
                row.push_back("PATTERN");
                row.push_back(pat);
            }
            doc.ensure("PUMPS").appendRow(row);
            if (text::iequals(l.get("initial_status").asString(), "CLOSED"))
                doc.ensure("STATUS").appendRow({name, "Closed"});
        }
        else if (type == "Valve")
        {
            const std::string vt = text::toUpper(l.get("valve_type").asString());
            std::string setting;
            if (vt == "GPV")
                setting = l.get("headloss_curve_name").asString();
            else if (vt == "FCV")
                setting = fmt(l.get("initial_setting").asNumber() * q);
            else
                setting = fmt(l.get("initial_setting").asNumber());
            doc.ensure("VALVES").appendRow({name, a, b, fmt(l.get("diameter").asNumber() * 1000.0), vt, setting,
                                            fmt(l.get("minor_loss").asNumber())});
            // Active is the default; an open or closed valve needs a [STATUS] row.
            const std::string vs = text::toUpper(l.get("initial_status").asString());
            if (vs == "CLOSED" || vs == "OPEN")
                doc.ensure("STATUS").appendRow({name, vs == "CLOSED" ? "Closed" : "Open"});
        }
        else
        {
            note("skipped link " + name + " of unknown type " + type);
            continue;
        }
        for (const auto& v : l.get("vertices").items())
        {
            if (v.size() >= 2)
                doc.ensure("VERTICES").appendRow({name, fmt(v[0].asNumber()), fmt(v[1].asNumber())});
        }
    }
    if (w.get("controls").size() > 0)
        note(std::to_string(w.get("controls").size()) + " control(s) were not imported");
    if (w.get("sources").size() > 0)
        note(std::to_string(w.get("sources").size()) + " quality source(s) were not imported");
    model.rebuild();
    return model;
}

core::Model readWntrJson(const std::filesystem::path& path, std::vector<std::string>* notes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.u8string());
    std::ostringstream buf;
    buf << in.rdbuf();
    return fromWntrJson(Value::parse(buf.str()), notes);
}

} // namespace mirab::interop
