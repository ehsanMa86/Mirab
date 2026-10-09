/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/Icons.h
 * Purpose: Vector line icons drawn in code, crisp at every screen scale.
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

#include <QIcon>
#include <QString>

namespace anahita::app::icons
{

/// Returns the icon with the given name, for example "open", "run",
/// "zoom-fit", "basemap" or an element kind such as "Junction" or "Pipe".
/// Unknown names give an empty icon.
QIcon get(const QString& name);

/// Icon for an element kind (Junction, Reservoir, Tank, Pipe, Pump, Valve,
/// Outfall, Storage, Divider, Conduit, Orifice, Weir, Outlet, Subcatchment, RainGage).
QIcon element(const QString& kind);

} // namespace anahita::app::icons
