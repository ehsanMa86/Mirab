/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/MsxEngine.cpp
 * Purpose: Adapter that runs EPANET-MSX multi-species quality on top of an EPANET engine.
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

#include <algorithm>
#include <array>
#include <cmath>

namespace mirab::engine::detail
{

namespace
{
constexpr int MSX_NODE = 0;
constexpr int MSX_LINK = 1;
constexpr int MSX_SPECIES = 3;
constexpr int MSX_BULK = 0;

struct MsxApi
{
    // Legacy EPANET API of the companion library (used by MSX internally).
    int (*ENopen)(const char*, const char*, const char*) = nullptr;
    int (*ENclose)() = nullptr;
    // EPANET-MSX toolkit.
    int (*MSXopen)(char*) = nullptr;
    int (*MSXsolveH)() = nullptr;
    int (*MSXinit)(int) = nullptr;
    int (*MSXstep)(double*, double*) = nullptr;
    int (*MSXreport)() = nullptr;
    int (*MSXclose)() = nullptr;
    int (*MSXgetcount)(int, int*) = nullptr;
    int (*MSXgetID)(int, int, char*, int) = nullptr;
    int (*MSXgetspecies)(int, int*, char*, double*, double*) = nullptr;
    int (*MSXgetqual)(int, int, int, double*) = nullptr;
    int (*MSXgeterror)(int, char*, int) = nullptr;
};

class MsxEngine : public Engine
{
public:
    MsxEngine(EngineDescriptor d, std::filesystem::path msxLib, std::filesystem::path epanetLib)
        : Engine(std::move(d), std::move(msxLib)), epanetPath_(std::move(epanetLib))
    {
    }

    bool load(std::string* error) override
    {
        if (!epanet_.load(epanetPath_, error))
            return false;
        if (!msxLib_.open(libraryPath_, error))
            return false;
        std::string missing;
        bool ok = true;
        ok &= epanet_.library().bind(api_.ENopen, "ENopen", &missing);
        ok &= epanet_.library().bind(api_.ENclose, "ENclose", &missing);
        ok &= msxLib_.bind(api_.MSXopen, "MSXopen", &missing);
        ok &= msxLib_.bind(api_.MSXsolveH, "MSXsolveH", &missing);
        ok &= msxLib_.bind(api_.MSXinit, "MSXinit", &missing);
        ok &= msxLib_.bind(api_.MSXstep, "MSXstep", &missing);
        ok &= msxLib_.bind(api_.MSXreport, "MSXreport", &missing);
        ok &= msxLib_.bind(api_.MSXclose, "MSXclose", &missing);
        ok &= msxLib_.bind(api_.MSXgetcount, "MSXgetcount", &missing);
        ok &= msxLib_.bind(api_.MSXgetID, "MSXgetID", &missing);
        ok &= msxLib_.bind(api_.MSXgetspecies, "MSXgetspecies", &missing);
        ok &= msxLib_.bind(api_.MSXgetqual, "MSXgetqual", &missing);
        ok &= msxLib_.bind(api_.MSXgeterror, "MSXgeterror", &missing);
        if (!ok && error)
            *error = "EPANET-MSX library lacks functions: " + missing;
        return ok;
    }

    std::string engineVersion() const override
    {
        int v = 0;
        epanet_.api().getversion(&v);
        return "MSX 2.0 / EPANET " + formatEpanetVersion(v);
    }

    RunOutcome run(const RunRequest& req, const RunCallbacks& cb) override
    {
        RunOutcome out;
        out.results.engineId = descriptor_.id;
        out.results.engineName = descriptor_.name;
        if (req.msxInput.empty())
        {
            out.code = -1;
            out.message = "an EPANET-MSX input file (.msx) is required for this engine";
            return out;
        }
        try
        {
            // 1. Hydraulics (and any single-species quality) through the thread-safe API.
            FramePacer pacer(cb);
            {
                EpanetSession s(epanet_.api());
                cb.report(0.0, "Reading the network");
                s.open(req.input, req.reportFile);
                if (!runEpanetLoop(s, req.input, out.results, cb, pacer, 0.02, 0.40))
                {
                    pacer.finish(lastFrame(out.results, 1));
                    out.cancelled = true;
                    out.message = "simulation cancelled";
                    return out;
                }
                try
                {
                    s.report();
                }
                catch (const EpanetError&)
                {
                }
                for (const auto& w : s.warnings())
                    out.results.messages.push_back(w);
            }

            // 2. Multi-species transport through the legacy project used by MSX.
            std::filesystem::path msxReport = req.reportFile;
            msxReport.replace_extension(".msx.rpt");
            const std::string inp = req.input.u8string();
            const std::string rpt = msxReport.u8string();
            int code = api_.ENopen(inp.c_str(), rpt.c_str(), "");
            if (code > 100)
                return fail(out, code, "EPANET could not open the network for MSX");
            std::string msxFile = req.msxInput.u8string();
            code = api_.MSXopen(msxFile.data());
            if (code)
            {
                api_.ENclose();
                return fail(out, code, msxError(code));
            }
            cb.report(0.42, "Solving hydraulics for EPANET-MSX");
            code = api_.MSXsolveH();
            if (!code)
                code = api_.MSXinit(0);
            if (code)
            {
                cleanup();
                return fail(out, code, msxError(code));
            }

            // MSXgetcount only counts species, constants, parameters and patterns;
            // nodes and links share EPANET's indexing, taken from the hydraulic pass.
            int nSpecies = 0;
            api_.MSXgetcount(MSX_SPECIES, &nSpecies);
            const int nNodes = out.results.table("node")->elementCount();
            const int nLinks = out.results.table("link")->elementCount();
            std::vector<core::Variable> nodeVars, linkVars;
            std::vector<int> bulkSpecies, allSpecies;
            for (int i = 1; i <= nSpecies; ++i)
            {
                std::array<char, 64> id{}, units{};
                int type = 0;
                double atol = 0, rtol = 0;
                api_.MSXgetID(MSX_SPECIES, i, id.data(), static_cast<int>(id.size()) - 1);
                api_.MSXgetspecies(i, &type, units.data(), &atol, &rtol);
                const std::string name = id.data();
                const std::string unit = std::string(units.data()) + (type == MSX_BULK ? "/L" : "/area");
                if (type == MSX_BULK)
                {
                    nodeVars.push_back({name, unit});
                    bulkSpecies.push_back(i);
                }
                linkVars.push_back({name, unit});
                allSpecies.push_back(i);
            }
            auto* nodes = out.results.table("node");
            auto* links = out.results.table("link");
            const int nodeBase = nodes->variableCount();
            const int linkBase = links->variableCount();
            nodes->appendVariables(nodeVars);
            links->appendVariables(linkVars);
            out.results.metadata["msxFile"] = req.msxInput.filename().u8string();
            out.results.metadata["species"] = std::to_string(nSpecies);

            const auto& times = out.results.times;
            const double duration = times.empty() ? 0.0 : times.back();
            std::size_t nextPeriod = 0;
            // The species add variables: frames start over with a second layout.
            constexpr int kSpeciesLayout = 2;
            pacer.restart();
            auto frameAt = [&](int p) {
                ResultFrame f;
                f.results = &out.results;
                f.slot = p;
                f.period = p;
                f.time = p >= 0 ? times[static_cast<std::size_t>(p)] : 0.0;
                f.layout = kSpeciesLayout;
                return f;
            };
            auto sampleUpTo = [&](double t) {
                while (nextPeriod < times.size() && times[nextPeriod] <= t + 0.5)
                {
                    const int p = static_cast<int>(nextPeriod);
                    for (std::size_t k = 0; k < bulkSpecies.size(); ++k)
                    {
                        for (int i = 1; i <= nNodes; ++i)
                        {
                            double c = 0;
                            api_.MSXgetqual(MSX_NODE, i, bulkSpecies[k], &c);
                            nodes->set(p, nodeBase + static_cast<int>(k), i - 1, static_cast<float>(c));
                        }
                    }
                    for (std::size_t k = 0; k < allSpecies.size(); ++k)
                    {
                        for (int i = 1; i <= nLinks; ++i)
                        {
                            double c = 0;
                            api_.MSXgetqual(MSX_LINK, i, allSpecies[k], &c);
                            links->set(p, linkBase + static_cast<int>(k), i - 1, static_cast<float>(c));
                        }
                    }
                    ++nextPeriod;
                    if (pacer.due())
                        pacer.send(frameAt(p));
                }
            };
            sampleUpTo(0.0);
            double t = 0, tleft = 1;
            while (tleft > 0)
            {
                code = api_.MSXstep(&t, &tleft);
                if (code)
                {
                    cleanup();
                    return fail(out, code, msxError(code));
                }
                sampleUpTo(t);
                cb.report(0.45 + 0.5 * (duration > 0 ? std::min(1.0, t / duration) : 1.0),
                          "Simulating multi-species water quality");
                if (!cb.proceed(t))
                {
                    pacer.finish(frameAt(static_cast<int>(nextPeriod) - 1));
                    cleanup();
                    out.cancelled = true;
                    out.message = "simulation cancelled";
                    return out;
                }
            }
            pacer.finish(frameAt(static_cast<int>(nextPeriod) - 1));
            api_.MSXreport();
            cleanup();
            out.success = true;
            out.message = "simulation completed";
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
    void cleanup()
    {
        api_.MSXclose();
        api_.ENclose();
    }

    std::string msxError(int code) const
    {
        std::array<char, 256> buf{};
        if (code > 500 && api_.MSXgeterror(code, buf.data(), static_cast<int>(buf.size()) - 1) == 0 && buf[0])
            return std::string("EPANET-MSX: ") + buf.data();
        return "EPANET-MSX error " + std::to_string(code);
    }

    static RunOutcome& fail(RunOutcome& out, int code, const std::string& message)
    {
        out.code = code;
        out.message = message;
        return out;
    }

    std::filesystem::path epanetPath_;
    EpanetLibrary epanet_;
    DynamicLibrary msxLib_;
    MsxApi api_;
};
} // namespace

std::unique_ptr<Engine> makeMsxEngine(EngineDescriptor d, std::filesystem::path msxLibrary,
                                      std::filesystem::path epanetLibrary)
{
    return std::make_unique<MsxEngine>(std::move(d), std::move(msxLibrary), std::move(epanetLibrary));
}

} // namespace mirab::engine::detail
