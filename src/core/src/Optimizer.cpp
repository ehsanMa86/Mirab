/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Optimizer.cpp
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
#include "mirab/core/Optimizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace mirab::core
{

Random::Random(std::uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

std::uint64_t Random::next()
{
    // splitmix64
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double Random::uniform()
{
    return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
}

double Random::normal()
{
    if (haveSpare_)
    {
        haveSpare_ = false;
        return spare_;
    }
    // Marsaglia polar method
    double u = 0, v = 0, s = 0;
    do
    {
        u = 2.0 * uniform() - 1.0;
        v = 2.0 * uniform() - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    const double m = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * m;
    haveSpare_ = true;
    return u * m;
}

int Random::below(int n)
{
    return n <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n));
}

DdsOptimizer::DdsOptimizer(std::vector<double> lower, std::vector<double> upper,
                           std::vector<double> initial, int budget, std::uint64_t seed,
                           double perturbation)
    : lower_(std::move(lower)), upper_(std::move(upper)), best_(std::move(initial)),
      bestValue_(std::numeric_limits<double>::infinity()), budget_(std::max(1, budget)),
      r_(perturbation), rng_(seed)
{
    if (lower_.size() != upper_.size() || lower_.size() != best_.size() || lower_.empty())
        throw std::invalid_argument("DDS needs bounds and an initial value for every parameter");
    for (std::size_t j = 0; j < best_.size(); ++j)
    {
        if (upper_[j] < lower_[j])
            std::swap(lower_[j], upper_[j]);
        best_[j] = std::clamp(best_[j], lower_[j], upper_[j]);
    }
}

std::vector<double> DdsOptimizer::candidate(int iteration)
{
    const std::size_t n = best_.size();
    // Probability of perturbing each dimension: 1 - ln(i) / ln(m).
    const double p = budget_ > 1 ? 1.0 - std::log(static_cast<double>(iteration)) /
                                             std::log(static_cast<double>(budget_))
                                 : 1.0;
    std::vector<double> x = best_;
    std::vector<std::size_t> chosen;
    for (std::size_t j = 0; j < n; ++j)
    {
        if (rng_.uniform() < p)
            chosen.push_back(j);
    }
    if (chosen.empty())
        chosen.push_back(static_cast<std::size_t>(rng_.below(static_cast<int>(n))));
    for (std::size_t j : chosen)
    {
        const double lo = lower_[j], hi = upper_[j];
        const double range = hi - lo;
        if (range <= 0)
            continue;
        double v = best_[j] + r_ * range * rng_.normal();
        // Reflect at the bounds; a second violation puts the value on the bound.
        if (v < lo)
        {
            v = lo + (lo - v);
            if (v > hi)
                v = lo;
        }
        else if (v > hi)
        {
            v = hi - (v - hi);
            if (v < lo)
                v = hi;
        }
        x[j] = v;
    }
    return x;
}

std::vector<std::vector<double>> DdsOptimizer::propose(int count)
{
    std::vector<std::vector<double>> out;
    if (finished())
        return out;
    if (proposed_ == 0)
    {
        out.push_back(best_);
        proposed_ = 1;
        return out;
    }
    count = std::max(1, std::min(count, budget_ - evaluations_));
    for (int k = 0; k < count; ++k)
        out.push_back(candidate(proposed_ + k));
    proposed_ += count;
    return out;
}

void DdsOptimizer::report(const std::vector<std::vector<double>>& points, const std::vector<double>& values)
{
    for (std::size_t k = 0; k < points.size() && k < values.size(); ++k)
    {
        ++evaluations_;
        const double f = std::isnan(values[k]) ? std::numeric_limits<double>::infinity() : values[k];
        // "<=" lets the search move along flat parts of the objective.
        if (f <= bestValue_)
        {
            bestValue_ = f;
            best_ = points[k];
        }
    }
    proposed_ = std::max(proposed_, evaluations_);
}

std::vector<double> minimizeDds(const std::function<double(const std::vector<double>&)>& f,
                                const std::vector<double>& lower, const std::vector<double>& upper,
                                const std::vector<double>& initial, int budget, std::uint64_t seed, double* bestValue)
{
    DdsOptimizer dds(lower, upper, initial, budget, seed);
    while (!dds.finished())
    {
        const auto batch = dds.propose(1);
        std::vector<double> values;
        for (const auto& x : batch)
            values.push_back(f(x));
        dds.report(batch, values);
    }
    if (bestValue)
        *bestValue = dds.bestValue();
    return dds.best();
}

std::vector<std::vector<double>> latinHypercube(const std::vector<double>& lower,
                                                const std::vector<double>& upper, int count,
                                                std::uint64_t seed)
{
    Random rng(seed);
    const std::size_t dims = lower.size();
    count = std::max(0, count);
    std::vector<std::vector<double>> pts(static_cast<std::size_t>(count), std::vector<double>(dims, 0.0));
    std::vector<int> strata(static_cast<std::size_t>(count));
    for (std::size_t j = 0; j < dims; ++j)
    {
        std::iota(strata.begin(), strata.end(), 0);
        for (int i = count - 1; i > 0; --i)
            std::swap(strata[static_cast<std::size_t>(i)],
                      strata[static_cast<std::size_t>(rng.below(i + 1))]);
        const double range = upper[j] - lower[j];
        for (int i = 0; i < count; ++i)
        {
            const double u = (strata[static_cast<std::size_t>(i)] + rng.uniform()) / count;
            pts[static_cast<std::size_t>(i)][j] = lower[j] + u * range;
        }
    }
    return pts;
}

namespace
{
std::vector<double> ranks(const std::vector<double>& v)
{
    std::vector<std::size_t> order(v.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
    std::vector<double> r(v.size());
    for (std::size_t i = 0; i < order.size();)
    {
        std::size_t j = i;
        while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]])
            ++j;
        const double avg = (static_cast<double>(i) + static_cast<double>(j)) / 2.0 + 1.0;
        for (std::size_t k = i; k <= j; ++k)
            r[order[k]] = avg;
        i = j + 1;
    }
    return r;
}
} // namespace

double spearman(const std::vector<double>& x, const std::vector<double>& y)
{
    std::vector<double> a, b;
    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i)
    {
        if (std::isfinite(x[i]) && std::isfinite(y[i]))
        {
            a.push_back(x[i]);
            b.push_back(y[i]);
        }
    }
    const std::size_t n = a.size();
    if (n < 3)
        return std::numeric_limits<double>::quiet_NaN();
    const auto ra = ranks(a), rb = ranks(b);
    const double ma = std::accumulate(ra.begin(), ra.end(), 0.0) / n;
    const double mb = std::accumulate(rb.begin(), rb.end(), 0.0) / n;
    double sab = 0, saa = 0, sbb = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        sab += (ra[i] - ma) * (rb[i] - mb);
        saa += (ra[i] - ma) * (ra[i] - ma);
        sbb += (rb[i] - mb) * (rb[i] - mb);
    }
    if (saa <= 0 || sbb <= 0)
        return std::numeric_limits<double>::quiet_NaN();
    return sab / std::sqrt(saa * sbb);
}

} // namespace mirab::core
