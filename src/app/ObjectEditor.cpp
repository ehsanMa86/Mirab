/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ObjectEditor.cpp
 * Purpose: Editor tab for a named model object (pollutant, land use, aquifer, ...), built from the section schema.
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
#include "ObjectEditor.h"

#include "Icons.h"
#include "Language.h"
#include "SchemaWidgets.h"
#include "mirab/core/Text.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace mirab::app
{

using core::ColumnType;

namespace
{
/// Combo box editors for table columns that have choices or references.
class ColumnDelegate : public DataItemDelegate
{
public:
    ColumnDelegate(std::vector<QStringList> choices, std::vector<bool> editable, QObject* parent)
        : DataItemDelegate(parent), choices_(std::move(choices)), editable_(std::move(editable))
    {
    }
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const auto c = static_cast<std::size_t>(idx.column());
        if (c >= choices_.size() || choices_[c].isEmpty())
            return DataItemDelegate::createEditor(parent, opt, idx);
        auto* box = new QComboBox(parent);
        box->setEditable(editable_[c]);
        box->addItems(choices_[c]);
        return box;
    }
    void setEditorData(QWidget* editor, const QModelIndex& idx) const override
    {
        if (auto* box = qobject_cast<QComboBox*>(editor))
        {
            const QString v = idx.data(Qt::EditRole).toString();
            const int i = box->findText(v, Qt::MatchFixedString);
            if (i >= 0)
                box->setCurrentIndex(i);
            else if (box->isEditable())
                box->setCurrentText(v);
        }
        else
        {
            QStyledItemDelegate::setEditorData(editor, idx);
        }
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& idx) const override
    {
        if (auto* box = qobject_cast<QComboBox*>(editor))
            model->setData(idx, box->currentText(), Qt::EditRole);
        else
            QStyledItemDelegate::setModelData(editor, model, idx);
    }

private:
    std::vector<QStringList> choices_;
    std::vector<bool> editable_;
};

/// Makes a table as tall as its rows (at least two), so that the page scrolls instead.
void fitTable(QTableWidget* t)
{
    const int rows = std::max(2, t->rowCount());
    t->setFixedHeight(t->horizontalHeader()->sizeHint().height() + rows * t->verticalHeader()->defaultSectionSize() +
                      2 * t->frameWidth() + 2);
}

QString variantTitle(const std::string& section, const std::string& key)
{
    static const QHash<QString, const char*> titles = {
        {"PLOWABLE", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Plowable impervious area")},
        {"IMPERVIOUS", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Impervious area")},
        {"PERVIOUS", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Pervious area")},
        {"REMOVAL", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Snow removal")},
        {"GRATE", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Grate inlet")},
        {"CURB", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Curb opening inlet")},
        {"SLOTTED", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Slotted drain inlet")},
        {"DROP_GRATE", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Drop grate inlet")},
        {"DROP_CURB", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Drop curb inlet")},
        {"CUSTOM", QT_TRANSLATE_NOOP("mirab::app::ObjectEditor", "Custom inlet")}};
    const QString k = QString::fromStdString(text::toUpper(key));
    if (titles.contains(k))
        return QCoreApplication::translate("mirab::app::ObjectEditor", titles.value(k));
    return QString::fromStdString(section) + " " + k;
}

/// Default rows of a new object, from a scratch project with the same units.
std::vector<std::vector<std::string>> defaultRows(const core::Model& model, const QString& kind)
{
    auto scratch = core::Model::newProject(model.dialect(), model.option("FLOW_UNITS"));
    scratch.addGage("G", {0, 0});
    scratch.addObject(kind.toStdString(), "X");
    std::vector<std::vector<std::string>> out;
    for (const auto& r : scratch.objectRows(kind.toStdString(), "X"))
        out.push_back(r.tokens);
    return out;
}
} // namespace

ObjectEditor::ObjectEditor(ProjectDocument* doc, const QString& kind, const QString& name, QWidget* parent)
    : QWidget(parent), doc_(doc), kind_(kind), name_(name)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* head = new QWidget(this);
    head->setObjectName("panelHeader");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(14, 10, 14, 10);
    auto* icon = new QLabel(head);
    icon->setPixmap(objectKindIcon(kind).pixmap(28, 28));
    hl->addWidget(icon);
    auto* titles = new QVBoxLayout;
    title_ = new QLabel(tr("%1 %2").arg(objectKindLabel(kind), name), head);
    title_->setObjectName("panelTitle");
    state_ = new QLabel(head);
    state_->setObjectName("panelSubtitle");
    titles->addWidget(title_);
    titles->addWidget(state_);
    hl->addLayout(titles, 1);
    revert_ = new QPushButton(icons::get("undo"), tr("Revert"), head);
    apply_ = new QPushButton(tr("Apply"), head);
    apply_->setProperty("primary", true);
    hl->addWidget(revert_);
    hl->addWidget(apply_);
    layout->addWidget(head);

    banner_ = new QLabel(this);
    banner_->setWordWrap(true);
    banner_->setContentsMargins(14, 6, 14, 6);
    banner_->setStyleSheet("QLabel { background: #fff4d6; color: #6b5200; }");
    banner_->hide();
    layout->addWidget(banner_);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(14, 12, 14, 12);
    content_ = new QWidget(page);
    content_->setMaximumWidth(860);
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(12);
    pageLayout->addWidget(content_, 1);
    pageLayout->addStretch(0);
    scroll->setWidget(page);
    layout->addWidget(scroll, 1);

    connect(apply_, &QPushButton::clicked, this, &ObjectEditor::apply);
    connect(revert_, &QPushButton::clicked, this, &ObjectEditor::load);
    connect(doc_, &ProjectDocument::modelChanged, this, [this] {
        if (!apply_->isEnabled())
            load();
    });
    connect(doc_, &ProjectDocument::modelReset, this, &ObjectEditor::load);
    load();
}

void ObjectEditor::setName(const QString& name)
{
    name_ = name;
    title_->setText(tr("%1 %2").arg(objectKindLabel(kind_), name_));
    load();
}

void ObjectEditor::setEngine(const engine::EngineDescriptor* engine)
{
    engine_ = engine;
    updateSupport();
}

void ObjectEditor::updateSupport()
{
    const core::SectionDef* def = core::sectionForObject(doc_->model().dialect(), kind_.toStdString());
    QString why;
    const bool ok = !def || engineSupportsSection(engine_, doc_->model().dialect(), def->name, {}, &why);
    banner_->setText(why + " " + tr("The data stays in the input file but is not simulated."));
    banner_->setVisible(!ok);
}

void ObjectEditor::setDirty(bool on)
{
    apply_->setEnabled(on);
    revert_->setEnabled(on);
    state_->setText(on ? tr("Edited; Apply writes the changes to the project (undo with Ctrl+Z).")
                       : tr("Hover over a field to see what it means."));
}

void ObjectEditor::addForm(Group& g, const std::vector<std::string>& tokens)
{
    auto* frame = new QFrame(g.box);
    frame->setFrameShape(QFrame::StyledPanel);
    auto* fl = new QVBoxLayout(frame);
    fl->setContentsMargins(10, 8, 10, 10);
    auto* top = new QHBoxLayout;
    auto* title = new QLabel(frame);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    auto* remove = new QPushButton(icons::get("delete"), tr("Remove"), frame);
    remove->setFlat(true);
    top->addWidget(title, 1);
    top->addWidget(remove);
    fl->addLayout(top);
    auto* form = new RowForm(doc_->model(), *g.def, g.def->variantColumn, frame);
    form->setTokens(tokens);
    fl->addWidget(form);
    const int vc = g.def->variantColumn;
    auto retitle = [title, form, vc, section = g.section] {
        const auto t = form->tokens();
        title->setText(static_cast<int>(t.size()) > vc ? variantTitle(section, t[static_cast<std::size_t>(vc)]) : QString());
    };
    retitle();
    connect(form, &RowForm::edited, this, [this, retitle] {
        retitle();
        if (!loading_)
            setDirty(true);
    });
    const std::string section = g.section;
    connect(remove, &QPushButton::clicked, this, [this, frame, form, section] {
        for (auto& gr : groups_)
        {
            if (gr.section != section)
                continue;
            gr.forms.erase(std::remove(gr.forms.begin(), gr.forms.end(), form), gr.forms.end());
        }
        frame->deleteLater();
        setDirty(true);
    });
    g.formsLayout->addWidget(frame);
    g.forms.push_back(form);
}

void ObjectEditor::addTableRow(Group& g, const std::vector<std::string>& tokens)
{
    const bool wasLoading = loading_;
    loading_ = true;
    const int r = g.table->rowCount();
    g.table->insertRow(r);
    for (int c = 0; c < g.table->columnCount(); ++c)
    {
        const std::size_t t = static_cast<std::size_t>(c) + 1;
        QString v = t < tokens.size() ? QString::fromStdString(text::unquote(tokens[t])) : QString();
        if (v == "*")
            v.clear();
        g.table->setItem(r, c, new QTableWidgetItem(v));
    }
    loading_ = wasLoading;
}

void ObjectEditor::load()
{
    loading_ = true;
    groups_.clear();
    while (QLayoutItem* item = contentLayout_->takeAt(0))
    {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    const auto& model = doc_->model();
    const std::string kind = kind_.toStdString();
    const std::string name = name_.toStdString();
    const core::SectionDef* def = core::sectionForObject(model.dialect(), kind);
    if (!def || !model.objectExists(kind, name))
    {
        auto* gone = new QLabel(tr("%1 %2 no longer exists in the project.").arg(objectKindLabel(kind_), name_), content_);
        contentLayout_->addWidget(gone);
        contentLayout_->addStretch(1);
        loading_ = false;
        setDirty(false);
        apply_->setEnabled(false);
        return;
    }
    std::vector<const core::SectionDef*> sections = {def};
    for (const auto& d : core::sectionDefs(model.dialect()))
    {
        if (&d != def && d.keyCategory == core::Category::None && !d.columns.empty() &&
            d.columns.front().type == ColumnType::ObjectRef && text::iequals(d.columns.front().object, kind))
            sections.push_back(&d);
    }
    const auto rows = model.objectRows(kind, name);
    const auto units = model.units();
    for (const core::SectionDef* d : sections)
    {
        Group g;
        g.section = d->name;
        g.def = d;
        std::vector<std::vector<std::string>> mine;
        for (const auto& r : rows)
        {
            if (r.section == d->name)
                mine.push_back(r.tokens);
        }
        const bool defining = d == def;
        g.mode = d->columns.empty() ? Mode::Raw
                 : !defining        ? Mode::Table
                 : !d->variants.empty() ? Mode::Forms
                                        : Mode::Form;
        auto* box = new QGroupBox(defining ? tr("Properties") : coreText(d->description), content_);
        g.box = box;
        auto* bl = new QVBoxLayout(box);
        bl->setSpacing(8);
        QString why;
        if (!engineSupportsSection(engine_, model.dialect(), d->name, {}, &why))
            box->setToolTip(why);
        switch (g.mode)
        {
        case Mode::Form:
        {
            auto* form = new RowForm(model, *d, 1, box);
            form->setTokens(mine.empty() ? std::vector<std::string>{name} : mine.front());
            connect(form, &RowForm::edited, this, [this] {
                if (!loading_)
                    setDirty(true);
            });
            bl->addWidget(form);
            g.forms.push_back(form);
            break;
        }
        case Mode::Forms:
        {
            g.formsLayout = new QVBoxLayout;
            g.formsLayout->setSpacing(10);
            bl->addLayout(g.formsLayout);
            for (const auto& t : mine)
                addForm(g, t);
            auto* add = new QPushButton(icons::get("new"), d->name == "SNOWPACKS" ? tr("Add surface") : tr("Add inlet part"), box);
            add->setToolTip(d->name == "INLETS" ? tr("A combination inlet has a grate row and a curb opening row.")
                                                : QString());
            const std::string section = d->name;
            connect(add, &QPushButton::clicked, this, [this, section, kind = kind_] {
                for (auto& gr : groups_)
                {
                    if (gr.section != section)
                        continue;
                    auto defaults = defaultRows(doc_->model(), kind);
                    std::vector<std::string> present;
                    for (auto* f : gr.forms)
                    {
                        const auto t = f->tokens();
                        if (t.size() > 1)
                            present.push_back(text::toUpper(t[1]));
                    }
                    std::vector<std::string> row;
                    for (auto& t : defaults)
                    {
                        if (t.size() > 1 && std::find(present.begin(), present.end(), text::toUpper(t[1])) == present.end())
                        {
                            row = t;
                            break;
                        }
                    }
                    if (row.empty() && !defaults.empty())
                    {
                        row = defaults.front();
                        if (section == "INLETS" && row.size() > 3)
                            row = {row[0], "CURB", row[2], row[3]};
                    }
                    if (row.empty())
                        return;
                    row[0] = name_.toStdString();
                    addForm(gr, row);
                    setDirty(true);
                }
            });
            bl->addWidget(add, 0, Qt::AlignLeft);
            break;
        }
        case Mode::Table:
        {
            const int ncols = static_cast<int>(d->columns.size()) - 1;
            g.table = new QTableWidget(0, ncols, box);
            QStringList headers;
            std::vector<QStringList> choices;
            std::vector<bool> editable;
            for (int c = 1; c <= ncols; ++c)
            {
                const auto& col = d->columns[static_cast<std::size_t>(c)];
                QString h = QString::fromStdString(col.name);
                if (!col.quantity.empty() && !units.label(col.quantity).empty())
                    h += " (" + QString::fromStdString(units.label(col.quantity)) + ")";
                headers << h;
                choices.push_back(columnChoices(model, col));
                editable.push_back(col.type != ColumnType::Choice);
            }
            g.table->setHorizontalHeaderLabels(headers);
            for (int c = 0; c < ncols; ++c)
            {
                const auto& col = d->columns[static_cast<std::size_t>(c) + 1];
                g.table->horizontalHeaderItem(c)->setToolTip(coreText(col.help));
            }
            g.table->setItemDelegate(new ColumnDelegate(choices, editable, g.table));
            g.table->verticalHeader()->hide();
            g.table->verticalHeader()->setDefaultSectionSize(26);
            g.table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            g.table->horizontalHeader()->setStretchLastSection(true);
            g.table->setAlternatingRowColors(true);
            g.table->setSelectionBehavior(QAbstractItemView::SelectRows);
            g.table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            g.table->setEditTriggers(QAbstractItemView::AllEditTriggers);
            bl->addWidget(g.table);
            for (const auto& t : mine)
                addTableRow(g, t);
            fitTable(g.table);
            connect(g.table, &QTableWidget::itemChanged, this, [this] {
                if (!loading_)
                    setDirty(true);
            });
            auto* buttons = new QHBoxLayout;
            auto* add = new QPushButton(icons::get("new"), tr("Add row"), box);
            auto* del = new QPushButton(icons::get("delete"), tr("Remove row"), box);
            buttons->addWidget(add);
            buttons->addWidget(del);
            buttons->addStretch(1);
            bl->addLayout(buttons);
            QTableWidget* table = g.table;
            const core::SectionDef* sd = d;
            connect(add, &QPushButton::clicked, this, [this, table, sd] {
                std::string missing;
                auto row = doc_->model().newDataRow(*sd, name_.toStdString(), &missing);
                if (row.empty())
                {
                    emit statusMessage(tr("Add a %1 to the project first.").arg(objectKindLabel(QString::fromStdString(missing)).toLower()));
                    return;
                }
                // Prefer a pollutant that has no row yet.
                if (row.size() > 1 && sd->columns.size() > 1 && sd->columns[1].type == ColumnType::ObjectRef)
                {
                    QStringList used;
                    for (int r = 0; r < table->rowCount(); ++r)
                        used << (table->item(r, 0) ? table->item(r, 0)->text() : QString());
                    for (const auto& n : doc_->model().objectNames(sd->columns[1].object))
                    {
                        if (!used.contains(QString::fromStdString(n)))
                        {
                            row[1] = n;
                            break;
                        }
                    }
                }
                for (auto& gr : groups_)
                {
                    if (gr.table == table)
                        addTableRow(gr, row);
                }
                fitTable(table);
                setDirty(true);
            });
            connect(del, &QPushButton::clicked, this, [this, table] {
                if (table->currentRow() < 0)
                    return;
                table->removeRow(table->currentRow());
                fitTable(table);
                setDirty(true);
            });
            break;
        }
        case Mode::Raw:
        {
            auto* note = new QLabel(tr("The rows of this object as they appear in the input file."), box);
            note->setObjectName("panelSubtitle");
            bl->addWidget(note);
            g.raw = new QPlainTextEdit(box);
            g.raw->setLayoutDirection(Qt::LeftToRight); // input file text
            g.raw->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            g.raw->setLineWrapMode(QPlainTextEdit::NoWrap);
            g.raw->setMinimumHeight(260);
            QStringList lines;
            if (const auto* s = model.document().find(d->name))
            {
                const auto data = s->dataLineIndices();
                for (int r : model.rowsOfKey(d->name, name))
                    lines << QString::fromStdString(s->lines[static_cast<std::size_t>(data[static_cast<std::size_t>(r)])]);
            }
            g.raw->setPlainText(lines.join('\n'));
            connect(g.raw, &QPlainTextEdit::textChanged, this, [this] {
                if (!loading_)
                    setDirty(true);
            });
            bl->addWidget(g.raw, 1);
            break;
        }
        }
        contentLayout_->addWidget(box);
        groups_.push_back(g);
    }
    contentLayout_->addStretch(1);
    loading_ = false;
    setDirty(false);
    updateSupport();
}

bool ObjectEditor::collect(const Group& g, std::vector<std::vector<std::string>>& rows, QString& error) const
{
    const std::string name = name_.toStdString();
    switch (g.mode)
    {
    case Mode::Form:
    case Mode::Forms:
        for (auto* f : g.forms)
        {
            error = f->check();
            if (!error.isEmpty())
                return false;
            auto t = f->tokens();
            t[0] = name;
            rows.push_back(t);
        }
        return true;
    case Mode::Table:
        for (int r = 0; r < g.table->rowCount(); ++r)
        {
            std::vector<std::string> t = {name};
            for (int c = 0; c < g.table->columnCount(); ++c)
            {
                const auto& col = g.def->columns[static_cast<std::size_t>(c) + 1];
                const auto* item = g.table->item(r, c);
                const std::string v = item ? item->text().trimmed().toStdString() : std::string();
                if (v.empty() && !col.optional)
                {
                    error = tr("Row %1: %2 needs a value.").arg(r + 1).arg(QString::fromStdString(col.name));
                    return false;
                }
                if (!v.empty() && col.type == ColumnType::Number && !text::toDouble(v))
                {
                    error = tr("Row %1: \"%2\" is not a number.").arg(r + 1).arg(QString::fromStdString(v));
                    return false;
                }
                t.push_back(v.empty() ? col.blank : text::quoteIfNeeded(v));
            }
            while (t.size() > 1 && t.back() == g.def->columns[t.size() - 1].blank &&
                   g.def->columns[t.size() - 1].optional)
                t.pop_back();
            rows.push_back(t);
        }
        return true;
    case Mode::Raw:
        for (const QString& line : g.raw->toPlainText().split('\n'))
        {
            auto t = text::splitInpLine(line.toStdString()).tokens;
            if (!t.empty())
                rows.push_back(t);
        }
        return true;
    }
    return true;
}

void ObjectEditor::apply()
{
    std::vector<std::pair<std::string, std::vector<std::vector<std::string>>>> edits;
    for (const auto& g : groups_)
    {
        std::vector<std::vector<std::string>> rows;
        QString error;
        if (!collect(g, rows, error))
        {
            state_->setText("<span style='color:#c84b4b'>" + error.toHtmlEscaped() + "</span>");
            return;
        }
        edits.emplace_back(g.section, std::move(rows));
    }
    const std::string name = name_.toStdString();
    QString err;
    const bool ok = doc_->edit(tr("Edit %1 %2").arg(objectKindLabel(kind_).toLower(), name_),
                               [&](core::Model& m, std::string*) {
                                   for (const auto& [section, rows] : edits)
                                       m.replaceRows(section, m.rowsOfKey(section, name), rows);
                                   return true;
                               },
                               &err);
    if (!ok)
    {
        state_->setText(err);
        return;
    }
    setDirty(false);
    load();
    emit statusMessage(tr("%1 %2 updated.").arg(objectKindLabel(kind_), name_));
}

} // namespace mirab::app
