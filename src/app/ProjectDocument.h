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
#include "mirab/core/Scenario.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QUndoStack>

#include <functional>
#include <map>
#include <memory>
#include <unordered_set>

namespace mirab::app
{

struct Selection
{
    core::Category category = core::Category::None;
    QString id;
    bool isEmpty() const { return category == core::Category::None || id.isEmpty(); }
    bool operator==(const Selection& o) const { return category == o.category && id == o.id; }
    bool operator!=(const Selection& o) const { return !(*this == o); }
    core::ElementRef ref() const { return {category, id.toStdString()}; }
    static Selection of(const core::ElementRef& e) { return {e.category, QString::fromStdString(e.id)}; }
};

class ProjectDocument : public QObject
{
    Q_OBJECT
public:
    explicit ProjectDocument(QObject* parent = nullptr);

    void newProject(core::Dialect dialect, const QString& flowUnits);
    /// Throws std::exception on read errors.
    void open(const QString& path);
    /// Writes the base model and the settings (with the scenarios). Throws
    /// std::exception on write errors.
    void save(const QString& path);

    /// The model shown and edited: the base model, or the active scenario's model.
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

    // Selection: an ordered set of elements, one of which is current (the
    // element the property panel and the time series show).
    /// The current element; empty when nothing is selected.
    const Selection& selection() const { return selection_; }
    /// Every selected element in the order it was picked.
    const QList<Selection>& selectedElements() const { return selected_; }
    int selectionCount() const { return static_cast<int>(selected_.size()); }
    bool isSelected(core::Category category, const std::string& id) const;
    bool isSelected(const Selection& s) const { return isSelected(s.category, s.id.toStdString()); }
    /// Selects one element only; an empty selection clears it.
    void select(const Selection& s);
    /// Replaces the selection. The current element is @p current when it is
    /// part of @p items, otherwise the first item.
    void setSelection(const QList<Selection>& items, const Selection& current = {});
    /// Adds elements; the last one added becomes current.
    void addToSelection(const QList<Selection>& items);
    void removeFromSelection(const QList<Selection>& items);
    /// Adds the element, or removes it when it is already selected.
    void toggleSelected(const Selection& s);
    /// Makes a selected element current without changing the selection.
    void setCurrent(const Selection& s);
    void clearSelection() { select({}); }

    /// Restores a full input text (used by undo commands).
    void restoreText(const std::string& text);

    // Scenarios: alternatives of the model stored as changes in the settings.
    // While one is active, model() is the scenario's model and every edit is
    // recorded as a change of that scenario; the base model stays unchanged.
    const std::vector<core::Scenario>& scenarios() const { return settings_.scenarios; }
    /// Name of the active scenario, empty for the base model.
    QString activeScenario() const { return active_; }
    bool isScenarioActive() const { return !active_.isEmpty(); }
    /// Shows and edits a scenario (empty: the base model). Its results replace the shown ones.
    bool setActiveScenario(const QString& name, QString* error = nullptr);
    const core::Model& baseModel() const { return active_.isEmpty() ? model_ : base_; }
    /// Changes of the active scenario that no longer apply cleanly to the base model.
    const std::vector<core::ScenarioConflict>& conflicts() const { return conflicts_; }
    bool addScenario(core::Scenario scenario, QString* error = nullptr);
    bool renameScenario(const QString& from, const QString& to, QString* error = nullptr);
    /// Removes a scenario. Scenarios based on it keep their models and are
    /// based on its parent from then on.
    void removeScenario(const QString& name);
    /// Sets the description, the parent and the engine of a scenario. A new
    /// parent keeps the scenario's model as it is (its changes are recomputed).
    bool updateScenario(const QString& name, const core::Scenario& properties, QString* error = nullptr);
    /// Model of any scenario (empty: the base model).
    core::Model scenarioModel(const QString& name, std::vector<core::ScenarioConflict>* conflicts = nullptr) const;
    /// Removes changes of the active scenario by index (undoable).
    void revertChanges(std::vector<int> indices);
    /// When a scenario is active and changes the value at a row and column of
    /// model(): true, with the base model's value in @p base. @p tail compares
    /// the rest of the row from @p column on.
    bool baseValue(const std::string& section, int row, int column, bool tail, QString* base) const;
    /// Restores the changes (scenario) or the text (base model) recorded by an undo command.
    void restoreState(const QString& scenario, const std::string& state);

    /// Engine, MSX use and MSX file of a scenario (empty: the base model) after inheritance.
    core::ScenarioEngine engineChoice(const QString& scenario) const;
    core::ScenarioEngine engineChoice() const { return engineChoice(active_); }
    /// Sets the engine of the active scenario, or of the base model.
    void setEngineChoice(const std::string& engineId, bool useMsx);

    /// Results file of a scenario (empty: the base model): <project>.<scenario>.anr
    QString resultsPath(const QString& scenario) const { return runBase(scenario) + ".anr"; }
    /// Path of a scenario's results and report files without the extension.
    QString runBase(const QString& scenario) const;
    /// Results of a scenario: those shown, kept from this session, or read from its results file.
    std::shared_ptr<const core::ResultSet> resultsFor(const QString& scenario);
    /// Forgets the kept results of a scenario after its results file was written;
    /// the active scenario's results are read again.
    void reloadResults(const QString& scenario);

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
    /// Scenarios were added, removed, renamed or changed.
    void scenariosChanged();
    void activeScenarioChanged();

private:
    bool commit(core::Model working, const QString& description);
    void recomputeScenario();
    void scenarioStructureChanged();
    void showResults(std::shared_ptr<const core::ResultSet> r);
    const core::SectionKeys& keysOf(const core::Model& m, const std::string& section, bool base) const;

    core::Model model_;
    core::Model base_; ///< the base model while a scenario is active
    QString active_;
    core::InpDocument parentDoc_; ///< the active scenario's parent model, for recording changes
    std::vector<core::ScenarioConflict> conflicts_;
    std::vector<core::ScenarioChange> orphans_; ///< changes of the active scenario that did not apply
    std::map<QString, std::shared_ptr<const core::ResultSet>> kept_;
    mutable std::map<std::string, core::SectionKeys> workKeys_, baseKeys_;
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
    QList<Selection> selected_;
    std::unordered_set<std::string> selectedIds_[5]; ///< per core::Category

    void rebuildSelectionIndex();
    /// Drops selected elements that no longer exist; returns true when the selection changed.
    bool pruneSelection();
};

} // namespace mirab::app
