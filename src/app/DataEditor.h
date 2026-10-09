/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/DataEditor.h
 * Purpose: Editor for time patterns, curves and time series, with a live chart.
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

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace anahita::app
{

class ChartWidget;

/// Edits one pattern ([PATTERNS]), curve ([CURVES]) or time series
/// ([TIMESERIES]) of the project. Changes are applied as one undoable edit.
class DataEditor : public QWidget
{
    Q_OBJECT
public:
    DataEditor(ProjectDocument* doc, const QString& section, const QString& id, QWidget* parent = nullptr);

private:
    void load();
    void apply();
    void updateChart();
    void setDirty(bool on);
    double patternStepHours() const;

    ProjectDocument* doc_;
    QString section_;
    QString id_;
    bool swmm_ = false;
    bool loading_ = false;
    QTableWidget* table_;
    QComboBox* curveType_;
    QLabel* state_;
    QPushButton* apply_;
    QPushButton* revert_;
    ChartWidget* chart_;
};

} // namespace anahita::app
