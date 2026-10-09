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

#include <QFileInfo>
#include <QUndoCommand>

namespace mirab::app
{

namespace
{
class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(ProjectDocument* doc, std::string before, std::string after, const QString& text)
        : QUndoCommand(text), doc_(doc), before_(std::move(before)), after_(std::move(after))
    {
    }
    void undo() override { doc_->restoreText(before_); }
    void redo() override
    {
        if (first_)
        {
            first_ = false; // the edit has already been applied
            return;
        }
        doc_->restoreText(after_);
    }

private:
    ProjectDocument* doc_;
    std::string before_;
    std::string after_;
    bool first_ = true;
};
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
    clearResults();
    selection_ = {};
    emit modelReset();
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
    clearResults();
    selection_ = {};
    emit modelReset();
    emit titleChanged();
}

void ProjectDocument::save(const QString& path)
{
    const std::filesystem::path p = std::filesystem::u8path(path.toStdString());
    model_.save(p);
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
    std::string before = model_.toText();
    std::string err;
    core::Model working = model_;
    if (!fn(working, &err))
    {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    std::string after = working.toText();
    if (after == before)
        return true;
    model_ = std::move(working);
    undo_.push(new SnapshotCommand(this, std::move(before), std::move(after), description));
    emit modelChanged();
    return true;
}

bool ProjectDocument::replaceText(const QString& text, QString* error)
{
    const std::string after = text.toStdString();
    if (after == model_.toText())
        return true;
    try
    {
        auto m = core::Model::fromText(after);
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
    std::string before = model_.toText();
    restoreText(after);
    undo_.push(new SnapshotCommand(this, std::move(before), after, tr("Edit input text")));
    return true;
}

void ProjectDocument::restoreText(const std::string& text)
{
    const core::Dialect d = model_.dialect();
    model_ = core::Model(core::InpDocument::parse(text), d);
    if (!selection_.isEmpty() && !model_.idInUse(selection_.category, selection_.id.toStdString()))
    {
        selection_ = {};
        emit selectionChanged();
    }
    emit modelChanged();
}

void ProjectDocument::markSettingsChanged()
{
    settingsDirty_ = true;
    emit titleChanged();
}

void ProjectDocument::setResults(std::unique_ptr<core::ResultSet> r)
{
    results_ = std::move(r);
    period_ = results_ && results_->periodCount() > 0 ? 0 : 0;
    emit resultsChanged();
    emit periodChanged(period_);
}

void ProjectDocument::clearResults()
{
    if (!results_)
        return;
    results_.reset();
    period_ = 0;
    emit resultsChanged();
}

void ProjectDocument::setPeriod(int p)
{
    if (!results_)
        return;
    p = std::max(0, std::min(p, results_->periodCount() - 1));
    if (p == period_)
        return;
    period_ = p;
    emit periodChanged(p);
}

void ProjectDocument::select(const Selection& s)
{
    if (s == selection_)
        return;
    selection_ = s;
    emit selectionChanged();
}

} // namespace mirab::app
