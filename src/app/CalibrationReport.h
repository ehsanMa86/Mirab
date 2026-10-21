/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/CalibrationReport.h
 * Purpose: Calibration report: simulated versus observed charts and goodness-of-fit table.
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

#include "ProjectDocument.h"

#include <QColor>
#include <QString>
#include <QWidget>

namespace mirab::app
{

/// Report tab comparing every observed series with the simulation.
QWidget* createCalibrationReport(ProjectDocument* doc, QWidget* parent);

/// Quality band of a metric value: 3 very good, 2 good, 1 satisfactory,
/// 0 unsatisfactory, -1 when the metric has no bands or no value.
int qualityBand(const QString& metric, double value);
/// Background colour of a quality band.
QColor qualityColor(int band);
QString qualityName(int band);

} // namespace mirab::app
