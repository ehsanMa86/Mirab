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

#include "mirab/core/LiveResults.h"

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
        // The owner is being destroyed: it must not hear about this run any more.
        process_.disconnect(this);
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
    const QString beside = QDir(dir).filePath("mirab-cli");
#if defined(Q_OS_MACOS)
    // In the build tree the bundle is bin/Mirab.app/Contents/MacOS and mirab-cli is in bin.
    const QString build = QDir(dir).filePath("../../../mirab-cli");
    if (!QFileInfo::exists(beside) && QFileInfo::exists(build))
        return QDir::cleanPath(build);
#endif
    return beside;
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
    paused_ = false;
    live_.reset();
    pendingLayout_.reset();
    layoutId_ = 0;
    liveTime_ = 0.0;
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

void SolverProcess::pause()
{
    if (isRunning())
        process_.write("pause\n");
}

void SolverProcess::resume()
{
    if (isRunning())
        process_.write("resume\n");
}

void SolverProcess::readLiveLine(const QByteArray& line, bool* started, bool* grew)
{
    const std::string_view text(line.constData(), static_cast<std::size_t>(line.size()));
    try
    {
        if (line.startsWith("{\"event\":\"frame\""))
        {
            const auto frame = core::live::parseFrameLine(text);
            if (frame.layout != layoutId_)
                return;
            if (pendingLayout_)
            {
                live_ = std::move(pendingLayout_);
                *started = true;
            }
            const int p = live_ ? core::live::appendFrame(*live_, frame) : -1;
            if (p >= 0)
            {
                liveTime_ = frame.time;
                firstChanged_ = firstChanged_ < 0 ? p : std::min(firstChanged_, p);
                *grew = true;
            }
            return;
        }
        int id = 0;
        auto layout = core::live::parseLayout(json::Value::parse(text), &id);
        pendingLayout_ = std::make_shared<core::ResultSet>(std::move(layout));
        layoutId_ = id;
    }
    catch (const std::exception& e)
    {
        emit log("warning",
                 tr("Live results could not be read: %1").arg(QString::fromUtf8(e.what())));
    }
}

void SolverProcess::readOutput()
{
    buffer_ += process_.readAllStandardOutput();
    bool started = false, grew = false;
    firstChanged_ = -1;
    int nl;
    int from = 0;
    while ((nl = buffer_.indexOf('\n', from)) >= 0)
    {
        QByteArray line = buffer_.mid(from, nl - from).trimmed();
        from = nl + 1;
        if (line.isEmpty())
            continue;
        // An engine may print to standard output without ending its line.
        const int brace = line.startsWith('{') ? 0 : line.indexOf("{\"");
        if (brace > 0)
        {
            emit log("info", QString::fromUtf8(line.left(brace)).trimmed());
            line = line.mid(brace);
        }
        // Frames are large and frequent: they skip the general JSON parser.
        if (line.startsWith("{\"event\":\"frame\"") || line.startsWith("{\"layout\""))
        {
            readLiveLine(line, &started, &grew);
            continue;
        }
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
        else if (event == "paused")
        {
            paused_ = true;
            emit paused(o.value("time").toDouble());
        }
        else if (event == "resumed")
        {
            paused_ = false;
            emit resumed();
        }
        else if (event == "done")
        {
            done_ = o;
            haveDone_ = true;
        }
    }
    buffer_.remove(0, from);
    if (started)
        emit liveStarted();
    else if (grew)
        emit liveUpdated(firstChanged_);
}

void SolverProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    readOutput();
    paused_ = false;
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
