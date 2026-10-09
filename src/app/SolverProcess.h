/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SolverProcess.h
 * Purpose: Runs mirab-cli as an isolated solver process and relays its progress.
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

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace mirab::app
{

/// Engines run in a separate process: a crash or a Fortran STOP inside a
/// solver ends that process only, never the desktop application.
class SolverProcess : public QObject
{
    Q_OBJECT
public:
    explicit SolverProcess(QObject* parent = nullptr);
    ~SolverProcess() override;

    static QString cliPath();
    bool start(const QStringList& arguments);
    void cancel();
    bool isRunning() const { return process_.state() != QProcess::NotRunning; }

signals:
    void progress(double fraction, const QString& stage);
    void log(const QString& level, const QString& message);
    void finished(bool success, const QJsonObject& result);

private:
    void readOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QProcess process_;
    QByteArray buffer_;
    QJsonObject done_;
    bool haveDone_ = false;
    QString stderr_;
};

} // namespace mirab::app
