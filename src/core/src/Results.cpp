/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/src/Results.cpp
 * Purpose: Results storage, the .anr file format and readers for EPANET and SWMM binary output.
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
#include "anahita/core/Results.h"

#include "anahita/core/Json.h"
#include "anahita/core/Text.h"
#include "anahita/core/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace anahita::core
{

namespace
{
constexpr char kMagic[16] = {'A', 'N', 'A', 'H', 'I', 'T', 'A', '-',
                             'R', 'E', 'S', 'U', 'L', 'T', 'S', '\0'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::int32_t kEpanetMagic = 516114521;
constexpr std::int32_t kSwmmMagic = 516114522;
} // namespace

int ResultTable::periodCount() const
{
    const std::size_t stride = ids.size() * variables.size();
    return stride == 0 ? 0 : static_cast<int>(data.size() / stride);
}

int ResultTable::variableIndex(std::string_view name) const
{
    for (std::size_t i = 0; i < variables.size(); ++i)
    {
        if (text::iequals(variables[i].name, name))
            return static_cast<int>(i);
    }
    return -1;
}

int ResultTable::elementIndex(std::string_view id) const
{
    if (indexed_ != ids.size())
    {
        index_.clear();
        index_.reserve(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i)
            index_.emplace(ids[i], static_cast<int>(i));
        indexed_ = ids.size();
    }
    const auto it = index_.find(std::string(id));
    return it == index_.end() ? -1 : it->second;
}

float ResultTable::value(int period, int variable, int element) const
{
    const std::size_t idx =
        (static_cast<std::size_t>(period) * variables.size() + static_cast<std::size_t>(variable)) *
            ids.size() +
        static_cast<std::size_t>(element);
    return idx < data.size() ? data[idx] : 0.0f;
}

void ResultTable::set(int period, int variable, int element, float v)
{
    const std::size_t idx =
        (static_cast<std::size_t>(period) * variables.size() + static_cast<std::size_t>(variable)) *
            ids.size() +
        static_cast<std::size_t>(element);
    if (idx < data.size())
        data[idx] = v;
}

std::vector<float> ResultTable::series(int variable, int element) const
{
    std::vector<float> s;
    const int n = periodCount();
    s.reserve(static_cast<std::size_t>(n));
    for (int p = 0; p < n; ++p)
        s.push_back(value(p, variable, element));
    return s;
}

std::vector<float> ResultTable::snapshot(int period, int variable) const
{
    std::vector<float> s(ids.size(), 0.0f);
    for (std::size_t e = 0; e < ids.size(); ++e)
        s[e] = value(period, variable, static_cast<int>(e));
    return s;
}

void ResultTable::resizePeriods(int periods)
{
    data.resize(static_cast<std::size_t>(periods) * ids.size() * variables.size(), 0.0f);
}

void ResultTable::appendVariables(const std::vector<Variable>& more)
{
    if (more.empty())
        return;
    const int periods = periodCount();
    const std::size_t oldVars = variables.size();
    const std::size_t newVars = oldVars + more.size();
    const std::size_t n = ids.size();
    std::vector<float> grown(static_cast<std::size_t>(periods) * newVars * n, 0.0f);
    for (int p = 0; p < periods; ++p)
    {
        for (std::size_t v = 0; v < oldVars; ++v)
        {
            for (std::size_t e = 0; e < n; ++e)
                grown[(static_cast<std::size_t>(p) * newVars + v) * n + e] =
                    data[(static_cast<std::size_t>(p) * oldVars + v) * n + e];
        }
    }
    data = std::move(grown);
    variables.insert(variables.end(), more.begin(), more.end());
}

ResultTable* ResultSet::table(std::string_view kind)
{
    for (auto& t : tables)
    {
        if (t.kind == kind)
            return &t;
    }
    return nullptr;
}

const ResultTable* ResultSet::table(std::string_view kind) const
{
    return const_cast<ResultSet*>(this)->table(kind);
}

ResultTable& ResultSet::defineTable(const std::string& kind, std::vector<std::string> ids,
                                   std::vector<Variable> variables)
{
    ResultTable* t = table(kind);
    if (!t)
    {
        tables.emplace_back();
        t = &tables.back();
    }
    t->kind = kind;
    t->ids = std::move(ids);
    t->variables = std::move(variables);
    t->data.clear();
    t->resizePeriods(periodCount());
    return *t;
}

int ResultSet::addPeriod(double seconds)
{
    times.push_back(seconds);
    for (auto& t : tables)
        t.resizePeriods(periodCount());
    return periodCount() - 1;
}

void ResultSet::save(const std::filesystem::path& path) const
{
    json::Value header = json::Value::object();
    header.set("engineId", engineId);
    header.set("engineName", engineName);
    header.set("title", title);
    header.set("flowUnits", flowUnits);
    header.set("unitSystem", unitSystem);
    header.set("reportStart", reportStart);
    header.set("reportStep", reportStep);
    json::Value t = json::Value::array();
    for (double v : times)
        t.push(v);
    header.set("times", t);
    json::Value meta = json::Value::object();
    for (const auto& [k, v] : metadata)
        meta.set(k, v);
    header.set("metadata", meta);
    json::Value msgs = json::Value::array();
    for (const auto& m : messages)
        msgs.push(m);
    header.set("messages", msgs);
    json::Value tabs = json::Value::array();
    for (const auto& tab : tables)
    {
        json::Value jt = json::Value::object();
        jt.set("kind", tab.kind);
        json::Value ids = json::Value::array();
        for (const auto& id : tab.ids)
            ids.push(id);
        jt.set("ids", ids);
        json::Value vars = json::Value::array();
        for (const auto& v : tab.variables)
        {
            json::Value jv = json::Value::object();
            jv.set("name", v.name);
            jv.set("unit", v.unit);
            vars.push(jv);
        }
        jt.set("variables", vars);
        tabs.push(jt);
    }
    header.set("tables", tabs);
    const std::string headerText = header.dump();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + path.u8string());
    out.write(kMagic, sizeof(kMagic));
    const std::uint32_t version = kFormatVersion;
    const auto headerLen = static_cast<std::uint32_t>(headerText.size());
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&headerLen), sizeof(headerLen));
    out.write(headerText.data(), static_cast<std::streamsize>(headerText.size()));
    for (const auto& tab : tables)
    {
        const std::size_t expected =
            static_cast<std::size_t>(periodCount()) * tab.ids.size() * tab.variables.size();
        std::vector<float> data = tab.data;
        data.resize(expected, 0.0f);
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size() * sizeof(float)));
    }
    if (!out)
        throw std::runtime_error("error while writing " + path.u8string());
}

ResultSet ResultSet::load(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.u8string());
    char magic[16] = {};
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
        throw std::runtime_error(path.u8string() + " is not an Anahita results file");
    std::uint32_t version = 0, headerLen = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&headerLen), sizeof(headerLen));
    if (!in || version != kFormatVersion || headerLen > (256u << 20))
        throw std::runtime_error("unsupported results file version in " + path.u8string());
    std::string headerText(headerLen, '\0');
    in.read(headerText.data(), headerLen);
    const json::Value h = json::Value::parse(headerText);

    ResultSet r;
    r.engineId = h.get("engineId").asString();
    r.engineName = h.get("engineName").asString();
    r.title = h.get("title").asString();
    r.flowUnits = h.get("flowUnits").asString();
    r.unitSystem = h.get("unitSystem").asString();
    r.reportStart = h.get("reportStart").asNumber();
    r.reportStep = h.get("reportStep").asNumber();
    for (const auto& t : h.get("times").items())
        r.times.push_back(t.asNumber());
    const auto& meta = h.get("metadata");
    for (std::size_t i = 0; i < meta.keys().size(); ++i)
        r.metadata[meta.keys()[i]] = meta[i].asString();
    for (const auto& m : h.get("messages").items())
        r.messages.push_back(m.asString());
    for (const auto& jt : h.get("tables").items())
    {
        ResultTable t;
        t.kind = jt.get("kind").asString();
        for (const auto& id : jt.get("ids").items())
            t.ids.push_back(id.asString());
        for (const auto& jv : jt.get("variables").items())
            t.variables.push_back({jv.get("name").asString(), jv.get("unit").asString()});
        t.resizePeriods(r.periodCount());
        in.read(reinterpret_cast<char*>(t.data.data()),
                static_cast<std::streamsize>(t.data.size() * sizeof(float)));
        if (!in)
            throw std::runtime_error("results file " + path.u8string() + " is truncated");
        r.tables.push_back(std::move(t));
    }
    return r;
}

namespace
{
class BinaryReader
{
public:
    explicit BinaryReader(const std::filesystem::path& path) : in_(path, std::ios::binary)
    {
        if (!in_)
            throw std::runtime_error("cannot open " + path.u8string());
        in_.seekg(0, std::ios::end);
        size_ = static_cast<std::int64_t>(in_.tellg());
        in_.seekg(0, std::ios::beg);
    }
    std::int64_t size() const { return size_; }
    void seek(std::int64_t pos) { in_.seekg(pos, std::ios::beg); }
    std::int64_t tell() { return static_cast<std::int64_t>(in_.tellg()); }
    std::int32_t i32()
    {
        std::int32_t v = 0;
        read(&v, sizeof(v));
        return v;
    }
    float f32()
    {
        float v = 0;
        read(&v, sizeof(v));
        return v;
    }
    double f64()
    {
        double v = 0;
        read(&v, sizeof(v));
        return v;
    }
    std::string fixedString(std::size_t n)
    {
        std::string s(n, '\0');
        read(s.data(), n);
        const auto end = s.find('\0');
        if (end != std::string::npos)
            s.resize(end);
        return text::trim(s);
    }
    void read(void* dst, std::size_t n)
    {
        in_.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
        if (!in_)
            throw std::runtime_error("unexpected end of binary output file");
    }
    void readFloats(std::vector<float>& v, std::size_t n)
    {
        v.resize(n);
        if (n)
            read(v.data(), n * sizeof(float));
    }

private:
    std::ifstream in_;
    std::int64_t size_ = 0;
};

const char* epanetFlowUnits(int code)
{
    static const char* names[] = {"CFS", "GPM", "MGD", "IMGD", "AFD", "LPS",
                                  "LPM", "MLD", "CMH", "CMD", "CMS"};
    return (code >= 0 && code <= 10) ? names[code] : "GPM";
}

const char* swmmFlowUnits(int code)
{
    static const char* names[] = {"CFS", "GPM", "MGD", "CMS", "LPS", "MLD"};
    return (code >= 0 && code <= 5) ? names[code] : "CFS";
}
} // namespace

ResultSet ResultSet::readEpanetOutput(const std::filesystem::path& path)
{
    BinaryReader in(path);
    if (in.size() < 15 * 4 + 28)
        throw std::runtime_error(path.u8string() + " is too small to be an EPANET output file");
    in.seek(in.size() - 4);
    if (in.i32() != kEpanetMagic)
        throw std::runtime_error(path.u8string() + " is incomplete or not an EPANET output file");
    in.seek(in.size() - 12);
    const int periods = in.i32();
    const int warning = in.i32();

    in.seek(0);
    std::int32_t prolog[15];
    for (auto& v : prolog)
        v = in.i32();
    if (prolog[0] != kEpanetMagic)
        throw std::runtime_error(path.u8string() + " is not an EPANET output file");
    const int nNodes = prolog[2], nTanks = prolog[3], nLinks = prolog[4], nPumps = prolog[5];
    const int qualFlag = prolog[7], flowCode = prolog[9], pressCode = prolog[10];
    const int rStart = prolog[12], rStep = prolog[13];

    ResultSet r;
    r.engineId = "epanet-binary";
    r.engineName = "EPANET binary output (version " + std::to_string(prolog[1]) + ")";
    r.title = in.fixedString(80);
    in.fixedString(80);
    in.fixedString(80);
    in.fixedString(260);
    in.fixedString(260);
    const std::string chem = in.fixedString(32);
    const std::string chemUnits = in.fixedString(32);
    std::vector<std::string> nodeIds, linkIds;
    for (int i = 0; i < nNodes; ++i)
        nodeIds.push_back(in.fixedString(32));
    for (int i = 0; i < nLinks; ++i)
        linkIds.push_back(in.fixedString(32));

    // Skip the remaining network data and the energy section.
    const std::int64_t prologBytes = 15 * 4 + 3 * 80 + 2 * 260 + 2 * 32 +
                                     32LL * (nNodes + nLinks) + 12LL * nLinks + 8LL * nTanks +
                                     4LL * nNodes + 8LL * nLinks;
    const std::int64_t energyBytes = 28LL * nPumps + 4;
    const std::int64_t periodBytes = 4LL * (4LL * nNodes + 8LL * nLinks);
    in.seek(prologBytes + energyBytes);

    r.flowUnits = epanetFlowUnits(flowCode);
    const auto units = ProjectUnits::fromFlowUnits(Dialect::Epanet, r.flowUnits,
                                                   pressCode == 1   ? "KPA"
                                                   : pressCode == 2 ? "METERS"
                                                   : pressCode == 3 ? "BAR"
                                                   : pressCode == 4 ? "FEET"
                                                                    : "PSI");
    r.unitSystem = units.system == UnitSystem::SI ? "SI" : "US";
    r.reportStart = rStart;
    r.reportStep = rStep;
    std::string qualName = qualFlag == 2 ? "Age" : qualFlag == 3 ? "Trace" : chem.empty() ? "Quality" : chem;
    std::string qualUnit = qualFlag == 2 ? "hrs" : qualFlag == 3 ? "%" : chemUnits;

    r.defineTable("node", nodeIds,
                  {{"Demand", units.label("flow")},
                   {"Head", units.label("head")},
                   {"Pressure", units.label("pressure")},
                   {qualName, qualUnit}});
    r.defineTable("link", linkIds,
                  {{"Flow", units.label("flow")},
                   {"Velocity", units.label("velocity")},
                   {"Headloss", units.label("headloss")},
                   {"Quality", qualUnit},
                   {"Status", ""},
                   {"Setting", ""},
                   {"Reaction Rate", ""},
                   {"Friction Factor", ""}});

    const std::int64_t available =
        (in.size() - 28 - prologBytes - energyBytes) / (periodBytes > 0 ? periodBytes : 1);
    const int n = static_cast<int>(std::min<std::int64_t>(periods, available));
    std::vector<float> buf;
    for (int p = 0; p < n; ++p)
    {
        const int period = r.addPeriod(static_cast<double>(rStart) + static_cast<double>(p) * rStep);
        ResultTable& nodes = *r.table("node");
        ResultTable& links = *r.table("link");
        for (int v = 0; v < 4; ++v)
        {
            in.readFloats(buf, static_cast<std::size_t>(nNodes));
            for (int e = 0; e < nNodes; ++e)
                nodes.set(period, v, e, buf[static_cast<std::size_t>(e)]);
        }
        for (int v = 0; v < 8; ++v)
        {
            in.readFloats(buf, static_cast<std::size_t>(nLinks));
            for (int e = 0; e < nLinks; ++e)
                links.set(period, v, e, buf[static_cast<std::size_t>(e)]);
        }
    }
    if (warning)
        r.messages.push_back("EPANET reported warnings during this run");
    return r;
}

ResultSet ResultSet::readSwmmOutput(const std::filesystem::path& path)
{
    BinaryReader in(path);
    if (in.size() < 7 * 4 + 6 * 4)
        throw std::runtime_error(path.u8string() + " is too small to be a SWMM output file");
    in.seek(in.size() - 6 * 4);
    const std::int32_t idPos = in.i32();
    in.i32(); // input properties position
    const std::int32_t outPos = in.i32();
    const std::int32_t periods = in.i32();
    const std::int32_t errorCode = in.i32();
    if (in.i32() != kSwmmMagic)
        throw std::runtime_error(path.u8string() + " is incomplete or not a SWMM output file");

    in.seek(0);
    if (in.i32() != kSwmmMagic)
        throw std::runtime_error(path.u8string() + " is not a SWMM output file");
    const int version = in.i32();
    const int flowCode = in.i32();
    const int nSub = in.i32(), nNodes = in.i32(), nLinks = in.i32(), nPoll = in.i32();

    in.seek(idPos);
    auto readId = [&]() {
        const int len = in.i32();
        if (len < 0 || len > 4096)
            throw std::runtime_error("corrupt object name in SWMM output file");
        std::string s(static_cast<std::size_t>(len), '\0');
        if (len)
            in.read(s.data(), static_cast<std::size_t>(len));
        return s;
    };
    std::vector<std::string> subIds, nodeIds, linkIds, pollIds;
    for (int i = 0; i < nSub; ++i)
        subIds.push_back(readId());
    for (int i = 0; i < nNodes; ++i)
        nodeIds.push_back(readId());
    for (int i = 0; i < nLinks; ++i)
        linkIds.push_back(readId());
    for (int i = 0; i < nPoll; ++i)
        pollIds.push_back(readId());
    std::vector<std::string> pollUnits;
    static const char* concUnits[] = {"mg/L", "ug/L", "count/L"};
    for (int i = 0; i < nPoll; ++i)
    {
        const int u = in.i32();
        pollUnits.push_back((u >= 0 && u <= 2) ? concUnits[u] : "");
    }

    // Input properties: subcatch(1 + n*k), nodes, links; skip them by reading counts.
    {
        const int k = in.i32();
        for (int i = 0; i < k; ++i)
            in.i32();
        for (int i = 0; i < nSub * k; ++i)
            in.f32();
        const int kn = in.i32();
        for (int i = 0; i < kn; ++i)
            in.i32();
        for (int i = 0; i < nNodes; ++i)
        {
            in.i32();
            for (int j = 1; j < kn; ++j)
                in.f32();
        }
        const int kl = in.i32();
        for (int i = 0; i < kl; ++i)
            in.i32();
        for (int i = 0; i < nLinks; ++i)
        {
            in.i32();
            for (int j = 1; j < kl; ++j)
                in.f32();
        }
    }
    const int nSubVars = in.i32();
    for (int i = 0; i < nSubVars; ++i)
        in.i32();
    const int nNodeVars = in.i32();
    for (int i = 0; i < nNodeVars; ++i)
        in.i32();
    const int nLinkVars = in.i32();
    for (int i = 0; i < nLinkVars; ++i)
        in.i32();
    const int nSysVars = in.i32();
    for (int i = 0; i < nSysVars; ++i)
        in.i32();
    const double startDate = in.f64();
    const int reportStep = in.i32();

    ResultSet r;
    r.engineId = "swmm-binary";
    r.engineName = "SWMM binary output (version " + std::to_string(version) + ")";
    r.flowUnits = swmmFlowUnits(flowCode);
    const auto units = ProjectUnits::fromFlowUnits(Dialect::Swmm, r.flowUnits);
    r.unitSystem = units.system == UnitSystem::SI ? "SI" : "US";
    r.reportStep = reportStep;
    r.metadata["startDate"] = text::formatDouble(startDate, 12);

    const std::string len = units.label("length");
    const std::string fl = units.label("flow");
    std::vector<Variable> subVars = {{"Rainfall", units.label("rainfall")},
                                     {"Snow Depth", units.system == UnitSystem::SI ? "mm" : "in"},
                                     {"Evaporation Loss", units.label("rainfall")},
                                     {"Infiltration Loss", units.label("rainfall")},
                                     {"Runoff", fl},
                                     {"Groundwater Flow", fl},
                                     {"Groundwater Elevation", len},
                                     {"Soil Moisture", ""}};
    std::vector<Variable> nodeVars = {{"Depth", len},   {"Head", len},
                                      {"Volume", units.label("volume")},
                                      {"Lateral Inflow", fl}, {"Total Inflow", fl},
                                      {"Flooding", fl}};
    std::vector<Variable> linkVars = {{"Flow", fl},
                                      {"Depth", len},
                                      {"Velocity", units.label("velocity")},
                                      {"Volume", units.label("volume")},
                                      {"Capacity", ""}};
    for (int i = 0; i < nPoll; ++i)
    {
        subVars.push_back({"Washoff " + pollIds[static_cast<std::size_t>(i)], pollUnits[static_cast<std::size_t>(i)]});
        nodeVars.push_back({pollIds[static_cast<std::size_t>(i)], pollUnits[static_cast<std::size_t>(i)]});
        linkVars.push_back({pollIds[static_cast<std::size_t>(i)], pollUnits[static_cast<std::size_t>(i)]});
    }
    auto fit = [](std::vector<Variable> v, int n) {
        while (static_cast<int>(v.size()) < n)
            v.push_back({"Value " + std::to_string(v.size() + 1), ""});
        v.resize(static_cast<std::size_t>(n));
        return v;
    };
    std::vector<Variable> sysVars;
    static const char* sysNames[] = {"Air Temperature", "Rainfall",        "Snow Depth",
                                     "Infiltration",    "Runoff",          "Dry Weather Inflow",
                                     "Groundwater Inflow", "RDII Inflow",  "External Inflow",
                                     "Total Lateral Inflow", "Flooding",   "Outflow",
                                     "Storage Volume",  "Evaporation",     "Potential ET"};
    for (int i = 0; i < nSysVars; ++i)
        sysVars.push_back({i < 15 ? sysNames[i] : "System " + std::to_string(i + 1), ""});

    if (nSub > 0)
        r.defineTable("subcatchment", subIds, fit(subVars, nSubVars));
    r.defineTable("node", nodeIds, fit(nodeVars, nNodeVars));
    r.defineTable("link", linkIds, fit(linkVars, nLinkVars));
    r.defineTable("system", {"System"}, sysVars);

    in.seek(outPos);
    std::vector<float> buf;
    bool firstPeriod = true;
    for (int p = 0; p < periods; ++p)
    {
        const double date = in.f64();
        // SWMM report times are whole seconds; remove floating-point noise.
        const double seconds = std::round((date - startDate) * 86400.0);
        if (firstPeriod)
        {
            r.reportStart = seconds;
            firstPeriod = false;
        }
        const int period = r.addPeriod(seconds);
        auto readTable = [&](const char* kind, int elements, int vars) {
            ResultTable* t = r.table(kind);
            for (int e = 0; e < elements; ++e)
            {
                in.readFloats(buf, static_cast<std::size_t>(vars));
                if (t)
                {
                    for (int v = 0; v < vars; ++v)
                        t->set(period, v, e, buf[static_cast<std::size_t>(v)]);
                }
            }
        };
        readTable("subcatchment", nSub, nSubVars);
        readTable("node", nNodes, nNodeVars);
        readTable("link", nLinks, nLinkVars);
        readTable("system", 1, nSysVars);
    }
    if (errorCode)
        r.messages.push_back("SWMM reported error code " + std::to_string(errorCode));
    return r;
}

ResultSet ResultSet::loadAny(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.u8string());
    char head[16] = {};
    in.read(head, sizeof(head));
    in.close();
    if (std::memcmp(head, kMagic, sizeof(kMagic)) == 0)
        return load(path);
    std::int32_t magic = 0;
    std::memcpy(&magic, head, sizeof(magic));
    if (magic == kEpanetMagic)
        return readEpanetOutput(path);
    if (magic == kSwmmMagic)
        return readSwmmOutput(path);
    throw std::runtime_error(path.u8string() + " is not a recognized results file");
}

} // namespace anahita::core
