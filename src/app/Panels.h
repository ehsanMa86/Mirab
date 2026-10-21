/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
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
#include "mirab/engine/Engine.h"

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

namespace mirab::app
{

class ChartWidget;

/// Time-series chart of the selected element (bottom panel).
class ResultsPanel : public QWidget
{
    Q_OBJECT
public:
    ResultsPanel(ProjectDocument* doc, QWidget* parent = nullptr);
    QString csvForSelection() const;

private:
    void refreshChart();
    /// Redraws the series only (live results grew).
    void plot();

    ProjectDocument* doc_;
    QComboBox* chartVar_;
    QLabel* title_;
    ChartWidget* chart_;
};

/// Project tree: options, every element by type, and data such as patterns
/// and curves, with a filter box and context menus.
class ProjectBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectBrowser(ProjectDocument* doc, QWidget* parent = nullptr);
    /// Engine used to grey out collections it does not simulate.
    void setEngine(const engine::EngineDescriptor* engine);
    /// Expands and selects the item of a pattern, curve, series or named object.
    void revealData(const QString& section, const QString& id);

signals:
    void elementActivated();
    void zoomToRequested(const Selection& sel);
    void deleteRequested();
    void dataActivated(const QString& section, const QString& id);
    void optionsActivated();
    void statusMessage(const QString& text);
    /// A named object was renamed from the browser.
    void objectRenamed(const QString& section, const QString& oldName, const QString& newName);

private:
    void rebuild();
    void syncSelection();
    void applyFilter(const QString& text);
    void showContextMenu(const QPoint& pos);
    void addObject(const QString& kind);
    void renameObject(const QString& kind, const QString& name);
    void deleteObject(const QString& kind, const QString& name);

    ProjectDocument* doc_;
    const engine::EngineDescriptor* engine_ = nullptr;
    QLineEdit* filter_;
    QTreeWidget* tree_;
    QLabel* summary_;
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

} // namespace mirab::app
