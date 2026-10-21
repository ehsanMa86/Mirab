/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_calibration.cpp
 * Purpose: Tests of observed data import, goodness-of-fit metrics and the DDS optimiser.
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
#include "TestFramework.h"

#include "mirab/core/Calibration.h"
#include "mirab/core/Optimizer.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Text.h"
#include "mirab/engine/Compatibility.h"
#include "mirab/engine/EngineRegistry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

using namespace mirab;
namespace fs = std::filesystem;

namespace
{
fs::path example(const char* rel)
{
    return fs::path(MIRAB_SOURCE_DIR) / "examples" / rel;
}

fs::path scratch(const std::string& name)
{
    const auto dir = fs::temp_directory_path() / "mirab-tests";
    fs::create_directories(dir);
    return dir / name;
}

double at(int year, int month, int day, int hour, int minute)
{
    return static_cast<double>(core::daysFromCivil(year, month, day)) * 86400.0 + hour * 3600.0 + minute * 60.0;
}

core::AlignedSeries sample()
{
    core::AlignedSeries a;
    a.times = {0, 3600, 7200, 10800};
    a.observed = {1, 2, 3, 4};
    a.simulated = {1.5, 2, 2.5, 5};
    return a;
}

engine::EngineRegistry& registry()
{
    static engine::EngineRegistry r({fs::u8path(MIRAB_ENGINE_DIR)});
    return r;
}

core::ResultSet runModel(const std::string& engineId, const core::Model& model, const std::string& tag)
{
    std::string error;
    auto e = registry().create(engineId, &error);
    if (!e)
        throw std::runtime_error(engineId + ": " + error);
    const fs::path input = scratch("calib-" + tag + ".inp");
    std::ofstream(input, std::ios::binary) << engine::prepareInput(model, e->descriptor());
    engine::RunRequest req;
    req.input = input;
    req.reportFile = scratch("calib-" + tag + ".rpt");
    req.workDir = scratch("calib-" + tag + "-work");
    fs::create_directories(req.workDir);
    auto outcome = e->run(req, {});
    if (!outcome.success)
        throw std::runtime_error(engineId + " failed: " + outcome.message);
    return outcome.results;
}
} // namespace

TEST_CASE("calibration: metrics match hand-computed values")
{
    const auto m = core::computeMetrics(sample());
    CHECK_EQ(m.count, 4);
    CHECK_NEAR(m.nse, 0.7, 1e-12);                  // 1 - 1.5 / 5
    CHECK_NEAR(m.rmse, std::sqrt(0.375), 1e-12);    // sqrt(1.5 / 4)
    CHECK_NEAR(m.mae, 0.5, 1e-12);                  // (0.5 + 0 + 0.5 + 1) / 4
    CHECK_NEAR(m.pbias, 10.0, 1e-12);               // 100 * (11 - 10) / 10
    CHECK_NEAR(m.r2, 30.25 / 36.25, 1e-12);         // cov^2 / (varO * varS) = 5.5^2 / (5 * 7.25)
    // KGE 2012: r = 0.9135002, beta = 1.1, gamma = sqrt(1.45) * 2.5 / 2.75 = 1.0946904
    CHECK_NEAR(m.kge, 0.8373704, 1e-6);
    CHECK_NEAR(m.peakError, 1.0, 1e-12);
    CHECK_NEAR(m.peakErrorPct, 25.0, 1e-12);
    CHECK_NEAR(m.peakTiming, 0.0, 1e-12);
    CHECK_NEAR(m.volumeError, 100.0 * 0.25 / 7.5, 1e-9); // trapezoids 7.75 vs 7.5 (x 3600 s)
    CHECK_NEAR(m.meanObserved, 2.5, 1e-12);
    CHECK_NEAR(m.meanSimulated, 2.75, 1e-12);

    // A perfect fit.
    auto p = sample();
    p.simulated = p.observed;
    const auto perfect = core::computeMetrics(p);
    CHECK_NEAR(perfect.nse, 1.0, 1e-12);
    CHECK_NEAR(perfect.kge, 1.0, 1e-12);
    CHECK_NEAR(perfect.rmse, 0.0, 1e-12);

    // Peak timing: simulated peak two hours late.
    core::AlignedSeries late;
    late.times = {0, 3600, 7200, 10800};
    late.observed = {0, 5, 1, 0};
    late.simulated = {0, 1, 2, 6};
    CHECK_NEAR(core::computeMetrics(late).peakTiming, 7200.0, 1e-9);

    CHECK(std::isnan(core::computeMetrics({}).nse));
    CHECK_NEAR(core::metricLoss(core::Metric::Nse, m), 0.3, 1e-12);
    CHECK_NEAR(core::metricLoss(core::Metric::Pbias, m), 0.1, 1e-12);
    CHECK(*core::metricFromName("r²") == core::Metric::R2);
    CHECK(*core::metricFromName("kge") == core::Metric::Kge);
}

TEST_CASE("calibration: simulated values are interpolated at observation times")
{
    core::SimulatedSeries sim;
    sim.times = {0, 3600, 7200};
    sim.values = {10, 20, 40};
    CHECK_NEAR(core::interpolate(sim.times, sim.values, 1800), 15.0, 1e-12);
    CHECK_NEAR(core::interpolate(sim.times, sim.values, 7200), 40.0, 1e-12);
    CHECK(std::isnan(core::interpolate(sim.times, sim.values, 7201)));
    const double nan = std::nan("");
    const auto a = core::align(sim, {1800, 5400, 7200, 9000, 3600}, {1, 2, 3, 4, nan}, 2.0);
    CHECK_EQ(a.observed.size(), std::size_t(3));
    CHECK_EQ(a.outside, 1);
    CHECK_EQ(a.missing, 1);
    CHECK_NEAR(a.simulated[0], 15.0, 1e-12);
    CHECK_NEAR(a.simulated[1], 30.0, 1e-12);
    CHECK_NEAR(a.observed[2], 6.0, 1e-12); // scaled by the unit factor
}

TEST_CASE("calibration: dates in ISO 8601, US and day-first formats")
{
    const double expect = at(2024, 5, 1, 13, 30);
    CHECK_NEAR(*core::parseDateTime("2024-05-01T13:30:00"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("2024-05-01T13:30:00Z"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("2024-05-01T13:30:00+02:00"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("2024-05-01 13:30"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("2024/05/01 13:30"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("05/01/2024 13:30"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("5/1/2024 1:30 PM"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("01/05/2024 13:30", core::DateOrder::DayFirst), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("01-May-2024 13:30"), expect, 1e-6);
    CHECK_NEAR(*core::parseDateTime("2024-05-01"), at(2024, 5, 1, 0, 0), 1e-6);
    CHECK(!core::parseDateTime("13:30"));
    CHECK(!core::parseDateTime("2024-13-01"));
    CHECK_EQ(core::formatDateTime(expect), std::string("2024-05-01 13:30"));
    CHECK_NEAR(*core::parseTimeOfDay("12 AM"), 0.0, 1e-9);
    CHECK_NEAR(*core::parseTimeOfDay("12:15 pm"), 12 * 3600.0 + 900.0, 1e-9);
}

TEST_CASE("calibration: CSV with date and time columns, units and missing values")
{
    const std::string csv = "Date,Time,J5 Pressure (psi),P1 flow\n"
                            "05/01/2024,00:00,50.0,NA\n"
                            "05/01/2024,00:30,51.5,12.5\n"
                            "05/01/2024,01:00,-9999,13\n";
    core::ObservedSource src;
    src.file = "obs.csv";
    src.noData = "-9999";
    std::vector<std::string> warnings;
    const auto series = core::readObservedTable(csv, src, &warnings);
    CHECK(warnings.empty());
    CHECK_EQ(series.size(), std::size_t(2));
    CHECK(series[0].calendar);
    CHECK_EQ(series[0].times.size(), std::size_t(3));
    CHECK_NEAR(series[0].times[1], at(2024, 5, 1, 0, 30), 1e-6);
    CHECK_NEAR(series[0].values[1], 51.5, 1e-12);
    CHECK(std::isnan(series[0].values[2]));
    CHECK(std::isnan(series[1].values[0]));

    const auto model = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    const auto j5 = core::guessLink("J5 Pressure (psi)", model);
    CHECK_EQ(j5.element, std::string("J5"));
    CHECK_EQ(j5.table, std::string("node"));
    CHECK_EQ(j5.variable, std::string("Pressure"));
    CHECK_EQ(j5.unit, std::string("psi"));
    const auto p1 = core::guessLink("p1_flow", model);
    CHECK_EQ(p1.element, std::string("P1"));
    CHECK_EQ(p1.table, std::string("link"));
    CHECK_EQ(p1.variable, std::string("Flow"));
    CHECK_EQ(core::guessLink("J9", model).variable, std::string("Pressure"));

    // Semicolons with decimal commas and elapsed minutes, without a header.
    core::ObservedSource tsv;
    tsv.file = "obs.txt";
    tsv.timeUnit = "minutes";
    const auto s2 = core::readObservedTable("0;1,5\n30;2,25\n90;3\n", tsv);
    CHECK_EQ(s2.size(), std::size_t(1));
    CHECK(!s2[0].calendar);
    CHECK_NEAR(s2[0].times[1], 1800.0, 1e-9);
    CHECK_NEAR(s2[0].values[1], 2.25, 1e-12);

    // Tab separated, ISO times, explicit links.
    core::ObservedSource iso;
    iso.file = "obs.tsv";
    iso.links.push_back({"depth_j2", "node", "J2", "Depth", "m", 2.0, true});
    const auto s3 = core::readObservedTable("time\tdepth_j2\tignored\n2026-01-01T00:15\t0.1\t9\n"
                                            "2026-01-01T00:30\t0.2\t9\n",
                                            iso);
    CHECK_EQ(s3.size(), std::size_t(1));
    CHECK_EQ(s3[0].element, std::string("J2"));
    CHECK_NEAR(s3[0].weight, 2.0, 1e-12);
    CHECK_NEAR(s3[0].times[0], at(2026, 1, 1, 0, 15), 1e-6);
}

TEST_CASE("calibration: EPANET and SWMM calibration files")
{
    const std::string dat = "; Pressure measurements\n"
                            "J5   0     50.1\n"
                            "     1     49.8\n"
                            "     2:30  48\n"
                            "J9   0.5   40   ; comment\n";
    core::ObservedSource src;
    src.file = "pressure.dat";
    src.table = "node";
    src.variable = "Pressure";
    CHECK_EQ(src.resolvedFormat(dat), std::string("epanet"));
    const auto s = core::readCalibrationFile(dat, src);
    CHECK_EQ(s.size(), std::size_t(2));
    CHECK_EQ(s[0].element, std::string("J5"));
    CHECK_EQ(s[0].times.size(), std::size_t(3));
    CHECK_NEAR(s[0].times[2], 9000.0, 1e-9);
    CHECK_NEAR(s[1].times[0], 1800.0, 1e-9);
    CHECK_NEAR(s[1].values[0], 40.0, 1e-12);
    CHECK_EQ(s[1].variable, std::string("Pressure"));

    const std::string swmm = ";Flow at the outfall\n"
                             "O1\n"
                             "01/01/2026  00:15  0.2\n"
                             "01/01/2026  00:30  0.5\n"
                             "C3\n"
                             "0  00:15  1.0\n"
                             "0  01:00  1.5\n";
    core::ObservedSource sw;
    sw.file = "flow.dat";
    sw.table = "link";
    sw.variable = "Flow";
    sw.links.push_back({"O1", "node", "O1", "Total Inflow", "", 1.0, true});
    CHECK_EQ(sw.resolvedFormat(swmm), std::string("swmm"));
    const auto f = core::readCalibrationFile(swmm, sw);
    CHECK_EQ(f.size(), std::size_t(2));
    CHECK(f[0].calendar);
    CHECK_EQ(f[0].table, std::string("node"));
    CHECK_EQ(f[0].variable, std::string("Total Inflow"));
    CHECK_NEAR(f[0].times[1], at(2026, 1, 1, 0, 30), 1e-6);
    CHECK(!f[1].calendar);
    CHECK_EQ(f[1].table, std::string("link"));
    CHECK_NEAR(f[1].times[1], 3600.0, 1e-9);

    // Calendar times relative to the SWMM start date.
    const auto urban = core::Model::fromFile(example("swmm/mirab-urban.inp"));
    const auto base = core::timeBaseFor(&urban, nullptr);
    CHECK(base.startEpoch.has_value());
    CHECK_NEAR(core::simulationTimes(f[0], base)[0], 900.0, 1e-6);
    // EPANET: the start clock time on the day of the first observation, unless overridden.
    const auto demo = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    core::ObservedSeries cal;
    cal.calendar = true;
    cal.times = {at(2024, 5, 1, 6, 0)};
    CHECK_NEAR(core::simulationTimes(cal, core::timeBaseFor(&demo, nullptr))[0], 6 * 3600.0, 1e-6);
    CHECK_NEAR(core::simulationTimes(cal, core::timeBaseFor(&demo, nullptr, "2024-04-30 00:00"))[0], 30 * 3600.0, 1e-6);
}

TEST_CASE("calibration: unit conversion")
{
    CHECK_NEAR(*core::unitFactor("psi", "m"), 0.703069578, 1e-9);
    CHECK_NEAR(*core::unitFactor("ft", "m"), 0.3048, 1e-12);
    CHECK_NEAR(*core::unitFactor("L/s", "CMS"), 0.001, 1e-12);
    CHECK_NEAR(*core::unitFactor("GPM", "LPS"), 0.0630901964, 1e-9);
    CHECK_NEAR(*core::unitFactor("", "psi"), 1.0, 1e-12);
    CHECK(!core::unitFactor("psi", "LPS"));
    CHECK(core::isFlowVariable("Total Inflow"));
    CHECK(!core::isFlowVariable("Pressure"));
}

TEST_CASE("calibration: configuration and parameters")
{
    const auto cfgText = R"({
      "observed": [{"file": "obs.csv", "format": "csv", "noData": -9999,
                    "links": [{"column": "J5", "table": "node", "element": "J5", "variable": "Pressure"}]}],
      "simulationStart": "2024-05-01 00:00",
      "parameters": [{"name": "Roughness", "section": "[pipes]", "column": "Roughness", "elements": "all",
                      "mode": "multiply", "min": 0.5, "max": 1.5},
                     {"section": "PIPES", "column": "Roughness", "elements": ["P2"], "mode": "replace",
                      "min": 80, "max": 140}],
      "objective": [{"metric": "NSE", "weight": 1}, "RMSE"],
      "budget": 50, "jobs": 2, "seed": 7})";
    const auto cfg = core::CalibrationConfig::fromJson(json::Value::parse(cfgText));
    CHECK_EQ(cfg.observed.size(), std::size_t(1));
    CHECK_EQ(cfg.observed[0].noData, std::string("-9999"));
    CHECK_EQ(cfg.parameters.size(), std::size_t(2));
    CHECK_EQ(cfg.parameters[0].section, std::string("PIPES"));
    CHECK(cfg.parameters[0].elements.empty());
    CHECK_EQ(cfg.objective.size(), std::size_t(2));
    CHECK(cfg.objective[1].metric == core::Metric::Rmse);
    CHECK_EQ(cfg.budget, 50);
    const auto back = core::CalibrationConfig::fromJson(json::Value::parse(cfg.toJson().dump()));
    CHECK_EQ(back.toJson().dump(), cfg.toJson().dump());

    auto model = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    CHECK_EQ(core::parameterColumn(model, cfg.parameters[0]), 5);
    CHECK_EQ(core::parameterRowCount(model, cfg.parameters[0]), 13);
    CHECK_EQ(core::parameterRowCount(model, cfg.parameters[1]), 1);
    CHECK_NEAR(core::parameterInitialValue(model, cfg.parameters[0]), 1.0, 1e-12);
    CHECK_NEAR(core::parameterInitialValue(model, cfg.parameters[1]), 120.0, 1e-12);
    std::string error;
    CHECK(core::applyParameters(model, cfg.parameters, {0.5, 100}, &error));
    const auto p1 = model.rowsFor(core::Category::Link, "P1");
    CHECK_EQ(p1[0].tokens[5], std::string("60"));
    CHECK_EQ(model.rowsFor(core::Category::Link, "P2")[0].tokens[5], std::string("100"));
    core::CalibrationParameter bad;
    bad.section = "PIPES";
    bad.column = "Nothing";
    CHECK(!core::applyParameters(model, {bad}, {1.0}, &error));
    CHECK(!error.empty());
}

TEST_CASE("calibration: set-up is kept in the project sidecar")
{
    const auto inp = scratch("calib-settings.inp");
    core::CalibrationConfig c;
    c.simulationStart = "2024-05-01 00:00";
    core::ObservedSource src;
    src.file = "obs.csv";
    c.observed.push_back(src);
    c.budget = 25;
    core::ProjectSettings s;
    s.engineId = "epanet-2.2-epa";
    s.calibration = c.toJson();
    s.saveFor(inp);
    const auto back = core::CalibrationConfig::fromJson(core::ProjectSettings::loadFor(inp).calibration);
    CHECK_EQ(back.observed.size(), std::size_t(1));
    CHECK_EQ(back.budget, 25);
    CHECK_EQ(back.simulationStart, c.simulationStart);

    // The examples load with all their series.
    for (const char* rel : {"epanet/mirab-demo.inp", "swmm/mirab-urban.inp"})
    {
        const auto model = core::Model::fromFile(example(rel));
        const auto cfg = core::CalibrationConfig::fromJson(core::ProjectSettings::loadFor(example(rel)).calibration);
        std::vector<std::string> warnings;
        const auto series = core::loadObserved(cfg, example(rel).parent_path(), &model, &warnings);
        CHECK(warnings.empty());
        CHECK_EQ(series.size(), std::string(rel).find("epanet") == 0 ? std::size_t(4) : std::size_t(2));
        CHECK(!cfg.parameters.empty());
        for (const auto& p : cfg.parameters)
            CHECK(core::parameterRowCount(model, p) > 0);
    }
}

TEST_CASE("calibration: DDS finds the minimum of an analytic function")
{
    const std::vector<double> target = {1.0, -2.0, 0.5, 3.0};
    auto sphere = [&](const std::vector<double>& x) {
        double s = 0;
        for (std::size_t i = 0; i < x.size(); ++i)
            s += (x[i] - target[i]) * (x[i] - target[i]);
        return s;
    };
    const std::vector<double> lo(4, -5.0), hi(4, 5.0), x0(4, 0.0);
    double best = 0;
    const auto x = core::minimizeDds(sphere, lo, hi, x0, 500, 3, &best);
    CHECK(best < 0.05);
    for (std::size_t i = 0; i < x.size(); ++i)
        CHECK_NEAR(x[i], target[i], 0.25);
    // Same seed, same search.
    double again = 0;
    core::minimizeDds(sphere, lo, hi, x0, 500, 3, &again);
    CHECK_EQ(best, again);

    // Batched proposals stay inside the bounds and use the budget exactly.
    core::DdsOptimizer dds(lo, hi, x0, 37, 11);
    int evaluated = 0;
    while (!dds.finished())
    {
        const auto batch = dds.propose(4);
        CHECK(!batch.empty());
        std::vector<double> values;
        for (const auto& p : batch)
        {
            for (double v : p)
                CHECK(v >= -5.0 && v <= 5.0);
            values.push_back(sphere(p));
        }
        evaluated += static_cast<int>(batch.size());
        dds.report(batch, values);
    }
    CHECK_EQ(evaluated, 37);
    CHECK(dds.bestValue() < sphere(x0));
}

TEST_CASE("calibration: Latin hypercube strata and rank correlation")
{
    const auto pts = core::latinHypercube({0.0, 10.0}, {1.0, 20.0}, 8, 5);
    CHECK_EQ(pts.size(), std::size_t(8));
    for (int d = 0; d < 2; ++d)
    {
        std::set<int> strata;
        for (const auto& p : pts)
        {
            const double u = d == 0 ? p[0] : (p[1] - 10.0) / 10.0;
            CHECK(u >= 0.0 && u < 1.0);
            strata.insert(static_cast<int>(u * 8));
        }
        CHECK_EQ(strata.size(), std::size_t(8));
    }
    CHECK_NEAR(core::spearman({1, 2, 3, 4}, {10, 20, 25, 100}), 1.0, 1e-12);
    CHECK_NEAR(core::spearman({1, 2, 3, 4}, {4, 3, 2, 1}), -1.0, 1e-12);
}

TEST_CASE("calibration-run: SWMM results fit their own outfall flow")
{
    const auto model = core::Model::fromFile(example("swmm/mirab-urban.inp"));
    const auto results = runModel("swmm-5.2-epa", model, "swmm");
    const auto base = core::timeBaseFor(&model, &results);
    std::string error;
    const auto sim = core::simulatedSeries(results, "node", "O1", "inflow", &error);
    CHECK(sim.has_value());
    core::ObservedSeries obs;
    obs.table = "node";
    obs.element = "O1";
    obs.variable = "Total Inflow";
    obs.calendar = true;
    for (std::size_t i = 0; i < sim->times.size(); i += 2)
    {
        obs.times.push_back(*base.startEpoch + sim->times[i]);
        obs.values.push_back(sim->values[i]);
    }
    const auto fits = core::evaluateFit({obs}, results, base);
    CHECK(fits[0].ok);
    CHECK_NEAR(fits[0].metrics.nse, 1.0, 1e-9);
    CHECK_NEAR(fits[0].metrics.volumeError, 0.0, 1e-6);
}

TEST_CASE("calibration-run: DDS recovers a pipe roughness multiplier")
{
    const auto base = core::Model::fromFile(example("epanet/mirab-demo.inp"));
    core::CalibrationParameter rough;
    rough.section = "PIPES";
    rough.column = "Roughness";
    rough.min = 0.5;
    rough.max = 1.5;
    const double truth = 0.8;

    // Synthetic observations from the known multiplier.
    auto truthModel = base;
    CHECK(core::applyParameters(truthModel, {rough}, {truth}));
    const auto truthResults = runModel("epanet-2.2-epa", truthModel, "truth");
    std::vector<core::ObservedSeries> observed;
    for (const char* id : {"J5", "J9"})
    {
        core::ObservedSeries s;
        s.table = "node";
        s.element = id;
        s.variable = "Pressure";
        const auto sim = core::simulatedSeries(truthResults, "node", id, "Pressure");
        s.times = sim->times;
        s.values = sim->values;
        observed.push_back(s);
    }
    const core::TimeBase tb = core::timeBaseFor(&base, nullptr);
    int runs = 0;
    auto objective = [&](const std::vector<double>& x) {
        auto m = base;
        if (!core::applyParameters(m, {rough}, x))
            return std::numeric_limits<double>::infinity();
        const auto r = runModel("epanet-2.2-epa", m, "eval");
        ++runs;
        return core::objectiveValue({{core::Metric::Rmse, 1.0}}, observed, core::evaluateFit(observed, r, tb));
    };
    double best = 0;
    const auto x = core::minimizeDds(objective, {rough.min}, {rough.max}, {1.0}, 50, 1, &best);
    CHECK_EQ(runs, 50);
    CHECK_NEAR(x[0], truth, 0.02);
    CHECK(best < 0.2); // RMSE of the pressures, m
}
