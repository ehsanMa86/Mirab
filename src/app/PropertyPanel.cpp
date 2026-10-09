/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
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
#include "Theme.h"
#include "anahita/core/Features.h"
#include "anahita/core/Text.h"

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

#include <map>
#include <set>

namespace anahita::app
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
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->setColumnWidth(0, 170);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setWordWrap(false);
    table_->setItemDelegate(new ChoiceDelegate(table_));
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

bool PropertyPanel::sectionSupported(const std::string& section, QString* why) const
{
    if (!engine_)
        return true;
    struct Map
    {
        const char* section;
        core::Feature feature;
    };
    static const Map maps[] = {{"LEAKAGE", core::Feature::EpanetLeakage},
                               {"POLLUTANTS", core::Feature::SwmmWaterQuality},
                               {"LID_USAGE", core::Feature::SwmmLowImpactDevelopment},
                               {"INLET_USAGE", core::Feature::SwmmStreetsAndInlets}};
    for (const auto& m : maps)
    {
        if (section == m.section && engine_->supportFor(m.feature) != engine::Support::Full)
        {
            if (why)
                *why = tr("%1 does not model %2.").arg(QString::fromStdString(engine_->name),
                                                       QString::fromUtf8(core::featureName(m.feature)));
            return false;
        }
    }
    return true;
}

void PropertyPanel::rebuild()
{
    populating_ = true;
    table_->setRowCount(0);
    const Selection& sel = doc_->selection();
    if (sel.isEmpty())
        showOptions();
    else
        showElement(sel);
    populateAddMenu();
    populating_ = false;
    applyFilter();
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
    Qt::ItemFlags flags = Qt::ItemIsSelectable;
    if (enabled)
        flags |= Qt::ItemIsEnabled;
    if (enabled && editable)
        flags |= Qt::ItemIsEditable;
    v->setFlags(flags);
    auto* u = new QTableWidgetItem(unit);
    u->setFlags(Qt::ItemIsEnabled);
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
        const bool supported = sectionSupported(er.section, &why);
        const int n = ++occurrence[er.section];
        QString header = er.def && !er.def->description.empty() ? QString::fromStdString(er.def->description)
                                                                 : "[" + QString::fromStdString(er.section) + "]";
        if (er.def && er.def->multiRowPerKey)
            header += "  #" + QString::number(n);
        addHeader(header, supported, why.isEmpty() ? "[" + QString::fromStdString(er.section) + "]" : why);
        const auto& cols = er.def ? er.def->columns : std::vector<core::ColumnDef>{};
        const std::size_t ncols = std::max(cols.size(), er.tokens.size());
        for (std::size_t c = 0; c < ncols; ++c)
        {
            const bool known = c < cols.size();
            const bool tail = known && er.def->variadic && c + 1 == cols.size();
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
                    value = QString::fromStdString(er.tokens[c]);
                }
            }
            const QString name = known ? QString::fromStdString(cols[c].name) : tr("Value %1").arg(c + 1);
            const QString unit = known && !cols[c].quantity.empty() ? QString::fromStdString(units.label(cols[c].quantity)) : QString();
            QString tip = known ? QString::fromStdString(cols[c].help) : QString();
            if (!supported)
                tip = why;
            QStringList choices;
            if (known)
            {
                for (const auto& ch : cols[c].choices)
                {
                    // Grey out choices the selected engine cannot run.
                    if (engine_ && ch == "PCV" && engine_->supportFor(core::Feature::EpanetPcvValve) != engine::Support::Full)
                        continue;
                    choices << QString::fromStdString(ch);
                }
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
                    addRow(QString::fromStdString(t->variables[v].name),
                           QString::fromStdString(text::formatDouble(t->value(doc_->period(), v, el), 6)),
                           QString::fromStdString(t->variables[v].unit), {}, {}, -1, -1, false, {}, true, false);
            }
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

void PropertyPanel::populateAddMenu()
{
    QMenu* menu = addButton_->menu();
    menu->clear();
    const Selection sel = doc_->selection();
    addButton_->setEnabled(!sel.isEmpty());
    if (sel.isEmpty())
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
        QString why;
        QAction* a = menu->addAction("[" + QString::fromStdString(def.name) + "]  " + QString::fromStdString(def.description));
        if (!sectionSupported(def.name, &why))
        {
            a->setEnabled(false);
            a->setToolTip(why);
            a->setText(a->text() + "  " + tr("(not supported by the selected engine)"));
        }
        const std::string section = def.name;
        const std::size_t ncols = def.columns.size();
        connect(a, &QAction::triggered, this, [this, section, id, ncols] {
            doc_->edit(tr("Add [%1] data").arg(QString::fromStdString(section)), [&](core::Model& m, std::string*) {
                std::vector<std::string> row = {id};
                for (std::size_t c = 1; c < ncols; ++c)
                    row.push_back(c == 1 ? "0" : "");
                while (!row.empty() && row.back().empty())
                    row.pop_back();
                m.document().ensure(section).appendRow(row);
                m.rebuild();
                return true;
            });
        });
    }
    addButton_->setEnabled(!menu->isEmpty());
}

} // namespace anahita::app
