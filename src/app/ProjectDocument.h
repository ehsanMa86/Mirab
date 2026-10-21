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
    /// Replaces the results. When live results were shown, the period at
    /// the same time stays selected.
    void setResults(std::unique_ptr<core::ResultSet> r);
    void clearResults();
    int period() const { return period_; }
    void setPeriod(int p);

    // Live results: a result set that grows while a simulation runs.
    /// Shows @p live (at least one period) and follows its latest period.
    void beginLiveResults(std::shared_ptr<const core::ResultSet> live);
    /// Call after periods from @p firstChanged on were appended to (or
    /// replaced in) the live result set @p live.
    void liveResultsGrew(const core::ResultSet* live, int firstChanged);
    /// Keeps the live results as partial results, described by @p note.
    void endLiveResults(const QString& note);
    bool isLive() const { return live_; }
    /// Shown with partial results, e.g. "Partial results (cancelled at 6:00:00)".
    QString resultsNote() const { return note_; }

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
    /// Periods from @p firstChanged on were appended to or replaced in the live results.
    void liveResultsUpdated(int firstChanged);
    /// The live state or the results note changed.
    void liveStateChanged();
    void periodChanged(int period);
    void titleChanged();

private:
    core::Model model_;
    core::ProjectSettings settings_;
    QString path_;
    QUndoStack undo_;
    bool settingsDirty_ = false;
    std::shared_ptr<const core::ResultSet> results_;
    int period_ = 0;
    bool live_ = false;
    bool followLatest_ = true;
    QString note_;
    Selection selection_;
};

} // namespace mirab::app
