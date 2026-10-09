/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/Adapters.h
 * Purpose: Internal factory functions for the engine adapters.
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

#include <filesystem>
#include <memory>

namespace mirab::engine::detail
{

std::unique_ptr<Engine> makeEpanetEngine(EngineDescriptor d, std::filesystem::path library);
std::unique_ptr<Engine> makeMsxEngine(EngineDescriptor d, std::filesystem::path msxLibrary,
                                      std::filesystem::path epanetLibrary);
std::unique_ptr<Engine> makeSwmmEngine(EngineDescriptor d, std::filesystem::path library);
std::unique_ptr<Engine> makeSwmm5PlusEngine(EngineDescriptor d, std::filesystem::path library);

} // namespace mirab::engine::detail
