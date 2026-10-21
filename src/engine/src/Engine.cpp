/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/Engine.cpp
 * Purpose: Pacing of the result frames that engines deliver during a run.
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
#include "mirab/engine/Engine.h"

namespace mirab::engine
{

bool FramePacer::due() const
{
    if (!active())
        return false;
    if (!sentAny_ || cb_.frameInterval <= 0.0)
        return true;
    const auto elapsed = std::chrono::steady_clock::now() - last_;
    return std::chrono::duration<double>(elapsed).count() >= cb_.frameInterval;
}

void FramePacer::send(const ResultFrame& frame)
{
    if (!active() || !frame.results)
        return;
    cb_.frame(frame);
    lastPeriod_ = frame.period;
    lastLayout_ = frame.layout;
    last_ = std::chrono::steady_clock::now();
    sentAny_ = true;
}

void FramePacer::finish(const ResultFrame& frame)
{
    if (frame.period < 0 || (frame.period == lastPeriod_ && frame.layout == lastLayout_))
        return;
    send(frame);
}

void FramePacer::restart()
{
    lastPeriod_ = -1;
    sentAny_ = false;
}

} // namespace mirab::engine
