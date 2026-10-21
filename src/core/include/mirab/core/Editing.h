/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Editing.h
 * Purpose: Network edits on the map: splitting a pipe or conduit and reversing link direction.
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

#include "mirab/core/Model.h"

#include <string>
#include <string_view>

namespace mirab::core
{

struct SplitResult
{
    std::string node; ///< the new node
    std::string link; ///< the new link (the downstream part)
    double fraction = 0.0; ///< where the link was split, 0..1 along its drawn length
};

/// Splits an EPANET pipe or SWMM conduit at the point of its drawn line
/// closest to @p at. A new junction is placed there and the part beyond it
/// becomes a new link that copies the properties and attached data of the
/// original. Lengths are split in proportion to the drawn length and the
/// vertices go to the part they lie on. EPANET: the junction elevation is
/// interpolated between the end nodes, minor losses stay with the first part.
/// SWMM: the junction invert is the conduit invert at that point (offsets
/// honour LINK_OFFSETS), its depth reaches the interpolated rim, entry
/// losses and the flap gate stay with the first part and exit losses go to
/// the second. Empty IDs are chosen automatically.
bool splitLink(Model& model, std::string_view linkId, Point at, SplitResult* result = nullptr,
               std::string* error = nullptr, std::string nodeId = {}, std::string newLinkId = {});

/// Swaps the end nodes of a link and reverses its vertices. SWMM conduits
/// also swap their inlet and outlet offsets and their entry and exit losses.
bool reverseLink(Model& model, std::string_view linkId, std::string* error = nullptr);

} // namespace mirab::core
