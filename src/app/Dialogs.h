/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/Dialogs.h
 * Purpose: Dialogs of the desktop application: new project, engines, resilience, settings and about.
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
#include "anahita/engine/EngineRegistry.h"

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QCheckBox;

namespace anahita::app
{

class ChartWidget;
class SolverProcess;

class NewProjectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewProjectDialog(core::Dialect dialect, QWidget* parent = nullptr);
    QString flowUnits() const;

private:
    QComboBox* units_;
};

class EngineDownloader;

class EnginesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EnginesDialog(const engine::EngineRegistry& registry, QWidget* parent = nullptr);

signals:
    /// Emitted after an engine was installed or removed.
    void enginesChanged();

private:
    void populate();
    void installFinished(bool ok, const QString& message);

    const engine::EngineRegistry& registry_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* install_ = nullptr;
    QPushButton* installFile_ = nullptr;
    QPushButton* remove_ = nullptr;
    EngineDownloader* downloader_ = nullptr;
};

class ResilienceDialog : public QDialog
{
    Q_OBJECT
public:
    /// @param inputFile engine-ready copy of the model for analyses that re-run hydraulics
    ResilienceDialog(ProjectDocument* doc, const QString& engineId, const QString& inputFile,
                     QWidget* parent = nullptr);

private:
    void run();
    void showPayload(const QJsonObject& payload);
    void exportCsv();

    ProjectDocument* doc_;
    QString engineId_;
    QString inputFile_;
    QComboBox* analysis_;
    QStackedWidget* params_;
    QDoubleSpinBox* requiredPressure_;
    QDoubleSpinBox* threshold_;
    QCheckBox* pda_;
    QDoubleSpinBox* fireFlow_;
    QDoubleSpinBox* residual_;
    QDoubleSpinBox* hour_;
    QLineEdit* nodes_;
    QLabel* summary_;
    QTableWidget* table_;
    ChartWidget* chart_;
    QProgressBar* progress_;
    QPushButton* runButton_;
    SolverProcess* solver_;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    static QString qgisPath();
    static QString findQgis();

private:
    QLineEdit* qgis_;
};

class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace anahita::app
