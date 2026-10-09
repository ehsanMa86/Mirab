/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/Panels.h
 * Purpose: Results, project browser and input-file editor panels of the main window.
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

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace anahita::app
{

class ChartWidget;
class MapView;

/// Map coloring, time navigation and the time-series chart.
class ResultsPanel : public QWidget
{
    Q_OBJECT
public:
    ResultsPanel(ProjectDocument* doc, MapView* map, QWidget* parent = nullptr);
    QString csvForSelection() const;

private:
    void resultsChanged();
    void refreshChart();
    void updateTimeLabel();

    ProjectDocument* doc_;
    MapView* map_;
    QComboBox* nodeVar_;
    QComboBox* linkVar_;
    QComboBox* areaVar_;
    QLabel* areaLabel_;
    QComboBox* chartVar_;
    QSlider* slider_;
    QLabel* timeLabel_;
    QToolButton* play_;
    QTimer* timer_;
    ChartWidget* chart_;
};

/// Tree of every element in the model, with a filter box.
class ProjectBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectBrowser(ProjectDocument* doc, QWidget* parent = nullptr);

signals:
    void elementActivated();

private:
    void rebuild();
    void syncSelection();
    void applyFilter(const QString& text);

    ProjectDocument* doc_;
    QLineEdit* filter_;
    QTreeWidget* tree_;
    bool syncing_ = false;
};

class InpHighlighter : public QSyntaxHighlighter
{
public:
    explicit InpHighlighter(QTextDocument* doc);

protected:
    void highlightBlock(const QString& text) override;
};

/// Plain-text view of the input file. Edits are applied as one undoable step.
class InpEditor : public QWidget
{
    Q_OBJECT
public:
    explicit InpEditor(ProjectDocument* doc, QWidget* parent = nullptr);
    void reload();
    bool hasPendingEdits() const { return dirty_; }
    bool apply();

signals:
    void statusMessage(const QString& text);

private:
    ProjectDocument* doc_;
    QPlainTextEdit* edit_;
    QPushButton* applyButton_;
    QPushButton* revertButton_;
    QLabel* state_;
    bool dirty_ = false;
    bool loading_ = false;
};

/// Editor for the EPANET-MSX file attached to the project.
class MsxEditor : public QWidget
{
    Q_OBJECT
public:
    explicit MsxEditor(QWidget* parent = nullptr);
    bool load(const QString& path, QString* error = nullptr);
    bool save(QString* error = nullptr);
    QString path() const { return path_; }
    bool isModified() const;

signals:
    void statusMessage(const QString& text);

private:
    QPlainTextEdit* edit_;
    QLabel* label_;
    QString path_;
};

} // namespace anahita::app
