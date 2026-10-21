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

#include "mirab/core/Results.h"

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <memory>

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
    /// Asks the solver to wait between two time steps; paused() follows.
    void pause();
    void resume();
    bool isRunning() const { return process_.state() != QProcess::NotRunning; }
    bool isPaused() const { return paused_; }

    /// Results received so far ("layout" and "frame" events of a run started
    /// with --live), or null before the first frame. The set grows in place.
    std::shared_ptr<const core::ResultSet> liveResults() const { return live_; }
    /// Simulation time of the latest frame, in seconds.
    double liveTime() const { return liveTime_; }

signals:
    void progress(double fraction, const QString& stage);
    void log(const QString& level, const QString& message);
    void finished(bool success, const QJsonObject& result);
    /// A new live result set started (the first frame of a layout).
    void liveStarted();
    /// Periods from @p firstChanged on were appended to (or replaced in) liveResults().
    void liveUpdated(int firstChanged);
    void paused(double seconds);
    void resumed();

private:
    void readOutput();
    void readLiveLine(const QByteArray& line, bool* started, bool* grew);
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QProcess process_;
    QByteArray buffer_;
    QJsonObject done_;
    bool haveDone_ = false;
    QString stderr_;
    bool paused_ = false;
    std::shared_ptr<core::ResultSet> live_;
    std::shared_ptr<core::ResultSet> pendingLayout_;
    int layoutId_ = 0;
    double liveTime_ = 0.0;
    int firstChanged_ = -1;
};

} // namespace mirab::app
