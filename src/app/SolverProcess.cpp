/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SolverProcess.cpp
 * Purpose: Implementation of the solver process bridge (JSON lines over standard output).
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
#include "SolverProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

namespace mirab::app
{

SolverProcess::SolverProcess(QObject* parent) : QObject(parent)
{
    connect(&process_, &QProcess::readyReadStandardOutput, this, &SolverProcess::readOutput);
    connect(&process_, &QProcess::readyReadStandardError, this,
            [this] { stderr_ += QString::fromUtf8(process_.readAllStandardError()); });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &SolverProcess::onFinished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
        {
            QJsonObject r;
            r["success"] = false;
            r["message"] = tr("The solver process could not be started (%1).").arg(cliPath());
            emit finished(false, r);
        }
    });
}

SolverProcess::~SolverProcess()
{
    if (isRunning())
    {
        process_.kill();
        process_.waitForFinished(2000);
    }
}

QString SolverProcess::cliPath()
{
    const QString dir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
    return QDir(dir).filePath("mirab-cli.exe");
#else
    return QDir(dir).filePath("mirab-cli");
#endif
}

bool SolverProcess::start(const QStringList& arguments)
{
    if (isRunning())
        return false;
    buffer_.clear();
    done_ = {};
    haveDone_ = false;
    stderr_.clear();
    QStringList args = arguments;
    args << "--machine";
    process_.setProgram(cliPath());
    process_.setArguments(args);
    process_.setWorkingDirectory(QDir::tempPath());
    process_.start();
    return true;
}

void SolverProcess::cancel()
{
    if (!isRunning())
        return;
    process_.write("cancel\n");
    QTimer::singleShot(4000, this, [this] {
        if (isRunning())
            process_.kill();
    });
}

void SolverProcess::readOutput()
{
    buffer_ += process_.readAllStandardOutput();
    int nl;
    while ((nl = buffer_.indexOf('\n')) >= 0)
    {
        const QByteArray line = buffer_.left(nl).trimmed();
        buffer_.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
        {
            emit log("info", QString::fromUtf8(line));
            continue;
        }
        const QJsonObject o = doc.object();
        const QString event = o.value("event").toString();
        if (event == "progress")
            emit progress(o.value("fraction").toDouble(), o.value("stage").toString());
        else if (event == "log")
            emit log(o.value("level").toString(), o.value("message").toString());
        else if (event == "done")
        {
            done_ = o;
            haveDone_ = true;
        }
    }
}

void SolverProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    readOutput();
    if (!haveDone_)
    {
        done_ = {};
        done_["success"] = false;
        QString msg = status == QProcess::CrashExit
                          ? tr("The solver process stopped unexpectedly (the engine crashed).")
                          : tr("The solver process ended with code %1.").arg(exitCode);
        if (!stderr_.trimmed().isEmpty())
            msg += "\n" + stderr_.trimmed().right(2000);
        done_["message"] = msg;
    }
    emit finished(done_.value("success").toBool(), done_);
}

} // namespace mirab::app
