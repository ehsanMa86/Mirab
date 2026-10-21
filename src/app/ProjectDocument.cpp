/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ProjectDocument.cpp
 * Purpose: Project document implementation with snapshot-based undo and redo.
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
#include "ProjectDocument.h"

#include "mirab/core/Text.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUndoCommand>

#include <algorithm>

namespace mirab::app
{

namespace
{
/// An edit of the base model (full text before and after) or of a scenario
/// (its changes before and after, as JSON).
class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(ProjectDocument* doc, QString scenario, std::string before, std::string after,
                    const QString& text)
        : QUndoCommand(text), doc_(doc), scenario_(std::move(scenario)), before_(std::move(before)),
          after_(std::move(after))
    {
    }
    void undo() override { doc_->restoreState(scenario_, before_); }
    void redo() override
    {
        if (first_)
        {
            first_ = false; // the edit has already been applied
            return;
        }
        doc_->restoreState(scenario_, after_);
    }
    const QString& scenario() const { return scenario_; }
    void setScenario(const QString& s) { scenario_ = s; }

private:
    ProjectDocument* doc_;
    QString scenario_;
    std::string before_;
    std::string after_;
    bool first_ = true;
};

std::string changesState(const std::vector<core::ScenarioChange>& changes)
{
    core::Scenario s;
    s.name = "state";
    s.changes = changes;
    return core::scenariosToJson({s}).dump();
}

std::vector<core::ScenarioChange> changesFromState(const std::string& state)
{
    try
    {
        const auto list = core::scenariosFromJson(json::Value::parse(state));
        return list.empty() ? std::vector<core::ScenarioChange>{} : list.front().changes;
    }
    catch (const json::ParseError&)
    {
        return {};
    }
}

bool sameTarget(const core::ScenarioChange& c, const core::ScenarioConflict& k)
{
    return c.section == k.section && c.key == k.key && c.ordinal == k.ordinal &&
           (c.kind != core::ScenarioChange::Kind::Set || c.column == k.column);
}
} // namespace

ProjectDocument::ProjectDocument(QObject* parent) : QObject(parent)
{
    connect(&undo_, &QUndoStack::cleanChanged, this, &ProjectDocument::titleChanged);
}

void ProjectDocument::newProject(core::Dialect dialect, const QString& flowUnits)
{
    model_ = core::Model::newProject(dialect, flowUnits.toStdString());
    settings_ = core::ProjectSettings{};
    path_.clear();
    undo_.clear();
    settingsDirty_ = false;
    active_.clear();
    base_ = core::Model();
    conflicts_.clear();
    orphans_.clear();
    kept_.clear();
    workKeys_.clear();
    baseKeys_.clear();
    clearResults();
    selection_ = {};
    selected_.clear();
    rebuildSelectionIndex();
    emit modelReset();
    emit activeScenarioChanged();
    emit scenariosChanged();
    emit titleChanged();
}

void ProjectDocument::open(const QString& path)
{
    const std::filesystem::path p = std::filesystem::u8path(path.toStdString());
    core::Model m = core::Model::fromFile(p);
    model_ = std::move(m);
    settings_ = core::ProjectSettings::loadFor(p);
    path_ = path;
    undo_.clear();
    settingsDirty_ = false;
    active_.clear();
    base_ = core::Model();
    conflicts_.clear();
    orphans_.clear();
    kept_.clear();
    workKeys_.clear();
    baseKeys_.clear();
    clearResults();
    selection_ = {};
    selected_.clear();
    rebuildSelectionIndex();
    emit modelReset();
    emit activeScenarioChanged();
    emit scenariosChanged();
    emit titleChanged();
}

void ProjectDocument::save(const QString& path)
{
    const std::filesystem::path p = std::filesystem::u8path(path.toStdString());
    baseModel().save(p);
    settings_.saveFor(p);
    path_ = path;
    undo_.setClean();
    settingsDirty_ = false;
    emit titleChanged();
}

QString ProjectDocument::displayName() const
{
    return path_.isEmpty() ? tr("Untitled") : QFileInfo(path_).fileName();
}

bool ProjectDocument::edit(const QString& description,
                           const std::function<bool(core::Model&, std::string*)>& fn, QString* error)
{
    std::string err;
    core::Model working = model_;
    if (!fn(working, &err))
    {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    commit(std::move(working), description);
    return true;
}

bool ProjectDocument::commit(core::Model working, const QString& description)
{
    std::string before = model_.toText();
    std::string after = working.toText();
    if (after == before)
        return false;
    if (active_.isEmpty())
    {
        model_ = std::move(working);
        workKeys_.clear();
        undo_.push(new SnapshotCommand(this, {}, std::move(before), std::move(after), description));
        if (pruneSelection())
            emit selectionChanged();
        emit modelChanged();
        return true;
    }
    core::Scenario* s = core::findScenario(settings_.scenarios, active_.toStdString());
    if (!s)
        return false;
    // The scenario's changes are what differs from its parent, plus the
    // changes that could not be applied (kept until they are reverted).
    std::string beforeState = changesState(s->changes);
    auto changes = core::diffDocuments(parentDoc_, working.document(), working.dialect());
    changes.insert(changes.end(), orphans_.begin(), orphans_.end());
    std::string afterState = changesState(changes);
    model_ = std::move(working);
    workKeys_.clear();
    if (afterState != beforeState)
    {
        s->changes = std::move(changes);
        undo_.push(new SnapshotCommand(this, active_, std::move(beforeState), std::move(afterState),
                                       tr("%1 (%2)").arg(description, active_)));
        emit scenariosChanged();
    }
    if (pruneSelection())
        emit selectionChanged();
    emit modelChanged();
    return true;
}

bool ProjectDocument::replaceText(const QString& text, QString* error)
{
    const std::string after = text.toStdString();
    if (after == model_.toText())
        return true;
    core::Model m;
    try
    {
        m = core::Model::fromText(after);
        if (m.dialect() != model_.dialect())
        {
            if (error)
                *error = tr("The text describes a different kind of model.");
            return false;
        }
    }
    catch (const std::exception& e)
    {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
    if (active_.isEmpty())
    {
        std::string before = model_.toText();
        restoreText(after);
        undo_.push(new SnapshotCommand(this, {}, std::move(before), after, tr("Edit input text")));
        return true;
    }
    commit(core::Model(core::InpDocument::parse(after), model_.dialect()), tr("Edit input text"));
    return true;
}

void ProjectDocument::restoreText(const std::string& text)
{
    const core::Dialect d = model_.dialect();
    model_ = core::Model(core::InpDocument::parse(text), d);
    workKeys_.clear();
    if (pruneSelection())
        emit selectionChanged();
    emit modelChanged();
}

void ProjectDocument::restoreState(const QString& scenario, const std::string& state)
{
    // Undo and redo act where the edit was made: switch there first.
    if (scenario != active_ && !setActiveScenario(scenario))
        return;
    if (scenario.isEmpty())
    {
        restoreText(state);
        return;
    }
    core::Scenario* s = core::findScenario(settings_.scenarios, scenario.toStdString());
    if (!s)
        return;
    s->changes = changesFromState(state);
    recomputeScenario();
    emit scenariosChanged();
    emit modelChanged();
}

void ProjectDocument::markSettingsChanged()
{
    settingsDirty_ = true;
    emit titleChanged();
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------
void ProjectDocument::recomputeScenario()
{
    conflicts_.clear();
    orphans_.clear();
    workKeys_.clear();
    std::string error;
    const auto chain = core::scenarioChain(settings_.scenarios, active_.toStdString(), &error);
    if (chain.empty())
    {
        // The scenario is gone: show the base model again.
        active_.clear();
        model_ = std::move(base_);
        base_ = core::Model();
        parentDoc_ = core::InpDocument();
        baseKeys_.clear();
        return;
    }
    const core::Dialect d = base_.dialect();
    core::InpDocument doc = base_.document();
    for (std::size_t i = 0; i + 1 < chain.size(); ++i)
        core::applyChanges(doc, d, chain[i]->changes, &conflicts_, chain[i]->name);
    parentDoc_ = doc;
    std::vector<core::ScenarioConflict> own;
    core::applyChanges(doc, d, chain.back()->changes, &own, chain.back()->name);
    for (const auto& k : own)
    {
        if (!k.missing)
            continue;
        for (const auto& c : chain.back()->changes)
        {
            if (sameTarget(c, k))
            {
                orphans_.push_back(c);
                break;
            }
        }
    }
    conflicts_.insert(conflicts_.end(), own.begin(), own.end());
    model_ = core::Model(std::move(doc), d);
    // The selection stays where its elements exist in the other model.
    if (pruneSelection())
        emit selectionChanged();
}

bool ProjectDocument::setActiveScenario(const QString& name, QString* error)
{
    if (name == active_)
        return true;
    std::string why;
    if (!name.isEmpty() && core::scenarioChain(settings_.scenarios, name.toStdString(), &why).empty())
    {
        if (error)
            *error = QString::fromStdString(why);
        return false;
    }
    if (results_ && !live_)
        kept_[active_] = results_;
    // Looked up before the switch: the shown results belong to the previous scenario.
    auto next = resultsFor(name);
    if (active_.isEmpty())
        base_ = model_;
    active_ = name;
    if (active_.isEmpty())
    {
        model_ = std::move(base_);
        base_ = core::Model();
        parentDoc_ = core::InpDocument();
        conflicts_.clear();
        orphans_.clear();
        workKeys_.clear();
        baseKeys_.clear();
        if (pruneSelection())
            emit selectionChanged();
    }
    else
    {
        baseKeys_.clear();
        recomputeScenario();
    }
    emit modelChanged();
    showResults(std::move(next));
    emit activeScenarioChanged();
    emit titleChanged();
    return true;
}

void ProjectDocument::showResults(std::shared_ptr<const core::ResultSet> r)
{
    // Stay at the same time of day when the other results have it.
    const double time = results_ && period_ < results_->periodCount() ? results_->times[static_cast<std::size_t>(period_)]
                                                                       : 0.0;
    const bool stateChanged = live_ || !note_.isEmpty();
    results_ = std::move(r);
    live_ = false;
    note_.clear();
    period_ = 0;
    if (results_)
    {
        const auto& t = results_->times;
        const auto it = std::lower_bound(t.begin(), t.end(), time - 0.5);
        period_ = it == t.end() ? std::max(0, results_->periodCount() - 1) : static_cast<int>(it - t.begin());
    }
    emit resultsChanged();
    emit periodChanged(period_);
    if (stateChanged)
        emit liveStateChanged();
}

void ProjectDocument::scenarioStructureChanged()
{
    // Undo states of scenarios refer to their parents; after structural
    // changes they would no longer restore the same models.
    if (!undo_.isClean())
        settingsDirty_ = true;
    undo_.clear();
    markSettingsChanged();
    emit scenariosChanged();
}

bool ProjectDocument::addScenario(core::Scenario scenario, QString* error)
{
    const std::string why = core::checkScenarioName(settings_.scenarios, scenario.name);
    if (!why.empty() || (!scenario.parent.empty() && !core::findScenario(settings_.scenarios, scenario.parent)))
    {
        if (error)
            *error = why.empty() ? tr("The parent scenario does not exist.") : QString::fromStdString(why);
        return false;
    }
    settings_.scenarios.push_back(std::move(scenario));
    markSettingsChanged();
    emit scenariosChanged();
    return true;
}

bool ProjectDocument::renameScenario(const QString& from, const QString& to, QString* error)
{
    if (from == to)
        return true;
    core::Scenario* s = core::findScenario(settings_.scenarios, from.toStdString());
    if (!s)
        return false;
    std::vector<core::Scenario> others;
    for (const auto& x : settings_.scenarios)
    {
        if (x.name != s->name)
            others.push_back(x);
    }
    const std::string why = core::checkScenarioName(others, to.toStdString());
    if (!why.empty())
    {
        if (error)
            *error = QString::fromStdString(why);
        return false;
    }
    // The results files follow the name.
    const QString oldBase = runBase(from);
    s->name = to.toStdString();
    const QString newBase = runBase(to);
    if (!path_.isEmpty())
    {
        for (const char* ext : {".anr", ".rpt"})
        {
            if (QFile::exists(oldBase + ext) && !QFile::exists(newBase + ext))
                QFile::rename(oldBase + ext, newBase + ext);
        }
    }
    for (auto& x : settings_.scenarios)
    {
        if (x.parent == from.toStdString())
            x.parent = s->name;
    }
    if (kept_.count(from))
    {
        kept_[to] = kept_[from];
        kept_.erase(from);
    }
    for (int i = 0; i < undo_.count(); ++i)
    {
        if (auto* c = dynamic_cast<SnapshotCommand*>(const_cast<QUndoCommand*>(undo_.command(i))))
        {
            if (c->scenario() == from)
                c->setScenario(to);
        }
    }
    if (active_ == from)
    {
        active_ = to;
        emit activeScenarioChanged();
        emit titleChanged();
    }
    markSettingsChanged();
    emit scenariosChanged();
    return true;
}

void ProjectDocument::removeScenario(const QString& name)
{
    const std::string n = name.toStdString();
    const core::Scenario* s = core::findScenario(settings_.scenarios, n);
    if (!s)
        return;
    const std::string parent = s->parent;
    if (active_ == name)
        setActiveScenario(QString::fromStdString(parent));
    // Scenarios based on this one keep their models.
    const core::Model& base = baseModel();
    const core::InpDocument parentDoc =
        core::scenarioDocument(base.document(), base.dialect(), settings_.scenarios, parent);
    std::vector<std::pair<std::string, std::vector<core::ScenarioChange>>> rebased;
    for (const auto& x : settings_.scenarios)
    {
        if (x.parent != n)
            continue;
        const auto doc = core::scenarioDocument(base.document(), base.dialect(), settings_.scenarios, x.name);
        rebased.emplace_back(x.name, core::diffDocuments(parentDoc, doc, base.dialect()));
    }
    for (auto& [child, changes] : rebased)
    {
        core::Scenario* c = core::findScenario(settings_.scenarios, child);
        c->parent = parent;
        c->changes = std::move(changes);
    }
    settings_.scenarios.erase(std::remove_if(settings_.scenarios.begin(), settings_.scenarios.end(),
                                             [&](const core::Scenario& x) { return x.name == n; }),
                              settings_.scenarios.end());
    kept_.erase(name);
    if (!active_.isEmpty())
    {
        recomputeScenario();
        emit modelChanged();
    }
    scenarioStructureChanged();
}

bool ProjectDocument::updateScenario(const QString& name, const core::Scenario& properties, QString* error)
{
    core::Scenario* s = core::findScenario(settings_.scenarios, name.toStdString());
    if (!s)
        return false;
    s->description = properties.description;
    s->engineId = properties.engineId;
    s->useMsx = properties.useMsx;
    s->msxFile = properties.msxFile;
    if (properties.parent != s->parent)
    {
        // The new parent must not be the scenario itself or one based on it.
        for (const core::Scenario* x : core::scenarioChain(settings_.scenarios, properties.parent))
        {
            if (x->name == s->name)
            {
                if (error)
                    *error = tr("A scenario cannot be based on itself or on a scenario based on it.");
                markSettingsChanged();
                emit scenariosChanged();
                return false;
            }
        }
        if (!properties.parent.empty() && !core::findScenario(settings_.scenarios, properties.parent))
        {
            if (error)
                *error = tr("The parent scenario does not exist.");
            return false;
        }
        const core::Model& base = baseModel();
        const auto doc = core::scenarioDocument(base.document(), base.dialect(), settings_.scenarios, s->name);
        const auto parentDoc =
            core::scenarioDocument(base.document(), base.dialect(), settings_.scenarios, properties.parent);
        s->changes = core::diffDocuments(parentDoc, doc, base.dialect());
        s->parent = properties.parent;
        if (!active_.isEmpty())
        {
            recomputeScenario();
            emit modelChanged();
        }
        scenarioStructureChanged();
        return true;
    }
    markSettingsChanged();
    emit scenariosChanged();
    return true;
}

core::Model ProjectDocument::scenarioModel(const QString& name, std::vector<core::ScenarioConflict>* conflicts) const
{
    if (name == active_)
    {
        if (conflicts)
            *conflicts = conflicts_;
        return model_;
    }
    return core::scenarioModel(baseModel(), settings_.scenarios, name.toStdString(), conflicts);
}

void ProjectDocument::revertChanges(std::vector<int> indices)
{
    core::Scenario* s = core::findScenario(settings_.scenarios, active_.toStdString());
    if (!s || indices.empty())
        return;
    std::string before = changesState(s->changes);
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
    {
        if (*it >= 0 && *it < static_cast<int>(s->changes.size()))
            s->changes.erase(s->changes.begin() + *it);
    }
    std::string after = changesState(s->changes);
    if (after == before)
        return;
    recomputeScenario();
    undo_.push(new SnapshotCommand(this, active_, std::move(before), std::move(after),
                                   tr("Revert %n change(s) (%1)", nullptr, static_cast<int>(indices.size())).arg(active_)));
    emit scenariosChanged();
    emit modelChanged();
}

const core::SectionKeys& ProjectDocument::keysOf(const core::Model& m, const std::string& section, bool base) const
{
    auto& cache = base ? baseKeys_ : workKeys_;
    auto it = cache.find(section);
    if (it == cache.end())
    {
        const core::InpSection* s = m.document().find(section);
        it = cache.emplace(section, s ? core::SectionKeys(*s, m.dialect()) : core::SectionKeys()).first;
    }
    return it->second;
}

bool ProjectDocument::baseValue(const std::string& section, int row, int column, bool tail, QString* base) const
{
    if (active_.isEmpty() || row < 0 || column < 0)
        return false;
    const core::InpSection* ws = model_.document().find(section);
    if (!ws)
        return false;
    const core::SectionKeys& wk = keysOf(model_, section, false);
    if (row >= wk.rowCount())
        return false;
    const core::InpSection* bs = base_.document().find(section);
    const int br = bs ? keysOf(base_, section, true).find(wk.key(row), wk.ordinal(row)) : -1;
    if (br < 0)
    {
        *base = tr("(not in the base model)");
        return true;
    }
    const auto wt = ws->rowTokens(row);
    const auto bt = bs->rowTokens(br);
    auto valueAt = [&](const std::vector<std::string>& t) {
        const auto c = static_cast<std::size_t>(column);
        if (c >= t.size())
            return std::string();
        if (!tail)
            return text::unquote(t[c]);
        return text::join(std::vector<std::string>(t.begin() + static_cast<std::ptrdiff_t>(c), t.end()), " ");
    };
    const std::string w = valueAt(wt), b = valueAt(bt);
    if (w == b)
        return false;
    *base = b.empty() ? tr("(empty)") : QString::fromStdString(b);
    return true;
}

core::ScenarioEngine ProjectDocument::engineChoice(const QString& scenario) const
{
    return core::scenarioEngine(settings_.scenarios, scenario.toStdString(),
                                {settings_.engineId, settings_.useMsx, settings_.msxFile});
}

void ProjectDocument::setEngineChoice(const std::string& engineId, bool useMsx)
{
    core::Scenario* s = core::findScenario(settings_.scenarios, active_.toStdString());
    if (!s)
    {
        settings_.engineId = engineId;
        settings_.useMsx = useMsx;
        markSettingsChanged();
        return;
    }
    // Store only what differs from the parent.
    const auto parent = engineChoice(QString::fromStdString(s->parent));
    s->engineId = engineId == parent.engineId ? std::string() : engineId;
    s->useMsx = useMsx == parent.useMsx ? -1 : (useMsx ? 1 : 0);
    markSettingsChanged();
    emit scenariosChanged();
}

QString ProjectDocument::runBase(const QString& scenario) const
{
    const QString suffix =
        scenario.isEmpty() ? QString() : "." + QString::fromStdString(core::scenarioFileStem(scenario.toStdString()));
    if (path_.isEmpty())
        return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("untitled" + suffix);
    const QFileInfo fi(path_);
    return fi.dir().filePath(fi.completeBaseName() + suffix);
}

std::shared_ptr<const core::ResultSet> ProjectDocument::resultsFor(const QString& scenario)
{
    if (scenario == active_ && results_)
        return results_;
    const auto it = kept_.find(scenario);
    if (it != kept_.end())
        return it->second;
    const QString file = resultsPath(scenario);
    if (path_.isEmpty() || !QFileInfo::exists(file))
        return nullptr;
    try
    {
        auto r = std::make_shared<const core::ResultSet>(core::ResultSet::loadAny(std::filesystem::u8path(file.toStdString())));
        kept_[scenario] = r;
        return r;
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
}

void ProjectDocument::reloadResults(const QString& scenario)
{
    kept_.erase(scenario);
    if (scenario != active_)
        return;
    const QString file = resultsPath(scenario);
    if (!QFileInfo::exists(file))
        return;
    try
    {
        setResults(std::make_unique<core::ResultSet>(core::ResultSet::loadAny(std::filesystem::u8path(file.toStdString()))));
    }
    catch (const std::exception&)
    {
    }
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------
void ProjectDocument::setResults(std::unique_ptr<core::ResultSet> r)
{
    // Coming from live results, stay at the time the map showed.
    const bool keepTime = live_ || !note_.isEmpty();
    const double time = keepTime && results_ && period_ < results_->periodCount()
                            ? results_->times[static_cast<std::size_t>(period_)]
                            : 0.0;
    results_ = std::move(r);
    if (results_)
        kept_[active_] = results_;
    period_ = 0;
    if (keepTime && results_)
    {
        const auto& t = results_->times;
        const auto it = std::lower_bound(t.begin(), t.end(), time - 0.5);
        period_ = it == t.end() ? std::max(0, results_->periodCount() - 1)
                                : static_cast<int>(it - t.begin());
    }
    const bool stateChanged = live_ || !note_.isEmpty();
    live_ = false;
    note_.clear();
    emit resultsChanged();
    emit periodChanged(period_);
    if (stateChanged)
        emit liveStateChanged();
}

void ProjectDocument::clearResults()
{
    kept_.erase(active_);
    if (!results_)
        return;
    results_.reset();
    period_ = 0;
    const bool stateChanged = live_ || !note_.isEmpty();
    live_ = false;
    note_.clear();
    emit resultsChanged();
    if (stateChanged)
        emit liveStateChanged();
}

void ProjectDocument::setPeriod(int p)
{
    if (!results_)
        return;
    const int last = results_->periodCount() - 1;
    p = std::max(0, std::min(p, last));
    if (live_)
        followLatest_ = p == last;
    if (p == period_)
        return;
    period_ = p;
    emit periodChanged(p);
}

void ProjectDocument::beginLiveResults(std::shared_ptr<const core::ResultSet> live)
{
    if (!live || live->periodCount() == 0)
        return;
    results_ = std::move(live);
    live_ = true;
    followLatest_ = true;
    note_.clear();
    period_ = results_->periodCount() - 1;
    emit resultsChanged();
    emit periodChanged(period_);
    emit liveStateChanged();
}

void ProjectDocument::liveResultsGrew(const core::ResultSet* live, int firstChanged)
{
    // Ignore a run whose results are no longer shown (cleared or replaced).
    if (!live_ || results_.get() != live)
        return;
    const int count = results_->periodCount();
    emit liveResultsUpdated(std::max(0, std::min(firstChanged, count - 1)));
    if (followLatest_ && period_ != count - 1)
    {
        period_ = count - 1;
        emit periodChanged(period_);
    }
}

void ProjectDocument::endLiveResults(const QString& note)
{
    if (!live_)
        return;
    live_ = false;
    note_ = note;
    emit liveStateChanged();
}

bool ProjectDocument::isSelected(core::Category category, const std::string& id) const
{
    const int c = static_cast<int>(category);
    return c > 0 && c < 5 && selectedIds_[c].count(id) > 0;
}

void ProjectDocument::rebuildSelectionIndex()
{
    for (auto& set : selectedIds_)
        set.clear();
    for (const auto& s : selected_)
        selectedIds_[static_cast<int>(s.category)].insert(s.id.toStdString());
}

bool ProjectDocument::pruneSelection()
{
    QList<Selection> kept;
    for (const auto& s : selected_)
    {
        if (model_.idInUse(s.category, s.id.toStdString()))
            kept.append(s);
    }
    if (kept.size() == selected_.size())
        return false;
    selected_ = kept;
    if (!selection_.isEmpty() && !model_.idInUse(selection_.category, selection_.id.toStdString()))
        selection_ = selected_.isEmpty() ? Selection{} : selected_.last();
    rebuildSelectionIndex();
    return true;
}

void ProjectDocument::select(const Selection& s)
{
    if (s.isEmpty())
    {
        if (selected_.isEmpty() && selection_.isEmpty())
            return;
        selected_.clear();
        selection_ = {};
    }
    else
    {
        if (selected_.size() == 1 && selected_.first() == s && selection_ == s)
            return;
        selected_ = {s};
        selection_ = s;
    }
    rebuildSelectionIndex();
    emit selectionChanged();
}

void ProjectDocument::setSelection(const QList<Selection>& items, const Selection& current)
{
    QList<Selection> list;
    std::unordered_set<std::string> seen[5];
    for (const auto& s : items)
    {
        if (!s.isEmpty() && seen[static_cast<int>(s.category)].insert(s.id.toStdString()).second)
            list.append(s);
    }
    Selection cur = list.contains(current) ? current : (list.isEmpty() ? Selection{} : list.first());
    if (list == selected_ && cur == selection_)
        return;
    selected_ = list;
    selection_ = cur;
    rebuildSelectionIndex();
    emit selectionChanged();
}

void ProjectDocument::addToSelection(const QList<Selection>& items)
{
    QList<Selection> list = selected_;
    Selection last = selection_;
    for (const auto& s : items)
    {
        if (s.isEmpty())
            continue;
        if (!isSelected(s))
            list.append(s);
        last = s;
    }
    setSelection(list, last);
}

void ProjectDocument::removeFromSelection(const QList<Selection>& items)
{
    std::unordered_set<std::string> drop[5];
    for (const auto& s : items)
        drop[static_cast<int>(s.category)].insert(s.id.toStdString());
    QList<Selection> list;
    for (const auto& s : selected_)
    {
        if (!drop[static_cast<int>(s.category)].count(s.id.toStdString()))
            list.append(s);
    }
    setSelection(list, list.contains(selection_) ? selection_ : (list.isEmpty() ? Selection{} : list.last()));
}

void ProjectDocument::toggleSelected(const Selection& s)
{
    if (s.isEmpty())
        return;
    if (isSelected(s))
        removeFromSelection({s});
    else
        addToSelection({s});
}

void ProjectDocument::setCurrent(const Selection& s)
{
    if (!isSelected(s) || s == selection_)
        return;
    selection_ = s;
    emit selectionChanged();
}

} // namespace mirab::app
