/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/EpanetRun.h
 * Purpose: Shared EPANET simulation loop used by the EPANET and EPANET-MSX adapters.
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

#include "mirab/engine/Engine.h"
#include "mirab/engine/EpanetToolkit.h"

#include <filesystem>
#include <string>

namespace mirab::engine::detail
{

std::string formatEpanetVersion(int v);

/// Runs hydraulics and single-species quality concurrently on an open
/// session and records every reporting period into @p results. Frames are
/// delivered through @p pacer with layout 1. Progress is mapped into
/// [progressFrom, progressTo]. Returns false when the user cancelled.
bool runEpanetLoop(EpanetSession& session, const std::filesystem::path& inputFile,
                   core::ResultSet& results, const RunCallbacks& callbacks, FramePacer& pacer,
                   double progressFrom = 0.0, double progressTo = 1.0);

/// Frame of the last period recorded in @p results.
ResultFrame lastFrame(const core::ResultSet& results, int layout);

} // namespace mirab::engine::detail
