/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ObjectEditor.h
 * Purpose: Editor tab for a named model object (pollutant, land use, aquifer, ...), built from the section schema.
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

#include <vector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace mirab::app
{

class RowForm;

/// Edits the rows of one named object, for example a SWMM pollutant, land
/// use (with its buildup and washoff rows), aquifer, snow pack, street or
/// inlet. The layout comes from the section schema: a form for single-row
/// objects, one form per row for objects whose rows have types (snow pack
/// surfaces, inlet types), a table for rows keyed by the object, and the
/// plain rows for sections without a column schema. Apply writes all
/// changes as one undoable edit.
class ObjectEditor : public QWidget
{
    Q_OBJECT
public:
    ObjectEditor(ProjectDocument* doc, const QString& kind, const QString& name, QWidget* parent = nullptr);
    /// Engine used to grey out data it does not simulate.
    void setEngine(const engine::EngineDescriptor* engine);
    QString kind() const { return kind_; }
    QString name() const { return name_; }
    /// Follows a rename of the object.
    void setName(const QString& name);

signals:
    void statusMessage(const QString& text);

private:
    enum class Mode
    {
        Form,  ///< one row as a form
        Forms, ///< one form per typed row
        Table, ///< rows keyed by the object
        Raw    ///< rows as text
    };
    struct Group
    {
        std::string section;
        const core::SectionDef* def = nullptr;
        Mode mode = Mode::Form;
        QWidget* box = nullptr;
        QVBoxLayout* formsLayout = nullptr;
        std::vector<RowForm*> forms;
        QTableWidget* table = nullptr;
        QPlainTextEdit* raw = nullptr;
    };

    void load();
    void apply();
    void setDirty(bool on);
    void updateSupport();
    void addForm(Group& g, const std::vector<std::string>& tokens);
    void addTableRow(Group& g, const std::vector<std::string>& tokens);
    bool collect(const Group& g, std::vector<std::vector<std::string>>& rows, QString& error) const;

    ProjectDocument* doc_;
    QString kind_;
    QString name_;
    const engine::EngineDescriptor* engine_ = nullptr;
    std::vector<Group> groups_;
    bool loading_ = false;
    QLabel* title_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* banner_ = nullptr;
    QWidget* content_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* revert_ = nullptr;
};

} // namespace mirab::app
