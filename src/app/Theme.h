/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Theme.h
 * Purpose: Light application theme: palette, style sheet and shared colors.
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

#include <QColor>
#include <QString>

class QApplication;

namespace mirab::app::theme
{

// Design tokens used by custom-painted widgets (map, charts, cards).
inline const QColor kWindow(244, 246, 249);
inline const QColor kSurface(255, 255, 255);
inline const QColor kSurfaceAlt(247, 249, 252);
inline const QColor kBorder(221, 227, 234);
inline const QColor kBorderStrong(199, 207, 217);
inline const QColor kText(31, 42, 55);
inline const QColor kTextMuted(96, 110, 128);
inline const QColor kAccent(31, 111, 235);
inline const QColor kAccentHover(24, 94, 204);
inline const QColor kAccentSoft(220, 235, 255);
inline const QColor kSelection(255, 140, 0);
inline const QColor kSuccess(30, 140, 70);
inline const QColor kWarning(196, 120, 0);
inline const QColor kDanger(200, 50, 50);
inline const QColor kMapBackground(250, 251, 253);

/// Applies the Fusion style with a light palette and the Mirab style sheet.
/// The application always uses the light scheme, whatever the system setting.
void apply(QApplication& app);

/// Style sheet of the whole application (exposed for tests and plugins).
QString styleSheet();

} // namespace mirab::app::theme
