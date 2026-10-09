/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/EngineDownloader.cpp
 * Purpose: Implementation of the engine downloader and installer.
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
#include "EngineDownloader.h"

#include "SolverProcess.h"
#include "mirab/MirabConfig.h"
#include "mirab/engine/EngineInstall.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

namespace mirab::app
{

EngineDownloader::EngineDownloader(QObject* parent) : QObject(parent) {}

EngineDownloader::~EngineDownloader()
{
    if (reply_)
        reply_->abort();
}

QUrl EngineDownloader::swmm5PlusManifestUrl()
{
    const QString configured = QSettings().value("engines/swmm5plusManifestUrl").toString();
    if (!configured.isEmpty())
        return QUrl(configured);
    return QUrl(QString::fromStdString(engine::defaultSwmm5PlusManifestUrl()));
}

void EngineDownloader::fail(const QString& message)
{
    running_ = false;
    if (file_)
    {
        file_->close();
        file_.reset();
    }
    emit finished(false, message);
}

void EngineDownloader::start(const QUrl& manifestUrl)
{
    if (running_)
        return;
    running_ = true;
    manifestUrl_ = manifestUrl;
    tmp_ = std::make_unique<QTemporaryDir>();
    if (!tmp_->isValid())
    {
        fail(tr("A temporary folder could not be created."));
        return;
    }
    assets_ = {QStringLiteral("manifest.json")};
    sizes_ = {-1};
    index_ = -1;
    done_ = 0;
    total_ = 0;
    fetchNext();
}

void EngineDownloader::fetchNext()
{
    ++index_;
    if (index_ >= assets_.size())
    {
        install(tmp_->path(), tmp_->filePath("manifest.json"));
        return;
    }
    const bool isManifest = index_ == 0;
    const QUrl url = isManifest ? manifestUrl_ : manifestUrl_.resolved(QUrl(assets_[index_]));
    file_ = std::make_unique<QFile>(tmp_->filePath(isManifest ? QStringLiteral("manifest.json") : assets_[index_]));
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        fail(tr("Cannot write %1").arg(file_->fileName()));
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mirab/" MIRAB_VERSION_STRING));
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = network_.get(request);
    const QString stage = isManifest ? tr("Reading the package description") : tr("Downloading %1").arg(assets_[index_]);
    emit progress(done_, total_, stage);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (file_ && reply_)
            file_->write(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this, stage](qint64 received, qint64) {
        emit progress(done_ + received, total_, stage);
    });
    connect(reply_, &QNetworkReply::finished, this, [this, url] {
        QNetworkReply* r = reply_;
        if (!r)
            return;
        r->deleteLater();
        if (file_)
            file_->write(r->readAll());
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() != QNetworkReply::NoError || (status != 0 && status >= 400))
        {
            if (status == 404 && index_ == 0)
            {
                fail(tr("No engine package was found at %1. There may be no release for this computer (%2) yet.")
                         .arg(url.toString(), QString::fromStdString(engine::platformId())));
                return;
            }
            QString why;
            if (r->error() == QNetworkReply::OperationCanceledError)
                why = tr("cancelled");
            else if (status >= 400)
                why = tr("the server answered HTTP %1").arg(status);
            else
                why = r->errorString();
            fail(tr("Download of %1 failed: %2").arg(url.toString(), why));
            return;
        }
        file_->close();
        if (index_ == 0)
        {
            // Learn the file list from the manifest.
            try
            {
                const auto m = engine::EngineManifest::load(std::filesystem::u8path(file_->fileName().toStdString()));
                for (const auto& f : m.files)
                {
                    assets_ << QString::fromStdString(f.asset);
                    sizes_ << f.size;
                    total_ += f.size;
                }
            }
            catch (const std::exception& e)
            {
                fail(QString::fromUtf8(e.what()));
                return;
            }
        }
        else
        {
            done_ += sizes_[index_] > 0 ? sizes_[index_] : QFileInfo(file_->fileName()).size();
        }
        file_.reset();
        fetchNext();
    });
}

void EngineDownloader::installLocal(const QString& manifestPath)
{
    if (running_)
        return;
    running_ = true;
    install(QFileInfo(manifestPath).absolutePath(), manifestPath);
}

void EngineDownloader::install(const QString& folder, const QString& manifestPath)
{
    std::string engineId;
    try
    {
        emit progress(total_, total_, tr("Checking and installing the files"));
        const auto m = engine::EngineManifest::load(std::filesystem::u8path(manifestPath.toStdString()));
        engineId = m.engine;
        engine::installFromManifest(m, std::filesystem::u8path(folder.toStdString()), engine::userEngineDirectory());
    }
    catch (const std::exception& e)
    {
        fail(QString::fromUtf8(e.what()));
        return;
    }
    // Confirm that the engine loads, in the solver process (never in the GUI).
    emit progress(total_, total_, tr("Checking that the engine loads"));
    auto* check = new QProcess(this);
    check->setProgram(SolverProcess::cliPath());
    check->setArguments({"engines", "verify", QString::fromStdString(engineId)});
    connect(check, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, check](int code, QProcess::ExitStatus status) {
                const QString out = QString::fromUtf8(check->readAllStandardOutput()).trimmed();
                const QString err = QString::fromUtf8(check->readAllStandardError()).trimmed();
                check->deleteLater();
                running_ = false;
                if (status == QProcess::NormalExit && code == 0)
                    emit finished(true, out);
                else
                    emit finished(false, tr("The files were installed, but the engine does not load: %1")
                                             .arg(err.isEmpty() ? out : err));
            });
    check->start();
}

void EngineDownloader::cancel()
{
    if (reply_)
        reply_->abort();
}

} // namespace mirab::app
