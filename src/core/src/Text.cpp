/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/src/Text.cpp
 * Purpose: Implementation of the locale-independent string helpers.
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
#include "anahita/core/Text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace anahita::text
{

namespace
{
bool isBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
} // namespace

std::string trim(std::string_view s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isBlank(s[b]))
        ++b;
    while (e > b && isBlank(s[e - 1]))
        --e;
    return std::string(s.substr(b, e - b));
}

std::string toUpper(std::string_view s)
{
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return r;
}

std::string toLower(std::string_view s)
{
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool istartsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

std::string join(const std::vector<std::string>& parts, std::string_view sep)
{
    std::string r;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
            r += sep;
        r += parts[i];
    }
    return r;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to)
{
    if (from.empty())
        return s;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

SplitLine splitInpLine(std::string_view line)
{
    SplitLine out;
    std::string current;
    bool inQuotes = false;
    bool inToken = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"')
        {
            inQuotes = !inQuotes;
            current += c;
            inToken = true;
            continue;
        }
        if (!inQuotes && c == ';')
        {
            out.hasComment = true;
            out.comment = std::string(line.substr(i + 1));
            if (!out.comment.empty() && out.comment.back() == '\r')
                out.comment.pop_back();
            break;
        }
        if (!inQuotes && isBlank(c))
        {
            if (inToken)
            {
                out.tokens.push_back(current);
                current.clear();
                inToken = false;
            }
            continue;
        }
        current += c;
        inToken = true;
    }
    if (inToken)
        out.tokens.push_back(current);
    return out;
}

std::string unquote(std::string_view token)
{
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        return std::string(token.substr(1, token.size() - 2));
    return std::string(token);
}

std::string quoteIfNeeded(std::string_view value)
{
    if (value.empty())
        return "\"\"";
    for (char c : value)
    {
        if (isBlank(c))
            return "\"" + std::string(value) + "\"";
    }
    return std::string(value);
}

std::optional<double> toDouble(std::string_view s)
{
    const std::string t = trim(s);
    if (t.empty())
        return std::nullopt;
    std::istringstream in(t);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail())
        return std::nullopt;
    in >> std::ws;
    if (!in.eof())
        return std::nullopt;
    if (!std::isfinite(v))
        return std::nullopt;
    return v;
}

std::optional<long long> toInt(std::string_view s)
{
    const std::string t = trim(s);
    if (t.empty())
        return std::nullopt;
    std::istringstream in(t);
    in.imbue(std::locale::classic());
    long long v = 0;
    in >> v;
    if (in.fail())
        return std::nullopt;
    in >> std::ws;
    if (!in.eof())
        return std::nullopt;
    return v;
}

std::string formatDouble(double value, int precision)
{
    if (!std::isfinite(value))
        return "0";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (std::fabs(value) < 1e15 && value == std::floor(value))
    {
        out << static_cast<long long>(value);
        return out.str();
    }
    out << std::setprecision(precision) << value;
    return out.str();
}

std::optional<double> parseClockOrDuration(std::string_view value, std::string_view unit)
{
    const std::string v = trim(value);
    if (v.empty())
        return std::nullopt;
    const std::string u = toUpper(trim(unit));
    if (v.find(':') != std::string::npos)
    {
        double parts[3] = {0, 0, 0};
        int n = 0;
        std::string cur;
        for (char c : v + ":")
        {
            if (c == ':')
            {
                if (n >= 3)
                    return std::nullopt;
                auto d = toDouble(cur);
                if (!d)
                    return std::nullopt;
                parts[n++] = *d;
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        double seconds = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
        if (u == "PM" && parts[0] < 12)
            seconds += 12 * 3600.0;
        if (u == "AM" && parts[0] == 12)
            seconds -= 12 * 3600.0;
        return seconds;
    }
    auto d = toDouble(v);
    if (!d)
        return std::nullopt;
    if (u.empty() || istartsWith(u, "HOUR"))
        return *d * 3600.0;
    if (istartsWith(u, "SEC"))
        return *d;
    if (istartsWith(u, "MIN"))
        return *d * 60.0;
    if (istartsWith(u, "DAY"))
        return *d * 86400.0;
    if (u == "AM")
        return std::fmod(*d, 12.0) * 3600.0;
    if (u == "PM")
        return (std::fmod(*d, 12.0) + 12.0) * 3600.0;
    return std::nullopt;
}

std::string formatDuration(double seconds)
{
    if (seconds < 0)
        seconds = 0;
    long long total = static_cast<long long>(std::llround(seconds));
    const long long days = total / 86400;
    total %= 86400;
    const long long hours = total / 3600;
    const long long minutes = (total % 3600) / 60;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (days > 0)
        out << days << "d ";
    out << hours << ':' << std::setw(2) << std::setfill('0') << minutes;
    return out.str();
}

} // namespace anahita::text
