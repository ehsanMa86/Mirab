/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ProjectDocument.h
 * Purpose: Open project state for the desktop application: model, file, settings, undo history and results.
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

#include "mirab/core/Model.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Results.h"

#include <QObject>
#include <QString>
#include <QUndoStack>

#include <functional>
#include <memory>

namespace mirab::app
{

struct Selection
{
    core::Category category = core::Category::None;
    QString id;
    bool isEmpty() const { return category == core::Category::None || id.isEmpty(); }
    bool operator==(const Selection& o) const { return category == o.category && id == o.id; }
};

class ProjectDocument : public QObject
{
    Q_OBJECT
public:
    explicit ProjectDocument(QObject* parent = nullptr);

    void newProject(core::Dialect dialect, const QString& flowUnits);
    /// Throws std::exception on read errors.
    void open(const QString& path);
    /// Throws std::exception on write errors.
    void save(const QString& path);

    const core::Model& model() const { return model_; }
    core::ProjectSettings& settings() { return settings_; }
    const core::ProjectSettings& settings() const { return settings_; }
    QString path() const { return path_; }
    QString displayName() const;
    bool isModified() const { return !undo_.isClean() || settingsDirty_; }
    QUndoStack* undoStack() { return &undo_; }

    /// Applies an edit through the undo stack. The function returns false
    /// (and may set an error) to abandon the edit.
    bool edit(const QString& description, const std::function<bool(core::Model&, std::string*)>& fn,
              QString* error = nullptr);
    /// Replaces the whole input text (from the text editor).
    bool replaceText(const QString& text, QString* error = nullptr);
    void markSettingsChanged();

    // Results
    bool hasResults() const { return results_ != nullptr; }
    const core::ResultSet* results() const { return results_.get(); }
    void setResults(std::unique_ptr<core::ResultSet> r);
    void clearResults();
    int period() const { return period_; }
    void setPeriod(int p);

    // Selection
    const Selection& selection() const { return selection_; }
    void select(const Selection& s);

    /// Restores a full input text (used by undo commands).
    void restoreText(const std::string& text);

signals:
    void modelChanged();
    void modelReset();
    void selectionChanged();
    void resultsChanged();
    void periodChanged(int period);
    void titleChanged();

private:
    core::Model model_;
    core::ProjectSettings settings_;
    QString path_;
    QUndoStack undo_;
    bool settingsDirty_ = false;
    std::unique_ptr<core::ResultSet> results_;
    int period_ = 0;
    Selection selection_;
};

} // namespace mirab::app
