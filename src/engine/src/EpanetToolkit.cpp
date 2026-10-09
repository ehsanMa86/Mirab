/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/EpanetToolkit.cpp
 * Purpose: Binding of the EPANET toolkit entry points and an RAII project session.
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
#include "mirab/engine/EpanetToolkit.h"

#include <array>
#include <cstdint>

namespace mirab::engine
{

bool EpanetApi::bind(const DynamicLibrary& lib, std::string* missing)
{
    std::string m;
    bool ok = true;
    ok &= lib.bind(createproject, "EN_createproject", &m);
    ok &= lib.bind(deleteproject, "EN_deleteproject", &m);
    ok &= lib.bind(open, "EN_open", &m);
    ok &= lib.bind(close, "EN_close", &m);
    ok &= lib.bind(openH, "EN_openH", &m);
    ok &= lib.bind(initH, "EN_initH", &m);
    ok &= lib.bind(runH, "EN_runH", &m);
    ok &= lib.bind(nextH, "EN_nextH", &m);
    ok &= lib.bind(closeH, "EN_closeH", &m);
    ok &= lib.bind(openQ, "EN_openQ", &m);
    ok &= lib.bind(initQ, "EN_initQ", &m);
    ok &= lib.bind(runQ, "EN_runQ", &m);
    ok &= lib.bind(nextQ, "EN_nextQ", &m);
    ok &= lib.bind(closeQ, "EN_closeQ", &m);
    ok &= lib.bind(report, "EN_report", &m);
    ok &= lib.bind(getcount, "EN_getcount", &m);
    ok &= lib.bind(getnodeid, "EN_getnodeid", &m);
    ok &= lib.bind(getlinkid, "EN_getlinkid", &m);
    ok &= lib.bind(getnodeindex, "EN_getnodeindex", &m);
    ok &= lib.bind(getlinkindex, "EN_getlinkindex", &m);
    ok &= lib.bind(getnodetype, "EN_getnodetype", &m);
    ok &= lib.bind(getlinktype, "EN_getlinktype", &m);
    ok &= lib.bind(getlinknodes, "EN_getlinknodes", &m);
    ok &= lib.bind(getnodevalue, "EN_getnodevalue", &m);
    ok &= lib.bind(getlinkvalue, "EN_getlinkvalue", &m);
    ok &= lib.bind(setnodevalue, "EN_setnodevalue", &m);
    ok &= lib.bind(setlinkvalue, "EN_setlinkvalue", &m);
    ok &= lib.bind(gettimeparam, "EN_gettimeparam", &m);
    ok &= lib.bind(settimeparam, "EN_settimeparam", &m);
    ok &= lib.bind(getflowunits, "EN_getflowunits", &m);
    ok &= lib.bind(getoption, "EN_getoption", &m);
    ok &= lib.bind(setoption, "EN_setoption", &m);
    ok &= lib.bind(getqualinfo, "EN_getqualinfo", &m);
    ok &= lib.bind(geterror, "EN_geterror", &m);
    ok &= lib.bind(getversion, "EN_getversion", &m);
    ok &= lib.bind(getdemandmodel, "EN_getdemandmodel", &m);
    ok &= lib.bind(setdemandmodel, "EN_setdemandmodel", &m);
    ok &= lib.bind(adddemand, "EN_adddemand", &m);
    ok &= lib.bind(getnumdemands, "EN_getnumdemands", &m);
    ok &= lib.bind(deletedemand, "EN_deletedemand", &m);
    ok &= lib.bind(addpattern, "EN_addpattern", &m);
    ok &= lib.bind(getpatternindex, "EN_getpatternindex", &m);
    ok &= lib.bind(setpattern, "EN_setpattern", &m);
    lib.bind(setstatusreport, "EN_setstatusreport"); // optional
    if (!ok && missing)
        *missing = m;
    return ok;
}

bool EpanetLibrary::load(const std::filesystem::path& path, std::string* error)
{
    if (!lib_.open(path, error))
        return false;
    std::string missing;
    if (!api_.bind(lib_, &missing))
    {
        if (error)
            *error = path.filename().u8string() + " lacks EPANET toolkit functions: " + missing;
        return false;
    }
    return true;
}

EpanetSession::EpanetSession(const EpanetApi& api) : api_(api)
{
    if (int code = api_.createproject(&ph_))
        throw EpanetError(code, "EPANET could not create a project (error " + std::to_string(code) + ")");
}

EpanetSession::~EpanetSession()
{
    close();
    if (ph_)
        api_.deleteproject(ph_);
}

std::string EpanetSession::errorText(int code) const
{
    std::array<char, 256> buf{};
    if (api_.geterror(code, buf.data(), static_cast<int>(buf.size()) - 1) == 0 && buf[0])
        return buf.data();
    return "EPANET error " + std::to_string(code);
}

int EpanetSession::check(int code, const char* what) const
{
    if (code > 100)
        throw EpanetError(code, std::string(what) + ": " + errorText(code));
    if (code > 0)
    {
        const std::string w = errorText(code);
        bool seen = false;
        for (const auto& existing : warnings_)
            seen = seen || existing == w;
        if (!seen)
            warnings_.push_back(w);
    }
    return code;
}

void EpanetSession::open(const std::filesystem::path& input, const std::filesystem::path& report,
                         const std::filesystem::path& binaryOutput)
{
    // EPANET writes its report to standard output when no report file is
    // given; always hand it a file so console output stays clean.
    std::filesystem::path rptPath = report;
    if (rptPath.empty())
    {
        static int counter = 0;
        rptPath = std::filesystem::temp_directory_path() /
                  ("mirab-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "-" +
                   std::to_string(++counter) + ".rpt");
        scratchReport_ = rptPath;
    }
    const std::string in = input.u8string();
    const std::string rpt = rptPath.u8string();
    const std::string out = binaryOutput.u8string();
    check(api_.open(ph_, in.c_str(), rpt.c_str(), out.c_str()), "opening the input file");
    opened_ = true;
}

void EpanetSession::close()
{
    if (opened_)
    {
        api_.close(ph_);
        opened_ = false;
    }
    if (!scratchReport_.empty())
    {
        std::error_code ec;
        std::filesystem::remove(scratchReport_, ec);
        scratchReport_.clear();
    }
}

int EpanetSession::version() const
{
    int v = 0;
    api_.getversion(&v);
    return v;
}

int EpanetSession::nodeCount() const
{
    int n = 0;
    check(api_.getcount(ph_, en::NODECOUNT, &n), "counting nodes");
    return n;
}

int EpanetSession::linkCount() const
{
    int n = 0;
    check(api_.getcount(ph_, en::LINKCOUNT, &n), "counting links");
    return n;
}

std::string EpanetSession::nodeId(int index) const
{
    std::array<char, 64> buf{};
    check(api_.getnodeid(ph_, index, buf.data()), "reading a node ID");
    return buf.data();
}

std::string EpanetSession::linkId(int index) const
{
    std::array<char, 64> buf{};
    check(api_.getlinkid(ph_, index, buf.data()), "reading a link ID");
    return buf.data();
}

int EpanetSession::nodeIndex(const std::string& id) const
{
    int idx = 0;
    if (api_.getnodeindex(ph_, id.c_str(), &idx) != 0)
        return 0;
    return idx;
}

int EpanetSession::linkIndex(const std::string& id) const
{
    int idx = 0;
    if (api_.getlinkindex(ph_, id.c_str(), &idx) != 0)
        return 0;
    return idx;
}

int EpanetSession::nodeType(int index) const
{
    int t = 0;
    check(api_.getnodetype(ph_, index, &t), "reading a node type");
    return t;
}

int EpanetSession::linkType(int index) const
{
    int t = 0;
    check(api_.getlinktype(ph_, index, &t), "reading a link type");
    return t;
}

std::pair<int, int> EpanetSession::linkNodes(int index) const
{
    int a = 0, b = 0;
    check(api_.getlinknodes(ph_, index, &a, &b), "reading link end nodes");
    return {a, b};
}

double EpanetSession::nodeValue(int index, int property) const
{
    double v = 0;
    check(api_.getnodevalue(ph_, index, property, &v), "reading a node value");
    return v;
}

double EpanetSession::linkValue(int index, int property) const
{
    double v = 0;
    check(api_.getlinkvalue(ph_, index, property, &v), "reading a link value");
    return v;
}

bool EpanetSession::tryNodeValue(int index, int property, double& value) const
{
    return api_.getnodevalue(ph_, index, property, &value) == 0;
}

bool EpanetSession::tryLinkValue(int index, int property, double& value) const
{
    return api_.getlinkvalue(ph_, index, property, &value) == 0;
}

void EpanetSession::setNodeValue(int index, int property, double value)
{
    check(api_.setnodevalue(ph_, index, property, value), "setting a node value");
}

void EpanetSession::setLinkValue(int index, int property, double value)
{
    check(api_.setlinkvalue(ph_, index, property, value), "setting a link value");
}

bool EpanetSession::tryOption(int option, double& value) const
{
    return api_.getoption(ph_, option, &value) == 0;
}

long EpanetSession::timeParam(int param) const
{
    long v = 0;
    check(api_.gettimeparam(ph_, param, &v), "reading a time parameter");
    return v;
}

void EpanetSession::setTimeParam(int param, long value)
{
    check(api_.settimeparam(ph_, param, value), "setting a time parameter");
}

int EpanetSession::flowUnits() const
{
    int u = 0;
    check(api_.getflowunits(ph_, &u), "reading flow units");
    return u;
}

void EpanetSession::setDemandModel(int model, double pmin, double preq, double pexp)
{
    check(api_.setdemandmodel(ph_, model, pmin, preq, pexp), "setting the demand model");
}

void EpanetSession::getDemandModel(int& model, double& pmin, double& preq, double& pexp) const
{
    check(api_.getdemandmodel(ph_, &model, &pmin, &preq, &pexp), "reading the demand model");
}

int EpanetSession::qualityType(std::string* chemName, std::string* chemUnits) const
{
    int type = 0, trace = 0;
    std::array<char, 64> name{}, units{};
    check(api_.getqualinfo(ph_, &type, name.data(), units.data(), &trace), "reading quality options");
    if (chemName)
        *chemName = name.data();
    if (chemUnits)
        *chemUnits = units.data();
    return type;
}

void EpanetSession::openH()
{
    check(api_.openH(ph_), "opening the hydraulic solver");
}

void EpanetSession::initH(int flag)
{
    check(api_.initH(ph_, flag), "initializing hydraulics");
}

long EpanetSession::runH()
{
    long t = 0;
    check(api_.runH(ph_, &t), "solving hydraulics");
    return t;
}

long EpanetSession::nextH()
{
    long dt = 0;
    check(api_.nextH(ph_, &dt), "advancing hydraulics");
    return dt;
}

void EpanetSession::closeH()
{
    api_.closeH(ph_);
}

void EpanetSession::openQ()
{
    check(api_.openQ(ph_), "opening the quality solver");
}

void EpanetSession::initQ(int flag)
{
    check(api_.initQ(ph_, flag), "initializing water quality");
}

long EpanetSession::runQ()
{
    long t = 0;
    check(api_.runQ(ph_, &t), "solving water quality");
    return t;
}

long EpanetSession::nextQ()
{
    long dt = 0;
    check(api_.nextQ(ph_, &dt), "advancing water quality");
    return dt;
}

void EpanetSession::closeQ()
{
    api_.closeQ(ph_);
}

void EpanetSession::report()
{
    check(api_.report(ph_), "writing the report");
}

int EpanetSession::addPattern(const std::string& id, std::vector<double> factors)
{
    check(api_.addpattern(ph_, id.c_str()), "adding a pattern");
    int idx = 0;
    check(api_.getpatternindex(ph_, id.c_str(), &idx), "finding a pattern");
    check(api_.setpattern(ph_, idx, factors.data(), static_cast<int>(factors.size())), "setting a pattern");
    return idx;
}

void EpanetSession::addDemand(int node, double base, const std::string& pattern, const std::string& name)
{
    check(api_.adddemand(ph_, node, base, pattern.c_str(), name.c_str()), "adding a demand");
}

int EpanetSession::demandCount(int node) const
{
    int n = 0;
    check(api_.getnumdemands(ph_, node, &n), "counting demands");
    return n;
}

void EpanetSession::deleteDemand(int node, int demandIndex)
{
    check(api_.deletedemand(ph_, node, demandIndex), "deleting a demand");
}

} // namespace mirab::engine
