/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ScenarioCompare.h
 * Purpose: Scenario comparison report: results of several scenarios side by side.
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
#include "Reports.h"

#include <QWidget>

namespace mirab::app
{

/// Report page that compares the results of the base model and its
/// scenarios: key figures, a table of one result per element with the
/// differences from a reference, and the time series of one element.
QWidget* createScenarioComparison(ProjectDocument* doc, QWidget* parent);
/// Printable content of a comparison page: the key figures as notes, the
/// element table and the chart. False when @p page is not one.
bool scenarioComparisonSnapshot(const QWidget* page, ReportSnapshot& s);

} // namespace mirab::app
