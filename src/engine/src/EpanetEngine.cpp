/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/engine/src/EpanetEngine.cpp
 * Purpose: Adapter that runs EPA EPANET 2.2 and OWA EPANET 2.3 through their toolkit API.
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
#include "Adapters.h"
#include "EpanetRun.h"

#include "anahita/core/Model.h"
#include "anahita/core/Text.h"
#include "anahita/core/Units.h"

namespace anahita::engine::detail
{

std::string formatEpanetVersion(int v)
{
    return std::to_string(v / 10000) + "." + std::to_string((v / 100) % 100) + "." +
           std::to_string(v % 100);
}

namespace
{
const char* flowUnitName(int code)
{
    static const char* names[] = {"CFS", "GPM", "MGD", "IMGD", "AFD", "LPS",
                                  "LPM", "MLD", "CMH", "CMD", "CMS"};
    return (code >= 0 && code <= 10) ? names[code] : "GPM";
}

const char* pressureUnitName(int code)
{
    static const char* names[] = {"PSI", "KPA", "METERS", "BAR", "FEET"};
    return (code >= 0 && code <= 4) ? names[code] : "PSI";
}

struct Probe
{
    int property;
    bool link;
};
} // namespace

bool runEpanetLoop(EpanetSession& s, const std::filesystem::path& inputFile,
                   core::ResultSet& r, const RunCallbacks& cb, double from, double to)
{
    // Information that the toolkit does not expose in 2.2 comes from the file.
    core::Model model;
    bool haveModel = false;
    try
    {
        model = core::Model::fromFile(inputFile);
        haveModel = true;
    }
    catch (const std::exception&)
    {
    }

    const bool v23 = s.isVersion23();
    const std::string flow = flowUnitName(s.flowUnits());
    std::string pressure;
    double pressCode = 0;
    if (v23 && s.tryOption(en::PRESS_UNITS, pressCode)) // EN_PRESS_UNITS exists only in 2.3
        pressure = pressureUnitName(static_cast<int>(pressCode));
    if (haveModel)
    {
        const auto p = text::splitInpLine(model.option("PRESSURE")).tokens;
        if (!p.empty())
            pressure = text::toUpper(p[0]);
    }
    const auto units = core::ProjectUnits::fromFlowUnits(core::Dialect::Epanet, flow, pressure);
    r.flowUnits = units.flow;
    r.unitSystem = units.system == core::UnitSystem::SI ? "SI" : "US";
    if (haveModel)
        r.title = model.title();

    std::string chemName, chemUnits;
    const int qualType = s.qualityType(&chemName, &chemUnits);
    const bool pda = haveModel && model.demandModel() == "PDA";
    const bool leakage = v23 && haveModel && model.document().find("LEAKAGE") &&
                         model.document().find("LEAKAGE")->rowCount() > 0;
    const bool emitters = haveModel && model.document().find("EMITTERS") &&
                          model.document().find("EMITTERS")->rowCount() > 0;

    std::vector<core::Variable> nodeVars = {{"Demand", units.label("flow")},
                                            {"Head", units.label("head")},
                                            {"Pressure", units.label("pressure")}};
    std::vector<Probe> nodeProbes = {{en::DEMAND, false}, {en::HEAD, false}, {en::PRESSURE, false}};
    std::vector<core::Variable> linkVars = {{"Flow", units.label("flow")},
                                            {"Velocity", units.label("velocity")},
                                            {"Headloss", units.label("headloss")},
                                            {"Status", "0=closed 1=open"},
                                            {"Setting", ""},
                                            {"Pump Power", "kW"}};
    std::vector<Probe> linkProbes = {{en::FLOW, true},   {en::VELOCITY, true}, {en::HEADLOSS, true},
                                     {en::STATUS, true}, {en::SETTING, true},  {en::ENERGY, true}};
    if (qualType != 0)
    {
        const std::string qn = qualType == 2 ? "Age" : qualType == 3 ? "Trace" : (chemName.empty() ? "Quality" : chemName);
        const std::string qu = qualType == 2 ? "hrs" : qualType == 3 ? "%" : chemUnits;
        nodeVars.push_back({qn, qu});
        nodeProbes.push_back({en::QUALITY, false});
        linkVars.push_back({qn, qu});
        linkProbes.push_back({en::LINKQUAL, true});
    }
    if (pda)
    {
        nodeVars.push_back({"Demand Deficit", units.label("flow")});
        nodeProbes.push_back({en::DEMANDDEFICIT, false});
        if (v23)
        {
            nodeVars.push_back({"Full Demand", units.label("flow")});
            nodeProbes.push_back({en::FULLDEMAND, false});
        }
    }
    if (v23 && emitters)
    {
        nodeVars.push_back({"Emitter Flow", units.label("flow")});
        nodeProbes.push_back({en::EMITTERFLOW, false});
    }
    if (leakage)
    {
        nodeVars.push_back({"Leakage", units.label("flow")});
        nodeProbes.push_back({en::LEAKAGEFLOW, false});
        linkVars.push_back({"Leakage", units.label("flow")});
        linkProbes.push_back({en::LINK_LEAKAGE, true});
    }

    const int nNodes = s.nodeCount();
    const int nLinks = s.linkCount();
    std::vector<std::string> nodeIds, linkIds;
    for (int i = 1; i <= nNodes; ++i)
        nodeIds.push_back(s.nodeId(i));
    for (int i = 1; i <= nLinks; ++i)
        linkIds.push_back(s.linkId(i));
    auto& nodes = r.defineTable("node", nodeIds, nodeVars);
    auto& links = r.defineTable("link", linkIds, linkVars);
    (void)nodes;
    (void)links;

    const long duration = s.timeParam(en::DURATION);
    const long rstep = s.timeParam(en::REPORTSTEP);
    const long rstart = s.timeParam(en::REPORTSTART);
    r.reportStart = static_cast<double>(rstart);
    r.reportStep = static_cast<double>(rstep);

    // EPANET shares one save flag between hydraulics and quality, so both
    // the hydraulics file and the output file must be open for the report.
    s.openH();
    s.initH(en::SAVE);
    s.openQ();
    s.initQ(en::SAVE);
    long tstep = 0;
    bool cancelled = false;
    do
    {
        const long t = s.runH();
        s.runQ();
        if (t >= rstart && (rstep <= 0 || (t - rstart) % rstep == 0))
        {
            const int p = r.addPeriod(static_cast<double>(t));
            auto* nt = r.table("node");
            auto* lt = r.table("link");
            for (std::size_t v = 0; v < nodeProbes.size(); ++v)
            {
                for (int i = 1; i <= nNodes; ++i)
                {
                    double value = 0;
                    s.tryNodeValue(i, nodeProbes[v].property, value);
                    nt->set(p, static_cast<int>(v), i - 1, static_cast<float>(value));
                }
            }
            for (std::size_t v = 0; v < linkProbes.size(); ++v)
            {
                for (int i = 1; i <= nLinks; ++i)
                {
                    double value = 0;
                    s.tryLinkValue(i, linkProbes[v].property, value);
                    lt->set(p, static_cast<int>(v), i - 1, static_cast<float>(value));
                }
            }
        }
        cb.report(from + (to - from) * (duration > 0 ? static_cast<double>(t) / duration : 1.0),
                  "Simulating hydraulics and water quality");
        if (cb.cancelled())
        {
            cancelled = true;
            break;
        }
        tstep = s.nextH();
        s.nextQ();
    } while (tstep > 0);
    s.closeQ();
    s.closeH();
    return !cancelled;
}

namespace
{
class EpanetEngine : public Engine
{
public:
    using Engine::Engine;

    bool load(std::string* error) override { return lib_.load(libraryPath_, error); }

    std::string engineVersion() const override
    {
        int v = 0;
        lib_.api().getversion(&v);
        return formatEpanetVersion(v);
    }

    RunOutcome run(const RunRequest& req, const RunCallbacks& cb) override
    {
        RunOutcome out;
        out.results.engineId = descriptor_.id;
        out.results.engineName = descriptor_.name + " (" + engineVersion() + ")";
        try
        {
            EpanetSession s(lib_.api());
            cb.report(0.0, "Reading the network");
            s.open(req.input, req.reportFile);
            const bool finished = runEpanetLoop(s, req.input, out.results, cb, 0.02, 0.98);
            if (finished)
            {
                cb.report(0.99, "Writing the report");
                try
                {
                    s.report();
                }
                catch (const EpanetError& e)
                {
                    cb.message(LogLevel::Warning, std::string("report not written: ") + e.what());
                }
            }
            for (const auto& w : s.warnings())
            {
                out.results.messages.push_back(w);
                cb.message(LogLevel::Warning, w);
            }
            s.close();
            out.cancelled = !finished;
            out.success = finished;
            out.message = finished ? "simulation completed" : "simulation cancelled";
        }
        catch (const EpanetError& e)
        {
            out.code = e.code();
            out.message = e.what();
        }
        catch (const std::exception& e)
        {
            out.code = -1;
            out.message = e.what();
        }
        cb.report(1.0, out.success ? "Done" : "Stopped");
        return out;
    }

private:
    EpanetLibrary lib_;
};
} // namespace

std::unique_ptr<Engine> makeEpanetEngine(EngineDescriptor d, std::filesystem::path library)
{
    return std::make_unique<EpanetEngine>(std::move(d), std::move(library));
}

} // namespace anahita::engine::detail
