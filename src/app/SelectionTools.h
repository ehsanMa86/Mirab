/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SelectionTools.h
 * Purpose: Selection tools: select by query, network traces and shortest paths on the project selection.
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

#include "mirab/core/Network.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace mirab::app
{

struct TraceSettings
{
    /// Follow the flow direction of the results shown (when there are any).
    bool followFlows = true;
    /// Closed links (input status, or status in the results) stop a trace.
    bool stopAtClosed = true;
};

/// Traces from the selected nodes and links. Returns the elements reached,
/// start elements included, and a summary for the status bar.
QList<Selection> traceSelection(const ProjectDocument* doc, core::TraceDirection direction,
                                const TraceSettings& settings, QString* summary);

/// Nodes and links of the shortest path between the first and the last
/// selected node. Empty (with a message) when there are fewer than two nodes
/// or they are not connected.
QList<Selection> shortestPathSelection(const ProjectDocument* doc, QString* message);

/// Converts a distance in project length units (m or ft) to map units, using
/// the project's coordinate system when it has one.
double toMapUnits(const ProjectDocument* doc, double distance);

/// Selects elements by attribute conditions on their properties and on the
/// results shown, optionally near the current selection.
class QueryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QueryDialog(ProjectDocument* doc, QWidget* parent = nullptr);
    /// Sets the expression (for automation and tests).
    void setExpression(const QString& text);

signals:
    void statusMessage(const QString& text);

private:
    void fillKinds();
    void fillFields();
    void updatePreview();
    void apply();
    /// Matching elements; false with a message when the expression is not valid.
    bool evaluate(QList<Selection>& out, QString* message) const;

    ProjectDocument* doc_;
    QComboBox* category_;
    QComboBox* kind_;
    QLineEdit* expression_;
    QListWidget* fields_;
    QCheckBox* near_;
    QDoubleSpinBox* distance_;
    QComboBox* mode_;
    QLabel* preview_;
    QLabel* resultsNote_;
    QTimer* timer_;
};

} // namespace mirab::app
