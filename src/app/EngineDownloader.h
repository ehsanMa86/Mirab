/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/EngineDownloader.h
 * Purpose: Downloads, verifies and installs separately distributed engines such as SWMM5+.
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

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <memory>

class QFile;
class QNetworkReply;
class QTemporaryDir;

namespace mirab::app
{

/// Fetches an "mirab-engine/1" manifest and the files it lists, checks
/// their SHA-256 sums, installs them into the per-user engine folder and
/// confirms that the engine loads (in the separate solver process).
class EngineDownloader : public QObject
{
    Q_OBJECT
public:
    explicit EngineDownloader(QObject* parent = nullptr);
    ~EngineDownloader() override;

    /// Default manifest location of SWMM5+ for this platform (overridable with
    /// the MIRAB_SWMM5PLUS_MANIFEST_URL environment variable or settings).
    static QUrl swmm5PlusManifestUrl();

    /// Downloads from an http(s) or file URL.
    void start(const QUrl& manifestUrl);
    /// Installs from a manifest whose files are in the same folder.
    void installLocal(const QString& manifestPath);
    void cancel();
    bool isRunning() const { return running_; }

signals:
    void progress(qint64 done, qint64 total, const QString& stage);
    void finished(bool success, const QString& message);

private:
    void fetchNext();
    void install(const QString& folder, const QString& manifestPath);
    void fail(const QString& message);

    QNetworkAccessManager network_;
    std::unique_ptr<QTemporaryDir> tmp_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QFile> file_;
    QUrl manifestUrl_;
    QStringList assets_;
    QList<qint64> sizes_;
    int index_ = -1;
    qint64 done_ = 0;
    qint64 total_ = 0;
    bool running_ = false;
};

} // namespace mirab::app
