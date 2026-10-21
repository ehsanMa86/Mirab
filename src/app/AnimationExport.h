/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/AnimationExport.h
 * Purpose: Export of the results animation as an animated GIF or a numbered PNG sequence.
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
#include "mirab/core/Gif.h"

#include <QDialog>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTimer;

namespace mirab::app
{

class MapView;

struct AnimationSettings
{
    QString path;
    bool gif = true; ///< false: numbered PNG files (name_0001.png, ...)
    int first = 0;   ///< first reporting period
    int last = -1;   ///< last reporting period; -1 for the last one
    int stride = 1;  ///< use every n-th period
    double fps = 4.0;
    QSize size; ///< empty: the size of the map view
    bool basemap = true;
    bool legend = true;
    bool timeStamp = true;
    bool scaleBar = true;
    bool northArrow = true;
    bool dither = true;
    QString title;

    /// Periods that become frames, for a result set with @p periods periods.
    std::vector<int> frames(int periods) const;
};

/// Renders the frames on the GUI thread, a few at a time, while a worker
/// thread quantizes and writes them, so the window stays responsive.
class AnimationExporter : public QObject
{
    Q_OBJECT
public:
    AnimationExporter(MapView* map, ProjectDocument* doc, const AnimationSettings& settings,
                      QObject* parent = nullptr);
    ~AnimationExporter() override;
    void start();
    void cancel();
    int frameCount() const { return static_cast<int>(frames_.size()); }

signals:
    void progress(int done, int total, const QString& stage);
    void finished(bool ok, const QString& message);

private:
    void waitForTiles();
    void begin();
    void step();
    QImage renderFrame(int period);
    void work();
    void done(bool ok, const QString& message);
    QString framePath(int index) const;

    MapView* map_;
    ProjectDocument* doc_;
    AnimationSettings s_;
    std::vector<int> frames_;
    QSize size_;
    QImage background_;
    QTimer* timer_;
    int next_ = 0;
    int waited_ = 0;
    bool finished_ = false;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QImage> queue_;
    bool closed_ = false;
    std::atomic<bool> cancel_{false};
    std::atomic<int> written_{0};
    std::atomic<bool> workerDone_{false};
    QString error_; ///< guarded by mutex_
    core::gif::Palette palette_;
    double groundPerUnit_ = 0;
    QString groundUnit_;
    double base_ = 13.0; ///< text size of the overlays, in pixels
};

/// Options of the animation export.
class AnimationDialog : public QDialog
{
    Q_OBJECT
public:
    AnimationDialog(MapView* map, ProjectDocument* doc, QWidget* parent = nullptr);
    AnimationSettings settings() const;

private:
    void updateSummary();

    MapView* map_;
    ProjectDocument* doc_;
    QComboBox* format_;
    QComboBox* from_;
    QComboBox* to_;
    QSpinBox* stride_;
    QDoubleSpinBox* fps_;
    QComboBox* size_;
    QSpinBox* width_;
    QSpinBox* height_;
    QLineEdit* title_;
    QCheckBox* basemap_;
    QCheckBox* legend_;
    QCheckBox* time_;
    QCheckBox* scale_;
    QCheckBox* north_;
    QCheckBox* dither_;
    QLabel* summary_;
};

} // namespace mirab::app
