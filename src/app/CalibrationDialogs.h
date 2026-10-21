/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/CalibrationDialogs.h
 * Purpose: Dialogs for observed data and automated calibration.
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
#include "mirab/core/Calibration.h"

#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QString>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace mirab::app
{

class ChartWidget;
class ObservedData;
class SolverProcess;

/// Adds, removes and links observed-data files, with a preview chart.
class ObservedDataDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ObservedDataDialog(ProjectDocument* doc, QWidget* parent = nullptr);

    void accept() override;

private:
    void addFiles();
    void removeSource();
    void showSource(int index);
    void storeSource();
    void fillLinks();
    void updatePreview();
    QString fileText(const core::ObservedSource& source);
    QString absolutePath(const QString& stored) const;
    QStringList variablesFor(const QString& table) const;

    ProjectDocument* doc_;
    ObservedData* data_;
    core::CalibrationConfig config_;
    int current_ = -1;
    bool loading_ = false;
    QMap<QString, QString> texts_;

    QListWidget* sources_;
    QLabel* fileLabel_;
    QComboBox* format_;
    QGroupBox* tableBox_;
    QComboBox* timeColumn_;
    QComboBox* timeFormat_;
    QComboBox* timeUnit_;
    QComboBox* dateOrder_;
    QLineEdit* noData_;
    QTableWidget* links_;
    QLineEdit* start_;
    ChartWidget* preview_;
    QLabel* status_;
};

/// Sets up and runs an automated calibration in a background solver process.
class CalibrationDialog : public QDialog
{
    Q_OBJECT
public:
    CalibrationDialog(ProjectDocument* doc, const QString& engineId, const QString& msxFile,
                      QWidget* parent = nullptr);
    ~CalibrationDialog() override;

    /// Starts the calibration (also used for automated screenshots).
    void start();

protected:
    void reject() override;

private:
    void addParameter(const core::CalibrationParameter& p);
    void fillColumns(int row);
    void updateRowCount(int row);
    void addTerm(const core::ObjectiveTerm& t);
    bool readSetup(core::CalibrationConfig& cfg, QString* error);
    void onEvent(const QString& name, const QJsonObject& o);
    void onFinished(bool ok, const QJsonObject& r);
    void plotProgress();
    void applyBest();
    void setRunning(bool running);
    void cleanUp();

    ProjectDocument* doc_;
    ObservedData* data_;
    QString engineId_;
    QString msxFile_;
    SolverProcess* solver_;
    QString scratchInput_;
    QString configFile_;
    std::vector<core::CalibrationParameter> runParams_;
    std::vector<double> best_;
    QVector<QPointF> evaluations_;
    QVector<QPointF> bestSoFar_;

    QTableWidget* params_;
    QTableWidget* terms_;
    QSpinBox* budget_;
    QSpinBox* jobs_;
    QSpinBox* seed_;
    QComboBox* method_;
    QLabel* status_;
    QProgressBar* progress_;
    ChartWidget* chart_;
    QTableWidget* bestTable_;
    QPushButton* startBtn_;
    QPushButton* cancelBtn_;
    QPushButton* applyBtn_;
};

} // namespace mirab::app
