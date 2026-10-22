/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/WaterLinkDialog.h
 * Purpose: Wastewater from a water model: the link dialog of a SWMM project and the linked network layer on the map.
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

#include "mirab/core/Linkage.h"
#include "mirab/interop/WaterLink.h"

#include <QDialog>
#include <QObject>
#include <QWidget>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace mirab::app
{

class MapView;

/// Map preview of a link: the SWMM network, the water demand nodes and a
/// line from each of them to its SWMM node.
class LinkPreview : public QWidget
{
    Q_OBJECT
public:
    explicit LinkPreview(QWidget* parent = nullptr);
    void setData(const core::Model* swmm, std::vector<std::vector<core::Point>> waterLines,
                 std::vector<core::NodeAssignment> assignments);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const core::Model* swmm_ = nullptr;
    std::vector<std::vector<core::Point>> waterLines_;
    std::vector<core::NodeAssignment> assignments_;
};

/// Tools > Wastewater from water model: links the SWMM project to the EPANET
/// model of the same area, previews the dry-weather inflow computed from its
/// water use and writes it ([DWF], [PATTERNS]) as one undoable edit.
class WaterLinkDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WaterLinkDialog(ProjectDocument* doc, QWidget* parent = nullptr);

signals:
    /// The link stored in the project settings changed.
    void linkChanged();

private:
    void browseWater();
    void browseResults();
    void chooseAreas();
    bool loadWater();
    void fillCategories();
    void schedule();
    void recompute();
    void apply();
    void setOverride(int mode);
    core::WaterLinkSettings collect() const;
    QString stored(const QString& file) const;
    QString resolved(const std::string& stored) const;

    ProjectDocument* doc_;
    QLineEdit* water_;
    QLabel* waterInfo_;
    QDoubleSpinBox* distance_;
    QComboBox* tag_;
    QComboBox* areas_;
    QCheckBox* storage_;
    QDoubleSpinBox* factor_;
    QTableWidget* categories_;
    QComboBox* source_;
    QLineEdit* results_;
    QPushButton* resultsBrowse_;
    QCheckBox* weekend_;
    QComboBox* weekStart_;
    QDoubleSpinBox* lag_;
    QLineEdit* prefix_;
    QTableWidget* inflows_;
    QTableWidget* nodes_;
    LinkPreview* preview_;
    QLabel* summary_;
    QPushButton* apply_;
    QTimer* timer_;

    core::WaterLinkSettings initial_;
    std::unique_ptr<interop::LinkedModels> linked_;
    std::string loadedKey_;
    std::map<std::string, std::string> overrides_;
    core::DwfPlan plan_;
    bool valid_ = false;
    bool filling_ = false;
};

/// The linked network drawn faintly on the map: for a SWMM project the EPANET
/// model of its link, with a line from each water node to its SWMM node; for
/// an EPANET project a sewer model chosen by the user (with the lines when
/// that model is linked to this one).
class LinkedNetworkLayer : public QObject
{
    Q_OBJECT
public:
    LinkedNetworkLayer(ProjectDocument* doc, MapView* map, QObject* parent = nullptr);
    /// True when there is something to show for the current project.
    bool available() const;
    bool isVisible() const { return visible_; }
    bool hasSewerModel() const { return !sewerFile_.isEmpty(); }
    void setVisible(bool on);
    /// Rebuilds the layer from the project settings (after opening a project or applying a link).
    void refresh();
    /// EPANET projects: asks for the sewer model to show.
    bool chooseSewerModel(QWidget* parent);

private:
    void clear();

    ProjectDocument* doc_;
    MapView* map_;
    bool visible_ = true;
    QString sewerFile_; ///< EPANET projects: the sewer model shown
    QString project_;   ///< project the layer was built for
};

} // namespace mirab::app
