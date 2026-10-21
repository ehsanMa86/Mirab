/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SwmmEditors.h
 * Purpose: Editors of SWMM objects: LID controls and usage, transects and unit hydrographs.
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

#include "mirab/core/SwmmObjects.h"

#include <QDialog>
#include <QWidget>

#include <map>
#include <vector>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSplitter;
class QTabWidget;
class QTableWidget;

namespace mirab::app
{

class ChartWidget;

/// Frame shared by the object editors: a header with the title, a state line,
/// Revert and Apply, a body and a list of validation messages. Apply writes
/// the object as one undoable edit.
class SpecialEditor : public QWidget
{
    Q_OBJECT
public:
    QString section() const { return section_; }
    QString objectId() const { return id_; }
    bool hasPendingEdits() const;

signals:
    /// Apply gave the object a new ID.
    void renamed(const QString& section, const QString& newId);

protected:
    SpecialEditor(ProjectDocument* doc, const QString& section, const QString& id, QWidget* parent);

    virtual void load() = 0;
    virtual void apply() = 0;

    void setTitle(const QString& title);
    void setState(const QString& text);
    void setDirty(bool on);
    /// Fills the message list; returns true when there are errors.
    bool showIssues(const std::vector<core::ObjectIssue>& issues);
    /// Called by apply() after a successful edit.
    void applied(const QString& newId);
    core::UnitSystem unitSystem() const;
    void setMessageHeight(int height);

    QHBoxLayout* headerTools() const { return tools_; }
    QWidget* body() const { return body_; }
    QListWidget* messageList() const { return messages_; }

    ProjectDocument* doc_;
    QString section_;
    QString id_;
    bool loading_ = false;
    bool hasErrors_ = false;

private:
    void updateButtons();

    QLabel* title_;
    QLabel* state_;
    QHBoxLayout* tools_;
    QPushButton* revert_;
    QPushButton* apply_;
    QWidget* body_;
    QListWidget* messages_;
    QSplitter* split_ = nullptr;
    bool dirty_ = false;
};

/// Schematic cross-section of a LID control, drawn from its layers.
class LidSchematic : public QWidget
{
    Q_OBJECT
public:
    explicit LidSchematic(QWidget* parent = nullptr);
    void setControl(const core::LidControl& control, core::UnitSystem system);
    QSize sizeHint() const override { return {420, 360}; }
    QSize minimumSizeHint() const override { return {260, 240}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    core::LidControl control_;
    core::UnitSystem system_ = core::UnitSystem::US;
};

/// Editor of one LID control ([LID_CONTROLS]).
class LidControlEditor : public SpecialEditor
{
    Q_OBJECT
public:
    LidControlEditor(ProjectDocument* doc, const QString& id, QWidget* parent = nullptr);

protected:
    void load() override;
    void apply() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Field
    {
        core::LidLayer layer;
        int index = 0;
        QLineEdit* edit = nullptr;
        QComboBox* combo = nullptr;
        QString text() const;
        void setText(const QString& value);
        QWidget* widget() const;
    };

    QString typeCode() const;
    void buildLayerTabs();
    void collect();
    void refresh();
    core::LidControl current() const;

    QLineEdit* name_;
    QComboBox* type_;
    QLabel* typeHelp_;
    QLabel* paramHelp_;
    QTabWidget* layerTabs_;
    LidSchematic* schematic_;
    QTableWidget* removals_ = nullptr;
    std::vector<Field> fields_;
    std::map<core::LidLayer, std::vector<std::string>> values_;
    bool exists_ = false;
};

/// LID units placed in one subcatchment ([LID_USAGE]).
class LidUsageDialog : public QDialog
{
    Q_OBJECT
public:
    LidUsageDialog(ProjectDocument* doc, const QString& subcatchment, QWidget* parent = nullptr);

    /// Shows the dialog for a subcatchment and applies the changes as one
    /// undoable edit. Returns true when the project changed.
    static bool edit(ProjectDocument* doc, const QString& subcatchment, QWidget* parent);

    std::vector<core::LidUsage> units() const;

private:
    void addRow(const core::LidUsage& unit);
    void refresh();
    void accept() override;

    ProjectDocument* doc_;
    QString subcatchment_;
    QStringList lidNames_;
    QStringList drainTargets_;
    QTableWidget* table_;
    QLabel* summary_;
    QListWidget* messages_;
    QPushButton* ok_;
};

/// Editor of one transect ([TRANSECTS]) with a cross-section chart.
class TransectEditor : public SpecialEditor
{
    Q_OBJECT
public:
    TransectEditor(ProjectDocument* doc, const QString& id, QWidget* parent = nullptr);

protected:
    void load() override;
    void apply() override;

private:
    core::Transect current(std::vector<core::ObjectIssue>* issues = nullptr) const;
    void refresh();
    void paste();

    QLineEdit* name_;
    QLineEdit* nLeft_;
    QLineEdit* nChannel_;
    QLineEdit* nRight_;
    QLineEdit* leftBank_;
    QLineEdit* rightBank_;
    QLineEdit* stationFactor_;
    QLineEdit* elevationOffset_;
    QLineEdit* meander_;
    QTableWidget* table_;
    QSlider* depth_;
    QLabel* geometry_;
    ChartWidget* chart_;
    bool exists_ = false;
};

/// Editor of one RDII unit hydrograph group ([HYDROGRAPHS]).
class HydrographEditor : public SpecialEditor
{
    Q_OBJECT
public:
    HydrographEditor(ProjectDocument* doc, const QString& id, QWidget* parent = nullptr);

protected:
    void load() override;
    void apply() override;

private:
    void showMonth(int month);
    void storeMonth();
    void refresh();
    void updateMonthLabels();

    QLineEdit* name_;
    QComboBox* gage_;
    QListWidget* months_;
    QTableWidget* table_;
    QLabel* note_;
    ChartWidget* chart_;
    core::UnitHydrographGroup group_;
    int month_ = 0;
    bool exists_ = false;
};

/// True when @p section has one of the editors above (or the control rule editor).
bool hasSpecialEditor(core::Dialect dialect, const QString& section);
/// Creates the editor of an object; nullptr when the section has none.
SpecialEditor* createSpecialEditor(ProjectDocument* doc, const QString& section, const QString& id,
                                 QWidget* parent);
QString objectEditorTitle(core::Dialect dialect, const QString& section, const QString& id);
QString objectEditorIcon(const QString& section);

} // namespace mirab::app
