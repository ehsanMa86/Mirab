/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/include/mirab/engine/EpanetToolkit.h
 * Purpose: Typed access to the EPANET 2.2/2.3 programmer's toolkit loaded at run time.
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
#pragma once

#include "mirab/engine/DynamicLibrary.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mirab::engine
{

/// Toolkit constants shared by EPANET 2.2 and 2.3 (values from epanet2_enums.h).
namespace en
{
enum Count
{
    NODECOUNT = 0,
    TANKCOUNT = 1,
    LINKCOUNT = 2,
    PATCOUNT = 3
};
enum NodeProperty
{
    ELEVATION = 0,
    BASEDEMAND = 1,
    PATTERN = 2,
    EMITTER = 3,
    TANKLEVEL = 8,
    DEMAND = 9,
    HEAD = 10,
    PRESSURE = 11,
    QUALITY = 12,
    DEMANDDEFICIT = 27,
    EMITTERFLOW = 29,  ///< 2.3
    LEAKAGEFLOW = 30,  ///< 2.3
    DEMANDFLOW = 31,   ///< 2.3
    FULLDEMAND = 32    ///< 2.3
};
enum LinkProperty
{
    DIAMETER = 0,
    LENGTH = 1,
    ROUGHNESS = 2,
    INITSTATUS = 4,
    INITSETTING = 5,
    FLOW = 8,
    VELOCITY = 9,
    HEADLOSS = 10,
    STATUS = 11,
    SETTING = 12,
    ENERGY = 13,
    LINKQUAL = 14,
    LINK_LEAKAGE = 28  ///< 2.3
};
enum TimeParameter
{
    DURATION = 0,
    HYDSTEP = 1,
    QUALSTEP = 2,
    PATTERNSTEP = 3,
    PATTERNSTART = 4,
    REPORTSTEP = 5,
    REPORTSTART = 6
};
enum NodeType
{
    JUNCTION = 0,
    RESERVOIR = 1,
    TANK = 2
};
enum LinkType
{
    CVPIPE = 0,
    PIPE = 1,
    PUMP = 2
};
enum Option
{
    TRIALS = 0,
    ACCURACY = 1,
    PRESS_UNITS = 25 ///< 2.3
};
enum DemandModel
{
    DDA = 0,
    PDA = 1
};
constexpr int NOSAVE = 0;
constexpr int SAVE = 1;
constexpr int CLOSED = 0;
constexpr int OPEN = 1;
} // namespace en

/// Function table of the thread-safe ("EN_") API.
struct EpanetApi
{
    using Project = void*;
    int (*createproject)(Project*) = nullptr;
    int (*deleteproject)(Project) = nullptr;
    int (*open)(Project, const char*, const char*, const char*) = nullptr;
    int (*close)(Project) = nullptr;
    int (*openH)(Project) = nullptr;
    int (*initH)(Project, int) = nullptr;
    int (*runH)(Project, long*) = nullptr;
    int (*nextH)(Project, long*) = nullptr;
    int (*closeH)(Project) = nullptr;
    int (*openQ)(Project) = nullptr;
    int (*initQ)(Project, int) = nullptr;
    int (*runQ)(Project, long*) = nullptr;
    int (*nextQ)(Project, long*) = nullptr;
    int (*closeQ)(Project) = nullptr;
    int (*report)(Project) = nullptr;
    int (*getcount)(Project, int, int*) = nullptr;
    int (*getnodeid)(Project, int, char*) = nullptr;
    int (*getlinkid)(Project, int, char*) = nullptr;
    int (*getnodeindex)(Project, const char*, int*) = nullptr;
    int (*getlinkindex)(Project, const char*, int*) = nullptr;
    int (*getnodetype)(Project, int, int*) = nullptr;
    int (*getlinktype)(Project, int, int*) = nullptr;
    int (*getlinknodes)(Project, int, int*, int*) = nullptr;
    int (*getnodevalue)(Project, int, int, double*) = nullptr;
    int (*getlinkvalue)(Project, int, int, double*) = nullptr;
    int (*setnodevalue)(Project, int, int, double) = nullptr;
    int (*setlinkvalue)(Project, int, int, double) = nullptr;
    int (*gettimeparam)(Project, int, long*) = nullptr;
    int (*settimeparam)(Project, int, long) = nullptr;
    int (*getflowunits)(Project, int*) = nullptr;
    int (*getoption)(Project, int, double*) = nullptr;
    int (*setoption)(Project, int, double) = nullptr;
    int (*getqualinfo)(Project, int*, char*, char*, int*) = nullptr;
    int (*geterror)(int, char*, int) = nullptr;
    int (*getversion)(int*) = nullptr;
    int (*getdemandmodel)(Project, int*, double*, double*, double*) = nullptr;
    int (*setdemandmodel)(Project, int, double, double, double) = nullptr;
    int (*adddemand)(Project, int, double, const char*, const char*) = nullptr;
    int (*getnumdemands)(Project, int, int*) = nullptr;
    int (*deletedemand)(Project, int, int) = nullptr;
    int (*addpattern)(Project, const char*) = nullptr;
    int (*getpatternindex)(Project, const char*, int*) = nullptr;
    int (*setpattern)(Project, int, double*, int) = nullptr;
    int (*setstatusreport)(Project, int) = nullptr; ///< optional

    bool bind(const DynamicLibrary& lib, std::string* missing);
};

class EpanetError : public std::runtime_error
{
public:
    EpanetError(int code, const std::string& message) : std::runtime_error(message), code_(code) {}
    int code() const { return code_; }

private:
    int code_;
};

/// Owns an EPANET project handle. All calls throw EpanetError on toolkit errors
/// (codes above 100); warnings (codes 1 to 6) are collected in warnings().
class EpanetSession
{
public:
    EpanetSession(const EpanetApi& api);
    ~EpanetSession();
    EpanetSession(const EpanetSession&) = delete;
    EpanetSession& operator=(const EpanetSession&) = delete;

    void open(const std::filesystem::path& input, const std::filesystem::path& report = {},
              const std::filesystem::path& binaryOutput = {});
    void close();
    bool isOpen() const { return opened_; }

    int version() const;
    bool isVersion23() const { return version() >= 20300; }

    int nodeCount() const;
    int linkCount() const;
    std::string nodeId(int index) const;
    std::string linkId(int index) const;
    int nodeIndex(const std::string& id) const;
    int linkIndex(const std::string& id) const;
    int nodeType(int index) const;
    int linkType(int index) const;
    std::pair<int, int> linkNodes(int index) const;
    double nodeValue(int index, int property) const;
    double linkValue(int index, int property) const;
    /// Returns false when the property is not available in this engine version.
    bool tryNodeValue(int index, int property, double& value) const;
    bool tryLinkValue(int index, int property, double& value) const;
    void setNodeValue(int index, int property, double value);
    void setLinkValue(int index, int property, double value);
    bool tryOption(int option, double& value) const;
    long timeParam(int param) const;
    void setTimeParam(int param, long value);
    int flowUnits() const;
    void setDemandModel(int model, double pmin, double preq, double pexp);
    void getDemandModel(int& model, double& pmin, double& preq, double& pexp) const;
    int qualityType(std::string* chemName = nullptr, std::string* chemUnits = nullptr) const;

    void openH();
    void initH(int flag);
    long runH();
    long nextH();
    void closeH();
    void openQ();
    void initQ(int flag);
    long runQ();
    long nextQ();
    void closeQ();
    void report();

    int addPattern(const std::string& id, std::vector<double> factors);
    void addDemand(int node, double base, const std::string& pattern, const std::string& name);
    int demandCount(int node) const;
    void deleteDemand(int node, int demandIndex);

    const std::vector<std::string>& warnings() const { return warnings_; }
    std::string errorText(int code) const;

private:
    int check(int code, const char* what) const;

    const EpanetApi& api_;
    void* ph_ = nullptr;
    bool opened_ = false;
    std::filesystem::path scratchReport_;
    mutable std::vector<std::string> warnings_;
};

/// Loads an EPANET engine library and binds its toolkit.
class EpanetLibrary
{
public:
    bool load(const std::filesystem::path& path, std::string* error);
    const EpanetApi& api() const { return api_; }
    const DynamicLibrary& library() const { return lib_; }

private:
    DynamicLibrary lib_;
    EpanetApi api_;
};

} // namespace mirab::engine
