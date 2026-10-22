/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Linkage.cpp
 * Purpose: Link between a water distribution model and a sewer model: node matching and dry-weather flow from water use.
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
#include "mirab/core/Linkage.h"

#include "mirab/core/Calibration.h"
#include "mirab/core/Crs.h"
#include "mirab/core/Geometry.h"
#include "mirab/core/Text.h"
#include "mirab/core/Units.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace mirab::core
{

namespace
{

constexpr double kDay = 86400.0;
constexpr double kHour = 3600.0;

const char* const kDayNames[7] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                  "Friday", "Saturday", "Sunday"};

double firstNumber(const std::string& value, double fallback)
{
    const auto tokens = text::splitInpLine(value).tokens;
    if (tokens.empty())
        return fallback;
    return text::toDouble(tokens[0]).value_or(fallback);
}

std::string firstToken(const std::string& value)
{
    const auto tokens = text::splitInpLine(value).tokens;
    return tokens.empty() ? std::string() : text::unquote(tokens[0]);
}

/// Multipliers of every EPANET pattern, rows of one pattern joined.
std::map<std::string, std::vector<double>> epanetPatterns(const Model& water)
{
    std::map<std::string, std::vector<double>> out;
    if (const InpSection* s = water.document().find("PATTERNS"))
    {
        for (const auto& row : s->allRowTokens())
        {
            if (row.empty())
                continue;
            auto& values = out[text::unquote(row[0])];
            for (std::size_t i = 1; i < row.size(); ++i)
                values.push_back(text::toDouble(row[i]).value_or(0.0));
        }
    }
    return out;
}

/// The pattern ID as defined (exact match first, then ignoring case); empty when unknown.
std::string findPattern(const std::map<std::string, std::vector<double>>& patterns,
                        const std::string& id)
{
    if (id.empty())
        return {};
    if (patterns.count(id))
        return id;
    for (const auto& [name, values] : patterns)
    {
        if (text::iequals(name, id))
            return name;
    }
    return {};
}

PatternTiming epanetTiming(const Model& water)
{
    PatternTiming t;
    const auto& doc = water.document();
    auto seconds = [&](const char* key, double fallback)
    {
        const auto v = doc.keyValue("TIMES", key);
        if (!v)
            return fallback;
        const auto tokens = text::splitInpLine(*v).tokens;
        if (tokens.empty())
            return fallback;
        return text::parseClockOrDuration(tokens[0], tokens.size() > 1 ? tokens[1] : std::string())
            .value_or(fallback);
    };
    t.step = seconds("PATTERN TIMESTEP", kHour);
    if (t.step <= 0)
        t.step = kHour;
    t.start = seconds("PATTERN START", 0.0);
    if (const auto v = doc.keyValue("TIMES", "START CLOCKTIME"))
        t.clock = parseTimeOfDay(*v).value_or(0.0);
    t.clock = std::fmod(t.clock, kDay);
    if (t.clock < 0)
        t.clock += kDay;
    return t;
}

double mean(const std::vector<double>& v)
{
    if (v.empty())
        return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double round4(double v)
{
    return std::round(v * 10000.0) / 10000.0;
}

std::string patternSignature(const std::vector<double>& v)
{
    std::string s;
    for (double x : v)
        s += text::formatDouble(round4(x), 8) + ",";
    return s;
}

bool isConstant(const std::vector<double>& v)
{
    for (double x : v)
    {
        if (std::fabs(round4(x) - 1.0) > 1e-9)
            return false;
    }
    return true;
}

/// Section created after the existing sections that precede it in SWMM's order.
InpSection& ensureSwmmSection(InpDocument& doc, const std::string& name,
                              const std::vector<std::string>& followers)
{
    if (InpSection* s = doc.find(name))
        return *s;
    auto it = doc.sections.end();
    for (auto i = doc.sections.begin(); i != doc.sections.end(); ++i)
    {
        if (std::find(followers.begin(), followers.end(), i->name) != followers.end())
        {
            it = i;
            break;
        }
    }
    if (it == doc.sections.end())
        return doc.ensure(name);
    InpSection s;
    s.name = name;
    s.header = "[" + name + "]";
    s.lines.push_back("");
    if (it != doc.sections.begin())
    {
        auto prev = std::prev(it);
        if (prev->lines.empty() || !text::trim(prev->lines.back()).empty())
            prev->lines.push_back("");
    }
    return *doc.sections.insert(it, std::move(s));
}

} // namespace

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

double WaterLinkSettings::factorFor(const std::string& category) const
{
    if (!categories.empty() &&
        std::find(categories.begin(), categories.end(), category) == categories.end())
        return 0.0;
    const auto it = categoryFactors.find(category);
    return std::max(0.0, it == categoryFactors.end() ? returnFactor : it->second);
}

WaterLinkSettings WaterLinkSettings::fromJson(const json::Value& v)
{
    WaterLinkSettings s;
    if (!v.isObject())
        return s;
    s.waterModel = v.get("waterModel").asString();
    s.maxDistance = v.get("maxDistance").asNumber(0.0);
    s.nodeTag = v.get("nodeTag").asString();
    s.serviceAreas = v.get("serviceAreas").asString();
    s.storage = v.get("storage").asBool(true);
    s.returnFactor = v.get("returnFactor").asNumber(0.8);
    const json::Value& factors = v.get("categoryFactors");
    for (std::size_t i = 0; i < factors.keys().size(); ++i)
        s.categoryFactors[factors.keys()[i]] = factors.items()[i].asNumber(s.returnFactor);
    for (const auto& c : v.get("categories").items())
        s.categories.push_back(c.asString());
    s.patternPrefix = v.get("patternPrefix").asString("WW_");
    s.weekend = v.get("weekend").asBool(false);
    const json::Value& start = v.get("weekStart");
    if (start.isNumber())
        s.weekStart = static_cast<int>(start.asNumber()) % 7;
    else
    {
        for (int d = 0; d < 7; ++d)
        {
            if (text::iequals(start.asString(), kDayNames[d]))
                s.weekStart = d;
        }
    }
    if (s.weekStart < 0)
        s.weekStart += 7;
    s.lagHours = v.get("lagHours").asNumber(0.0);
    s.results = v.get("results").asString();
    const json::Value& overrides = v.get("overrides");
    for (std::size_t i = 0; i < overrides.keys().size(); ++i)
        s.overrides[overrides.keys()[i]] = overrides.items()[i].asString();
    const json::Value& links = v.get("links");
    for (std::size_t i = 0; i < links.keys().size(); ++i)
        s.links[links.keys()[i]] = links.items()[i].asString();
    s.showReference = v.get("showReference").asBool(true);
    return s;
}

json::Value WaterLinkSettings::toJson() const
{
    json::Value v = json::Value::object();
    v.set("waterModel", waterModel);
    v.set("maxDistance", maxDistance);
    if (!nodeTag.empty())
        v.set("nodeTag", nodeTag);
    if (!serviceAreas.empty())
        v.set("serviceAreas", serviceAreas);
    if (!storage)
        v.set("storage", false);
    v.set("returnFactor", returnFactor);
    if (!categoryFactors.empty())
    {
        json::Value f = json::Value::object();
        for (const auto& [category, factor] : categoryFactors)
            f.set(category, factor);
        v.set("categoryFactors", f);
    }
    if (!categories.empty())
    {
        json::Value c = json::Value::array();
        for (const auto& name : categories)
            c.push(name);
        v.set("categories", c);
    }
    v.set("patternPrefix", patternPrefix);
    if (weekend)
    {
        v.set("weekend", true);
        v.set("weekStart", kDayNames[((weekStart % 7) + 7) % 7]);
    }
    if (lagHours != 0.0)
        v.set("lagHours", lagHours);
    if (!results.empty())
        v.set("results", results);
    if (!overrides.empty())
    {
        json::Value o = json::Value::object();
        for (const auto& [water, swmm] : overrides)
            o.set(water, swmm);
        v.set("overrides", o);
    }
    if (!links.empty())
    {
        json::Value l = json::Value::object();
        for (const auto& [water, swmm] : links)
            l.set(water, swmm);
        v.set("links", l);
    }
    if (!showReference)
        v.set("showReference", false);
    return v;
}

// ---------------------------------------------------------------------------
// Water demands
// ---------------------------------------------------------------------------

std::vector<WaterDemand> waterDemands(const Model& water)
{
    std::vector<WaterDemand> out;
    const auto patterns = epanetPatterns(water);
    const double multiplier = firstNumber(water.option("DEMAND MULTIPLIER"), 1.0);
    std::string defaultPattern = firstToken(water.option("PATTERN"));
    if (defaultPattern.empty())
        defaultPattern = "1";
    defaultPattern = findPattern(patterns, defaultPattern);
    auto resolve = [&](const std::string& id)
    { return id.empty() ? defaultPattern : findPattern(patterns, id); };

    // [DEMANDS] rows, grouped by junction, replace the [JUNCTIONS] demand.
    std::unordered_map<std::string, std::vector<WaterDemand>> listed;
    if (const InpSection* s = water.document().find("DEMANDS"))
    {
        const auto rows = s->allRowTokens();
        for (int r = 0; r < static_cast<int>(rows.size()); ++r)
        {
            const auto& t = rows[static_cast<std::size_t>(r)];
            if (t.size() < 2)
                continue;
            WaterDemand d;
            d.node = text::unquote(t[0]);
            d.base = text::toDouble(t[1]).value_or(0.0) * multiplier;
            d.pattern = resolve(t.size() > 2 ? text::unquote(t[2]) : std::string());
            d.category = t.size() > 3 ? text::unquote(t[3]) : text::trim(s->rowComment(r));
            listed[d.node].push_back(d);
        }
    }
    for (const auto& n : water.nodes())
    {
        if (n.kind != "Junction")
            continue;
        if (auto it = listed.find(n.id); it != listed.end())
        {
            for (const auto& d : it->second)
                out.push_back(d);
            continue;
        }
        const InpSection* s = water.document().find(n.section);
        if (!s)
            continue;
        const auto t = s->rowTokens(n.row);
        if (t.size() < 3)
            continue;
        WaterDemand d;
        d.node = n.id;
        d.base = text::toDouble(t[2]).value_or(0.0) * multiplier;
        d.pattern = resolve(t.size() > 3 ? text::unquote(t[3]) : std::string());
        out.push_back(d);
    }
    return out;
}

std::vector<std::string> demandCategories(const Model& water)
{
    std::set<std::string> names;
    for (const auto& d : waterDemands(water))
        names.insert(d.category);
    return {names.begin(), names.end()};
}

std::vector<ServiceArea> subcatchmentAreas(const Model& swmm)
{
    std::vector<ServiceArea> out;
    for (const auto& a : swmm.areas())
    {
        if (a.polygon.size() >= 3)
            out.push_back({a.id, a.polygon});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

const char* statusName(NodeAssignment::Status status)
{
    switch (status)
    {
    case NodeAssignment::Status::Matched:
        return "matched";
    case NodeAssignment::Status::Manual:
        return "manual";
    case NodeAssignment::Status::TooFar:
        return "too-far";
    case NodeAssignment::Status::NoCoordinates:
        return "no-coordinates";
    case NodeAssignment::Status::OutsideAreas:
        return "outside-areas";
    case NodeAssignment::Status::NoCandidate:
        return "no-candidate";
    case NodeAssignment::Status::Excluded:
        return "excluded";
    }
    return "";
}

std::vector<NodeAssignment> matchWaterNodes(const Model& water, const Model& swmm,
                                            const WaterLinkSettings& settings,
                                            const LinkContext& context,
                                            std::vector<std::string>* notes)
{
    auto note = [&](const std::string& n)
    {
        if (notes)
            notes->push_back(n);
    };

    // Reprojection of EPANET coordinates into the SWMM system.
    std::optional<crs::Crs> from, to;
    if (!context.waterCrs.empty() && !context.swmmCrs.empty())
    {
        from = crs::parse(context.waterCrs);
        to = crs::parse(context.swmmCrs);
        if (!from || !to)
        {
            note("A coordinate system could not be read; coordinates are compared as they are.");
            from.reset();
            to.reset();
        }
        else if (crs::toString(*from) == crs::toString(*to))
        {
            from.reset();
            to.reset();
        }
        else
        {
            note("Water network coordinates are reprojected from " + from->name + " to " +
                 to->name + ".");
        }
    }
    else if (context.waterCrs != context.swmmCrs)
    {
        note("Only one of the models has a coordinate system; coordinates are compared as they "
             "are.");
    }

    // SWMM nodes that can receive flow.
    std::unordered_set<std::string> tagged;
    const bool byTag = !settings.nodeTag.empty();
    if (byTag)
    {
        if (const InpSection* s = swmm.document().find("TAGS"))
        {
            for (const auto& t : s->allRowTokens())
            {
                if (t.size() >= 3 && text::iequals(t[0], "Node") &&
                    text::iequals(text::unquote(t[2]), settings.nodeTag))
                    tagged.insert(text::unquote(t[1]));
            }
        }
        if (tagged.empty())
            note("No SWMM node has the tag \"" + settings.nodeTag + "\".");
    }
    auto areaOf = [&](const Point& p) -> int
    {
        for (std::size_t i = 0; i < context.serviceAreas.size(); ++i)
        {
            if (pointInPolygon(context.serviceAreas[i].ring, p))
                return static_cast<int>(i);
        }
        return -1;
    };
    const bool byArea = !context.serviceAreas.empty();

    struct Candidate
    {
        double x, y;
        const NodeItem* node;
    };
    std::map<int, std::vector<Candidate>> groups; // by service area (-1 without areas)
    for (const auto& n : swmm.nodes())
    {
        if (!n.placed || !(n.kind == "Junction" || (settings.storage && n.kind == "Storage")))
            continue;
        if (byTag && !tagged.count(n.id))
            continue;
        const int area = byArea ? areaOf(n.pos) : -1;
        if (byArea && area < 0)
            continue;
        groups[area].push_back({n.pos.x, n.pos.y, &n});
    }
    for (auto& [area, list] : groups)
    {
        std::stable_sort(list.begin(), list.end(),
                         [](const Candidate& a, const Candidate& b) { return a.x < b.x; });
    }
    auto nearest = [&](const std::vector<Candidate>& list, const Point& p, double& best)
    {
        const Candidate* found = nullptr;
        best = std::numeric_limits<double>::infinity();
        auto it = std::lower_bound(list.begin(), list.end(), p.x,
                                   [](const Candidate& c, double x) { return c.x < x; });
        auto consider = [&](const Candidate& c)
        {
            const double d = std::hypot(c.x - p.x, c.y - p.y);
            if (d < best)
            {
                best = d;
                found = &c;
            }
        };
        for (auto r = it; r != list.end() && r->x - p.x < best; ++r)
            consider(*r);
        for (auto l = it; l != list.begin();)
        {
            --l;
            if (p.x - l->x >= best)
                break;
            consider(*l);
        }
        return found;
    };

    // Demand nodes in the order of the EPANET model.
    std::vector<std::string> demandNodes;
    std::unordered_set<std::string> seen;
    for (const auto& d : waterDemands(water))
    {
        if (d.base != 0.0 && seen.insert(d.node).second)
            demandNodes.push_back(d.node);
    }

    std::vector<NodeAssignment> out;
    out.reserve(demandNodes.size());
    for (const auto& id : demandNodes)
    {
        NodeAssignment a;
        a.waterNode = id;
        const NodeItem* n = water.findNode(id);
        if (n && n->placed)
        {
            a.waterPos = n->pos;
            a.placed = true;
            if (from && !crs::transform(*from, *to, n->pos.x, n->pos.y, a.waterPos.x, a.waterPos.y))
                a.placed = false;
        }
        const int area = a.placed && byArea ? areaOf(a.waterPos) : -1;
        if (area >= 0)
            a.area = context.serviceAreas[static_cast<std::size_t>(area)].name;

        if (auto o = settings.overrides.find(id); o != settings.overrides.end())
        {
            if (o->second.empty())
            {
                a.status = NodeAssignment::Status::Excluded;
                out.push_back(a);
                continue;
            }
            if (const NodeItem* target = swmm.findNode(o->second))
            {
                a.swmmNode = target->id;
                a.status = NodeAssignment::Status::Manual;
                if (a.placed && target->placed)
                    a.distance = std::hypot(target->pos.x - a.waterPos.x,
                                            target->pos.y - a.waterPos.y);
                out.push_back(a);
                continue;
            }
            note("The manual choice for " + id + " names the unknown SWMM node " + o->second +
                 "; the nearest node is used.");
        }
        if (!a.placed)
        {
            a.status = NodeAssignment::Status::NoCoordinates;
            out.push_back(a);
            continue;
        }
        if (byArea && area < 0)
        {
            a.status = NodeAssignment::Status::OutsideAreas;
            out.push_back(a);
            continue;
        }
        const auto g = groups.find(area);
        double d = 0;
        const Candidate* c = g == groups.end() ? nullptr : nearest(g->second, a.waterPos, d);
        if (!c)
        {
            a.status = NodeAssignment::Status::NoCandidate;
        }
        else if (settings.maxDistance > 0 && d > settings.maxDistance)
        {
            a.status = NodeAssignment::Status::TooFar;
            a.nearest = c->node->id;
            a.distance = d;
        }
        else
        {
            a.swmmNode = c->node->id;
            a.distance = d;
        }
        out.push_back(a);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

double convertFlow(double value, std::string_view from, std::string_view to)
{
    const double a = flowToCms(from);
    const double b = flowToCms(to);
    if (a <= 0 || b <= 0)
        throw std::runtime_error("unknown flow units " + std::string(a <= 0 ? from : to));
    return value * a / b;
}

std::vector<double> hourlyPatternSeries(const std::vector<double>& multipliers,
                                        const PatternTiming& timing, int days)
{
    const int hours = std::max(1, days) * 24;
    std::vector<double> out(static_cast<std::size_t>(hours), 1.0);
    if (multipliers.empty())
        return out;
    const double step = timing.step > 0 ? timing.step : kHour;
    const long long n = static_cast<long long>(multipliers.size());
    auto valueAt = [&](long long k)
    { return multipliers[static_cast<std::size_t>(((k % n) + n) % n)]; };
    for (int h = 0; h < hours; ++h)
    {
        // Clock hour h of the first day starts timing.clock seconds before time zero.
        const double a = h * kHour - timing.clock + timing.start;
        const double b = a + kHour;
        double sum = 0.0;
        double t = a;
        while (t < b - 1e-9)
        {
            const long long k = static_cast<long long>(std::floor(t / step + 1e-12));
            const double end = std::min(b, (static_cast<double>(k) + 1.0) * step);
            sum += valueAt(k) * (end - t);
            t = end;
        }
        out[static_cast<std::size_t>(h)] = sum / kHour;
    }
    return out;
}

std::vector<double> hourlyResultSeries(const std::vector<double>& times,
                                       const std::vector<double>& values, double clock, int days)
{
    const int hours = std::max(1, days) * 24;
    std::vector<double> out(static_cast<std::size_t>(hours), 0.0);
    const std::size_t count = std::min(times.size(), values.size());
    if (count == 0)
        return out;
    // Index of the value in effect at time t (held until the next report).
    auto indexAt = [&](double t) -> std::size_t
    {
        const auto end = times.begin() + static_cast<std::ptrdiff_t>(count);
        const auto it = std::upper_bound(times.begin(), end, t);
        return it == times.begin() ? 0 : static_cast<std::size_t>(it - times.begin()) - 1;
    };
    for (int h = 0; h < hours; ++h)
    {
        double a = h * kHour - clock;
        // Hours before the first report take the same clock hour of the next day.
        while (a < times[0] - 1e-9)
            a += kDay;
        const double b = a + kHour;
        double sum = 0.0;
        double t = a;
        while (t < b - 1e-9)
        {
            const std::size_t i = indexAt(t);
            const double end = i + 1 < count ? std::min(b, std::max(times[i + 1], t + 1e-6)) : b;
            sum += values[i] * (end - t);
            t = end;
        }
        out[static_cast<std::size_t>(h)] = sum / kHour;
    }
    return out;
}

std::vector<double> lagSeries(const std::vector<double>& series, double hours)
{
    const long long n = static_cast<long long>(series.size());
    if (n == 0 || hours == 0.0)
        return series;
    std::vector<double> out(series.size());
    auto at = [&](long long i) { return series[static_cast<std::size_t>(((i % n) + n) % n)]; };
    for (long long i = 0; i < n; ++i)
    {
        const double pos = static_cast<double>(i) - hours;
        const double lo = std::floor(pos);
        const double frac = pos - lo;
        const long long k = static_cast<long long>(lo);
        out[static_cast<std::size_t>(i)] = at(k) * (1.0 - frac) + at(k + 1) * frac;
    }
    return out;
}

std::vector<double> normalised(std::vector<double> series)
{
    const double m = mean(series);
    if (m > 0)
    {
        for (double& v : series)
            v /= m;
    }
    return series;
}

DailyProfiles dailyProfiles(const std::vector<double>& series, bool weekend, int weekStart)
{
    DailyProfiles p;
    const int days = static_cast<int>(series.size() / 24);
    p.mean = mean(series);
    p.hourly.assign(24, 0.0);
    if (days == 0)
        return p;
    auto dow = [&](int d) { return (((weekStart + d) % 7) + 7) % 7; }; // 0 = Monday
    const bool split = weekend && days >= 7;
    std::vector<double> we(24, 0.0);
    int nWeekday = 0, nWeekend = 0;
    for (int d = 0; d < days; ++d)
    {
        const bool isWeekend = split && dow(d) >= 5;
        (isWeekend ? nWeekend : nWeekday)++;
        for (int h = 0; h < 24; ++h)
            (isWeekend ? we : p.hourly)[static_cast<std::size_t>(h)] +=
                series[static_cast<std::size_t>(d * 24 + h)];
    }
    for (double& v : p.hourly)
        v /= std::max(1, nWeekday);
    p.hourly = normalised(p.hourly);
    if (!split)
        return p;
    for (double& v : we)
        v /= std::max(1, nWeekend);
    p.weekend = normalised(we);
    std::vector<double> sum(7, 0.0);
    std::vector<int> count(7, 0);
    for (int d = 0; d < days; ++d)
    {
        double dayMean = 0;
        for (int h = 0; h < 24; ++h)
            dayMean += series[static_cast<std::size_t>(d * 24 + h)] / 24.0;
        const int sunday0 = (dow(d) + 1) % 7;
        sum[static_cast<std::size_t>(sunday0)] += dayMean;
        ++count[static_cast<std::size_t>(sunday0)];
    }
    p.daily.assign(7, 1.0);
    for (int j = 0; j < 7; ++j)
    {
        if (count[static_cast<std::size_t>(j)] > 0 && p.mean > 0)
            p.daily[static_cast<std::size_t>(j)] =
                sum[static_cast<std::size_t>(j)] / count[static_cast<std::size_t>(j)] / p.mean;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Planning
// ---------------------------------------------------------------------------

DwfPlan planDwf(const Model& water, const Model& swmm, const WaterLinkSettings& settings,
                const LinkContext& context)
{
    DwfPlan plan;
    plan.flowUnits = swmm.units().flow;
    const std::string waterUnits = water.units().flow;
    if (flowToCms(plan.flowUnits) <= 0 || flowToCms(waterUnits) <= 0)
        throw std::runtime_error("unknown flow units " +
                                 (flowToCms(plan.flowUnits) <= 0 ? plan.flowUnits : waterUnits));

    plan.assignments = matchWaterNodes(water, swmm, settings, context, &plan.notes);
    const auto demands = waterDemands(water);
    const auto patterns = epanetPatterns(water);
    const PatternTiming timing = epanetTiming(water);

    std::unordered_map<std::string, std::vector<const WaterDemand*>> byNode;
    for (const auto& d : demands)
        byNode[d.node].push_back(&d);

    // Days covered by the source profiles.
    const ResultTable* table = context.results ? context.results->table("node") : nullptr;
    if (context.results && (!table || table->variableIndex("Demand") < 0))
        throw std::runtime_error("the results have no node demands");
    double unitFactor = convertFlow(1.0, waterUnits, plan.flowUnits);
    int days = 1;
    if (table)
    {
        const auto& times = context.results->times;
        const double span = times.empty() ? 0.0 : times.back() - times.front();
        days = static_cast<int>(std::floor(span / kDay + 1e-6));
        if (days < 1)
            throw std::runtime_error("the results cover less than 24 hours");
        days = std::min(days, 7);
        if (!context.results->flowUnits.empty())
            unitFactor = convertFlow(1.0, context.results->flowUnits, plan.flowUnits);
    }
    else
    {
        double longest = 0;
        for (const auto& d : demands)
        {
            if (!d.pattern.empty())
                longest = std::max(longest, patterns.at(d.pattern).size() * timing.step);
        }
        days = static_cast<int>(std::ceil(longest / kDay - 1e-9));
        days = std::clamp(days, 1, 7);
        if (settings.weekend && days >= 7)
            days = 7;
    }
    if (settings.weekend && days < 7)
        plan.notes.push_back(table ? "The results cover less than a week; no weekend pattern is "
                                     "written."
                                   : "The demand patterns are shorter than a week; no weekend "
                                     "pattern is written.");
    plan.days = days;
    const std::size_t hours = static_cast<std::size_t>(days) * 24;

    std::map<std::string, std::vector<double>> patternSeries;
    auto seriesOf = [&](const std::string& id) -> const std::vector<double>&
    {
        auto it = patternSeries.find(id);
        if (it == patternSeries.end())
        {
            std::vector<double> s = id.empty()
                                        ? std::vector<double>(hours, 1.0)
                                        : hourlyPatternSeries(patterns.at(id), timing, days);
            it = patternSeries.emplace(id, std::move(s)).first;
        }
        return it->second;
    };
    auto patternMean = [&](const std::string& id)
    { return id.empty() ? 1.0 : mean(patterns.at(id)); };

    int demandVar = -1, emitterVar = -1, leakVar = -1;
    if (table)
    {
        demandVar = table->variableIndex("Demand");
        emitterVar = table->variableIndex("Emitter Flow");
        leakVar = table->variableIndex("Leakage");
        if (emitterVar >= 0 || leakVar >= 0)
            plan.notes.push_back("Emitter and leakage flows are left out of the delivered demand.");
    }

    // Returned flow series of each EPANET node, in SWMM flow units.
    std::map<std::string, std::vector<double>> returnedSeries;
    std::map<std::string, std::set<std::string>> nodePatterns;
    for (auto& a : plan.assignments)
    {
        const auto& list = byNode[a.waterNode];
        std::vector<double> demand(hours, 0.0), included(hours, 0.0), returned(hours, 0.0);
        for (const WaterDemand* d : list)
        {
            if (d->base != 0.0 && settings.factorFor(d->category) > 0)
                nodePatterns[a.waterNode].insert(d->pattern);
        }
        if (table)
        {
            const int e = table->elementIndex(a.waterNode);
            if (e < 0)
            {
                plan.notes.push_back("The results have no values for " + a.waterNode + ".");
                continue;
            }
            std::vector<double> values(static_cast<std::size_t>(table->periodCount()));
            for (int p = 0; p < table->periodCount(); ++p)
            {
                double v = table->value(p, demandVar, e);
                if (emitterVar >= 0)
                    v -= table->value(p, emitterVar, e);
                if (leakVar >= 0)
                    v -= table->value(p, leakVar, e);
                values[static_cast<std::size_t>(p)] = std::max(0.0, v);
            }
            const auto delivered =
                hourlyResultSeries(context.results->times, values, timing.clock, days);
            // Shares of the expected demand that return, and that are included.
            double expected = 0, back = 0, kept = 0;
            for (const WaterDemand* d : list)
            {
                const double q = d->base * patternMean(d->pattern);
                const double f = settings.factorFor(d->category);
                expected += q;
                back += q * f;
                kept += f > 0 ? q : 0.0;
            }
            const double share = expected > 0 ? back / expected : 0.0;
            const double keptShare = expected > 0 ? kept / expected : 0.0;
            for (std::size_t i = 0; i < hours; ++i)
            {
                demand[i] = delivered[i] * unitFactor;
                included[i] = demand[i] * keptShare;
                returned[i] = demand[i] * share;
            }
        }
        else
        {
            for (const WaterDemand* d : list)
            {
                const auto& s = seriesOf(d->pattern);
                const double f = settings.factorFor(d->category);
                for (std::size_t i = 0; i < hours; ++i)
                {
                    const double q = d->base * s[i] * unitFactor;
                    demand[i] += q;
                    included[i] += f > 0 ? q : 0.0;
                    returned[i] += q * f;
                }
            }
        }
        a.demand = mean(demand);
        a.returned = mean(returned);
        plan.totalDemand += a.demand;
        plan.includedDemand += mean(included);
        if (a.swmmNode.empty())
        {
            if (a.returned > 0 || a.demand > 0)
            {
                ++plan.unassigned;
                plan.unassignedFlow += a.returned;
                if (a.status == NodeAssignment::Status::TooFar)
                    ++plan.tooFar;
            }
            continue;
        }
        returnedSeries[a.waterNode] = std::move(returned);
    }

    // Inflows of the SWMM nodes, in the order the nodes appear in the SWMM model.
    std::map<std::string, std::vector<const NodeAssignment*>> bySwmm;
    for (const auto& a : plan.assignments)
    {
        if (!a.swmmNode.empty() && returnedSeries.count(a.waterNode))
            bySwmm[a.swmmNode].push_back(&a);
    }
    std::map<std::string, std::string> names; // signature -> pattern name
    std::set<std::string> used;
    // Patterns of the SWMM model that an earlier link did not write keep their names.
    if (const InpSection* s = swmm.document().find("PATTERNS"))
    {
        for (const auto& row : s->allRowTokens())
        {
            const std::string name = row.empty() ? std::string() : text::unquote(row[0]);
            if (!name.empty() &&
                (settings.patternPrefix.empty() || name.rfind(settings.patternPrefix, 0) != 0))
                used.insert(name);
        }
    }
    auto addPattern = [&](const std::string& base, const std::string& type,
                          const std::vector<double>& factors) -> std::string
    {
        const std::string signature = type + ":" + patternSignature(factors);
        if (auto it = names.find(signature); it != names.end())
            return it->second;
        std::string name = base;
        for (int k = 2; used.count(name); ++k)
            name = base + "_" + std::to_string(k);
        used.insert(name);
        names[signature] = name;
        std::vector<double> rounded;
        for (double v : factors)
            rounded.push_back(round4(v));
        plan.patterns.push_back({name, type, rounded});
        return name;
    };
    for (const auto& n : swmm.nodes())
    {
        const auto it = bySwmm.find(n.id);
        if (it == bySwmm.end())
            continue;
        std::vector<double> series(hours, 0.0);
        std::set<std::string> sources;
        DwfInflow inflow;
        inflow.node = n.id;
        for (const NodeAssignment* a : it->second)
        {
            const auto& s = returnedSeries[a->waterNode];
            for (std::size_t i = 0; i < hours; ++i)
                series[i] += s[i];
            inflow.sources.push_back(a->waterNode);
            for (const auto& p : nodePatterns[a->waterNode])
                sources.insert(p);
        }
        const auto profiles = dailyProfiles(lagSeries(series, settings.lagHours),
                                            settings.weekend, settings.weekStart);
        inflow.average = profiles.mean;
        if (inflow.average <= 0)
            continue;
        const bool split = !profiles.weekend.empty();
        if (!isConstant(profiles.hourly) || split)
        {
            const std::string base = settings.patternPrefix +
                                     (sources.size() == 1 && !sources.begin()->empty()
                                          ? *sources.begin()
                                          : n.id);
            inflow.hourly = addPattern(base, "HOURLY", profiles.hourly);
            if (split)
            {
                inflow.weekend = addPattern(base + "_WE", "WEEKEND", profiles.weekend);
                if (!isConstant(profiles.daily))
                    inflow.daily = addPattern(base + "_DAY", "DAILY", profiles.daily);
            }
        }
        plan.returned += inflow.average;
        plan.inflows.push_back(inflow);
    }
    return plan;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

int applyDwf(Model& swmm, const DwfPlan& plan, const std::string& patternPrefix)
{
    int changed = 0;
    InpDocument& doc = swmm.document();
    const bool owned = !patternPrefix.empty();
    auto ownedPattern = [&](const std::string& token)
    { return owned && text::unquote(token).rfind(patternPrefix, 0) == 0; };

    // [DWF]: replace the FLOW row of every node that receives flow.
    std::map<std::string, std::vector<std::string>> rows;
    for (const auto& in : plan.inflows)
    {
        std::vector<std::string> r = {in.node, "FLOW", text::formatDouble(in.average, 6), "\"\"",
                                      in.daily.empty() ? "\"\"" : in.daily,
                                      in.hourly.empty() ? "\"\"" : in.hourly,
                                      in.weekend.empty() ? "\"\"" : in.weekend};
        while (r.size() > 3 && r.back() == "\"\"")
            r.pop_back();
        rows[in.node] = r;
    }
    const std::vector<std::string> afterDwf = {"RDII", "HYDROGRAPHS", "CURVES", "TIMESERIES",
                                               "PATTERNS", "REPORT", "TAGS", "MAP", "COORDINATES",
                                               "VERTICES", "POLYGONS", "SYMBOLS", "LABELS",
                                               "BACKDROP", "PROFILES", "END"};
    if (!rows.empty() || doc.find("DWF"))
    {
        InpSection& dwf = ensureSwmmSection(doc, "DWF", afterDwf);
        const auto all = dwf.allRowTokens();
        std::vector<int> stale;
        std::set<std::string> done;
        for (int r = 0; r < static_cast<int>(all.size()); ++r)
        {
            const auto& t = all[static_cast<std::size_t>(r)];
            if (t.size() < 2 || !text::iequals(text::unquote(t[1]), "FLOW"))
                continue;
            const std::string node = text::unquote(t[0]);
            auto it = rows.find(node);
            if (it != rows.end() && !done.count(node))
            {
                done.insert(node);
                if (t != it->second)
                {
                    dwf.setRowTokens(r, it->second);
                    ++changed;
                }
            }
            else if (it != rows.end() ||
                     (t.size() > 3 && std::any_of(t.begin() + 3, t.end(), ownedPattern)))
            {
                stale.push_back(r); // a second FLOW row, or one written by an earlier link
            }
        }
        for (auto r = stale.rbegin(); r != stale.rend(); ++r)
        {
            dwf.removeRow(*r);
            ++changed;
        }
        for (const auto& in : plan.inflows)
        {
            if (!done.count(in.node))
            {
                dwf.appendRow(rows[in.node]);
                ++changed;
            }
        }
    }

    // [PATTERNS]
    const std::vector<std::string> afterPatterns = {"REPORT",  "TAGS",     "MAP",     "COORDINATES",
                                                    "VERTICES", "POLYGONS", "SYMBOLS", "LABELS",
                                                    "BACKDROP", "PROFILES", "END"};
    std::set<std::string> written;
    if (!plan.patterns.empty())
        ensureSwmmSection(doc, "PATTERNS", afterPatterns);
    swmm.rebuild();
    for (const auto& p : plan.patterns)
    {
        written.insert(p.name);
        std::vector<std::vector<std::string>> prow;
        const std::size_t perRow = p.type == "DAILY" ? 7 : 6;
        for (std::size_t i = 0; i < p.factors.size(); i += perRow)
        {
            std::vector<std::string> r = {p.name};
            if (i == 0)
                r.push_back(p.type);
            for (std::size_t k = i; k < std::min(p.factors.size(), i + perRow); ++k)
                r.push_back(text::formatDouble(p.factors[k], 6));
            prow.push_back(r);
        }
        const auto existing = swmm.rowsOfKey("PATTERNS", p.name);
        std::vector<std::vector<std::string>> before;
        for (int r : existing)
            before.push_back(doc.find("PATTERNS")->rowTokens(r));
        if (before != prow)
        {
            swmm.replaceRows("PATTERNS", existing, prow);
            changed += static_cast<int>(std::max(existing.size(), prow.size()));
        }
    }
    // Patterns of an earlier link that nothing uses any more.
    if (InpSection* pats = doc.find("PATTERNS"); pats && owned)
    {
        std::unordered_set<std::string> referenced;
        for (const auto& s : doc.sections)
        {
            if (s.name == "PATTERNS")
                continue;
            for (const auto& t : s.allRowTokens())
            {
                for (const auto& token : t)
                    referenced.insert(text::unquote(token));
            }
        }
        const auto all = pats->allRowTokens();
        for (int r = static_cast<int>(all.size()) - 1; r >= 0; --r)
        {
            const auto& t = all[static_cast<std::size_t>(r)];
            if (t.empty())
                continue;
            const std::string name = text::unquote(t[0]);
            if (ownedPattern(name) && !written.count(name) && !referenced.count(name))
            {
                pats->removeRow(r);
                ++changed;
            }
        }
    }
    swmm.rebuild();
    return changed;
}

} // namespace mirab::core
