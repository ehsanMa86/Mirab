/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MapDecorations.h
 * Purpose: Map furniture for print layouts and animations: legend, scale bar, north arrow and
 * labels.
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

#include "MapView.h"

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

class QPainter;

namespace mirab::app::decor
{

/// Application font at @p px painter units (1/96 inch), independent of the screen.
QFont font(double px, bool bold = false);
/// Painter units per millimetre.
constexpr double kUnitsPerMm = 96.0 / 25.4;

/// Legend of the result classes and of the element symbols in @p kinds.
/// Returns the size; draws only when @p draw is set.
QSizeF legend(QPainter* p, const QPointF& topLeft, const MapView::Legend& legend,
              const QStringList& kinds, const QFont& f, bool framed, double maxHeight = 1e9);

/// Alternating black and white scale bar whose left end is at @p origin (the
/// bottom of the bar). Returns the occupied rectangle; draws when @p p is set.
QRectF scaleBar(QPainter* p, const QPointF& origin, double groundPerUnit, const QString& unit,
                double maxWidth, const QFont& f, bool framed);

/// North arrow fitted in @p box, rotated clockwise by @p angle degrees.
void northArrow(QPainter& p, const QRectF& box, double angle, const QFont& f, bool framed);

/// Text in a light rounded box. @p anchor is the corner given by @p corner
/// (Qt::AlignLeft/AlignRight with Qt::AlignTop/AlignBottom). Returns the box.
QRectF labelBox(QPainter& p, const QPointF& anchor, Qt::Alignment corner, const QString& text,
                const QFont& f);

} // namespace mirab::app::decor
