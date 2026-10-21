/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SchemaWidgets.cpp
 * Purpose: Helpers and widgets shared by the schema-driven editors: field editors, reference lists and engine support.
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
#include "SchemaWidgets.h"

#include "Icons.h"
#include "mirab/core/Features.h"
#include "mirab/core/Text.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QSet>

#include <algorithm>

namespace mirab::app
{

using core::ColumnType;

namespace
{
QStringList sectionKeys(const core::Model& model, const char* section)
{
    QStringList out;
    QSet<QString> seen;
    if (const auto* s = model.document().find(section))
    {
        for (const auto& t : s->allRowTokens())
        {
            if (t.empty())
                continue;
            const QString k = QString::fromStdString(t[0]);
            if (!seen.contains(k))
            {
                seen.insert(k);
                out << k;
            }
        }
    }
    return out;
}

bool isNumber(const QString& v)
{
    return text::toDouble(v.toStdString()).has_value();
}
} // namespace

QStringList referenceChoices(const core::Model& model, const core::ColumnDef& column)
{
    QStringList out;
    switch (column.type)
    {
    case ColumnType::ObjectRef:
        for (const auto& n : model.objectNames(column.object))
            out << QString::fromStdString(n);
        break;
    case ColumnType::SeriesRef:
        out = sectionKeys(model, "TIMESERIES");
        break;
    case ColumnType::PatternRef:
        out = sectionKeys(model, "PATTERNS");
        break;
    case ColumnType::CurveRef:
        out = sectionKeys(model, "CURVES");
        break;
    case ColumnType::GageRef:
        for (const auto& g : model.gages())
            out << QString::fromStdString(g.id);
        break;
    case ColumnType::AreaRef:
        for (const auto& a : model.areas())
            out << QString::fromStdString(a.id);
        break;
    default:
        break;
    }
    return out;
}

QStringList columnChoices(const core::Model& model, const core::ColumnDef& column)
{
    QStringList out;
    for (const auto& c : column.choices)
        out << QString::fromStdString(c);
    out << referenceChoices(model, column);
    return out;
}

bool engineSupportsSection(const engine::EngineDescriptor* engine, core::Dialect dialect,
                           const std::string& section, const std::vector<std::string>& tokens, QString* why)
{
    if (!engine)
        return true;
    core::Feature f{};
    if (!core::featureForSection(dialect, section, tokens, f) || engine->supportFor(f) == engine::Support::Full)
        return true;
    if (why)
        *why = QObject::tr("%1 does not model %2.")
                   .arg(QString::fromStdString(engine->name), QString::fromUtf8(core::featureName(f)));
    return false;
}

QString columnLabel(const core::ColumnDef& column)
{
    if (!column.help.empty() && column.help.size() <= 48)
    {
        QString h = QString::fromStdString(column.help);
        h[0] = h[0].toUpper();
        return h;
    }
    // "SweepInterval" -> "Sweep interval"
    const QString n = QString::fromStdString(column.name);
    QString out;
    for (int i = 0; i < n.size(); ++i)
    {
        const QChar c = n[i];
        if (i > 0 && c.isUpper() && n[i - 1].isLower())
            out += ' ' + c.toLower();
        else
            out += c;
    }
    out[0] = out[0].toUpper();
    return out;
}

QString objectKindLabel(const QString& kind, bool plural)
{
    struct Label
    {
        const char* kind;
        const char* one;
        const char* many;
    };
    static const Label labels[] = {
        {"Pollutant", QT_TRANSLATE_NOOP("mirab::app", "Pollutant"), QT_TRANSLATE_NOOP("mirab::app", "Pollutants")},
        {"Landuse", QT_TRANSLATE_NOOP("mirab::app", "Land use"), QT_TRANSLATE_NOOP("mirab::app", "Land uses")},
        {"Aquifer", QT_TRANSLATE_NOOP("mirab::app", "Aquifer"), QT_TRANSLATE_NOOP("mirab::app", "Aquifers")},
        {"Snowpack", QT_TRANSLATE_NOOP("mirab::app", "Snow pack"), QT_TRANSLATE_NOOP("mirab::app", "Snow packs")},
        {"Hydrograph", QT_TRANSLATE_NOOP("mirab::app", "Unit hydrograph"),
         QT_TRANSLATE_NOOP("mirab::app", "Unit hydrographs")},
        {"Transect", QT_TRANSLATE_NOOP("mirab::app", "Transect"), QT_TRANSLATE_NOOP("mirab::app", "Transects")},
        {"Street", QT_TRANSLATE_NOOP("mirab::app", "Street"), QT_TRANSLATE_NOOP("mirab::app", "Streets")},
        {"Inlet", QT_TRANSLATE_NOOP("mirab::app", "Inlet"), QT_TRANSLATE_NOOP("mirab::app", "Inlets")},
        {"LID", QT_TRANSLATE_NOOP("mirab::app", "LID control"), QT_TRANSLATE_NOOP("mirab::app", "LID controls")},
        {"Rule", QT_TRANSLATE_NOOP("mirab::app", "Control rule"), QT_TRANSLATE_NOOP("mirab::app", "Control rules")}};
    for (const auto& l : labels)
    {
        if (kind.compare(QLatin1String(l.kind), Qt::CaseInsensitive) == 0)
            return QCoreApplication::translate("mirab::app", plural ? l.many : l.one);
    }
    return kind;
}

QIcon objectKindIcon(const QString& kind)
{
    static const QHash<QString, QString> names = {
        {"Pollutant", "variability"}, {"Landuse", "layers"},  {"Aquifer", "basemap"},
        {"Snowpack", "settings"},     {"Hydrograph", "chart"}, {"Transect", "profile"},
        {"Street", "table"},          {"Inlet", "import"},     {"LID", "legend"},
        {"Rule", "controls"}};
    return icons::get(names.value(kind, "properties"));
}

// ---------------------------------------------------------------------------
// RowForm
// ---------------------------------------------------------------------------
RowForm::RowForm(const core::Model& model, const core::SectionDef& def, int firstColumn, QWidget* parent)
    : QWidget(parent), model_(model), def_(def), first_(firstColumn)
{
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(10);
    grid_->setVerticalSpacing(6);
    grid_->setColumnStretch(1, 1);
    grid_->setColumnMinimumWidth(0, 250);
}

void RowForm::setTokens(const std::vector<std::string>& tokens)
{
    tokens_ = tokens;
    build();
}

QString RowForm::valueAt(int column) const
{
    const int i = column - first_;
    if (i < 0 || i >= static_cast<int>(editors_.size()))
        return {};
    QWidget* w = editors_[static_cast<std::size_t>(i)];
    if (auto* box = qobject_cast<QComboBox*>(w))
        return box->currentText().trimmed();
    if (auto* edit = qobject_cast<QLineEdit*>(w))
        return edit->text().trimmed();
    return {};
}

void RowForm::build()
{
    building_ = true;
    while (QLayoutItem* item = grid_->takeAt(0))
    {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    editors_.clear();
    layout_ = def_.layout(tokens_);
    const auto units = model_.units();
    const int ncols = static_cast<int>(layout_.columns.size());
    for (int c = first_; c < ncols; ++c)
    {
        const core::ColumnDef& col = layout_.columns[static_cast<std::size_t>(c)];
        const bool tail = layout_.variadic && c + 1 == ncols;
        QString value;
        if (c < static_cast<int>(tokens_.size()))
        {
            if (tail)
            {
                std::vector<std::string> rest(tokens_.begin() + c, tokens_.end());
                value = QString::fromStdString(text::join(rest, " "));
            }
            else
            {
                value = QString::fromStdString(text::unquote(tokens_[static_cast<std::size_t>(c)]));
            }
        }
        if (value == "*" && col.type != ColumnType::Number)
            value.clear();
        const QString tip = QString::fromStdString(col.name) +
                            (col.help.empty() ? QString() : ": " + QString::fromStdString(col.help)) +
                            (col.optional ? tr(" (optional)") : QString());
        auto* label = new QLabel(columnLabel(col), this);
        label->setToolTip(tip);
        label->setWordWrap(true);
        QWidget* editor = nullptr;
        const QStringList choices = columnChoices(model_, col);
        if (!choices.isEmpty() || col.type == ColumnType::ObjectRef || col.type == ColumnType::SeriesRef ||
            col.type == ColumnType::PatternRef || col.type == ColumnType::CurveRef)
        {
            auto* box = new QComboBox(this);
            const bool pureChoice = col.type == ColumnType::Choice;
            box->setEditable(!pureChoice);
            if (col.optional)
                box->addItem(QString());
            box->addItems(choices);
            if (!value.isEmpty() && box->findText(value, Qt::MatchFixedString) < 0)
                box->addItem(value);
            const int idx = box->findText(value, Qt::MatchFixedString);
            box->setCurrentIndex(idx >= 0 ? idx : 0);
            if (!pureChoice)
                box->setCurrentText(value);
            connect(box, &QComboBox::currentTextChanged, this, [this, c] {
                if (building_)
                    return;
                if (c == def_.variantColumn)
                {
                    // Another type: keep the values that still fit, then rebuild.
                    auto t = tokens();
                    QMetaObject::invokeMethod(
                        this,
                        [this, t] {
                            tokens_ = t;
                            const auto l = def_.layout(tokens_);
                            for (std::size_t i = static_cast<std::size_t>(def_.variantColumn) + 1; i < tokens_.size(); ++i)
                            {
                                const auto* col = l.column(i);
                                if (!col || (col->type == ColumnType::Number && !text::toDouble(tokens_[i])) ||
                                    (col->type == ColumnType::Choice && !col->choices.empty() &&
                                     std::find(col->choices.begin(), col->choices.end(), tokens_[i]) == col->choices.end()))
                                {
                                    tokens_.resize(i);
                                    break;
                                }
                            }
                            build();
                            emit edited();
                        },
                        Qt::QueuedConnection);
                    return;
                }
                emit edited();
            });
            editor = box;
        }
        else
        {
            auto* edit = new QLineEdit(value, this);
            if (tail)
                edit->setPlaceholderText(tr("values separated by blanks"));
            connect(edit, &QLineEdit::textEdited, this, [this] {
                if (!building_)
                    emit edited();
            });
            editor = edit;
        }
        editor->setToolTip(tip);
        editor->setMinimumWidth(160);
        editor->setMaximumWidth(tail ? QWIDGETSIZE_MAX : 360);
        auto* unit = new QLabel(col.quantity.empty() ? QString() : QString::fromStdString(units.label(col.quantity)), this);
        unit->setObjectName("panelSubtitle");
        unit->setMinimumWidth(56);
        const int r = c - first_;
        grid_->addWidget(label, r, 0);
        grid_->addWidget(editor, r, 1);
        grid_->addWidget(unit, r, 2);
        editors_.push_back(editor);
    }
    building_ = false;
}

std::vector<std::string> RowForm::tokens() const
{
    std::vector<std::string> out(tokens_.begin(), tokens_.begin() + std::min<std::ptrdiff_t>(first_, static_cast<std::ptrdiff_t>(tokens_.size())));
    while (static_cast<int>(out.size()) < first_)
        out.push_back("*");
    const int ncols = static_cast<int>(layout_.columns.size());
    std::vector<std::string> values;
    for (int c = first_; c < ncols; ++c)
    {
        const std::string v = valueAt(c).toStdString();
        const bool tail = layout_.variadic && c + 1 == ncols;
        if (tail)
        {
            for (auto& t : text::splitInpLine(v).tokens)
                values.push_back(t);
            break;
        }
        values.push_back(v.empty() ? std::string() : text::quoteIfNeeded(text::unquote(v)));
    }
    while (!values.empty() && values.back().empty())
        values.pop_back();
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (values[i].empty())
        {
            const auto* col = layout_.column(static_cast<std::size_t>(first_) + i);
            values[i] = col ? col->blank : "*";
        }
        out.push_back(values[i]);
    }
    return out;
}

QString RowForm::check() const
{
    const int ncols = static_cast<int>(layout_.columns.size());
    for (int c = first_; c < ncols; ++c)
    {
        const core::ColumnDef& col = layout_.columns[static_cast<std::size_t>(c)];
        const QString v = valueAt(c);
        if (v.isEmpty())
        {
            if (!col.optional)
                return tr("%1 needs a value.").arg(columnLabel(col));
            continue;
        }
        if (col.type == ColumnType::Number && v != "*" && !isNumber(v))
            return tr("\"%1\" is not a number (%2).").arg(v, columnLabel(col));
        if (col.type == ColumnType::Choice && !col.choices.empty())
        {
            bool known = false;
            for (const auto& ch : col.choices)
                known = known || text::iequals(ch, v.toStdString());
            if (!known)
                return tr("\"%1\" is not a valid %2.").arg(v, columnLabel(col).toLower());
        }
    }
    return {};
}

} // namespace mirab::app
