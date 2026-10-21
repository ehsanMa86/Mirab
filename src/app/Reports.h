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

#include "ChartWidget.h"
#include "ProjectDocument.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace mirab::app
{

struct ReportInfo
{
    QString kind;   ///< "tables", "timeseries", "profile", "frequency", "variability", "systemflow", "energy", "calibration", "scenarios", "status"
    QString title;
    QString icon;
    QString description;
};

/// Reports offered for a model type.
QList<ReportInfo> availableReports(core::Dialect dialect);

/// Printable content of a report: its statistics, charts, table and text.
struct ReportSnapshot
{
    QString kind;
    QString title;
    QString description;
    QStringList notes; ///< summary and statistics lines
    QVector<ChartWidget::Data> charts;
    QStringList header;      ///< table columns
    QList<QStringList> rows; ///< table cells
    QList<bool> numeric;     ///< right-aligned columns
    QString text;            ///< preformatted text (status report)
    bool isEmpty() const
    {
        return notes.isEmpty() && charts.isEmpty() && rows.isEmpty() && text.isEmpty();
    }
};

/// Content of a report for print layouts and documents. @p page is the open
/// report tab of that kind, if any, so the choices made in it are kept;
/// otherwise a report with default choices is built and discarded.
ReportSnapshot reportSnapshot(const QString& kind, ProjectDocument* doc, const QString& reportFile,
                              QWidget* page = nullptr);

/// Creates a report page for a tab. @p reportFile is the engine report (.rpt)
/// used by the status report. Returns nullptr for an unknown kind.
QWidget* createReport(const QString& kind, ProjectDocument* doc, const QString& reportFile, QWidget* parent);

/// Draws a profile report along the shortest path between two nodes.
/// Returns false when @p report is not a profile report.
bool showProfilePath(QWidget* report, const QString& fromNode, const QString& toNode);

} // namespace mirab::app
