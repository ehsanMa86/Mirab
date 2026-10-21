/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/PropertyPanel.cpp
 * Purpose: Schema-driven property editor with engine-aware greying of unsupported data.
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
#include "PropertyPanel.h"

#include "Icons.h"
#include "SchemaWidgets.h"
#include "Theme.h"
#include "mirab/core/Features.h"
#include "mirab/core/Text.h"

#include <QComboBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>
#include <map>
#include <set>

namespace mirab::app
{

using core::Category;

namespace
{
enum Role
{
    SectionRole = Qt::UserRole + 1,
    RowRole,
    ColumnRole,
    TailRole,
    ChoicesRole,
    OptionRole
};

/// RowRole of a value shared by the selected elements (edited on all of them).
constexpr int kAllSelected = -2;

QString kindOfElement(const core::Model& m, const Selection& s)
{
    if (s.category == Category::Node)
        if (const auto* n = m.findNode(s.id.toStdString()))
            return QString::fromStdString(n->kind);
    if (s.category == Category::Link)
        if (const auto* l = m.findLink(s.id.toStdString()))
            return QString::fromStdString(l->kind);
    if (s.category == Category::Area)
        return "Subcatchment";
    if (s.category == Category::Gage)
        return "RainGage";
    return {};
}

QString kindLabel(const QString& kind, int count)
{
    if (kind == "RainGage")
        return count == 1 ? QObject::tr("rain gage") : QObject::tr("rain gages");
    if (kind == "Storage")
        return count == 1 ? QObject::tr("storage unit") : QObject::tr("storage units");
    const QString lower = kind.toLower();
    return count == 1 ? lower : lower + "s";
}

class ChoiceDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const QStringList choices = idx.data(ChoicesRole).toStringList();
        if (choices.isEmpty())
            return QStyledItemDelegate::createEditor(parent, opt, idx);
        auto* box = new QComboBox(parent);
        box->setEditable(true);
        box->addItems(choices);
        return box;
    }
    void setEditorData(QWidget* editor, const QModelIndex& idx) const override
    {
        if (auto* box = qobject_cast<QComboBox*>(editor))
            box->setCurrentText(idx.data(Qt::EditRole).toString());
        else
            QStyledItemDelegate::setEditorData(editor, idx);
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& idx) const override
    {
        if (auto* box = qobject_cast<QComboBox*>(editor))
            model->setData(idx, box->currentText(), Qt::EditRole);
        else
            QStyledItemDelegate::setModelData(editor, model, idx);
    }
};

const char* kMultiWordKeys[] = {"DEMAND MODEL",       "DEMAND MULTIPLIER", "SPECIFIC GRAVITY",  "EMITTER EXPONENT",
                                "EMITTER BACKFLOW",   "MINIMUM PRESSURE",  "REQUIRED PRESSURE", "PRESSURE EXPONENT",
                                "HYDRAULIC TIMESTEP", "QUALITY TIMESTEP",  "PATTERN TIMESTEP",  "PATTERN START",
                                "REPORT TIMESTEP",    "REPORT START",      "START CLOCKTIME",   "RULE TIMESTEP",
                                "STATUS REPORT",      "DEMAND CHARGE",     "GLOBAL EFFICIENCY", "GLOBAL PRICE",
                                "GLOBAL PATTERN"};

std::size_t keyLength(const std::vector<std::string>& tokens)
{
    if (tokens.size() >= 3)
    {
        const std::string two = text::toUpper(tokens[0] + " " + tokens[1]);
        for (const char* k : kMultiWordKeys)
        {
            if (two == k)
                return 2;
        }
    }
    return 1;
}
} // namespace

PropertyPanel::PropertyPanel(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* head = new QWidget(this);
    head->setObjectName("panelHeader");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* hl = new QVBoxLayout(head);
    hl->setContentsMargins(10, 8, 10, 8);
    hl->setSpacing(6);
    auto* top = new QHBoxLayout;
    top->setSpacing(8);
    icon_ = new QLabel(head);
    icon_->setFixedSize(28, 28);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    title_ = new QLabel(head);
    title_->setObjectName("panelTitle");
    title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    subtitle_ = new QLabel(head);
    subtitle_->setObjectName("panelSubtitle");
    subtitle_->setWordWrap(false);
    titles->addWidget(title_);
    titles->addWidget(subtitle_);
    addButton_ = new QToolButton(head);
    addButton_->setText(tr("Add data"));
    addButton_->setIcon(icons::get("new"));
    addButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton_->setToolTip(tr("Attach more data to the selected element (demands, emitters, leakage, losses, ...)"));
    addButton_->setPopupMode(QToolButton::InstantPopup);
    addButton_->setMenu(new QMenu(addButton_));
    top->addWidget(icon_);
    top->addLayout(titles, 1);
    top->addWidget(addButton_, 0, Qt::AlignTop);
    hl->addLayout(top);
    filter_ = new QLineEdit(head);
    filter_->setPlaceholderText(tr("Filter properties"));
    filter_->setClearButtonEnabled(true);
    filter_->addAction(icons::get("find"), QLineEdit::LeadingPosition);
    hl->addWidget(filter_);
    layout->addWidget(head);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Property"), tr("Value"), tr("Units")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->setColumnWidth(0, 170);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setWordWrap(false);
    table_->setItemDelegate(new ChoiceDelegate(table_));
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, &PropertyPanel::showContextMenu);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked |
                            QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    // One click on a value starts editing it.
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        QTableWidgetItem* item = table_->item(row, column);
        if (column == 1 && item && (item->flags() & Qt::ItemIsEditable))
            table_->editItem(item);
    });
    layout->addWidget(table_, 1);
    connect(table_, &QTableWidget::itemChanged, this, &PropertyPanel::commit);
    connect(filter_, &QLineEdit::textChanged, this, &PropertyPanel::applyFilter);
    connect(doc_, &ProjectDocument::selectionChanged, this, &PropertyPanel::rebuild);
    connect(doc_, &ProjectDocument::modelChanged, this, &PropertyPanel::rebuild);
    connect(doc_, &ProjectDocument::modelReset, this, &PropertyPanel::rebuild);
    connect(doc_, &ProjectDocument::periodChanged, this, &PropertyPanel::rebuild);
    rebuild();
}

void PropertyPanel::applyFilter()
{
    const QString text = filter_->text().trimmed();
    int header = -1;
    bool headerHasMatch = false;
    auto closeHeader = [&] {
        if (header >= 0)
            table_->setRowHidden(header, !text.isEmpty() && !headerHasMatch);
    };
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        const bool isHeader = table_->columnSpan(r, 0) > 1;
        if (isHeader)
        {
            closeHeader();
            header = r;
            headerHasMatch = false;
            continue;
        }
        const bool match = text.isEmpty() || table_->item(r, 0)->text().contains(text, Qt::CaseInsensitive);
        table_->setRowHidden(r, !match);
        headerHasMatch = headerHasMatch || match;
    }
    closeHeader();
}

void PropertyPanel::setEngine(const engine::EngineDescriptor* engine)
{
    engine_ = engine;
    rebuild();
}

bool PropertyPanel::sectionSupported(const std::string& section, const std::vector<std::string>& tokens,
                                     QString* why) const
{
    return engineSupportsSection(engine_, doc_->model().dialect(), section, tokens, why);
}

void PropertyPanel::showContextMenu(const QPoint& pos)
{
    const int r = table_->rowAt(pos.y());
    if (r < 0)
        return;
    // The header row above the clicked row names the data row.
    int h = r;
    while (h >= 0 && table_->columnSpan(h, 0) <= 1)
        --h;
    if (h < 0)
        return;
    const QTableWidgetItem* header = table_->item(h, 0);
    const QString section = header->data(SectionRole).toString();
    const int row = header->data(RowRole).toInt();
    if (section.isEmpty() || row < 0)
        return;
    const auto* def = core::findSectionDef(doc_->model().dialect(), section.toStdString());
    if (!def || !def->elementKind.empty())
        return;
    QMenu menu(this);
    menu.addAction(icons::get("delete"), tr("Remove this %1 row").arg(header->text().section("  #", 0, 0).toLower()), this,
                   [this, section, row] {
                       doc_->edit(tr("Remove [%1] row").arg(section), [&](core::Model& m, std::string*) {
                           auto* s = m.document().find(section.toStdString());
                           if (!s || row >= s->rowCount())
                               return false;
                           s->removeRow(row);
                           m.rebuild();
                           return true;
                       });
                   });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

void PropertyPanel::rebuild()
{
    populating_ = true;
    table_->setRowCount(0);
    const Selection& sel = doc_->selection();
    if (sel.isEmpty())
        showOptions();
    else if (doc_->selectionCount() > 1)
        showMultiple();
    else
        showElement(sel);
    populateAddMenu();
    populating_ = false;
    applyFilter();
    // Units are short; a long result legend (such as a status code) must not squeeze the values.
    table_->resizeColumnToContents(2);
    table_->setColumnWidth(2, std::min(table_->columnWidth(2), 72));
}

void PropertyPanel::addHeader(const QString& text, bool enabled, const QString& tip)
{
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto* item = new QTableWidgetItem(text);
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
    item->setFlags(Qt::ItemIsEnabled);
    if (!enabled)
        item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
    item->setToolTip(tip);
    item->setBackground(QColor(236, 241, 248));
    item->setForeground(enabled ? theme::kText : QColor(160, 170, 182));
    table_->setItem(r, 0, item);
    table_->setSpan(r, 0, 1, 3);
}

void PropertyPanel::addRow(const QString& name, const QString& value, const QString& unit, const QString& tip,
                           const QString& section, int row, int column, bool tail, const QStringList& choices,
                           bool enabled, bool editable)
{
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto* n = new QTableWidgetItem(name);
    n->setFlags(Qt::ItemIsEnabled);
    n->setToolTip(tip);
    auto* v = new QTableWidgetItem(value);
    v->setData(SectionRole, section);
    v->setData(RowRole, row);
    v->setData(ColumnRole, column);
    v->setData(TailRole, tail);
    v->setData(ChoicesRole, choices);
    v->setToolTip(tip);
    // Values the active scenario changes stand out, with the base value in the tooltip.
    QString baseValue;
    if (!section.isEmpty() && doc_->baseValue(section.toStdString(), row, column, tail, &baseValue))
    {
        const QColor changed(255, 236, 196);
        n->setBackground(changed);
        v->setBackground(changed);
        v->setToolTip(tr("Changed in scenario %1. Base model: %2").arg(doc_->activeScenario(), baseValue) +
                      (tip.isEmpty() ? QString() : "\n\n" + tip));
    }
    Qt::ItemFlags flags = Qt::ItemIsSelectable;
    if (enabled)
        flags |= Qt::ItemIsEnabled;
    if (enabled && editable)
        flags |= Qt::ItemIsEditable;
    v->setFlags(flags);
    auto* u = new QTableWidgetItem(unit);
    u->setFlags(Qt::ItemIsEnabled);
    u->setToolTip(unit);
    if (!enabled)
    {
        const QColor grey = palette().color(QPalette::Disabled, QPalette::Text);
        n->setForeground(grey);
        v->setForeground(grey);
        u->setForeground(grey);
    }
    table_->setItem(r, 0, n);
    table_->setItem(r, 1, v);
    table_->setItem(r, 2, u);
}

void PropertyPanel::showElement(const Selection& sel)
{
    const auto& model = doc_->model();
    const std::string id = sel.id.toStdString();
    QString kind;
    if (sel.category == Category::Node)
        kind = QString::fromStdString(model.findNode(id) ? model.findNode(id)->kind : "Node");
    else if (sel.category == Category::Link)
        kind = QString::fromStdString(model.findLink(id) ? model.findLink(id)->kind : "Link");
    else if (sel.category == Category::Area)
        kind = tr("Subcatchment");
    else if (sel.category == Category::Gage)
        kind = tr("Rain gage");
    title_->setText(sel.id);
    subtitle_->setText(kind);
    subtitle_->setToolTip({});
    icon_->setPixmap(icons::element(sel.category == Category::Gage ? "RainGage" : sel.category == Category::Area ? "Subcatchment" : kind).pixmap(28, 28));
    const auto units = model.units();

    std::map<std::string, int> occurrence;
    for (const auto& er : model.rowsFor(sel.category, id))
    {
        QString why;
        const bool supported = sectionSupported(er.section, er.tokens, &why);
        const int n = ++occurrence[er.section];
        QString header = er.def && !er.def->description.empty() ? QString::fromStdString(er.def->description)
                                                                 : "[" + QString::fromStdString(er.section) + "]";
        if (er.def && er.def->multiRowPerKey)
            header += "  #" + QString::number(n);
        addHeader(header, supported, why.isEmpty() ? "[" + QString::fromStdString(er.section) + "]" : why);
        QTableWidgetItem* headerItem = table_->item(table_->rowCount() - 1, 0);
        headerItem->setData(SectionRole, QString::fromStdString(er.section));
        headerItem->setData(RowRole, er.row);
        const core::RowLayout layout = er.def ? er.def->layout(er.tokens) : core::RowLayout{};
        const auto& cols = layout.columns;
        std::size_t ncols = std::max(cols.size(), er.tokens.size());
        if (layout.repeatFrom >= 0 && ncols > cols.size())
        {
            // Show whole groups of repeated columns (land use and percent, ...).
            const std::size_t period = cols.size() - static_cast<std::size_t>(layout.repeatFrom);
            ncols = cols.size() + (ncols - cols.size() + period - 1) / period * period;
        }
        for (std::size_t c = 0; c < ncols; ++c)
        {
            const core::ColumnDef* colDef = layout.repeatFrom >= 0 || c < cols.size() ? layout.column(c) : nullptr;
            const bool known = colDef != nullptr;
            const bool tail = known && layout.variadic && c + 1 == cols.size();
            QString value;
            if (c < er.tokens.size())
            {
                if (tail)
                {
                    std::vector<std::string> rest(er.tokens.begin() + static_cast<std::ptrdiff_t>(c), er.tokens.end());
                    value = QString::fromStdString(text::join(rest, " "));
                }
                else
                {
                    value = QString::fromStdString(text::unquote(er.tokens[c]));
                }
            }
            if (value == "\"\"")
                value.clear();
            const QString name = known ? QString::fromStdString(layout.columnName(c)) : tr("Value %1").arg(c + 1);
            const QString unit = known && !colDef->quantity.empty() ? QString::fromStdString(units.label(colDef->quantity)) : QString();
            QString tip = known ? QString::fromStdString(colDef->help) : QString();
            if (!supported)
                tip = why;
            QStringList choices;
            if (known)
            {
                for (const auto& ch : colDef->choices)
                {
                    // Grey out choices the selected engine cannot run.
                    if (engine_ && ch == "PCV" && engine_->supportFor(core::Feature::EpanetPcvValve) != engine::Support::Full)
                        continue;
                    choices << QString::fromStdString(ch);
                }
                choices << referenceChoices(model, *colDef);
            }
            addRow(name, value, unit, tip, QString::fromStdString(er.section), er.row, static_cast<int>(c), tail,
                   choices, supported);
            if (tail)
                break;
        }
    }
    // Results at the current period
    if (const auto* r = doc_->results())
    {
        const char* table = sel.category == Category::Node ? "node" : sel.category == Category::Link ? "link" : "subcatchment";
        if (const auto* t = r->table(table))
        {
            const int el = t->elementIndex(id);
            if (el >= 0)
            {
                addHeader(tr("Results at %1").arg(QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(doc_->period())]))),
                          true);
                for (int v = 0; v < t->variableCount(); ++v)
                {
                    // NaN: not available while the simulation runs.
                    const double x = t->value(doc_->period(), v, el);
                    addRow(QString::fromStdString(t->variables[v].name),
                           std::isnan(x) ? tr("n/a") : QString::fromStdString(text::formatDouble(x, 6)),
                           QString::fromStdString(t->variables[v].unit), {}, {}, -1, -1, false, {}, true, false);
                }
            }
        }
    }
}

void PropertyPanel::showMultiple()
{
    const auto& model = doc_->model();
    const auto& items = doc_->selectedElements();
    const int count = static_cast<int>(items.size());
    // Count the element types in the order they first appear.
    QStringList kinds;
    QHash<QString, int> perKind;
    for (const auto& s : items)
    {
        const QString k = kindOfElement(model, s);
        if (!perKind.contains(k))
            kinds << k;
        ++perKind[k];
    }
    QStringList parts;
    for (const auto& k : kinds)
        parts << QString::number(perKind[k]) + " " + kindLabel(k, perKind[k]);
    title_->setText(tr("%n selected", nullptr, count));
    subtitle_->setText(parts.join(", "));
    subtitle_->setToolTip(parts.join(", "));
    icon_->setPixmap(icons::element(kinds.size() == 1 ? kinds.first() : QString("Junction")).pixmap(28, 28));
    const bool uniform = kinds.size() == 1 && std::all_of(items.begin(), items.end(), [&](const Selection& s) {
                             return s.category == items.first().category;
                         });
    if (!uniform)
    {
        addHeader(tr("Selection"), true);
        for (const auto& k : kinds)
        {
            const QString label = kindLabel(k, 2);
            addRow(label.left(1).toUpper() + label.mid(1), QString::number(perKind[k]), {}, {}, {}, -1, -1, false,
                   {}, true, false);
        }
        addHeader(tr("Edit one element type at a time"), false,
                  tr("Select elements of one type to edit their shared properties together."));
        return;
    }

    const Category cat = items.first().category;
    std::vector<std::string> ids;
    ids.reserve(items.size());
    for (const auto& s : items)
        ids.push_back(s.id.toStdString());
    const auto rows = model.rowsForMany(cat, ids);
    const Selection& current = doc_->selection();
    const auto currentRows = rows.find(current.id.toStdString());
    if (currentRows == rows.end())
        return;
    const auto units = model.units();
    const QString mixed = tr("(mixed)");
    std::set<std::string> shown;
    for (const auto& er : currentRows->second)
    {
        if (!er.def || er.def->multiRowPerKey || !shown.insert(er.section).second)
            continue;
        // Rows of this section for every element (the first one each has).
        std::vector<const core::ElementRow*> all;
        for (const auto& id : ids)
        {
            const core::ElementRow* found = nullptr;
            if (const auto it = rows.find(id); it != rows.end())
            {
                for (const auto& r : it->second)
                {
                    if (r.section == er.section)
                    {
                        found = &r;
                        break;
                    }
                }
            }
            if (!found)
                break;
            all.push_back(found);
        }
        if (all.size() != ids.size())
            continue; // not every selected element has this data
        QString why;
        const bool supported = sectionSupported(er.section, er.tokens, &why);
        addHeader(er.def->description.empty() ? "[" + QString::fromStdString(er.section) + "]"
                                              : QString::fromStdString(er.def->description),
                  supported, why.isEmpty() ? "[" + QString::fromStdString(er.section) + "]" : why);
        const core::RowLayout layout = er.def->layout(er.tokens);
        const std::size_t ncols = layout.repeatFrom >= 0 ? std::max(layout.columns.size(), er.tokens.size())
                                                         : layout.columns.size();
        for (std::size_t c = 1; c < ncols; ++c)
        {
            const core::ColumnDef* colDef = layout.column(c);
            if (!colDef)
                break;
            const bool tail = layout.variadic && c + 1 == layout.columns.size();
            const std::string name = layout.columnName(c);
            // End nodes are not edited together (every link would join the same nodes).
            if (static_cast<int>(c) == er.def->fromColumn || static_cast<int>(c) == er.def->toColumn)
                continue;
            auto valueOf = [&](const core::ElementRow& r) -> std::optional<QString> {
                // A different row variant (cross-section shape, ...) has other columns.
                if (r.def->layout(r.tokens).columnName(c) != name)
                    return std::nullopt;
                if (c >= r.tokens.size())
                    return QString();
                if (tail)
                {
                    std::vector<std::string> rest(r.tokens.begin() + static_cast<std::ptrdiff_t>(c), r.tokens.end());
                    return QString::fromStdString(text::join(rest, " "));
                }
                QString v = QString::fromStdString(text::unquote(r.tokens[c]));
                return v == "\"\"" ? QString() : v;
            };
            bool comparable = true, same = true;
            std::optional<QString> first;
            for (const auto* r : all)
            {
                const auto v = valueOf(*r);
                if (!v)
                {
                    comparable = false;
                    break;
                }
                if (!first)
                    first = v;
                else if (*v != *first)
                    same = false;
            }
            if (!comparable)
                break;
            const QString unit =
                colDef->quantity.empty() ? QString() : QString::fromStdString(units.label(colDef->quantity));
            QStringList choices;
            for (const auto& ch : colDef->choices)
                choices << QString::fromStdString(ch);
            choices << referenceChoices(model, *colDef);
            addRow(QString::fromStdString(name), same ? first.value_or(QString()) : mixed, unit,
                   supported ? QString::fromStdString(colDef->help) : why, QString::fromStdString(er.section),
                   kAllSelected, static_cast<int>(c), tail, choices, supported);
            if (!same)
            {
                QTableWidgetItem* v = table_->item(table_->rowCount() - 1, 1);
                QFont f = v->font();
                f.setItalic(true);
                v->setFont(f);
                v->setForeground(QColor(130, 140, 152));
                v->setToolTip(tr("The selected elements have different values. A new value applies to all of them."));
            }
            // In a scenario, mark values it changes for any of the selected elements.
            int changed = 0;
            QString base;
            for (const auto* r : all)
                changed += doc_->baseValue(er.section, r->row, static_cast<int>(c), tail, &base) ? 1 : 0;
            if (changed > 0)
            {
                const QColor tint(255, 236, 196);
                table_->item(table_->rowCount() - 1, 0)->setBackground(tint);
                QTableWidgetItem* v = table_->item(table_->rowCount() - 1, 1);
                v->setBackground(tint);
                v->setToolTip(v->toolTip() + (v->toolTip().isEmpty() ? QString() : "\n\n") +
                              tr("Changed in scenario %1 for %2 of the %3 selected elements.")
                                  .arg(doc_->activeScenario())
                                  .arg(changed)
                                  .arg(all.size()));
            }
            if (tail)
                break;
        }
    }
}

void PropertyPanel::showOptions()
{
    const auto& model = doc_->model();
    title_->setText(doc_->displayName());
    subtitle_->setText(tr("Simulation options"));
    subtitle_->setToolTip(tr("Select an element on the map to edit it."));
    icon_->setPixmap(icons::get("options").pixmap(28, 28));
    const std::vector<std::string> sections = model.dialect() == core::Dialect::Swmm
                                                  ? std::vector<std::string>{"OPTIONS"}
                                                  : std::vector<std::string>{"OPTIONS", "TIMES", "REACTIONS", "ENERGY"};
    for (const auto& name : sections)
    {
        const auto* s = model.document().find(name);
        if (!s)
            continue;
        static const QHash<QString, QString> friendly = {
            {"OPTIONS", QObject::tr("General and hydraulic options")},
            {"TIMES", QObject::tr("Time settings")},
            {"REACTIONS", QObject::tr("Water quality reactions")},
            {"ENERGY", QObject::tr("Energy and pumping costs")}};
        const QString key = QString::fromStdString(name);
        addHeader(friendly.value(key, "[" + key + "]"), true, "[" + key + "]");
        for (int r = 0; r < s->rowCount(); ++r)
        {
            const auto tokens = s->rowTokens(r);
            if (tokens.empty())
                continue;
            const std::size_t k = std::min(keyLength(tokens), tokens.size());
            std::vector<std::string> key(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(k));
            std::vector<std::string> rest(tokens.begin() + static_cast<std::ptrdiff_t>(k), tokens.end());
            QString label = QString::fromStdString(text::toLower(text::join(key, " "))).replace('_', ' ');
            for (int i = 0; i < label.size(); ++i)
                if (i == 0 || label[i - 1] == ' ')
                    label[i] = label[i].toUpper();
            addRow(label, QString::fromStdString(text::join(rest, " ")), {}, {},
                   QString::fromStdString(name), r, static_cast<int>(k), true, {}, true);
        }
    }
}

void PropertyPanel::commit(QTableWidgetItem* item)
{
    if (populating_ || item->column() != 1)
        return;
    if (item->data(RowRole).toInt() == kAllSelected)
    {
        commitMultiple(item);
        return;
    }
    const QString section = item->data(SectionRole).toString();
    const int row = item->data(RowRole).toInt();
    const int column = item->data(ColumnRole).toInt();
    const bool tail = item->data(TailRole).toBool();
    if (section.isEmpty() || row < 0 || column < 0)
        return;
    const std::string value = item->text().trimmed().toStdString();
    const std::string sec = section.toStdString();
    const Selection sel = doc_->selection();
    QString err;
    bool ok = false;
    if (column == 0 && !sel.isEmpty())
    {
        const std::string oldId = sel.id.toStdString();
        ok = doc_->edit(tr("Rename %1").arg(sel.id), [&](core::Model& m, std::string* e) {
            return m.renameElement(sel.category, oldId, value, e);
        }, &err);
        if (ok)
            doc_->select({sel.category, QString::fromStdString(value)});
    }
    else
    {
        ok = doc_->edit(tr("Edit %1").arg(section), [&](core::Model& m, std::string*) {
            auto* s = m.document().find(sec);
            if (!s)
                return false;
            auto tokens = s->rowTokens(row);
            if (tail)
            {
                tokens.resize(std::min<std::size_t>(tokens.size(), static_cast<std::size_t>(column)));
                while (tokens.size() < static_cast<std::size_t>(column))
                    tokens.push_back("*");
                for (const auto& t : text::splitInpLine(value).tokens)
                    tokens.push_back(t);
                s->setRowTokens(row, tokens);
                m.rebuild();
                return true;
            }
            return m.setToken(sec, row, column, value);
        }, &err);
    }
    if (!ok)
    {
        emit statusMessage(err.isEmpty() ? tr("The value was not accepted.") : err);
        QMetaObject::invokeMethod(this, &PropertyPanel::rebuild, Qt::QueuedConnection);
    }
}

void PropertyPanel::commitMultiple(QTableWidgetItem* item)
{
    const QString text = item->text().trimmed();
    if (text == tr("(mixed)"))
        return;
    const std::string sec = item->data(SectionRole).toString().toStdString();
    const int column = item->data(ColumnRole).toInt();
    const bool tail = item->data(TailRole).toBool();
    const std::string value = text.toStdString();
    const auto& items = doc_->selectedElements();
    if (items.isEmpty())
        return;
    const Category cat = items.first().category;
    std::vector<std::string> ids;
    for (const auto& s : items)
        ids.push_back(s.id.toStdString());
    QString err;
    const bool ok = doc_->edit(
        tr("Edit %1 of %n elements", nullptr, static_cast<int>(ids.size())).arg(table_->item(item->row(), 0)->text()),
        [&](core::Model& m, std::string*) {
            const auto rows = m.rowsForMany(cat, ids);
            auto* s = m.document().find(sec);
            if (!s)
                return false;
            for (const auto& id : ids)
            {
                const auto it = rows.find(id);
                if (it == rows.end())
                    continue;
                for (const auto& r : it->second)
                {
                    if (r.section != sec)
                        continue;
                    if (tail)
                    {
                        auto tokens = r.tokens;
                        tokens.resize(std::min<std::size_t>(tokens.size(), static_cast<std::size_t>(column)));
                        while (tokens.size() < static_cast<std::size_t>(column))
                            tokens.push_back("*");
                        for (const auto& t : text::splitInpLine(value).tokens)
                            tokens.push_back(t);
                        s->setRowTokens(r.row, tokens);
                    }
                    else
                    {
                        m.setToken(sec, r.row, column, value, false);
                    }
                    break;
                }
            }
            m.rebuild();
            return true;
        },
        &err);
    if (!ok)
    {
        emit statusMessage(err.isEmpty() ? tr("The value was not accepted.") : err);
        QMetaObject::invokeMethod(this, &PropertyPanel::rebuild, Qt::QueuedConnection);
    }
}

void PropertyPanel::populateAddMenu()
{
    QMenu* menu = addButton_->menu();
    menu->clear();
    const Selection sel = doc_->selection();
    addButton_->setEnabled(!sel.isEmpty() && doc_->selectionCount() == 1);
    if (sel.isEmpty() || doc_->selectionCount() != 1)
        return;
    const auto& model = doc_->model();
    const std::string id = sel.id.toStdString();
    std::set<std::string> present;
    for (const auto& r : model.rowsFor(sel.category, id))
        present.insert(r.section);
    for (const auto& def : core::sectionDefs(model.dialect()))
    {
        if (def.keyCategory != sel.category || !def.elementKind.empty())
            continue;
        if (def.name == "COORDINATES" || def.name == "VERTICES" || def.name == "POLYGONS" || def.name == "SYMBOLS")
            continue;
        if (present.count(def.name) && !def.multiRowPerKey)
            continue;
        // Only offer node data that applies to the node type.
        if (sel.category == Category::Node && model.dialect() == core::Dialect::Epanet)
        {
            const auto* n = model.findNode(id);
            const bool junction = n && n->kind == "Junction";
            if ((def.name == "DEMANDS" || def.name == "EMITTERS") && !junction)
                continue;
            if (def.name == "MIXING" && !(n && n->kind == "Tank"))
                continue;
        }
        if (sel.category == Category::Link && def.name == "LEAKAGE")
        {
            const auto* l = model.findLink(id);
            if (!l || l->kind != "Pipe")
                continue;
        }
        if (sel.category == Category::Link && (def.name == "INLET_USAGE" || def.name == "LOSSES"))
        {
            const auto* l = model.findLink(id);
            if (!l || l->kind != "Conduit")
                continue;
        }
        if (def.name == "GWF" && model.rowsOfKey(def.name, id).size() >= 2)
            continue; // one LATERAL and one DEEP expression at most
        QString why;
        QAction* a = menu->addAction("[" + QString::fromStdString(def.name) + "]  " + QString::fromStdString(def.description));
        std::string missing;
        const auto row = model.newDataRow(def, id, &missing);
        if (!sectionSupported(def.name, row, &why))
        {
            a->setEnabled(false);
            a->setToolTip(why);
            a->setText(a->text() + "  " + tr("(not supported by the selected engine)"));
        }
        else if (row.empty())
        {
            a->setEnabled(false);
            a->setText(a->text() + "  " +
                       tr("(add a %1 first)").arg(objectKindLabel(QString::fromStdString(missing)).toLower()));
        }
        const std::string section = def.name;
        connect(a, &QAction::triggered, this, [this, section, row] {
            doc_->edit(tr("Add [%1] data").arg(QString::fromStdString(section)), [&](core::Model& m, std::string*) {
                m.document().ensure(section).appendRow(row);
                m.rebuild();
                return true;
            });
        });
    }
    menu->setToolTipsVisible(true);
    addButton_->setEnabled(!menu->isEmpty());
}

} // namespace mirab::app
