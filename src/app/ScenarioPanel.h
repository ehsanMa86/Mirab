/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ScenarioPanel.h
 * Purpose: Scenarios dock: the scenarios of a project, their changes, and runs of several scenarios.
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
#include "mirab/engine/EngineRegistry.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QToolButton;
class QTreeWidget;

namespace mirab::app
{

class SolverProcess;

/// Lists the base model and its scenarios as a tree (a scenario under the
/// one it is based on), and the changes of the active scenario.
class ScenarioPanel : public QWidget
{
    Q_OBJECT
public:
    ScenarioPanel(ProjectDocument* doc, const engine::EngineRegistry* registry, QWidget* parent = nullptr);

    void newScenario();
    void duplicateScenario();
    void editScenario();
    void deleteScenario();

signals:
    void runRequested();
    void compareRequested();
    void statusMessage(const QString& text);

private:
    void rebuild();
    void rebuildChanges();
    /// Selected scenario; empty for the base model or no selection.
    QString selected() const;
    void activate(const QString& name);
    void updateButtons();

    ProjectDocument* doc_;
    const engine::EngineRegistry* registry_;
    QTreeWidget* tree_;
    QTreeWidget* changes_;
    QLabel* changesTitle_;
    QLabel* inherited_;
    QToolButton* newButton_;
    QToolButton* duplicateButton_;
    QToolButton* editButton_;
    QToolButton* deleteButton_;
    QPushButton* activateButton_;
    QPushButton* revertButton_;
};

/// Name, description, parent and engine of a scenario.
class ScenarioDialog : public QDialog
{
    Q_OBJECT
public:
    /// @p original is the scenario being edited, or empty for a new one.
    ScenarioDialog(const ProjectDocument* doc, const engine::EngineRegistry* registry, const core::Scenario& scenario,
                   const QString& original, QWidget* parent = nullptr);
    core::Scenario scenario() const;

protected:
    void accept() override;

private:
    const ProjectDocument* doc_;
    QString original_;
    core::Scenario scenario_;
    QLineEdit* name_;
    QPlainTextEdit* description_;
    QComboBox* parent_;
    QComboBox* engine_;
    QComboBox* msx_ = nullptr;
    QLineEdit* msxFile_ = nullptr;
    QLabel* error_;
};

/// Runs scenarios, each in its own solver process, several at a time.
class ScenarioRunDialog : public QDialog
{
    Q_OBJECT
public:
    ScenarioRunDialog(ProjectDocument* doc, const engine::EngineRegistry* registry, QWidget* parent = nullptr);
    /// Checks the given scenarios only ("" is the base model).
    void select(const QStringList& names);
    /// Starts the checked runs.
    void start();
    bool isRunning() const;

signals:
    /// A run ended; its results file was written when @p ok.
    void scenarioFinished(const QString& name, bool ok);
    void allFinished();

protected:
    void reject() override;

private:
    struct Job
    {
        QString name;
        int row = -1;
        SolverProcess* process = nullptr;
        QString input;
        bool queued = false;
    };
    void startNext();
    bool launch(Job& job, QString* error);
    void setStatus(int row, const QString& text, int progress = -1);
    void stop();

    ProjectDocument* doc_;
    const engine::EngineRegistry* registry_;
    QTableWidget* table_;
    QSpinBox* jobs_;
    QPushButton* runButton_;
    QPushButton* stopButton_;
    QLabel* summary_;
    std::vector<Job> queue_;
    int finished_ = 0;
    int failed_ = 0;
};

} // namespace mirab::app
