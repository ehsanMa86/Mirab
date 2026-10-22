/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Language.cpp
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
#include "Language.h"

#include <QApplication>
#include <QFontDatabase>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QTableView>
#include <QTranslator>

namespace mirab::app
{

namespace
{
QString& current()
{
    static QString code = QStringLiteral("en");
    return code;
}

bool rightToLeft(const QString& code)
{
    return code == "fa" || code == "ar";
}

/// Persian and Arabic use Vazirmatn, compiled into the program: system fonts
/// often join or space these letters poorly in interface sizes.
void useArabicScriptFont()
{
    const int regular = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Vazirmatn-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Vazirmatn-Bold.ttf"));
    if (regular < 0)
        return;
    QFont f = QApplication::font();
    QStringList families = QFontDatabase::applicationFontFamilies(regular);
    families << f.families();
    f.setFamilies(families);
    QApplication::setFont(f);
}

/// Gives tables with the default delegate a DataItemDelegate when they are first shown.
class DataTables : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish)
        {
            if (auto* view = qobject_cast<QTableView*>(watched))
            {
                const QAbstractItemDelegate* d = view->itemDelegate();
                if (d && d->metaObject() == &QStyledItemDelegate::staticMetaObject)
                    view->setItemDelegate(new DataItemDelegate(view));
            }
        }
        return false;
    }
};
} // namespace

QWidget* DataItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const
{
    QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (editor && parent->layoutDirection() == Qt::RightToLeft &&
        !index.data(Qt::EditRole).toString().isRightToLeft())
        editor->setLayoutDirection(Qt::LeftToRight);
    return editor;
}

void DataItemDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    if (option->direction == Qt::RightToLeft && !option->text.isRightToLeft())
    {
        option->displayAlignment =
            QStyle::visualAlignment(Qt::RightToLeft, option->displayAlignment);
        option->direction = Qt::LeftToRight;
    }
}

QList<UiLanguage> uiLanguages()
{
    return {{QString(), QCoreApplication::translate("Language", "System default"), QStringLiteral("System default")},
            {"en", QStringLiteral("English"), QStringLiteral("English")},
            {"fa", QStringLiteral(u"فارسی"), QStringLiteral("Persian")},
            {"ar", QStringLiteral(u"العربية"), QStringLiteral("Arabic")},
            {"es", QStringLiteral(u"Español"), QStringLiteral("Spanish")},
            {"zh_CN", QStringLiteral(u"简体中文"), QStringLiteral("Chinese, Simplified")}};
}

QString languageLabel(const UiLanguage& language)
{
    if (language.nativeName == language.englishName)
        return language.nativeName;
    // First-strong isolates keep the two names in their own directions in either layout.
    return language.nativeName + QStringLiteral(u" \u2068(") + language.englishName + QStringLiteral(u")\u2069");
}

QString withEnglish(const QString& translated, const QString& english)
{
    if (current() == "en" || translated == english)
        return translated;
    return translated + QStringLiteral(u" \u2068(") + english + QStringLiteral(u")\u2069");
}

bool askRestartForLanguage(QWidget* parent)
{
    QString text = QCoreApplication::translate("Language", "Restart Mirab to use the new language.");
    const QString english = QStringLiteral("Restart Mirab to use the new language.");
    if (current() != "en" && text != english)
        text += QStringLiteral("\n\n") + english;
    QMessageBox box(QMessageBox::Question, withEnglish(QCoreApplication::translate("Language", "Language"),
                                                      QStringLiteral("Language")),
                    text, QMessageBox::NoButton, parent);
    QPushButton* now = box.addButton(withEnglish(QCoreApplication::translate("Language", "Restart now"),
                                                 QStringLiteral("Restart now")),
                                     QMessageBox::AcceptRole);
    box.addButton(withEnglish(QCoreApplication::translate("Language", "Later"), QStringLiteral("Later")),
                  QMessageBox::RejectRole);
    box.setDefaultButton(now);
    box.exec();
    return box.clickedButton() == now;
}

QString configuredLanguage()
{
    return QSettings().value("ui/language").toString();
}

void setConfiguredLanguage(const QString& code)
{
    if (code.isEmpty())
        QSettings().remove("ui/language");
    else
        QSettings().setValue("ui/language", code);
}

QString installTranslations(const QString& code)
{
    // Translations are compiled into the program (":/i18n"), so packages need no extra files.
    const QString dir = QStringLiteral(":/i18n");
    QString lang = code;
    if (lang.isEmpty() || lang == "system")
    {
        lang = QStringLiteral("en");
        for (const QString& ui : QLocale::system().uiLanguages())
        {
            const QString name = QLocale(ui).name(); // "fa_IR", "zh_CN", "es_ES", ...
            QString match;
            for (const UiLanguage& l : uiLanguages())
            {
                if (l.code.isEmpty())
                    continue;
                if (name == l.code || (!l.code.contains('_') && name.section('_', 0, 0) == l.code))
                    match = l.code;
            }
            // Traditional Chinese has no translation; it stays in English.
            if (!match.isEmpty() || name.startsWith("en"))
            {
                if (!match.isEmpty())
                    lang = match;
                break;
            }
        }
    }
    current() = lang;
    if (lang != "en")
    {
        auto* mirab = new QTranslator(qApp);
        if (mirab->load("mirab_" + lang, dir))
            QCoreApplication::installTranslator(mirab);
        else
            current() = QStringLiteral("en");
        // Qt's own dialogs and buttons (file dialogs, "OK", "Cancel", ...).
        auto* qt = new QTranslator(qApp);
        if (current() != "en" && qt->load("qtbase_" + lang, dir))
            QCoreApplication::installTranslator(qt);
    }
    QGuiApplication::setLayoutDirection(rightToLeft(current()) ? Qt::RightToLeft : Qt::LeftToRight);
    if (rightToLeft(current()))
    {
        qApp->installEventFilter(new DataTables(qApp));
        useArabicScriptFont();
    }
    return current();
}

QString currentLanguage()
{
    return current();
}

QString coreText(std::string_view text)
{
    if (text.empty())
        return {};
    const std::string s(text);
    return QCoreApplication::translate("core", s.c_str());
}

} // namespace mirab::app
