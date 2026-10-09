/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/PropertyPanel.h
 * Purpose: Property editor for the selected element and for project options.
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
#include "mirab/engine/Engine.h"

#include <QWidget>

class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QLabel;
class QLineEdit;

namespace mirab::app
{

class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(ProjectDocument* doc, QWidget* parent = nullptr);
    void setEngine(const engine::EngineDescriptor* engine);

signals:
    void statusMessage(const QString& text);

private:
    void rebuild();
    void showElement(const Selection& sel);
    void showOptions();
    void addRow(const QString& name, const QString& value, const QString& unit, const QString& tip,
                const QString& section, int row, int column, bool tail, const QStringList& choices,
                bool enabled, bool editable = true);
    void addHeader(const QString& text, bool enabled, const QString& tip = {});
    void commit(QTableWidgetItem* item);
    void populateAddMenu();
    void applyFilter();
    bool sectionSupported(const std::string& section, QString* why) const;

    ProjectDocument* doc_;
    const engine::EngineDescriptor* engine_ = nullptr;
    QLabel* icon_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QTableWidget* table_ = nullptr;
    QToolButton* addButton_ = nullptr;
    bool populating_ = false;
};

} // namespace mirab::app
