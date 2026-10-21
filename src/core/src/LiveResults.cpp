/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/LiveResults.cpp
 * Purpose: Encoding and decoding of the results streamed while a simulation runs.
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
#include "mirab/core/LiveResults.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace mirab::core::live
{

namespace
{
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool littleEndian()
{
    const std::uint32_t one = 1;
    unsigned char b = 0;
    std::memcpy(&b, &one, 1);
    return b == 1;
}

void swapFloats(unsigned char* bytes, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        unsigned char* p = bytes + i * 4;
        std::swap(p[0], p[3]);
        std::swap(p[1], p[2]);
    }
}

int decodeChar(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}
} // namespace

std::string base64Encode(const void* data, std::size_t size)
{
    const auto* in = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < size; i += 3)
    {
        const std::uint32_t n = (std::uint32_t(in[i]) << 16) | (std::uint32_t(in[i + 1]) << 8) | in[i + 2];
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (i < size)
    {
        std::uint32_t n = std::uint32_t(in[i]) << 16;
        if (i + 1 < size)
            n |= std::uint32_t(in[i + 1]) << 8;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < size ? kAlphabet[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool base64Decode(std::string_view text, std::vector<unsigned char>& out)
{
    out.clear();
    if (text.size() % 4 != 0)
        return false;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4)
    {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k)
        {
            const unsigned char c = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if (c == '=' && i + 4 == text.size() && k >= 2)
            {
                v[k] = 0;
                ++pad;
                continue;
            }
            if (pad)
                return false;
            v[k] = decodeChar(c);
            if (v[k] < 0)
                return false;
        }
        const std::uint32_t n = (std::uint32_t(v[0]) << 18) | (std::uint32_t(v[1]) << 12) |
                                (std::uint32_t(v[2]) << 6) | std::uint32_t(v[3]);
        out.push_back(static_cast<unsigned char>(n >> 16));
        if (pad < 2)
            out.push_back(static_cast<unsigned char>((n >> 8) & 0xFF));
        if (pad < 1)
            out.push_back(static_cast<unsigned char>(n & 0xFF));
    }
    return true;
}

bool sameLayout(const ResultSet& a, const ResultSet& b)
{
    if (a.tables.size() != b.tables.size())
        return false;
    for (std::size_t i = 0; i < a.tables.size(); ++i)
    {
        const auto& x = a.tables[i];
        const auto& y = b.tables[i];
        if (x.kind != y.kind || x.ids != y.ids || x.variables.size() != y.variables.size())
            return false;
        for (std::size_t v = 0; v < x.variables.size(); ++v)
        {
            if (x.variables[v].name != y.variables[v].name || x.variables[v].unit != y.variables[v].unit)
                return false;
        }
    }
    return true;
}

json::Value layoutPayload(const ResultSet& r, int layout)
{
    json::Value v = json::Value::object();
    v.set("layout", layout);
    v.set("engineId", r.engineId);
    v.set("engineName", r.engineName);
    v.set("title", r.title);
    v.set("flowUnits", r.flowUnits);
    v.set("unitSystem", r.unitSystem);
    v.set("reportStart", r.reportStart);
    v.set("reportStep", r.reportStep);
    json::Value tables = json::Value::array();
    for (const auto& t : r.tables)
    {
        json::Value tv = json::Value::object();
        tv.set("kind", t.kind);
        json::Value ids = json::Value::array();
        for (const auto& id : t.ids)
            ids.push(id);
        tv.set("ids", ids);
        json::Value vars = json::Value::array();
        for (const auto& var : t.variables)
        {
            json::Value vv = json::Value::object();
            vv.set("name", var.name);
            vv.set("unit", var.unit);
            vars.push(vv);
        }
        tv.set("variables", vars);
        tables.push(tv);
    }
    v.set("tables", tables);
    return v;
}

std::size_t frameSize(const ResultSet& layout)
{
    std::size_t n = 0;
    for (const auto& t : layout.tables)
        n += t.ids.size() * t.variables.size();
    return n;
}

std::string frameLine(const ResultSet& r, int slot, int layout, int period, double time)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(frameSize(r) * 4);
    for (const auto& t : r.tables)
    {
        const std::size_t block = t.ids.size() * t.variables.size();
        const std::size_t offset = static_cast<std::size_t>(slot) * block;
        if (block == 0)
            continue;
        if (offset + block > t.data.size())
            throw std::runtime_error("frame period is outside the results");
        const auto* p = reinterpret_cast<const unsigned char*>(t.data.data() + offset);
        bytes.insert(bytes.end(), p, p + block * 4);
    }
    if (!littleEndian())
        swapFloats(bytes.data(), bytes.size() / 4);
    json::Value head = json::Value::object();
    head.set("event", "frame");
    head.set("layout", layout);
    head.set("period", period);
    head.set("time", time);
    std::string line = head.dump();
    line.pop_back(); // '}'
    line += ",\"data\":\"";
    line += base64Encode(bytes.data(), bytes.size());
    line += "\"}";
    return line;
}

ResultSet parseLayout(const json::Value& v, int* layout)
{
    if (!v.isObject() || !v.get("tables").isArray())
        throw std::runtime_error("layout event without tables");
    ResultSet r;
    r.engineId = v.get("engineId").asString();
    r.engineName = v.get("engineName").asString();
    r.title = v.get("title").asString();
    r.flowUnits = v.get("flowUnits").asString();
    r.unitSystem = v.get("unitSystem").asString();
    r.reportStart = v.get("reportStart").asNumber();
    r.reportStep = v.get("reportStep").asNumber();
    for (const auto& tv : v.get("tables").items())
    {
        std::vector<std::string> ids;
        for (const auto& id : tv.get("ids").items())
            ids.push_back(id.asString());
        std::vector<Variable> vars;
        for (const auto& var : tv.get("variables").items())
            vars.push_back({var.get("name").asString(), var.get("unit").asString()});
        const std::string kind = tv.get("kind").asString();
        if (kind.empty() || r.table(kind))
            throw std::runtime_error("layout event with a missing or repeated table kind");
        r.defineTable(kind, std::move(ids), std::move(vars));
    }
    if (layout)
        *layout = static_cast<int>(v.get("layout").asNumber());
    return r;
}

Frame parseFrameLine(std::string_view line)
{
    // The data member comes last, so the small header parses on its own
    // and the large base64 block is decoded in place.
    static constexpr std::string_view kData = ",\"data\":\"";
    Frame f;
    json::Value head;
    std::string_view data;
    const std::size_t at = line.rfind(kData);
    const std::size_t end = line.rfind('"');
    if (at != std::string_view::npos && end != std::string_view::npos && end > at + kData.size() - 1)
    {
        std::string h(line.substr(0, at));
        h += '}';
        head = json::Value::parse(h);
        data = line.substr(at + kData.size(), end - at - kData.size());
    }
    else
    {
        head = json::Value::parse(line);
    }
    std::string owned;
    if (data.empty())
    {
        owned = head.get("data").asString();
        data = owned;
    }
    if (head.get("event").asString() != "frame")
        throw std::runtime_error("not a frame event");
    f.layout = static_cast<int>(head.get("layout").asNumber());
    f.period = static_cast<int>(head.get("period").asNumber());
    f.time = head.get("time").asNumber();
    std::vector<unsigned char> bytes;
    if (!base64Decode(data, bytes) || bytes.size() % 4 != 0)
        throw std::runtime_error("frame event with malformed data");
    if (!littleEndian())
        swapFloats(bytes.data(), bytes.size() / 4);
    f.values.resize(bytes.size() / 4);
    if (!bytes.empty())
        std::memcpy(f.values.data(), bytes.data(), bytes.size());
    return f;
}

int appendFrame(ResultSet& results, const Frame& frame)
{
    if (frame.values.size() != frameSize(results))
        return -1;
    // A repeated period carries corrected values (for example the final
    // frame of SWMM, read from its output file).
    const bool repeat = !results.times.empty() && std::abs(results.times.back() - frame.time) < 1e-6;
    const int p = repeat ? results.periodCount() - 1 : results.addPeriod(frame.time);
    std::size_t offset = 0;
    for (auto& t : results.tables)
    {
        const std::size_t block = t.ids.size() * t.variables.size();
        if (block == 0)
            continue;
        std::memcpy(t.data.data() + static_cast<std::size_t>(p) * block, frame.values.data() + offset,
                    block * sizeof(float));
        offset += block;
    }
    return p;
}

} // namespace mirab::core::live
