/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SchemaWidgets.h
 * Purpose: Helpers and widgets shared by the schema-driven editors: field editors, reference lists and engine support.
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

#include <QStringList>
#include <QWidget>

#include <vector>

class QGridLayout;

namespace mirab::app
{

/// Names a column can refer to: objects, time series, patterns, curves, rain
/// gages and subcatchments. Nodes and links are not listed (networks can be large).
QStringList referenceChoices(const core::Model& model, const core::ColumnDef& column);
/// Values offered for a column: its keywords followed by the names it can refer to.
QStringList columnChoices(const core::Model& model, const core::ColumnDef& column);
/// Whether @p engine runs the data of a section (always true without an
/// engine). @p why receives the reason when it does not.
bool engineSupportsSection(const engine::EngineDescriptor* engine, core::Dialect dialect,
                           const std::string& section, const std::vector<std::string>& tokens,
                           QString* why = nullptr);
/// Label of a column for forms: its short description, or its name split into words.
QString columnLabel(const core::ColumnDef& column);
/// Display name of an object kind, e.g. "Landuse" -> "Land use" (plural: "Land uses").
QString objectKindLabel(const QString& kind, bool plural = false);
/// Icon of an object kind for browsers and tabs.
QIcon objectKindIcon(const QString& kind);

/// One input row shown as a form: a label, an editor and a unit per column.
/// Choices and references get combo boxes; a type column of a section with
/// variants (inlets, snow packs) rebuilds the fields when it changes.
class RowForm : public QWidget
{
    Q_OBJECT
public:
    /// @param firstColumn first column shown; earlier columns (the name) are kept as given.
    RowForm(const core::Model& model, const core::SectionDef& def, int firstColumn,
            QWidget* parent = nullptr);
    void setTokens(const std::vector<std::string>& tokens);
    /// The row with the edited values; empty optional values at the end are dropped.
    std::vector<std::string> tokens() const;
    /// Returns a message for the first invalid value, or an empty string.
    QString check() const;

signals:
    void edited();

private:
    void build();
    QString valueAt(int column) const;

    const core::Model& model_;
    const core::SectionDef& def_;
    int first_;
    std::vector<std::string> tokens_;
    core::RowLayout layout_;
    QGridLayout* grid_;
    std::vector<QWidget*> editors_; ///< one per shown column
    bool building_ = false;
};

} // namespace mirab::app
