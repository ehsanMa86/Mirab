/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ObservedData.cpp
 * Purpose: Observed data of a project: set-up in the sidecar, cached series and lookups for charts.
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
#include "ObservedData.h"

#include "mirab/core/Text.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace mirab::app
{

QString tableOfCategory(core::Category category)
{
    switch (category)
    {
    case core::Category::Node:
        return "node";
    case core::Category::Link:
        return "link";
    case core::Category::Area:
        return "subcatchment";
    default:
        return {};
    }
}

ObservedData::ObservedData(ProjectDocument* doc) : QObject(doc), doc_(doc)
{
    setObjectName("observedData");
    connect(doc_, &ProjectDocument::modelReset, this, [this] {
        reload();
        emit changed();
    });
}

ObservedData* ObservedData::of(ProjectDocument* doc)
{
    if (auto* existing = doc->findChild<ObservedData*>("observedData", Qt::FindDirectChildrenOnly))
        return existing;
    return new ObservedData(doc);
}

core::CalibrationConfig ObservedData::config() const
{
    return core::CalibrationConfig::fromJson(doc_->settings().calibration);
}

void ObservedData::setConfig(const core::CalibrationConfig& config)
{
    const json::Value v = config.toJson();
    if (v.dump() == doc_->settings().calibration.dump())
        return;
    doc_->settings().calibration = v;
    doc_->markSettingsChanged();
    reload();
    emit changed();
}

QString ObservedData::baseDir() const
{
    return doc_->path().isEmpty() ? QString() : QFileInfo(doc_->path()).absolutePath();
}

QString ObservedData::storedPath(const QString& absolutePath) const
{
    const QString base = baseDir();
    if (base.isEmpty())
        return QDir::cleanPath(absolutePath);
    const QString rel = QDir(base).relativeFilePath(absolutePath);
    // Keep files in other drives or far away absolute.
    if (QDir::isAbsolutePath(rel) || rel.startsWith("../../"))
        return QDir::cleanPath(absolutePath);
    return rel;
}

void ObservedData::reload()
{
    loaded_ = false;
}

void ObservedData::ensureLoaded()
{
    const std::string key = doc_->path().toStdString() + "\n" + doc_->settings().calibration.dump();
    if (loaded_ && key == loadedKey_)
        return;
    loadedKey_ = key;
    loaded_ = true;
    series_.clear();
    warnings_.clear();
    const auto cfg = config();
    if (cfg.observed.empty())
        return;
    std::vector<std::string> w;
    series_ = core::loadObserved(cfg, std::filesystem::u8path(baseDir().toStdString()), &doc_->model(), &w);
    for (const auto& s : w)
        warnings_ << QString::fromStdString(s);
}

const std::vector<core::ObservedSeries>& ObservedData::series()
{
    ensureLoaded();
    return series_;
}

QStringList ObservedData::warnings()
{
    ensureLoaded();
    return warnings_;
}

core::TimeBase ObservedData::timeBase() const
{
    return core::timeBaseFor(&doc_->model(), doc_->results(), config().simulationStart);
}

QVector<QPointF> ObservedData::pointsFor(core::Category category, const QString& id, const QString& variable,
                                         const QString& simUnit)
{
    QVector<QPointF> out;
    const QString table = tableOfCategory(category);
    if (table.isEmpty() || id.isEmpty())
        return out;
    const auto& all = series();
    if (all.empty())
        return out;
    const core::TimeBase base = timeBase();
    const auto* results = doc_->results();
    const double t0 = results && !results->times.empty() ? results->times.front() : -1e300;
    const double t1 = results && !results->times.empty() ? results->times.back() : 1e300;
    for (const auto& s : all)
    {
        if (QString::fromStdString(s.table) != table || QString::fromStdString(s.element) != id)
            continue;
        const QString v = QString::fromStdString(s.variable);
        const bool same = v.compare(variable, Qt::CaseInsensitive) == 0 ||
                          variable.endsWith(" " + v, Qt::CaseInsensitive);
        if (!same)
            continue;
        const double factor = core::unitFactor(s.unit, simUnit.toStdString()).value_or(1.0);
        const auto times = core::simulationTimes(s, base);
        for (std::size_t i = 0; i < times.size() && i < s.values.size(); ++i)
        {
            if (std::isfinite(s.values[i]) && std::isfinite(times[i]) && times[i] >= t0 && times[i] <= t1)
                out.append(QPointF(times[i], s.values[i] * factor));
        }
    }
    std::sort(out.begin(), out.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    return out;
}

} // namespace mirab::app
