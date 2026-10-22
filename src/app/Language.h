/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Language.h
 * Purpose: Interface languages: the language setting, translators and translations of library strings.
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

#include <QList>
#include <QString>
#include <QStyledItemDelegate>

#include <cstddef>
#include <string>
#include <string_view>

namespace mirab::app
{

struct UiLanguage
{
    QString code;       ///< "fa", "zh_CN", ...; empty for the system language
    QString nativeName; ///< name in the language itself
};

/// Languages offered in Settings. The first entry (empty code) follows the system.
QList<UiLanguage> uiLanguages();
/// Language chosen in Settings; empty for the system language.
QString configuredLanguage();
void setConfiguredLanguage(const QString& code);
/// Loads the translations of Mirab and Qt for @p code (empty: the system
/// language) and sets the layout direction. Returns the language in use
/// ("en" when no translation matches).
QString installTranslations(const QString& code);
/// Language in use after installTranslations().
QString currentLanguage();

/// Translation of a text that comes from the Qt-free libraries (section
/// descriptions, column help, element kinds, ...). Texts without a
/// translation are returned unchanged.
QString coreText(std::string_view text);
inline QString coreText(const std::string& text) { return coreText(std::string_view(text)); }
inline QString coreText(const char* text) { return coreText(std::string_view(text ? text : "")); }

/// Item delegate of data tables. In a right-to-left interface, a cell whose
/// text has no right-to-left letters ("-27", "12 AM", "1.0 mg/L") is laid out
/// left to right, so numbers and IDs read correctly; the cell keeps its
/// alignment. Its editor is laid out the same way. installTranslations() gives
/// every table with the default delegate one of these; tables with their own
/// delegate derive from it.
class DataItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;
};

/// Library texts that the interface translates with coreText(), listed for
/// lupdate in CoreStrings.cpp (kept in sync by the i18n test).
extern const char* const kCoreStrings[];
extern const std::size_t kCoreStringCount;

} // namespace mirab::app
