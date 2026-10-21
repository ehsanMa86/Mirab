/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ObservedData.h
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
#pragma once

#include "ProjectDocument.h"
#include "mirab/core/Calibration.h"

#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mirab::app
{

/// The observed data and calibration set-up of a project. One instance
/// lives as a child of each ProjectDocument (see of()). The series are read
/// from their files on demand and cached until the set-up or project changes.
class ObservedData : public QObject
{
    Q_OBJECT
public:
    static ObservedData* of(ProjectDocument* doc);

    core::CalibrationConfig config() const;
    /// Stores the set-up in the project sidecar (marks the project modified).
    void setConfig(const core::CalibrationConfig& config);

    /// Folder that relative file paths start from (the project folder).
    QString baseDir() const;
    /// Path to store for a file: relative to the project folder when possible.
    QString storedPath(const QString& absolutePath) const;

    const std::vector<core::ObservedSeries>& series();
    QStringList warnings();
    core::TimeBase timeBase() const;
    bool isEmpty() { return series().empty(); }

    /// Observed points of an element and result (x in seconds from the start,
    /// values in the unit @p simUnit when it can be converted).
    QVector<QPointF> pointsFor(core::Category category, const QString& id, const QString& variable,
                               const QString& simUnit = {});

    /// Re-reads the files at the next access.
    void reload();

signals:
    void changed();

private:
    explicit ObservedData(ProjectDocument* doc);
    void ensureLoaded();

    ProjectDocument* doc_;
    std::vector<core::ObservedSeries> series_;
    QStringList warnings_;
    std::string loadedKey_;
    bool loaded_ = false;
};

/// Result table name of a category ("node", "link", "subcatchment").
QString tableOfCategory(core::Category category);

} // namespace mirab::app
