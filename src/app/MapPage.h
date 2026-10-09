/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/MapPage.h
 * Purpose: Map tab: display bar (result colors, time playback, basemap), the map and the start page.
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

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QSlider;
class QTimer;
class QToolButton;

namespace anahita::app
{

class MapView;

/// Welcome page with quick actions, examples and recent projects.
class StartPage : public QWidget
{
    Q_OBJECT
public:
    explicit StartPage(QWidget* parent = nullptr);
    void setRecent(const QStringList& files);
    void setExamples(const QStringList& files);

signals:
    void newEpanet();
    void newSwmm();
    void openRequested();
    void openFile(const QString& path);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QListWidget* recent_;
    QListWidget* examples_;
};

class MapPage : public QWidget
{
    Q_OBJECT
public:
    MapPage(ProjectDocument* doc, QWidget* parent = nullptr);
    MapView* map() const { return map_; }
    StartPage* startPage() const { return start_; }
    void setStartVisible(bool on);
    /// Basemap menu: rebuilt when the coordinate system changes.
    void refreshBasemapMenu();

signals:
    void coordinateSystemRequested();

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void resultsChanged();
    void updateTimeLabel();
    void placeOverlays();

    ProjectDocument* doc_;
    MapView* map_;
    StartPage* start_;
    QWidget* bar_;
    QComboBox* nodeVar_;
    QComboBox* linkVar_;
    QComboBox* areaVar_;
    QLabel* areaLabel_;
    QToolButton* stepBack_;
    QToolButton* play_;
    QToolButton* stepForward_;
    QSlider* slider_;
    QLabel* timeLabel_;
    QTimer* timer_;
    QToolButton* basemap_;
    QWidget* zoomBox_;
};

} // namespace anahita::app
