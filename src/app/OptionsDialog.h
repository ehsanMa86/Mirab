/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/OptionsDialog.h
 * Purpose: Simulation options dialog for SWMM and EPANET projects.
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

#include <QDialog>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTableWidget;
class QTimeEdit;

namespace mirab::app
{

/// Structured editor of the option sections of a project: for SWMM [OPTIONS],
/// [FILES], [REPORT], [EVAPORATION], [TEMPERATURE] and [ADJUSTMENTS]; for
/// EPANET [OPTIONS], [TIMES], [ENERGY], [REACTIONS] and [REPORT]. Only values
/// the user changes are written, as one undoable edit; keys the dialog does
/// not show are kept.
class OptionsDialog : public QDialog
{
    Q_OBJECT
public:
    OptionsDialog(ProjectDocument* doc, QWidget* parent = nullptr);
    /// Shows a page by key: general, dates, steps, dynwave, files, report,
    /// evaporation, climate, adjustments (SWMM); hydraulics, quality, times,
    /// energy, reactions, report (EPANET).
    void showPage(const QString& key);
    /// Number of option rows the last accept() wrote.
    int changedCount() const { return changed_; }

private:
    struct Field
    {
        std::function<QString()> state;
        std::function<void(core::InpDocument&)> write;
        QString initial;
    };

    QFormLayout* addPage(const QString& key, const QString& title);
    void addField(std::function<QString()> state, std::function<void(core::InpDocument&)> write);
    QString value(const char* section, const char* key) const;

    // Field builders: each reads the current value and writes it back when it changes.
    QComboBox* choice(QFormLayout* form, const QString& label, const char* section, const char* key,
                      const QStringList& choices, const QString& fallback, const QString& tip = {});
    QCheckBox* yesNo(QFormLayout* form, const QString& label, const char* section, const char* key,
                     bool fallback, bool inverted = false, const QString& tip = {});
    QLineEdit* number(QFormLayout* form, const QString& label, const char* section, const char* key,
                      const QString& fallback, const QString& unit = {}, const QString& tip = {});
    QLineEdit* duration(QFormLayout* form, const QString& label, const char* section, const char* key,
                        const QString& fallback, const QString& tip = {});
    void dateTime(QFormLayout* form, const QString& label, const char* dateKey, const char* timeKey);
    QTableWidget* valueTable(const QStringList& rows, const QStringList& columns);
    QLineEdit* fileRow(QFormLayout* form, const QString& label, const char* key, bool save);

    void buildSwmm();
    void buildSwmmReport(QFormLayout* form);
    void buildEvaporation(QFormLayout* form);
    void buildClimate(QFormLayout* form);
    void buildAdjustments(QFormLayout* form);
    void buildEpanet();
    void accept() override;

    ProjectDocument* doc_;
    QListWidget* pages_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QStringList pageKeys_;
    std::vector<Field> fields_;
    int changed_ = 0;
};

} // namespace mirab::app
