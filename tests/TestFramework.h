/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/TestFramework.h
 * Purpose: Minimal self-registering unit test framework used by the Mirab test suite.
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

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mirab::test
{

struct Case
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Register
{
    Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

class Failure : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

inline int runAll(int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : "";
    int failed = 0, run = 0;
    for (const auto& c : registry())
    {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos)
            continue;
        ++run;
        try
        {
            c.fn();
            std::cout << "[pass] " << c.name << "\n";
        }
        catch (const std::exception& e)
        {
            ++failed;
            std::cout << "[FAIL] " << c.name << ": " << e.what() << "\n";
        }
    }
    std::cout << run - failed << " of " << run << " test(s) passed\n";
    return failed == 0 && run > 0 ? 0 : 1;
}

} // namespace mirab::test

#define MIRAB_CAT2(a, b) a##b
#define MIRAB_CAT(a, b) MIRAB_CAT2(a, b)

#define TEST_CASE(name)                                                                           \
    static void MIRAB_CAT(test_fn_, __LINE__)();                                                \
    static ::mirab::test::Register MIRAB_CAT(test_reg_, __LINE__)(name,                       \
                                                                      MIRAB_CAT(test_fn_, __LINE__)); \
    static void MIRAB_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                               \
    do                                                                                            \
    {                                                                                             \
        if (!(cond))                                                                              \
        {                                                                                         \
            std::ostringstream os_;                                                               \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed";                    \
            throw ::mirab::test::Failure(os_.str());                                            \
        }                                                                                         \
    } while (0)

#define CHECK_EQ(a, b)                                                                            \
    do                                                                                            \
    {                                                                                             \
        const auto va_ = (a);                                                                     \
        const auto vb_ = (b);                                                                     \
        if (!(va_ == vb_))                                                                        \
        {                                                                                         \
            std::ostringstream os_;                                                               \
            os_ << __FILE__ << ":" << __LINE__ << ": " #a " == " #b " failed (" << va_ << " vs "  \
                << vb_ << ")";                                                                    \
            throw ::mirab::test::Failure(os_.str());                                            \
        }                                                                                         \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                     \
    do                                                                                            \
    {                                                                                             \
        const double va_ = (a);                                                                   \
        const double vb_ = (b);                                                                   \
        if (!(std::fabs(va_ - vb_) <= (tol)))                                                     \
        {                                                                                         \
            std::ostringstream os_;                                                               \
            os_ << __FILE__ << ":" << __LINE__ << ": " #a " ~ " #b " failed (" << va_ << " vs "   \
                << vb_ << ")";                                                                    \
            throw ::mirab::test::Failure(os_.str());                                            \
        }                                                                                         \
    } while (0)
