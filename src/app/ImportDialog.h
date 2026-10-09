/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/ImportDialog.h
 * Purpose: Import of shapefiles, DXF drawings and CSV tables into the project, with field mapping.
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
#include "anahita/interop/Import.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace anahita::app
{

class ImportDialog : public QDialog
{
    Q_OBJECT
public:
    ImportDialog(ProjectDocument* doc, const QString& file, QWidget* parent = nullptr);
    /// One line describing everything that was imported.
    QString summary() const { return summary_; }
    QStringList notes() const { return notes_; }

private:
    void layerChanged();
    void targetChanged();
    void runImport();
    const interop::ImportedLayer* currentLayer() const;
    QString fieldOf(QComboBox* box) const;

    ProjectDocument* doc_;
    QString file_;
    std::vector<interop::ImportedLayer> layers_;
    QListWidget* layerList_;
    QLabel* layerInfo_;
    QComboBox* kind_;
    QComboBox* idField_;
    QLineEdit* idPrefix_;
    QComboBox* fromField_;
    QComboBox* toField_;
    QDoubleSpinBox* snap_;
    QComboBox* endpointKind_;
    QComboBox* outletField_;
    QTableWidget* mapping_;
    QLineEdit* sourceCrs_;
    QCheckBox* adoptCrs_;
    QLabel* crsInfo_;
    QWidget* linkBox_;
    QWidget* areaBox_;
    QPushButton* importButton_;
    QLabel* result_;
    QString summary_;
    QStringList notes_;
    int imported_ = 0;
};

} // namespace anahita::app
