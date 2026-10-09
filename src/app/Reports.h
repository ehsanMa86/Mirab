/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Reports.h
 * Purpose: Report tabs: network tables, time series, profile, frequency, variability, system flow, energy and status.
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

#include <QString>
#include <QWidget>

namespace mirab::app
{

struct ReportInfo
{
    QString kind;   ///< "tables", "timeseries", "profile", "frequency", "variability", "systemflow", "energy", "status"
    QString title;
    QString icon;
    QString description;
};

/// Reports offered for a model type.
QList<ReportInfo> availableReports(core::Dialect dialect);

/// Creates a report page for a tab. @p reportFile is the engine report (.rpt)
/// used by the status report. Returns nullptr for an unknown kind.
QWidget* createReport(const QString& kind, ProjectDocument* doc, const QString& reportFile, QWidget* parent);

} // namespace mirab::app
