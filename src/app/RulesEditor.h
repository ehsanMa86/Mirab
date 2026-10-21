/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/RulesEditor.h
 * Purpose: Control rule editor with syntax highlighting, live checks, templates and a builder.
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

#include "SwmmEditors.h"

#include "mirab/core/ControlRules.h"

#include <QDialog>
#include <QSet>
#include <QSyntaxHighlighter>

#include <set>
#include <string>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;

namespace mirab::app
{

/// Highlights rule keywords, object types, attributes, values and comments,
/// and underlines lines with errors.
class RuleHighlighter : public QSyntaxHighlighter
{
public:
    RuleHighlighter(QTextDocument* doc, core::RuleSyntax syntax, bool simpleControls);
    /// 0-based line numbers to underline.
    void setErrorLines(const QSet<int>& lines);

protected:
    void highlightBlock(const QString& text) override;

private:
    QSet<QString> keywords_, objects_, attributes_, others_;
    QSet<int> errors_;
};

/// Editor of SWMM [CONTROLS], EPANET [RULES] and EPANET simple [CONTROLS]:
/// a rule list, the text of the selected rule and live validation messages.
class ControlRulesEditor : public SpecialEditor
{
    Q_OBJECT
public:
    ControlRulesEditor(ProjectDocument* doc, const QString& section, QWidget* parent = nullptr);

protected:
    void load() override;
    void apply() override;

private:
    std::vector<std::string> allLines(std::vector<std::pair<int, int>>* origin = nullptr) const;
    void setLines(const std::vector<std::string>& lines, const QString& select = {});
    void selectBlock(int index);
    void validate();
    void addRule(const QStringList& variables, const QStringList& lines);
    void deleteRule();
    void openBuilder();
    void fillTemplateMenu();
    QString blockLabel(int index) const;

    core::RuleSyntax syntax_;
    bool simple_ = false;
    QListWidget* list_;
    QPlainTextEdit* edit_;
    RuleHighlighter* highlighter_;
    QTimer* timer_;
    QToolButton* newRule_;
    QPushButton* builder_;
    QPushButton* delete_;
    QStringList blocks_;
    int current_ = -1;
    bool updatingList_ = false;
};

/// Composes one rule from combo boxes: a condition (optionally two), an
/// action, an optional ELSE action and a priority.
class RuleBuilderDialog : public QDialog
{
    Q_OBJECT
public:
    RuleBuilderDialog(core::RuleSyntax syntax, const core::RuleContext& context,
                      const std::set<std::string>& usedNames, QWidget* parent = nullptr);
    QStringList ruleLines() const;

private:
    struct Clause
    {
        QComboBox* object = nullptr;
        QComboBox* id = nullptr;
        QComboBox* attribute = nullptr;
        QComboBox* relation = nullptr;
        QComboBox* value = nullptr;
    };
    Clause makeClause(QWidget* parent, bool action);
    void objectChanged(Clause& c, bool action);
    void attributeChanged(Clause& c, bool action);
    QString clauseText(const Clause& c, bool action, const QString& value = {}) const;
    void updatePreview();

    core::RuleSyntax syntax_;
    const core::RuleContext& context_;
    QLineEdit* name_ = nullptr;
    Clause cond_, cond2_, action_;
    QCheckBox* useCond2_ = nullptr;
    QComboBox* join_ = nullptr;
    QCheckBox* useElse_ = nullptr;
    QComboBox* elseValue_ = nullptr;
    QLineEdit* priority_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QListWidget* messages_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace mirab::app
