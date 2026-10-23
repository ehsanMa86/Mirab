/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Calibration.cpp
 * Purpose: Observed data, goodness-of-fit metrics and calibration parameters.
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
#include "mirab/core/Calibration.h"

#include "mirab/core/Text.h"
#include "mirab/core/Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace mirab::core
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

bool isDigits(std::string_view s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
               return std::isdigit(static_cast<unsigned char>(c)) != 0;
           });
}

int monthFromName(std::string_view s)
{
    static const char* names[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                  "jul", "aug", "sep", "oct", "nov", "dec"};
    const std::string l = text::toLower(s);
    if (l.size() < 3)
        return 0;
    for (int i = 0; i < 12; ++i)
    {
        if (l.compare(0, 3, names[i]) == 0)
            return i + 1;
    }
    return 0;
}

/// Splits on any of @p seps, keeping empty parts out.
std::vector<std::string> splitAny(std::string_view s, std::string_view seps)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (seps.find(c) != std::string_view::npos)
        {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::optional<long long> parseDate(std::string_view s, DateOrder order)
{
    const std::string t = text::trim(s);
    if (t.empty())
        return std::nullopt;
    char sep = 0;
    for (char c : t)
    {
        if (c == '/' || c == '-' || c == '.')
        {
            sep = c;
            break;
        }
    }
    if (!sep)
        return std::nullopt;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : t + sep)
    {
        if (c == sep)
        {
            parts.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (parts.size() != 3)
        return std::nullopt;
    int year = 0, month = 0, day = 0;
    auto num = [](const std::string& p) -> int {
        if (!isDigits(p) || p.size() > 4)
            return -1;
        return static_cast<int>(*text::toInt(p));
    };
    if (parts[0].size() == 4 && isDigits(parts[0]))
    {
        year = num(parts[0]);
        month = monthFromName(parts[1]);
        if (!month)
            month = num(parts[1]);
        day = num(parts[2]);
    }
    else
    {
        year = num(parts[2]);
        if (year < 0)
            return std::nullopt;
        if (parts[2].size() <= 2)
            year += year < 70 ? 2000 : 1900;
        const int m0 = monthFromName(parts[0]);
        const int m1 = monthFromName(parts[1]);
        if (m1)
        {
            month = m1;
            day = num(parts[0]);
        }
        else if (m0)
        {
            month = m0;
            day = num(parts[1]);
        }
        else if (order == DateOrder::DayFirst)
        {
            day = num(parts[0]);
            month = num(parts[1]);
        }
        else
        {
            month = num(parts[0]);
            day = num(parts[1]);
        }
    }
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31)
        return std::nullopt;
    return daysFromCivil(year, month, day);
}

bool isMissing(const std::string& cell, const std::string& noData)
{
    const std::string t = text::trim(cell);
    if (t.empty())
        return true;
    const std::string u = text::toUpper(t);
    if (u == "NA" || u == "N/A" || u == "NAN" || u == "NULL" || u == "-" || u == "--" ||
        u == "#N/A" || u == "*")
        return true;
    if (!noData.empty())
    {
        if (t == text::trim(noData))
            return true;
        const auto a = text::toDouble(t), b = text::toDouble(noData);
        if (a && b && *a == *b)
            return true;
    }
    return false;
}

/// Numbers with a decimal point or, when that fails, a decimal comma.
std::optional<double> parseNumber(const std::string& cell)
{
    if (auto v = text::toDouble(cell))
        return v;
    const std::string t = text::trim(cell);
    if (std::count(t.begin(), t.end(), ',') == 1 && t.find('.') == std::string::npos)
        return text::toDouble(text::replaceAll(t, ",", "."));
    return std::nullopt;
}

double elapsedUnitSeconds(const std::string& unit)
{
    const std::string u = text::toLower(unit);
    if (text::istartsWith(u, "sec") || u == "s")
        return 1.0;
    if (text::istartsWith(u, "min"))
        return 60.0;
    if (text::istartsWith(u, "day") || u == "d")
        return 86400.0;
    return 3600.0;
}

void warn(std::vector<std::string>* warnings, const std::string& message)
{
    if (warnings)
        warnings->push_back(message);
}

std::string tableOf(Category c)
{
    switch (c)
    {
    case Category::Node:
        return "node";
    case Category::Link:
        return "link";
    case Category::Area:
        return "subcatchment";
    default:
        return {};
    }
}

/// Canonical result variable for a word used in column headers.
std::string variableFromWord(const std::string& word, Dialect dialect, const std::string& table)
{
    const std::string w = text::toLower(word);
    if (w.empty())
        return {};
    if (w == "pressure" || w == "press" || w == "p")
        return "Pressure";
    if (w == "head" || w == "hgl" || w == "grade")
        return "Head";
    if (w == "demand")
        return "Demand";
    if (w == "velocity" || w == "vel" || w == "speed")
        return "Velocity";
    if (w == "headloss")
        return "Headloss";
    if (w == "quality" || w == "chlorine" || w == "concentration")
        return "Quality";
    if (w == "depth" || w == "level" || w == "stage")
        return "Depth";
    if (w == "volume")
        return "Volume";
    if (w == "flooding" || w == "overflow")
        return "Flooding";
    if (w == "lateral")
        return "Lateral Inflow";
    if (w == "inflow")
        return "Total Inflow";
    if (w == "runoff")
        return "Runoff";
    if (w == "rainfall" || w == "rain")
        return "Rainfall";
    if (w == "flow" || w == "discharge" || w == "q")
    {
        if (table == "node" && dialect == Dialect::Swmm)
            return "Total Inflow";
        if (table == "subcatchment")
            return "Runoff";
        return "Flow";
    }
    return {};
}

std::string unitKey(std::string_view unit)
{
    std::string u;
    for (char c : unit)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
            u += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return u;
}

/// Unit families: 0 length/pressure as water head (metres), 1 flow (m3/s), 2 velocity (m/s).
bool unitToBase(const std::string& u, int& family, double& factor)
{
    static const std::map<std::string, std::pair<int, double>> table = {
        {"m", {0, 1.0}},
        {"meter", {0, 1.0}},
        {"meters", {0, 1.0}},
        {"metre", {0, 1.0}},
        {"metres", {0, 1.0}},
        {"mh2o", {0, 1.0}},
        {"ft", {0, 0.3048}},
        {"feet", {0, 0.3048}},
        {"foot", {0, 0.3048}},
        {"in", {0, 0.0254}},
        {"inch", {0, 0.0254}},
        {"inches", {0, 0.0254}},
        {"mm", {0, 0.001}},
        {"cm", {0, 0.01}},
        {"psi", {0, 0.703069578}},
        {"kpa", {0, 0.101971621}},
        {"bar", {0, 10.1971621}},
        {"cfs", {1, 0.028316846592}},
        {"ft3/s", {1, 0.028316846592}},
        {"gpm", {1, 6.30901964e-5}},
        {"mgd", {1, 0.0438126364}},
        {"imgd", {1, 0.0526168042}},
        {"afd", {1, 0.0142764102}},
        {"lps", {1, 0.001}},
        {"l/s", {1, 0.001}},
        {"lpm", {1, 1.0 / 60000.0}},
        {"l/min", {1, 1.0 / 60000.0}},
        {"mld", {1, 1000.0 / 86400.0}},
        {"cmh", {1, 1.0 / 3600.0}},
        {"m3/h", {1, 1.0 / 3600.0}},
        {"cmd", {1, 1.0 / 86400.0}},
        {"m3/d", {1, 1.0 / 86400.0}},
        {"cms", {1, 1.0}},
        {"m3/s", {1, 1.0}},
        {"m/s", {2, 1.0}},
        {"ft/s", {2, 0.3048}},
        {"fps", {2, 0.3048}},
    };
    auto it = table.find(u);
    if (it == table.end())
        return false;
    family = it->second.first;
    factor = it->second.second;
    return true;
}

json::Value stringArray(const std::vector<std::string>& v)
{
    json::Value a = json::Value::array();
    for (const auto& s : v)
        a.push(s);
    return a;
}

void setIf(json::Value& v, const char* key, const std::string& value, const std::string& fallback = {})
{
    if (value != fallback)
        v.set(key, value);
}

double finiteOr(double v, double fallback)
{
    return std::isfinite(v) ? v : fallback;
}
} // namespace

// ---------------------------------------------------------------------------
// Dates and times
// ---------------------------------------------------------------------------
std::optional<double> parseTimeOfDay(std::string_view textValue)
{
    std::string t = text::toUpper(text::trim(textValue));
    if (t.empty())
        return std::nullopt;
    std::string suffix;
    for (const char* s : {"AM", "PM"})
    {
        if (t.size() >= 2 && t.compare(t.size() - 2, 2, s) == 0)
        {
            suffix = s;
            t = text::trim(t.substr(0, t.size() - 2));
            break;
        }
    }
    if (t.empty())
        return std::nullopt;
    double seconds = 0;
    if (t.find(':') != std::string::npos)
    {
        const auto parts = splitAny(t, ":");
        if (parts.size() < 2 || parts.size() > 3)
            return std::nullopt;
        double v[3] = {0, 0, 0};
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            auto d = text::toDouble(parts[i]);
            if (!d || *d < 0)
                return std::nullopt;
            v[i] = *d;
        }
        if (v[1] >= 60 || v[2] >= 61)
            return std::nullopt;
        seconds = v[0] * 3600 + v[1] * 60 + v[2];
    }
    else
    {
        auto d = text::toDouble(t);
        if (!d || *d < 0)
            return std::nullopt;
        seconds = *d * 3600;
    }
    const double hour = std::floor(seconds / 3600.0);
    if (suffix == "PM" && hour < 12)
        seconds += 12 * 3600.0;
    if (suffix == "AM" && hour == 12)
        seconds -= 12 * 3600.0;
    if (seconds > 86400.0)
        return std::nullopt;
    return seconds;
}

std::optional<double> parseDateTime(std::string_view textValue, DateOrder order)
{
    std::string t = text::trim(textValue);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        t = text::trim(t.substr(1, t.size() - 2));
    if (t.empty())
        return std::nullopt;
    // ISO 8601: a 'T' between the date and the time.
    const auto tpos = t.find_first_of("Tt");
    if (tpos != std::string::npos && tpos >= 8 && tpos + 1 < t.size() &&
        std::isdigit(static_cast<unsigned char>(t[tpos - 1])) &&
        std::isdigit(static_cast<unsigned char>(t[tpos + 1])))
        t[tpos] = ' ';
    std::string datePart, timePart;
    const auto space = t.find_first_of(" \t,");
    if (space == std::string::npos)
    {
        datePart = t;
    }
    else
    {
        datePart = t.substr(0, space);
        timePart = text::trim(t.substr(space + 1));
    }
    // Time zone designators are ignored: all times are wall-clock times.
    if (!timePart.empty())
    {
        if (timePart.back() == 'Z' || timePart.back() == 'z')
            timePart.pop_back();
        const auto colon = timePart.find(':');
        const auto zone = timePart.find_first_of("+-", colon == std::string::npos ? 0 : colon);
        if (colon != std::string::npos && zone != std::string::npos)
            timePart = text::trim(timePart.substr(0, zone));
    }
    const auto days = parseDate(datePart, order);
    if (!days)
        return std::nullopt;
    double seconds = 0;
    if (!timePart.empty())
    {
        auto tod = parseTimeOfDay(timePart);
        if (!tod)
            return std::nullopt;
        seconds = *tod;
    }
    return static_cast<double>(*days) * 86400.0 + seconds;
}

std::string formatDateTime(double epochSeconds)
{
    const double rounded = std::round(epochSeconds);
    const long long days = static_cast<long long>(std::floor(rounded / 86400.0));
    long long rest = static_cast<long long>(rounded) - days * 86400;
    int y = 0, m = 0, d = 0;
    civilFromDays(days, y, m, d);
    const int hh = static_cast<int>(rest / 3600);
    rest -= hh * 3600LL;
    const int mm = static_cast<int>(rest / 60);
    const int ss = static_cast<int>(rest - mm * 60);
    char buf[48];
    if (ss)
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, hh, mm, ss);
    else
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", y, m, d, hh, mm);
    return buf;
}

TimeBase timeBaseFor(const Model* model, const ResultSet* results, const std::string& simulationStart)
{
    TimeBase b;
    if (!text::trim(simulationStart).empty())
    {
        if (auto t = parseDateTime(simulationStart))
        {
            b.startEpoch = *t;
            b.startClock = std::fmod(*t, 86400.0);
            if (b.startClock < 0)
                b.startClock += 86400.0;
            return b;
        }
    }
    if (model && model->dialect() == Dialect::Swmm)
    {
        const auto& doc = model->document();
        long long days = 0;
        auto d = doc.keyValue("OPTIONS", "START_DATE");
        if (d && parseSwmmDate(*d, days))
        {
            double tod = 0;
            if (auto t = doc.keyValue("OPTIONS", "START_TIME"))
                tod = parseTimeOfDay(*t).value_or(0.0);
            b.startEpoch = static_cast<double>(days) * 86400.0 + tod;
            b.startClock = tod;
            return b;
        }
    }
    if (results)
    {
        auto it = results->metadata.find("startDate");
        if (it != results->metadata.end())
        {
            if (auto ole = text::toDouble(it->second))
            {
                // Days since 1899-12-30 (the SWMM output file convention).
                const double epoch = std::round((*ole - 25569.0) * 86400.0);
                b.startEpoch = epoch;
                b.startClock = std::fmod(epoch, 86400.0);
                if (b.startClock < 0)
                    b.startClock += 86400.0;
                return b;
            }
        }
    }
    if (model && model->dialect() != Dialect::Swmm)
    {
        if (auto v = model->document().keyValue("TIMES", "START CLOCKTIME"))
            b.startClock = parseTimeOfDay(*v).value_or(0.0);
    }
    return b;
}

std::vector<double> simulationTimes(const ObservedSeries& series, const TimeBase& base)
{
    if (!series.calendar)
        return series.times;
    double start = 0;
    if (base.startEpoch)
    {
        start = *base.startEpoch;
    }
    else
    {
        // Only a clock time is known (EPANET): the simulation starts on the
        // day of the first observation.
        double first = kNaN;
        for (double t : series.times)
        {
            if (std::isfinite(t))
            {
                first = t;
                break;
            }
        }
        if (std::isfinite(first))
            start = std::floor(first / 86400.0) * 86400.0 + base.startClock;
    }
    std::vector<double> out;
    out.reserve(series.times.size());
    for (double t : series.times)
        out.push_back(t - start);
    return out;
}

// ---------------------------------------------------------------------------
// Observed data
// ---------------------------------------------------------------------------
std::string ObservedSeries::key() const
{
    return table + ":" + element + ":" + variable;
}

ObservedSource ObservedSource::fromJson(const json::Value& v)
{
    ObservedSource s;
    s.file = v.get("file").asString();
    s.format = v.get("format").asString("auto");
    s.timeColumn = v.get("timeColumn").asString();
    s.timeFormat = v.get("timeFormat").asString("auto");
    s.timeUnit = v.get("timeUnit").asString("hours");
    s.dateOrder = v.get("dateOrder").asString("MDY");
    s.noData = v.get("noData").asString();
    if (v.get("noData").isNumber())
        s.noData = text::formatDouble(v.get("noData").asNumber());
    s.table = v.get("table").asString();
    s.variable = v.get("variable").asString();
    s.unit = v.get("unit").asString();
    for (const auto& l : v.get("links").items())
    {
        ObservedLink k;
        k.column = l.get("column").isNumber() ? text::formatDouble(l.get("column").asNumber())
                                              : l.get("column").asString();
        k.table = l.get("table").asString();
        k.element = l.get("element").asString();
        k.variable = l.get("variable").asString();
        k.unit = l.get("unit").asString();
        k.weight = l.get("weight").asNumber(1.0);
        k.enabled = l.get("enabled").asBool(true);
        s.links.push_back(k);
    }
    return s;
}

json::Value ObservedSource::toJson() const
{
    json::Value v = json::Value::object();
    v.set("file", file);
    v.set("format", format);
    setIf(v, "timeColumn", timeColumn);
    setIf(v, "timeFormat", timeFormat, "auto");
    setIf(v, "timeUnit", timeUnit, "hours");
    setIf(v, "dateOrder", dateOrder, "MDY");
    setIf(v, "noData", noData);
    setIf(v, "table", table);
    setIf(v, "variable", variable);
    setIf(v, "unit", unit);
    if (!links.empty())
    {
        json::Value a = json::Value::array();
        for (const auto& l : links)
        {
            json::Value o = json::Value::object();
            o.set("column", l.column);
            o.set("table", l.table);
            o.set("element", l.element);
            o.set("variable", l.variable);
            setIf(o, "unit", l.unit);
            if (l.weight != 1.0)
                o.set("weight", l.weight);
            if (!l.enabled)
                o.set("enabled", false);
            a.push(o);
        }
        v.set("links", a);
    }
    return v;
}

std::string ObservedSource::resolvedFormat(std::string_view content) const
{
    const std::string f = text::toLower(format);
    if (f == "csv" || f == "epanet" || f == "swmm")
        return f;
    const std::string ext = text::toLower(std::filesystem::u8path(file).extension().u8string());
    if (ext != ".dat")
        return "csv";
    // SWMM calibration files name each location on a line of its own.
    std::istringstream in{std::string(content)};
    std::string line;
    while (std::getline(in, line))
    {
        if (text::splitInpLine(line).tokens.size() == 1)
            return "swmm";
    }
    return "epanet";
}

DelimitedTable readDelimited(std::string_view textValue)
{
    DelimitedTable out;
    std::string body(textValue);
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF)
        body.erase(0, 3);
    std::vector<std::string> lines;
    {
        std::istringstream in(body);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const std::string t = text::trim(line);
            if (t.empty() || t[0] == '#' || t[0] == ';')
                continue;
            lines.push_back(line);
        }
    }
    // Delimiter: tab, then semicolon, then comma when present on every sampled line; else blanks.
    const std::size_t sample = std::min<std::size_t>(lines.size(), 20);
    char delim = ' ';
    for (char c : {'\t', ';', ','})
    {
        bool everywhere = sample > 0;
        for (std::size_t i = 0; i < sample && everywhere; ++i)
            everywhere = lines[i].find(c) != std::string::npos;
        if (everywhere)
        {
            delim = c;
            break;
        }
    }
    out.delimiter = delim;
    auto split = [delim](const std::string& line) {
        std::vector<std::string> cells;
        if (delim == ' ')
        {
            for (auto& s : splitAny(line, " \t"))
                cells.push_back(text::unquote(s));
            return cells;
        }
        std::string cur;
        bool quoted = false;
        for (std::size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (c == '"')
            {
                if (quoted && i + 1 < line.size() && line[i + 1] == '"')
                {
                    cur += '"';
                    ++i;
                }
                else
                {
                    quoted = !quoted;
                }
            }
            else if (c == delim && !quoted)
            {
                cells.push_back(text::trim(cur));
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        cells.push_back(text::trim(cur));
        return cells;
    };
    std::vector<std::vector<std::string>> rows;
    for (const auto& l : lines)
        rows.push_back(split(l));
    if (rows.empty())
    {
        out.hasHeader = false;
        return out;
    }
    // The first row is a header unless most of its cells are numbers, dates or times.
    int dataLike = 0;
    for (const auto& c : rows[0])
    {
        if (parseNumber(c) || parseDateTime(c) || (c.find(':') != std::string::npos && parseTimeOfDay(c)))
            ++dataLike;
    }
    out.hasHeader = dataLike * 2 < static_cast<int>(rows[0].size());
    std::size_t columns = 0;
    for (const auto& r : rows)
        columns = std::max(columns, r.size());
    if (out.hasHeader)
    {
        out.header = rows[0];
        rows.erase(rows.begin());
    }
    for (std::size_t c = out.header.size(); c < columns; ++c)
        out.header.push_back("Column " + std::to_string(c + 1));
    for (auto& r : rows)
        r.resize(columns);
    out.rows = std::move(rows);
    return out;
}

std::vector<ObservedSeries> readObservedTable(std::string_view textValue, const ObservedSource& source,
                                              std::vector<std::string>* warnings)
{
    const DelimitedTable t = readDelimited(textValue);
    if (t.rows.empty())
        throw std::runtime_error("no data rows");
    auto findColumn = [&](const std::string& name) -> int {
        const std::string n = text::trim(name);
        if (n.empty())
            return -1;
        for (std::size_t c = 0; c < t.header.size(); ++c)
        {
            if (text::iequals(text::trim(t.header[c]), n))
                return static_cast<int>(c);
        }
        if (isDigits(n))
        {
            const int c = static_cast<int>(*text::toInt(n)) - 1;
            if (c >= 0 && c < static_cast<int>(t.header.size()))
                return c;
        }
        return -1;
    };
    int timeCol = 0;
    if (!source.timeColumn.empty())
    {
        timeCol = findColumn(source.timeColumn);
        if (timeCol < 0)
            throw std::runtime_error("no time column " + source.timeColumn);
    }
    const DateOrder order =
        text::iequals(source.dateOrder, "DMY") ? DateOrder::DayFirst : DateOrder::MonthFirst;
    const std::string sample = text::trim(t.rows[0][static_cast<std::size_t>(timeCol)]);
    std::string format = text::toLower(source.timeFormat);
    if (format != "datetime" && format != "elapsed")
    {
        if (parseNumber(sample) || (!parseDateTime(sample, order) && text::parseClockOrDuration(sample)))
            format = "elapsed";
        else
            format = "datetime";
    }
    // A date column followed by a separate time column.
    int clockCol = -1;
    if (format == "datetime" && sample.find(':') == std::string::npos &&
        timeCol + 1 < static_cast<int>(t.header.size()))
    {
        const std::string next = text::trim(t.rows[0][static_cast<std::size_t>(timeCol + 1)]);
        if (next.find(':') != std::string::npos && parseTimeOfDay(next))
            clockCol = timeCol + 1;
    }
    const double unitSeconds = elapsedUnitSeconds(source.timeUnit);

    std::vector<double> times;
    std::vector<int> rowOf;
    int badTimes = 0;
    for (std::size_t r = 0; r < t.rows.size(); ++r)
    {
        const auto& row = t.rows[r];
        std::optional<double> s;
        const std::string cell = text::trim(row[static_cast<std::size_t>(timeCol)]);
        if (format == "elapsed")
        {
            if (auto n = parseNumber(cell))
                s = *n * unitSeconds;
            else
                s = text::parseClockOrDuration(cell);
        }
        else
        {
            std::string dt = cell;
            if (clockCol >= 0)
                dt += " " + text::trim(row[static_cast<std::size_t>(clockCol)]);
            s = parseDateTime(dt, order);
        }
        if (!s)
        {
            if (badTimes++ < 3)
                warn(warnings, source.file + ": row " + std::to_string(r + 1 + (t.hasHeader ? 1 : 0)) +
                                   ": cannot read the time \"" + cell + "\"");
            continue;
        }
        times.push_back(*s);
        rowOf.push_back(static_cast<int>(r));
    }
    if (badTimes > 3)
        warn(warnings, source.file + ": " + std::to_string(badTimes) +
                           " rows without a readable time were skipped");

    std::vector<ObservedLink> links = source.links;
    if (links.empty())
    {
        for (std::size_t c = 0; c < t.header.size(); ++c)
        {
            if (static_cast<int>(c) == timeCol || static_cast<int>(c) == clockCol)
                continue;
            ObservedLink l;
            l.column = t.header[c];
            l.unit = source.unit;
            l.table = source.table;
            l.variable = source.variable;
            links.push_back(l);
        }
    }
    std::vector<ObservedSeries> out;
    for (const auto& l : links)
    {
        if (!l.enabled)
            continue;
        const int c = findColumn(l.column);
        if (c < 0)
        {
            warn(warnings, source.file + ": no column " + l.column);
            continue;
        }
        ObservedSeries s;
        s.table = l.table;
        s.element = l.element;
        s.variable = l.variable;
        s.unit = l.unit;
        s.weight = l.weight;
        s.calendar = format == "datetime";
        s.source = source.file;
        s.name = t.header[static_cast<std::size_t>(c)];
        int badValues = 0;
        for (std::size_t i = 0; i < times.size(); ++i)
        {
            const std::string& cell = t.rows[static_cast<std::size_t>(rowOf[i])][static_cast<std::size_t>(c)];
            double v = kNaN;
            if (!isMissing(cell, source.noData))
            {
                if (auto n = parseNumber(cell))
                    v = *n;
                else
                    ++badValues;
            }
            s.times.push_back(times[i]);
            s.values.push_back(v);
        }
        if (badValues)
            warn(warnings, source.file + ": " + std::to_string(badValues) + " values in column " + s.name +
                               " are not numbers and were treated as missing");
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<ObservedSeries> readCalibrationFile(std::string_view textValue, const ObservedSource& source,
                                                std::vector<std::string>* warnings)
{
    const std::string style = source.resolvedFormat(textValue) == "swmm" ? "swmm" : "epanet";
    std::vector<ObservedSeries> out;
    std::map<std::string, std::size_t> index;
    ObservedSeries* current = nullptr;
    auto seriesFor = [&](const std::string& location) -> ObservedSeries* {
        auto it = index.find(location);
        if (it != index.end())
            return &out[it->second];
        ObservedSeries s;
        s.element = location;
        s.table = source.table;
        s.variable = source.variable;
        s.unit = source.unit;
        s.source = source.file;
        index[location] = out.size();
        out.push_back(s);
        return &out.back();
    };
    std::istringstream in{std::string(textValue)};
    std::string line;
    int lineNo = 0, bad = 0;
    auto badLine = [&](const std::string& why) {
        if (bad++ < 3)
            warn(warnings, source.file + ": line " + std::to_string(lineNo) + ": " + why);
    };
    while (std::getline(in, line))
    {
        ++lineNo;
        const auto tokens = text::splitInpLine(line).tokens;
        if (tokens.empty())
            continue;
        std::optional<double> time;
        std::string valueText;
        bool calendar = false;
        if (style == "epanet")
        {
            if (tokens.size() >= 3)
            {
                current = seriesFor(text::unquote(tokens[0]));
                time = text::parseClockOrDuration(tokens[1]);
                valueText = tokens[2];
            }
            else if (tokens.size() == 2)
            {
                time = text::parseClockOrDuration(tokens[0]);
                valueText = tokens[1];
            }
            else
            {
                badLine("expected a location, a time and a value");
                continue;
            }
        }
        else
        {
            if (tokens.size() == 1)
            {
                current = seriesFor(text::unquote(tokens[0]));
                continue;
            }
            if (tokens.size() >= 3)
            {
                if (tokens[0].find_first_of("/-") != std::string::npos)
                {
                    calendar = true;
                    time = parseDateTime(tokens[0] + " " + tokens[1]);
                }
                else if (auto days = text::toDouble(tokens[0]))
                {
                    auto clock = text::parseClockOrDuration(tokens[1]);
                    if (clock)
                        time = *days * 86400.0 + *clock;
                }
                valueText = tokens[2];
            }
            else
            {
                time = text::parseClockOrDuration(tokens[0]);
                valueText = tokens[1];
            }
        }
        if (!current)
        {
            badLine("a measurement before the first location");
            continue;
        }
        if (!time)
        {
            badLine("cannot read the time");
            continue;
        }
        if (current->times.empty())
            current->calendar = calendar;
        else if (current->calendar != calendar)
        {
            badLine("dates and elapsed times are mixed");
            continue;
        }
        double v = kNaN;
        if (!isMissing(valueText, source.noData))
        {
            if (auto n = parseNumber(valueText))
                v = *n;
            else
                badLine("the value is not a number");
        }
        current->times.push_back(*time);
        current->values.push_back(v);
    }
    if (bad > 3)
        warn(warnings, source.file + ": " + std::to_string(bad) + " lines could not be read");
    // Per-location settings from the links.
    std::vector<ObservedSeries> result;
    for (auto& s : out)
    {
        const std::string location = s.element;
        s.name = location;
        bool keep = true;
        for (const auto& l : source.links)
        {
            if (l.column != location)
                continue;
            keep = l.enabled;
            if (!l.element.empty())
                s.element = l.element;
            if (!l.table.empty())
                s.table = l.table;
            if (!l.variable.empty())
                s.variable = l.variable;
            if (!l.unit.empty())
                s.unit = l.unit;
            s.weight = l.weight;
        }
        if (keep)
            result.push_back(std::move(s));
    }
    return result;
}

ObservedLink guessLink(const std::string& header, const Model& model)
{
    ObservedLink l;
    l.column = header;
    const std::string h = text::trim(header);
    auto categoryOf = [&](const std::string& id, bool ignoreCase) -> Category {
        if (!ignoreCase)
        {
            if (model.findNode(id))
                return Category::Node;
            if (model.findLink(id))
                return Category::Link;
            if (model.findArea(id))
                return Category::Area;
            return Category::None;
        }
        for (const auto& n : model.nodes())
            if (text::iequals(n.id, id))
                return Category::Node;
        for (const auto& k : model.links())
            if (text::iequals(k.id, id))
                return Category::Link;
        for (const auto& a : model.areas())
            if (text::iequals(a.id, id))
                return Category::Area;
        return Category::None;
    };
    auto canonicalId = [&](const std::string& id, Category c) {
        if (c == Category::Node)
            for (const auto& n : model.nodes())
                if (text::iequals(n.id, id))
                    return n.id;
        if (c == Category::Link)
            for (const auto& k : model.links())
                if (text::iequals(k.id, id))
                    return k.id;
        if (c == Category::Area)
            for (const auto& a : model.areas())
                if (text::iequals(a.id, id))
                    return a.id;
        return id;
    };
    std::vector<std::string> words;
    Category cat = categoryOf(h, false);
    if (cat != Category::None)
    {
        l.element = h;
    }
    else
    {
        for (const char* seps : {" \t:|/()[],", " \t:|/()[],_-."})
        {
            words = splitAny(h, seps);
            for (bool ignoreCase : {false, true})
            {
                for (std::size_t i = 0; i < words.size() && l.element.empty(); ++i)
                {
                    const Category c = categoryOf(words[i], ignoreCase);
                    if (c != Category::None)
                    {
                        cat = c;
                        l.element = canonicalId(words[i], c);
                        words.erase(words.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                }
            }
            if (!l.element.empty())
                break;
        }
    }
    l.table = tableOf(cat);
    for (const auto& w : words)
    {
        if (l.variable.empty())
            l.variable = variableFromWord(w, model.dialect(), l.table);
        int family = 0;
        double factor = 0;
        if (l.unit.empty() && unitToBase(unitKey(w), family, factor))
            l.unit = w;
    }
    if (l.variable.empty() && !l.table.empty())
        l.variable = defaultVariable(model.dialect(), l.table);
    return l;
}

std::string defaultVariable(Dialect dialect, const std::string& table)
{
    if (table == "node")
        return dialect == Dialect::Swmm ? "Depth" : "Pressure";
    if (table == "link")
        return "Flow";
    if (table == "subcatchment")
        return "Runoff";
    return {};
}

std::vector<ObservedSeries> loadObservedSource(const ObservedSource& source,
                                               const std::filesystem::path& baseDir, const Model* model,
                                               std::vector<std::string>* warnings)
{
    std::filesystem::path p = std::filesystem::u8path(source.file);
    if (p.is_relative() && !baseDir.empty())
        p = baseDir / p;
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + p.u8string());
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string content = buf.str();
    const std::string format = source.resolvedFormat(content);
    auto series = format == "csv" ? readObservedTable(content, source, warnings)
                                  : readCalibrationFile(content, source, warnings);
    if (model)
    {
        for (auto& s : series)
        {
            if (!s.element.empty() && !s.table.empty() && !s.variable.empty())
                continue;
            const ObservedLink g = guessLink(s.element.empty() ? s.name : s.element, *model);
            if (s.element.empty())
                s.element = g.element;
            if (s.table.empty())
            {
                s.table = g.table;
                if (s.table.empty())
                {
                    if (model->findNode(s.element))
                        s.table = "node";
                    else if (model->findLink(s.element))
                        s.table = "link";
                    else if (model->findArea(s.element))
                        s.table = "subcatchment";
                }
            }
            if (s.variable.empty())
                s.variable = g.variable.empty() ? defaultVariable(model->dialect(), s.table) : g.variable;
            if (s.unit.empty())
                s.unit = g.unit;
        }
    }
    for (auto& s : series)
    {
        if (s.name.empty() || s.name == s.element)
            s.name = s.element + (s.variable.empty() ? std::string() : " " + s.variable);
    }
    return series;
}

std::vector<ObservedSeries> loadObserved(const CalibrationConfig& config,
                                         const std::filesystem::path& baseDir, const Model* model,
                                         std::vector<std::string>* warnings)
{
    std::vector<ObservedSeries> out;
    for (const auto& src : config.observed)
    {
        try
        {
            auto s = loadObservedSource(src, baseDir, model, warnings);
            out.insert(out.end(), std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
        }
        catch (const std::exception& e)
        {
            warn(warnings, src.file + ": " + e.what());
        }
    }
    return out;
}

std::optional<double> unitFactor(std::string_view from, std::string_view to)
{
    const std::string a = unitKey(from), b = unitKey(to);
    if (a.empty() || b.empty() || a == b)
        return 1.0;
    int fa = 0, fb = 0;
    double ka = 0, kb = 0;
    if (!unitToBase(a, fa, ka) || !unitToBase(b, fb, kb) || fa != fb)
        return std::nullopt;
    return ka / kb;
}

bool isFlowVariable(std::string_view variable)
{
    const std::string v = text::toLower(variable);
    return v.find("flow") != std::string::npos || v.find("runoff") != std::string::npos ||
           v.find("demand") != std::string::npos || v.find("flooding") != std::string::npos;
}

// ---------------------------------------------------------------------------
// Alignment and metrics
// ---------------------------------------------------------------------------
std::optional<SimulatedSeries> simulatedSeries(const ResultSet& results, const std::string& table,
                                               const std::string& element, const std::string& variable,
                                               std::string* error)
{
    auto fail = [&](const std::string& m) -> std::optional<SimulatedSeries> {
        if (error)
            *error = m;
        return std::nullopt;
    };
    if (table.empty() || element.empty() || variable.empty())
        return fail("the series is not linked to an element and a result");
    const ResultTable* t = results.table(table);
    if (!t)
        return fail("the results have no " + table + " table");
    const int e = t->elementIndex(element);
    if (e < 0)
        return fail("no results for " + table + " " + element);
    int v = -1;
    for (int i = 0; i < t->variableCount() && v < 0; ++i)
    {
        if (text::iequals(t->variables[static_cast<std::size_t>(i)].name, variable))
            v = i;
    }
    // "Inflow" finds "Total Inflow" before "Lateral Inflow".
    const std::string want = text::toLower(variable);
    for (const char* prefix : {"total ", ""})
    {
        for (int i = 0; i < t->variableCount() && v < 0; ++i)
        {
            const std::string n = text::toLower(t->variables[static_cast<std::size_t>(i)].name);
            if (n.size() > want.size() && n.compare(n.size() - want.size(), want.size(), want) == 0 &&
                text::istartsWith(n, prefix))
                v = i;
        }
    }
    if (v < 0)
        return fail("the results have no variable " + variable + " for " + table + "s");
    SimulatedSeries s;
    s.times = results.times;
    s.unit = t->variables[static_cast<std::size_t>(v)].unit;
    s.variable = t->variables[static_cast<std::size_t>(v)].name;
    s.values.reserve(results.times.size());
    for (int p = 0; p < results.periodCount(); ++p)
        s.values.push_back(t->value(p, v, e));
    return s;
}

double interpolate(const std::vector<double>& times, const std::vector<double>& values, double t)
{
    const std::size_t n = std::min(times.size(), values.size());
    if (n == 0 || !std::isfinite(t))
        return kNaN;
    const double eps = 1e-6;
    if (t < times[0] - eps || t > times[n - 1] + eps)
        return kNaN;
    if (n == 1)
        return values[0];
    const auto it = std::lower_bound(times.begin(), times.begin() + static_cast<std::ptrdiff_t>(n), t);
    std::size_t hi = static_cast<std::size_t>(it - times.begin());
    if (hi >= n)
        return values[n - 1];
    if (std::fabs(times[hi] - t) <= eps || hi == 0)
        return values[hi];
    const std::size_t lo = hi - 1;
    const double span = times[hi] - times[lo];
    if (span <= 0)
        return values[hi];
    const double w = (t - times[lo]) / span;
    return values[lo] + w * (values[hi] - values[lo]);
}

AlignedSeries align(const SimulatedSeries& simulated, const std::vector<double>& obsTimes,
                    const std::vector<double>& obsValues, double obsScale)
{
    AlignedSeries a;
    for (std::size_t i = 0; i < obsTimes.size() && i < obsValues.size(); ++i)
    {
        if (!std::isfinite(obsValues[i]))
        {
            ++a.missing;
            continue;
        }
        const double s = interpolate(simulated.times, simulated.values, obsTimes[i]);
        if (!std::isfinite(s))
        {
            ++a.outside;
            continue;
        }
        a.times.push_back(obsTimes[i]);
        a.observed.push_back(obsValues[i] * obsScale);
        a.simulated.push_back(s);
    }
    return a;
}

FitMetrics computeMetrics(const AlignedSeries& a)
{
    FitMetrics m;
    const std::size_t n = std::min(a.observed.size(), a.simulated.size());
    m.count = static_cast<int>(n);
    if (n == 0)
    {
        m.nse = m.kge = m.pbias = m.rmse = m.mae = m.r2 = kNaN;
        m.peakError = m.peakErrorPct = m.peakTiming = m.volumeError = kNaN;
        m.meanObserved = m.meanSimulated = kNaN;
        return m;
    }
    double so = 0, ss = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        so += a.observed[i];
        ss += a.simulated[i];
    }
    const double mo = so / n, ms = ss / n;
    double sse = 0, sae = 0, varO = 0, varS = 0, cov = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double o = a.observed[i], s = a.simulated[i];
        sse += (s - o) * (s - o);
        sae += std::fabs(s - o);
        varO += (o - mo) * (o - mo);
        varS += (s - ms) * (s - ms);
        cov += (o - mo) * (s - ms);
    }
    m.meanObserved = mo;
    m.meanSimulated = ms;
    m.rmse = std::sqrt(sse / n);
    m.mae = sae / n;
    m.nse = varO > 0 ? 1.0 - sse / varO : kNaN;
    m.pbias = so != 0 ? 100.0 * (ss - so) / so : kNaN;
    const double r = (varO > 0 && varS > 0) ? cov / std::sqrt(varO * varS) : kNaN;
    m.r2 = std::isfinite(r) ? r * r : kNaN;
    // KGE (Kling et al., 2012): correlation, bias ratio and ratio of the coefficients of variation.
    if (std::isfinite(r) && mo != 0 && ms != 0)
    {
        const double sdO = std::sqrt(varO / n), sdS = std::sqrt(varS / n);
        const double beta = ms / mo;
        const double gamma = (sdS / ms) / (sdO / mo);
        m.kge = 1.0 - std::sqrt((r - 1) * (r - 1) + (beta - 1) * (beta - 1) + (gamma - 1) * (gamma - 1));
    }
    else
    {
        m.kge = kNaN;
    }
    std::size_t io = 0, is = 0;
    for (std::size_t i = 1; i < n; ++i)
    {
        if (a.observed[i] > a.observed[io])
            io = i;
        if (a.simulated[i] > a.simulated[is])
            is = i;
    }
    m.peakError = a.simulated[is] - a.observed[io];
    m.peakErrorPct = a.observed[io] != 0 ? 100.0 * m.peakError / std::fabs(a.observed[io]) : kNaN;
    m.peakTiming = (is < a.times.size() && io < a.times.size()) ? a.times[is] - a.times[io] : kNaN;
    if (n >= 2 && a.times.size() >= n)
    {
        double vo = 0, vs = 0;
        for (std::size_t i = 0; i + 1 < n; ++i)
        {
            const double dt = a.times[i + 1] - a.times[i];
            vo += 0.5 * (a.observed[i] + a.observed[i + 1]) * dt;
            vs += 0.5 * (a.simulated[i] + a.simulated[i + 1]) * dt;
        }
        m.volumeError = vo != 0 ? 100.0 * (vs - vo) / vo : kNaN;
    }
    else
    {
        m.volumeError = kNaN;
    }
    return m;
}

const std::vector<Metric>& allMetrics()
{
    static const std::vector<Metric> all = {Metric::Nse,       Metric::Kge,        Metric::Pbias,
                                            Metric::Rmse,      Metric::Mae,        Metric::R2,
                                            Metric::PeakError, Metric::PeakTiming, Metric::VolumeError};
    return all;
}

const char* metricName(Metric m)
{
    switch (m)
    {
    case Metric::Nse:
        return "NSE";
    case Metric::Kge:
        return "KGE";
    case Metric::Pbias:
        return "PBIAS";
    case Metric::Rmse:
        return "RMSE";
    case Metric::Mae:
        return "MAE";
    case Metric::R2:
        return "R2";
    case Metric::PeakError:
        return "PeakError";
    case Metric::PeakTiming:
        return "PeakTiming";
    case Metric::VolumeError:
        return "VolumeError";
    }
    return "?";
}

std::optional<Metric> metricFromName(std::string_view name)
{
    std::string n;
    for (char c : text::replaceAll(std::string(name), "\xC2\xB2", "2"))
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (n == "r2" || n == "rsquared")
        return Metric::R2;
    if (n == "peak" || n == "peakerror")
        return Metric::PeakError;
    if (n == "timing" || n == "peaktiming")
        return Metric::PeakTiming;
    if (n == "volume" || n == "volumeerror")
        return Metric::VolumeError;
    for (Metric m : allMetrics())
    {
        if (text::iequals(metricName(m), n))
            return m;
    }
    return std::nullopt;
}

double metricValue(Metric m, const FitMetrics& f)
{
    switch (m)
    {
    case Metric::Nse:
        return f.nse;
    case Metric::Kge:
        return f.kge;
    case Metric::Pbias:
        return f.pbias;
    case Metric::Rmse:
        return f.rmse;
    case Metric::Mae:
        return f.mae;
    case Metric::R2:
        return f.r2;
    case Metric::PeakError:
        return f.peakErrorPct;
    case Metric::PeakTiming:
        return f.peakTiming / 3600.0;
    case Metric::VolumeError:
        return f.volumeError;
    }
    return kNaN;
}

double metricLoss(Metric m, const FitMetrics& f)
{
    double v = kNaN;
    switch (m)
    {
    case Metric::Nse:
        v = 1.0 - f.nse;
        break;
    case Metric::Kge:
        v = 1.0 - f.kge;
        break;
    case Metric::R2:
        v = 1.0 - f.r2;
        break;
    case Metric::Pbias:
        v = std::fabs(f.pbias) / 100.0;
        break;
    case Metric::Rmse:
        v = f.rmse;
        break;
    case Metric::Mae:
        v = f.mae;
        break;
    case Metric::PeakError:
        v = std::fabs(f.peakErrorPct) / 100.0;
        break;
    case Metric::PeakTiming:
        v = std::fabs(f.peakTiming) / 3600.0;
        break;
    case Metric::VolumeError:
        v = std::fabs(f.volumeError) / 100.0;
        break;
    }
    return std::isfinite(v) ? v : kInf;
}

std::vector<SeriesFit> evaluateFit(const std::vector<ObservedSeries>& observed, const ResultSet& results,
                                   const TimeBase& base)
{
    std::vector<SeriesFit> fits;
    for (const auto& s : observed)
    {
        SeriesFit f;
        f.key = s.key();
        f.name = s.name;
        std::string error;
        const auto sim = simulatedSeries(results, s.table, s.element, s.variable, &error);
        if (!sim)
        {
            f.error = error;
            fits.push_back(std::move(f));
            continue;
        }
        f.unit = sim->unit;
        const auto factor = unitFactor(s.unit, sim->unit);
        if (!factor)
        {
            f.error = "cannot convert " + s.unit + " to " + sim->unit;
            fits.push_back(std::move(f));
            continue;
        }
        f.aligned = align(*sim, simulationTimes(s, base), s.values, *factor);
        if (f.aligned.observed.empty())
        {
            f.error = f.aligned.outside > 0 ? "no observation falls inside the simulated period"
                                            : "the series has no values";
            fits.push_back(std::move(f));
            continue;
        }
        f.metrics = computeMetrics(f.aligned);
        if (!isFlowVariable(sim->variable))
            f.metrics.volumeError = kNaN;
        f.ok = true;
        fits.push_back(std::move(f));
    }
    return fits;
}

// ---------------------------------------------------------------------------
// Calibration set-up
// ---------------------------------------------------------------------------
CalibrationParameter CalibrationParameter::fromJson(const json::Value& v)
{
    CalibrationParameter p;
    p.name = v.get("name").asString();
    p.section = text::toUpper(text::trim(v.get("section").asString()));
    if (p.section.size() > 2 && p.section.front() == '[' && p.section.back() == ']')
        p.section = p.section.substr(1, p.section.size() - 2);
    p.column = v.get("column").isNumber() ? text::formatDouble(v.get("column").asNumber())
                                          : v.get("column").asString();
    const auto& e = v.get("elements");
    if (e.isArray())
    {
        for (const auto& id : e.items())
            p.elements.push_back(id.asString());
    }
    else if (e.isString() && !text::iequals(e.asString(), "all") && !text::trim(e.asString()).empty())
    {
        p.elements = splitAny(e.asString(), " ,;\t");
    }
    p.mode = text::iequals(v.get("mode").asString("multiply"), "replace") ? Mode::Replace : Mode::Multiply;
    p.min = v.get("min").asNumber(p.mode == Mode::Multiply ? 0.5 : 0.0);
    p.max = v.get("max").asNumber(p.mode == Mode::Multiply ? 1.5 : 1.0);
    if (p.max < p.min)
        std::swap(p.min, p.max);
    return p;
}

json::Value CalibrationParameter::toJson() const
{
    json::Value v = json::Value::object();
    if (!name.empty())
        v.set("name", name);
    v.set("section", section);
    v.set("column", column);
    if (elements.empty())
        v.set("elements", "all");
    else
        v.set("elements", stringArray(elements));
    v.set("mode", mode == Mode::Multiply ? "multiply" : "replace");
    v.set("min", min);
    v.set("max", max);
    return v;
}

std::string CalibrationParameter::label() const
{
    if (!name.empty())
        return name;
    std::string l = section + " " + column;
    if (!elements.empty())
        l += " (" +
             (elements.size() <= 3 ? text::join(elements, ", ")
                                   : std::to_string(elements.size()) + " elements") +
             ")";
    return l;
}

CalibrationConfig CalibrationConfig::fromJson(const json::Value& v)
{
    CalibrationConfig c;
    if (!v.isObject())
        return c;
    for (const auto& s : v.get("observed").items())
        c.observed.push_back(ObservedSource::fromJson(s));
    c.simulationStart = v.get("simulationStart").asString();
    for (const auto& p : v.get("parameters").items())
        c.parameters.push_back(CalibrationParameter::fromJson(p));
    for (const auto& t : v.get("objective").items())
    {
        ObjectiveTerm term;
        const auto m = metricFromName(t.isString() ? t.asString() : t.get("metric").asString());
        if (!m)
            continue;
        term.metric = *m;
        term.weight = t.isObject() ? t.get("weight").asNumber(1.0) : 1.0;
        c.objective.push_back(term);
    }
    c.budget = static_cast<int>(v.get("budget").asNumber(100));
    c.jobs = static_cast<int>(v.get("jobs").asNumber(0));
    c.seed = static_cast<unsigned>(v.get("seed").asNumber(1));
    c.engine = v.get("engine").asString();
    return c;
}

json::Value CalibrationConfig::toJson() const
{
    json::Value v = json::Value::object();
    json::Value obs = json::Value::array();
    for (const auto& s : observed)
        obs.push(s.toJson());
    v.set("observed", obs);
    setIf(v, "simulationStart", simulationStart);
    json::Value params = json::Value::array();
    for (const auto& p : parameters)
        params.push(p.toJson());
    v.set("parameters", params);
    json::Value obj = json::Value::array();
    for (const auto& t : objective)
    {
        json::Value o = json::Value::object();
        o.set("metric", metricName(t.metric));
        o.set("weight", t.weight);
        obj.push(o);
    }
    v.set("objective", obj);
    v.set("budget", budget);
    if (jobs > 0)
        v.set("jobs", jobs);
    v.set("seed", seed);
    setIf(v, "engine", engine);
    return v;
}

double objectiveValue(const std::vector<ObjectiveTerm>& terms, const std::vector<ObservedSeries>& observed,
                      const std::vector<SeriesFit>& fits)
{
    std::vector<ObjectiveTerm> use = terms;
    if (use.empty())
        use.push_back({Metric::Nse, 1.0});
    double total = 0;
    int counted = 0;
    for (std::size_t i = 0; i < observed.size() && i < fits.size(); ++i)
    {
        const double w = observed[i].weight;
        if (!(w > 0))
            continue;
        if (!fits[i].ok)
            return kInf;
        // Terms that do not apply to a series (volume error of a pressure) are left out.
        int terms = 0;
        for (const auto& t : use)
        {
            if (!std::isfinite(metricValue(t.metric, fits[i].metrics)))
                continue;
            total += w * t.weight * metricLoss(t.metric, fits[i].metrics);
            ++terms;
        }
        if (terms == 0)
            return kInf;
        ++counted;
    }
    return counted ? finiteOr(total, kInf) : kInf;
}

int parameterColumn(const Model& model, const CalibrationParameter& p)
{
    const std::string c = text::trim(p.column);
    if (isDigits(c))
        return static_cast<int>(*text::toInt(c));
    const SectionDef* def = findSectionDef(model.dialect(), p.section);
    if (!def)
        return -1;
    for (std::size_t i = 0; i < def->columns.size(); ++i)
    {
        if (text::iequals(def->columns[i].name, c))
            return static_cast<int>(i);
    }
    return -1;
}

namespace
{
template <typename Fn>
int forEachParameterRow(const InpSection& section, const CalibrationParameter& p, int column, Fn&& fn)
{
    int count = 0;
    const auto rows = section.allRowTokens();
    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        const auto& tokens = rows[r];
        if (tokens.empty() || column >= static_cast<int>(tokens.size()))
            continue;
        if (!p.elements.empty() &&
            std::find(p.elements.begin(), p.elements.end(), text::unquote(tokens[0])) == p.elements.end())
            continue;
        const auto v = text::toDouble(tokens[static_cast<std::size_t>(column)]);
        if (!v)
            continue;
        fn(static_cast<int>(r), tokens, *v);
        ++count;
    }
    return count;
}
} // namespace

int parameterRowCount(const Model& model, const CalibrationParameter& p)
{
    const int col = parameterColumn(model, p);
    const InpSection* s = model.document().find(p.section);
    if (col < 1 || !s)
        return 0;
    return forEachParameterRow(*s, p, col, [](int, const std::vector<std::string>&, double) {});
}

double parameterInitialValue(const Model& model, const CalibrationParameter& p)
{
    if (p.mode == CalibrationParameter::Mode::Multiply)
        return std::clamp(1.0, p.min, p.max);
    const int col = parameterColumn(model, p);
    const InpSection* s = model.document().find(p.section);
    double sum = 0;
    int n = 0;
    if (col >= 1 && s)
    {
        forEachParameterRow(*s, p, col, [&](int, const std::vector<std::string>&, double v) {
            sum += v;
            ++n;
        });
    }
    return n ? std::clamp(sum / n, p.min, p.max) : 0.5 * (p.min + p.max);
}

bool applyParameters(Model& model, const std::vector<CalibrationParameter>& params,
                     const std::vector<double>& values, std::string* error)
{
    if (values.size() != params.size())
    {
        if (error)
            *error = "expected " + std::to_string(params.size()) + " parameter values";
        return false;
    }
    for (std::size_t i = 0; i < params.size(); ++i)
    {
        const auto& p = params[i];
        const int col = parameterColumn(model, p);
        InpSection* s = model.document().find(p.section);
        if (!s || col < 1)
        {
            if (error)
                *error = p.label() + ": " +
                         (!s ? "the model has no [" + p.section + "] section"
                             : "unknown column " + p.column);
            return false;
        }
        std::vector<std::pair<int, std::vector<std::string>>> changes;
        forEachParameterRow(*s, p, col, [&](int row, const std::vector<std::string>& tokens, double v) {
            auto t = tokens;
            const double nv = p.mode == CalibrationParameter::Mode::Multiply ? v * values[i] : values[i];
            t[static_cast<std::size_t>(col)] = text::formatDouble(nv, 6);
            changes.emplace_back(row, std::move(t));
        });
        if (changes.empty())
        {
            if (error)
                *error = p.label() + ": no rows with a number in that column";
            return false;
        }
        for (const auto& [row, tokens] : changes)
            s->setRowTokens(row, tokens);
    }
    model.rebuild();
    return true;
}

} // namespace mirab::core
