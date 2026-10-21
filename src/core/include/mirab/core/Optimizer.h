/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Optimizer.h
 * Purpose: Dynamically Dimensioned Search and Latin hypercube sampling for calibration.
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

#include <cstdint>
#include <functional>
#include <vector>

namespace mirab::core
{

/// Small portable random number generator (the same sequence on every
/// platform, unlike the distributions of the C++ library).
class Random
{
public:
    explicit Random(std::uint64_t seed = 1);
    std::uint64_t next();
    double uniform();  ///< in [0, 1)
    double normal();   ///< standard normal
    int below(int n);  ///< in [0, n)

private:
    std::uint64_t state_;
    bool haveSpare_ = false;
    double spare_ = 0.0;
};

/// Dynamically Dimensioned Search (Tolson and Shoemaker, 2007) in ask/tell
/// form. The search starts at the initial point, then perturbs a random
/// subset of the dimensions of the best point; the subset shrinks as the
/// budget is used. Candidates can be requested in batches to evaluate them in
/// parallel; each batch is drawn around the best point known so far.
class DdsOptimizer
{
public:
    DdsOptimizer(std::vector<double> lower, std::vector<double> upper, std::vector<double> initial,
                 int budget, std::uint64_t seed = 1, double perturbation = 0.2);

    /// Up to @p count new points (fewer near the end of the budget). The first
    /// call returns the initial point alone.
    std::vector<std::vector<double>> propose(int count);
    /// Objective values (lower is better) of the points of the last proposal.
    void report(const std::vector<std::vector<double>>& points, const std::vector<double>& values);

    bool finished() const { return evaluations_ >= budget_; }
    int evaluations() const { return evaluations_; }
    int budget() const { return budget_; }
    const std::vector<double>& best() const { return best_; }
    double bestValue() const { return bestValue_; }

private:
    std::vector<double> candidate(int iteration);

    std::vector<double> lower_, upper_, best_;
    double bestValue_;
    int budget_;
    int evaluations_ = 0;
    int proposed_ = 0;
    double r_;
    Random rng_;
};

/// Minimises @p f with DDS, one evaluation at a time.
std::vector<double> minimizeDds(const std::function<double(const std::vector<double>&)>& f,
                                const std::vector<double>& lower, const std::vector<double>& upper,
                                const std::vector<double>& initial, int budget, std::uint64_t seed = 1,
                                double* bestValue = nullptr);

/// @p count points of a Latin hypercube sample: each dimension's range is cut
/// into @p count equal strata and every stratum is used exactly once.
std::vector<std::vector<double>> latinHypercube(const std::vector<double>& lower,
                                                const std::vector<double>& upper, int count,
                                                std::uint64_t seed = 1);

/// Spearman rank correlation (average ranks for ties); NaN when undefined.
double spearman(const std::vector<double>& x, const std::vector<double>& y);

} // namespace mirab::core
