/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/RasterDialogs.h
 * Purpose: Dialogs for raster layers and for node elevations from a DEM.
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

#include "mirab/core/Elevation.h"

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSlider;
class QTableWidget;

namespace mirab::app
{

/// Lists the project's raster layers: add, remove, reorder, style and opacity.
/// Changes apply to the map at once.
class RasterLayersDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RasterLayersDialog(ProjectDocument* doc, QWidget* parent = nullptr);
    /// Asks for a file and adds it as a layer; returns false when cancelled or unreadable.
    static bool addLayer(ProjectDocument* doc, QWidget* parent, const QString& file = {});

signals:
    void layersChanged();

private:
    void fill(int select);
    void showCurrent();
    void store();
    void move(int delta);

    ProjectDocument* doc_;
    QListWidget* list_;
    QComboBox* style_;
    QSlider* opacity_;
    QDoubleSpinBox* exaggeration_;
    QComboBox* units_;
    QLabel* info_;
    QPushButton* remove_;
    QPushButton* up_;
    QPushButton* down_;
    bool filling_ = false;
};

/// Tools > Elevations from DEM: samples a DEM at the nodes, previews the old
/// and new values and applies them as one undoable edit.
class ElevationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ElevationDialog(ProjectDocument* doc, QWidget* parent = nullptr);

signals:
    void layersChanged();

private:
    void browse();
    void recompute();
    void apply();

    ProjectDocument* doc_;
    QComboBox* dem_;
    QComboBox* units_;
    QLabel* demInfo_;
    QRadioButton* all_;
    QRadioButton* missing_;
    QRadioButton* selected_;
    QCheckBox* junctions_;
    QCheckBox* tanks_;
    QCheckBox* reservoirs_;
    QComboBox* swmmMode_;
    QCheckBox* addLayer_;
    QTableWidget* table_;
    QLabel* summary_;
    QPushButton* apply_;
    std::vector<core::ElevationChange> plan_;
};

} // namespace mirab::app
