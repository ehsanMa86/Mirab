/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/engine/include/anahita/engine/Compatibility.h
 * Purpose: Checks a project against an engine's capability manifest and prepares an engine-ready input file.
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

#include "anahita/core/Model.h"
#include "anahita/engine/Engine.h"

#include <string>
#include <vector>

namespace anahita::engine
{

struct CompatibilityIssue
{
    Support level = Support::Full;
    core::Feature feature{};
    std::string message;
    std::string section;
    int row = -1;
};

struct CompatibilityReport
{
    std::vector<CompatibilityIssue> issues;
    bool runnable() const;
    int count(Support level) const;
};

CompatibilityReport checkCompatibility(const core::Model& model, const EngineDescriptor& engine);

/// Returns the input text to hand to the engine: features the engine ignores
/// are removed, everything else is passed through unchanged. @p notes receives
/// a description of each change.
std::string prepareInput(const core::Model& model, const EngineDescriptor& engine,
                         std::vector<std::string>* notes = nullptr);

} // namespace anahita::engine
