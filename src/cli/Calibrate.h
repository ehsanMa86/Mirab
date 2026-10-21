/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/cli/Calibrate.h
 * Purpose: mirab-cli calibrate: goodness-of-fit metrics and automated calibration.
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

#include <filesystem>
#include <string>
#include <vector>

namespace mirab::cli
{

/// Runs "mirab-cli calibrate ..." with the arguments after the command name.
int runCalibrate(const std::vector<std::string>& args);

/// Usage lines for the help text.
const char* calibrateUsage();

/// Path of the running mirab-cli program; empty when unknown.
std::filesystem::path programPath();
/// Runs a program with its output written to @p log and waits for it; returns
/// its exit code (-1 with @p error set when it could not be started).
int runChildProcess(const std::filesystem::path& exe, const std::vector<std::string>& args,
                    const std::filesystem::path& log, std::string* error);

} // namespace mirab::cli
