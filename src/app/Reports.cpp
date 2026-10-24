/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Reports.cpp
 * Purpose: Report tabs: network tables, time series, profile, frequency, variability, system flow, energy and status.
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
#include "Reports.h"

#include "CalibrationReport.h"
#include "ChartWidget.h"
#include "Icons.h"
#include "Language.h"
#include "RasterLayers.h"
#include "ObservedData.h"
#include "ScenarioCompare.h"
#include "Theme.h"
#include "mirab/core/Network.h"
#include "mirab/core/Query.h"
#include "mirab/core/Text.h"
#include "mirab/core/Units.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <unordered_map>

namespace mirab::app
{

using core::Category;

namespace
{
const char* tableName(Category c)
{
    switch (c)
    {
    case Category::Node:
        return "node";
    case Category::Link:
        return "link";
    case Category::Area:
        return "subcatchment";
    default:
        return nullptr;
    }
}

QString num(double v, int digits = 5)
{
    return QString::fromStdString(text::formatDouble(std::fabs(v) < 1e-12 ? 0.0 : v, digits));
}

QString varLabel(const core::Variable& v)
{
    return QString::fromStdString(v.name) + (v.unit.empty() ? QString() : " (" + QString::fromStdString(v.unit) + ")");
}

/// Value of a named property (schema column) of an element, from any of its rows.
std::optional<double> elementNumber(const core::Model& m, Category c, const std::string& id,
                                    std::initializer_list<const char*> names)
{
    for (const auto& r : m.rowsFor(c, id))
    {
        if (!r.def)
            continue;
        for (std::size_t i = 0; i < r.def->columns.size() && i < r.tokens.size(); ++i)
        {
            for (const char* n : names)
            {
                if (text::toUpper(r.def->columns[i].name) == text::toUpper(n))
                {
                    if (auto v = text::toDouble(r.tokens[i]))
                        return v;
                }
            }
        }
    }
    return std::nullopt;
}

/// Header bar shared by every report: title, description and actions.
QWidget* reportHeader(const QString& title, const QString& description, QWidget* parent, QHBoxLayout** actions)
{
    auto* head = new QWidget(parent);
    head->setObjectName("panelHeader");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(14, 10, 14, 10);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(1);
    auto* t = new QLabel(title, head);
    t->setObjectName("panelTitle");
    auto* d = new QLabel(description, head);
    d->setObjectName("panelSubtitle");
    d->setWordWrap(true);
    titles->addWidget(t);
    titles->addWidget(d);
    hl->addLayout(titles, 1);
    *actions = new QHBoxLayout;
    (*actions)->setSpacing(8);
    hl->addLayout(*actions);
    return head;
}

QPushButton* actionButton(const QString& icon, const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(icons::get(icon), text, parent);
    b->setIconSize(QSize(16, 16));
    return b;
}

void saveText(QWidget* parent, const QString& suggested, const QString& textValue)
{
    const QString f = QFileDialog::getSaveFileName(parent, QObject::tr("Export"), suggested, QObject::tr("CSV (*.csv)"));
    if (f.isEmpty())
        return;
    QFile out(f);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text))
        out.write(textValue.toUtf8());
}

// ---------------------------------------------------------------------------
// Base class: rebuilds when results or the model change
// ---------------------------------------------------------------------------
class ReportPage : public QWidget
{
public:
    ReportPage(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
    {
        layout_ = new QVBoxLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setSpacing(0);
    }
    /// Adds the printable content of the page.
    virtual void snapshot(ReportSnapshot& s) const = 0;

protected:
    static void addChart(ReportSnapshot& s, const ChartWidget* chart)
    {
        if (chart->data().hasPoints())
            s.charts.append(chart->data());
        else if (!chart->data().placeholder.isEmpty())
            s.notes << chart->data().placeholder;
    }
    static void addNote(ReportSnapshot& s, const QLabel* label)
    {
        if (!label->text().trimmed().isEmpty())
            s.notes << label->text().trimmed();
    }
    static void addTable(ReportSnapshot& s, const QAbstractItemModel* model)
    {
        for (int c = 0; c < model->columnCount(); ++c)
        {
            s.header << model->headerData(c, Qt::Horizontal).toString();
            const QVariant align =
                model->rowCount() > 0 ? model->index(0, c).data(Qt::TextAlignmentRole) : QVariant();
            s.numeric << (align.isValid() && (align.toInt() & Qt::AlignRight));
        }
        for (int r = 0; r < model->rowCount(); ++r)
        {
            QStringList cells;
            for (int c = 0; c < model->columnCount(); ++c)
                cells << model->index(r, c).data().toString();
            s.rows << cells;
        }
    }

    void watch(bool period)
    {
        connect(doc_, &ProjectDocument::resultsChanged, this, [this] { refresh(); });
        connect(doc_, &ProjectDocument::modelReset, this, [this] { refresh(); });
        if (period)
            connect(doc_, &ProjectDocument::periodChanged, this, [this] { refresh(); });
    }
    virtual void refresh() = 0;
    QWidget* body()
    {
        auto* w = new QWidget(this);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(12, 10, 12, 12);
        layout_->addWidget(w, 1);
        return w;
    }

    ProjectDocument* doc_;
    QVBoxLayout* layout_;
};

// ---------------------------------------------------------------------------
// Network tables
// ---------------------------------------------------------------------------
class TableReport : public ReportPage
{
    Q_OBJECT
public:
    TableReport(ProjectDocument* doc, QWidget* parent) : ReportPage(doc, parent)
    {
        QHBoxLayout* actions = nullptr;
        layout_->addWidget(reportHeader(tr("Network tables"),
                                        tr("Properties and results of every element at the selected time. Click a "
                                           "column to sort; filter with expressions such as Pressure < 20 or "
                                           "Diameter >= 300 AND Roughness < 100."),
                                        this, &actions));
        auto* exportBtn = actionButton("export", tr("Export CSV"), this);
        auto* copyBtn = actionButton("table", tr("Copy"), this);
        actions->addWidget(copyBtn);
        actions->addWidget(exportBtn);
        QWidget* b = body();
        auto* bl = static_cast<QVBoxLayout*>(b->layout());
        auto* row = new QHBoxLayout;
        category_ = new QComboBox(b);
        kind_ = new QComboBox(b);
        filter_ = new QLineEdit(b);
        filter_->setPlaceholderText(tr("Filter, e.g. Pressure < 20 or Diameter >= 300"));
        filter_->setLayoutDirection(Qt::LeftToRight); // expressions use English names
        filter_->setClearButtonEnabled(true);
        count_ = new QLabel(b);
        count_->setObjectName("muted");
        row->addWidget(new QLabel(tr("Show"), b));
        row->addWidget(category_);
        row->addWidget(kind_);
        row->addWidget(filter_, 1);
        row->addWidget(count_);
        bl->addLayout(row);
        model_ = new QStandardItemModel(this);
        model_->setSortRole(Qt::UserRole);
        proxy_ = new FilterProxy(this);
        proxy_->setSourceModel(model_);
        proxy_->setSortRole(Qt::UserRole);
        view_ = new QTableView(b);
        view_->setModel(proxy_);
        view_->setSortingEnabled(true);
        view_->setAlternatingRowColors(true);
        view_->setShowGrid(false);
        view_->verticalHeader()->hide();
        view_->verticalHeader()->setDefaultSectionSize(24);
        view_->horizontalHeader()->setStretchLastSection(false);
        view_->setSelectionBehavior(QAbstractItemView::SelectRows);
        view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        bl->addWidget(view_, 1);

        connect(category_, &QComboBox::currentIndexChanged, this, [this] {
            fillKinds();
            refresh();
        });
        connect(kind_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
        connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& idx) {
            if (!idx.isValid())
                return;
            const QString id = proxy_->index(idx.row(), 0).data(Qt::DisplayRole).toString();
            doc_->select({static_cast<Category>(category_->currentData().toInt()), id});
        });
        connect(exportBtn, &QPushButton::clicked, this, [this] { saveText(this, "network-table.csv", csv()); });
        connect(copyBtn, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(csv().replace(',', '\t')); });
        fillCategories();
        watch(true);
        connect(doc_, &ProjectDocument::modelChanged, this, [this] { refresh(); });
        refresh();
    }

protected:
    class FilterProxy : public QSortFilterProxyModel
    {
    public:
        using QSortFilterProxyModel::QSortFilterProxyModel;
        std::optional<core::Query> query;
        QHash<QString, int> columns; ///< upper-case column name (without units) -> column
        bool filterAcceptsRow(int row, const QModelIndex& parent) const override
        {
            if (!query || query->empty())
                return true;
            return query->matches([&](const std::string& name) -> std::optional<std::string> {
                const int c = columns.value(QString::fromStdString(name).trimmed().toUpper(), -1);
                if (c < 0)
                    return std::nullopt;
                const QVariant v = sourceModel()->index(row, c, parent).data(Qt::UserRole);
                if (!v.isValid())
                    return std::nullopt;
                if (v.typeId() == QMetaType::Double)
                    return QString::number(v.toDouble(), 'g', 17).toStdString();
                return v.toString().toStdString();
            });
        }
        void refilter() { invalidateRowsFilter(); }
    };

    void fillCategories()
    {
        category_->blockSignals(true);
        category_->clear();
        category_->addItem(icons::element("Junction"), tr("Nodes"), static_cast<int>(Category::Node));
        category_->addItem(icons::element("Pipe"), tr("Links"), static_cast<int>(Category::Link));
        if (doc_->model().dialect() == core::Dialect::Swmm)
            category_->addItem(icons::element("Subcatchment"), tr("Subcatchments"), static_cast<int>(Category::Area));
        category_->blockSignals(false);
        fillKinds();
    }

    void fillKinds()
    {
        const auto& m = doc_->model();
        const auto cat = static_cast<Category>(category_->currentData().toInt());
        std::set<std::string> kinds;
        if (cat == Category::Node)
            for (const auto& n : m.nodes())
                kinds.insert(n.kind);
        else if (cat == Category::Link)
            for (const auto& l : m.links())
                kinds.insert(l.kind);
        kind_->blockSignals(true);
        const QString keep = kind_->currentData().toString();
        kind_->clear();
        kind_->addItem(tr("All types"));
        for (const auto& k : kinds)
            kind_->addItem(icons::element(QString::fromStdString(k)), coreText(k), QString::fromStdString(k));
        const int idx = keep.isEmpty() ? -1 : kind_->findData(keep);
        kind_->setCurrentIndex(idx >= 0 ? idx : 0);
        kind_->setVisible(cat != Category::Area);
        kind_->blockSignals(false);
    }

    void refresh() override
    {
        const auto& m = doc_->model();
        const auto cat = static_cast<Category>(category_->currentData().toInt());
        const QString kindFilter = kind_->currentIndex() > 0 ? kind_->currentData().toString() : QString();
        struct Row
        {
            std::string id, kind;
        };
        std::vector<Row> rows;
        if (cat == Category::Node)
            for (const auto& n : m.nodes())
                rows.push_back({n.id, n.kind});
        else if (cat == Category::Link)
            for (const auto& l : m.links())
                rows.push_back({l.id, l.kind});
        else
            for (const auto& a : m.areas())
                rows.push_back({a.id, "Subcatchment"});
        if (!kindFilter.isEmpty())
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Row& r) { return QString::fromStdString(r.kind) != kindFilter; }),
                       rows.end());

        // Property columns: the columns of the element's own section when one type is shown.
        std::vector<std::string> props;
        const auto units = m.units();
        std::vector<std::string> propUnits;
        if (!kindFilter.isEmpty() && !rows.empty())
        {
            for (const auto& r : m.rowsFor(cat, rows.front().id))
            {
                if (r.def && r.def->elementKind == kindFilter.toStdString())
                {
                    for (std::size_t c = 1; c < r.def->columns.size(); ++c)
                    {
                        const auto& col = r.def->columns[c];
                        if (col.type == core::ColumnType::Text && col.name == "Parameters")
                            continue;
                        props.push_back(col.name);
                        propUnits.push_back(col.quantity.empty() ? std::string() : units.label(col.quantity));
                    }
                    break;
                }
            }
        }
        const auto* r = doc_->results();
        const core::ResultTable* t = r ? r->table(tableName(cat)) : nullptr;
        QStringList headers = {tr("ID"), tr("Type")};
        for (std::size_t i = 0; i < props.size(); ++i)
            headers << QString::fromStdString(props[i]) + (propUnits[i].empty() ? QString() : " (" + QString::fromStdString(propUnits[i]) + ")");
        if (t)
            for (const auto& v : t->variables)
                headers << varLabel(v);
        model_->clear();
        model_->setHorizontalHeaderLabels(headers);
        auto cell = [](const QString& textValue, const QVariant& sortKey) {
            auto* item = new QStandardItem(textValue);
            item->setData(sortKey, Qt::UserRole);
            return item;
        };
        for (const auto& row : rows)
        {
            QList<QStandardItem*> items;
            items << cell(QString::fromStdString(row.id), QString::fromStdString(row.id));
            items << cell(coreText(row.kind), QString::fromStdString(row.kind));
            std::map<std::string, std::string> values;
            for (const auto& er : m.rowsFor(cat, row.id))
            {
                if (!er.def || er.def->elementKind != row.kind)
                    continue;
                for (std::size_t c = 1; c < er.def->columns.size() && c < er.tokens.size(); ++c)
                    values[er.def->columns[c].name] = er.tokens[c];
            }
            for (const auto& p : props)
            {
                const std::string v = values.count(p) ? values[p] : std::string();
                const auto d = text::toDouble(v);
                items << cell(QString::fromStdString(v), d ? QVariant(*d) : QVariant(QString::fromStdString(v)));
                items.last()->setTextAlignment(d ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
            }
            if (t)
            {
                const int e = t->elementIndex(row.id);
                for (int v = 0; v < t->variableCount(); ++v)
                {
                    const double d = e >= 0 ? t->value(doc_->period(), v, e) : 0.0;
                    items << cell(e >= 0 ? num(d, 5) : QString(), e >= 0 ? QVariant(d) : QVariant());
                    items.last()->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                }
            }
            model_->appendRow(items);
        }
        view_->resizeColumnsToContents();
        for (int c = 0; c < model_->columnCount(); ++c)
            view_->setColumnWidth(c, std::max(90, std::min(220, view_->columnWidth(c) + 18)));
        applyFilter();
    }

    void applyFilter()
    {
        proxy_->columns.clear();
        for (int c = 0; c < model_->columnCount(); ++c)
        {
            const QString h = model_->headerData(c, Qt::Horizontal).toString();
            for (const QString& name : {h.toUpper(), h.section(" (", 0, 0).toUpper()})
                if (!proxy_->columns.contains(name))
                    proxy_->columns.insert(name, c);
        }
        proxy_->columns.insert("KIND", proxy_->columns.value("TYPE", 1));
        std::string err;
        proxy_->query = core::Query::parse(filter_->text().toStdString(), &err);
        filter_->setStyleSheet({});
        filter_->setToolTip({});
        bool known = proxy_->query.has_value();
        if (proxy_->query)
        {
            for (const auto& f : proxy_->query->fields())
                known = known && proxy_->columns.contains(QString::fromStdString(f).trimmed().toUpper());
            if (!known)
                filter_->setToolTip(tr("The table has no column of that name."));
        }
        else
        {
            filter_->setToolTip(QString::fromStdString(err));
        }
        if (!known)
        {
            filter_->setStyleSheet("border-color: #c84b4b;");
            proxy_->query.reset();
        }
        proxy_->refilter();
        count_->setText(tr("%1 of %2 rows").arg(proxy_->rowCount()).arg(model_->rowCount()));
        if (const auto* r = doc_->results())
            count_->setText(count_->text() + "  ·  " +
                            QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(doc_->period())])));
    }

    QString csv() const
    {
        QString out;
        QStringList h;
        for (int c = 0; c < proxy_->columnCount(); ++c)
            h << proxy_->headerData(c, Qt::Horizontal).toString().replace(',', ';');
        out += h.join(',') + "\n";
        for (int r = 0; r < proxy_->rowCount(); ++r)
        {
            QStringList cells;
            for (int c = 0; c < proxy_->columnCount(); ++c)
                cells << proxy_->index(r, c).data().toString().replace(',', ';');
            out += cells.join(',') + "\n";
        }
        return out;
    }

    void snapshot(ReportSnapshot& s) const override
    {
        QString what = category_->currentText();
        if (!kind_->isHidden() && kind_->currentIndex() > 0)
            what += " (" + kind_->currentText() + ")";
        if (!filter_->text().trimmed().isEmpty())
            what += tr(", filter %1").arg(filter_->text().trimmed());
        s.notes << what + "  ·  " + count_->text();
        addTable(s, proxy_);
    }

    QComboBox* category_;
    QComboBox* kind_;
    QLineEdit* filter_;
    QLabel* count_;
    QStandardItemModel* model_;
    FilterProxy* proxy_;
    QTableView* view_;
};

// ---------------------------------------------------------------------------
// Time series of several elements
// ---------------------------------------------------------------------------
class TimeSeriesReport : public ReportPage
{
    Q_OBJECT
public:
    TimeSeriesReport(ProjectDocument* doc, QWidget* parent) : ReportPage(doc, parent)
    {
        QHBoxLayout* actions = nullptr;
        const bool swmm = doc_->model().dialect() == core::Dialect::Swmm;
        layout_->addWidget(reportHeader(tr("Time series"),
                                        swmm ? tr("Compare one result for several elements over the simulation. Add the "
                                                  "element selected on the map, or type IDs. System shows the totals of "
                                                  "the whole model.")
                                             : tr("Compare one result for several elements over the simulation. Add the "
                                                  "element selected on the map, or type IDs."),
                                        this, &actions));
        auto* exportBtn = actionButton("export", tr("Export CSV"), this);
        actions->addWidget(exportBtn);
        QWidget* b = body();
        auto* bl = static_cast<QVBoxLayout*>(b->layout());
        auto* split = new QSplitter(Qt::Horizontal, b);
        auto* side = new QWidget(split);
        auto* sl = new QVBoxLayout(side);
        sl->setContentsMargins(0, 0, 8, 0);
        category_ = new QComboBox(side);
        category_->addItem(tr("Nodes"), static_cast<int>(Category::Node));
        category_->addItem(tr("Links"), static_cast<int>(Category::Link));
        if (swmm)
        {
            category_->addItem(tr("Subcatchments"), static_cast<int>(Category::Area));
            // Category::None stands for the system totals of the SWMM results.
            category_->addItem(tr("System"), static_cast<int>(Category::None));
        }
        variable_ = new QComboBox(side);
        list_ = new QListWidget(side);
        list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        auto* idEdit = new QLineEdit(side);
        idEdit->setPlaceholderText(tr("Add by ID and press Enter"));
        auto* addSel = actionButton("new", tr("Add selected"), side);
        auto* remove = actionButton("delete", tr("Remove"), side);
        auto* buttons = new QWidget(side);
        auto* buttonRow = new QHBoxLayout(buttons);
        buttonRow->setContentsMargins(0, 0, 0, 0);
        buttonRow->addWidget(addSel);
        buttonRow->addWidget(remove);
        sl->addWidget(new QLabel(tr("Elements"), side));
        sl->addWidget(category_);
        sl->addWidget(list_, 1);
        sl->addWidget(idEdit);
        sl->addWidget(buttons);
        sl->addWidget(new QLabel(tr("Result"), side));
        sl->addWidget(variable_);
        sl->addStretch(0); // takes the free space while the element list is hidden
        elementWidgets_ = {list_, idEdit, buttons};
        chart_ = new ChartWidget(split);
        split->addWidget(side);
        split->addWidget(chart_);
        split->setStretchFactor(1, 1);
        split->setSizes({240, 800});
        bl->addWidget(split, 1);

        connect(category_, &QComboBox::currentIndexChanged, this, [this] {
            list_->clear();
            for (QWidget* w : elementWidgets_)
                w->setVisible(!system());
            refresh();
        });
        connect(variable_, &QComboBox::currentTextChanged, this, [this] { plot(); });
        connect(addSel, &QPushButton::clicked, this, [this] {
            const Selection s = doc_->selection();
            if (!s.isEmpty())
                addElement(s.category, s.id);
        });
        connect(idEdit, &QLineEdit::returnPressed, this, [this, idEdit] {
            for (const QString& id : idEdit->text().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts))
                addElement(static_cast<Category>(category_->currentData().toInt()), id);
            idEdit->clear();
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            qDeleteAll(list_->selectedItems());
            plot();
        });
        connect(exportBtn, &QPushButton::clicked, this, [this] { saveText(this, "time-series.csv", chart_->toCsv()); });
        connect(ObservedData::of(doc_), &ObservedData::changed, this, [this] { plot(); });
        connect(doc_, &ProjectDocument::periodChanged, this, [this](int p) {
            if (doc_->results())
                chart_->setMarker(doc_->results()->times[static_cast<std::size_t>(p)]);
        });
        watch(false);
        const Selection s = doc_->selection();
        if (!s.isEmpty() && s.category != Category::Gage && s.category != Category::None)
        {
            const int idx = category_->findData(static_cast<int>(s.category));
            if (idx >= 0)
                category_->setCurrentIndex(idx);
            addElement(s.category, s.id);
        }
        refresh();
    }

protected:
    bool system() const { return static_cast<Category>(category_->currentData().toInt()) == Category::None; }

    const core::ResultTable* table() const
    {
        const auto* r = doc_->results();
        if (!r)
            return nullptr;
        if (system())
            return r->table("system");
        return r->table(tableName(static_cast<Category>(category_->currentData().toInt())));
    }

    void addElement(Category c, const QString& id)
    {
        if (c == Category::None || c == Category::Gage)
            return;
        if (static_cast<int>(c) != category_->currentData().toInt())
        {
            const int idx = category_->findData(static_cast<int>(c));
            if (idx < 0)
                return;
            category_->setCurrentIndex(idx);
        }
        if (!list_->findItems(id, Qt::MatchExactly).isEmpty())
            return;
        new QListWidgetItem(id, list_);
        plot();
    }

    void refresh() override
    {
        const auto* t = table();
        const QString keep = variable_->currentText();
        variable_->blockSignals(true);
        variable_->clear();
        if (t)
            for (const auto& v : t->variables)
                variable_->addItem(QString::fromStdString(v.name));
        int idx = variable_->findText(keep);
        if (idx < 0 && system())
            idx = variable_->findText("Runoff");
        variable_->setCurrentIndex(idx >= 0 ? idx : 0);
        variable_->blockSignals(false);
        plot();
    }

    void plot()
    {
        const auto* r = doc_->results();
        const auto* t = table();
        if (!t)
        {
            chart_->clear(r && system() ? tr("These results have no system totals.")
                                        : tr("Run a simulation to plot results."));
            return;
        }
        const int v = t->variableIndex(variable_->currentText().toStdString());
        if (system())
        {
            plotSystem(*r, *t, v);
            return;
        }
        QVector<ChartWidget::Series> series;
        for (int i = 0; i < list_->count() && v >= 0; ++i)
        {
            const int e = t->elementIndex(list_->item(i)->text().toStdString());
            if (e < 0)
                continue;
            ChartWidget::Series s;
            s.name = list_->item(i)->text();
            for (int p = 0; p < r->periodCount(); ++p)
                s.points.append(QPointF(r->times[static_cast<std::size_t>(p)], t->value(p, v, e)));
            series << s;
            // Measurements linked to the element are drawn as markers.
            ChartWidget::Series o;
            o.points = ObservedData::of(doc_)->pointsFor(static_cast<Category>(category_->currentData().toInt()), s.name,
                                                         variable_->currentText(),
                                                         QString::fromStdString(t->variables[static_cast<std::size_t>(v)].unit));
            if (!o.points.isEmpty())
            {
                o.name = tr("%1 observed").arg(s.name);
                o.style = ChartWidget::Series::Style::Points;
                series << o;
            }
        }
        if (series.isEmpty())
        {
            chart_->clear(tr("Add elements to compare."));
            return;
        }
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries(series, varLabel(t->variables[static_cast<std::size_t>(v)]));
        chart_->setMarker(r->times[static_cast<std::size_t>(doc_->period())]);
    }

    /// One curve of the system totals; values not reported yet are left out.
    void plotSystem(const core::ResultSet& r, const core::ResultTable& t, int v)
    {
        if (v < 0 || t.elementCount() < 1)
        {
            chart_->clear(tr("These results have no system totals."));
            return;
        }
        const auto& var = t.variables[static_cast<std::size_t>(v)];
        ChartWidget::Series s;
        s.name = QString::fromStdString(var.name);
        for (int p = 0; p < r.periodCount(); ++p)
        {
            const float y = t.value(p, v, 0);
            if (!std::isnan(y))
                s.points.append(QPointF(r.times[static_cast<std::size_t>(p)], y));
        }
        if (s.points.isEmpty())
        {
            chart_->clear(tr("%1 is available when the simulation ends.").arg(s.name));
            return;
        }
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries({s}, varLabel(var), tr("System"));
        chart_->setMarker(r.times[static_cast<std::size_t>(doc_->period())]);
    }

    void snapshot(ReportSnapshot& s) const override { addChart(s, chart_); }

    QComboBox* category_;
    QComboBox* variable_;
    QListWidget* list_;
    ChartWidget* chart_;
    QList<QWidget*> elementWidgets_; ///< hidden for the system totals
};

// ---------------------------------------------------------------------------
// Longitudinal profile along the shortest path between two nodes
// ---------------------------------------------------------------------------
class ProfileReport : public ReportPage
{
    Q_OBJECT
public:
    ProfileReport(ProjectDocument* doc, QWidget* parent) : ReportPage(doc, parent)
    {
        QHBoxLayout* actions = nullptr;
        layout_->addWidget(reportHeader(tr("Profile plot"),
                                        tr("Ground (or invert) elevation and hydraulic grade along the shortest path "
                                           "between two nodes, with the minimum-maximum range over the simulation. "
                                           "It follows the time slider."),
                                        this, &actions));
        auto* exportBtn = actionButton("export", tr("Export CSV"), this);
        actions->addWidget(exportBtn);
        QWidget* b = body();
        auto* bl = static_cast<QVBoxLayout*>(b->layout());
        auto* row = new QHBoxLayout;
        from_ = new QComboBox(b);
        to_ = new QComboBox(b);
        for (auto* c : {from_, to_})
        {
            c->setEditable(true);
            c->setMinimumWidth(140);
            c->setInsertPolicy(QComboBox::NoInsert);
            c->completer()->setFilterMode(Qt::MatchContains);
        }
        auto* useFrom = new QToolButton(b);
        useFrom->setText(tr("Use selected"));
        auto* useTo = new QToolButton(b);
        useTo->setText(tr("Use selected"));
        auto* go = new QPushButton(tr("Draw profile"), b);
        go->setProperty("primary", true);
        path_ = new QLabel(b);
        path_->setObjectName("muted");
        row->addWidget(new QLabel(tr("From node"), b));
        row->addWidget(from_);
        row->addWidget(useFrom);
        row->addSpacing(12);
        row->addWidget(new QLabel(tr("to node"), b));
        row->addWidget(to_);
        row->addWidget(useTo);
        row->addSpacing(12);
        row->addWidget(go);
        row->addStretch(1);
        bl->addLayout(row);
        bl->addWidget(path_);
        chart_ = new ChartWidget(b);
        bl->addWidget(chart_, 1);
        connect(useFrom, &QToolButton::clicked, this, [this] {
            if (doc_->selection().category == Category::Node)
                from_->setCurrentText(doc_->selection().id);
        });
        connect(useTo, &QToolButton::clicked, this, [this] {
            if (doc_->selection().category == Category::Node)
                to_->setCurrentText(doc_->selection().id);
        });
        connect(go, &QPushButton::clicked, this, [this] {
            computePath();
            refresh();
        });
        connect(exportBtn, &QPushButton::clicked, this, [this] { saveText(this, "profile.csv", chart_->toCsv()); });
        connect(doc_, &ProjectDocument::modelReset, this, [this] { fillNodes(); });
        watch(true);
        fillNodes();
        chart_->clear(tr("Choose two nodes and press Draw profile."));
        // Start along the selected nodes (first to last picked) or, without
        // them, from the first node to the farthest connected one.
        QStringList picked;
        for (const auto& s : doc_->selectedElements())
            if (s.category == Category::Node)
                picked << s.id;
        if (picked.size() >= 2)
        {
            setPath(picked.first(), picked.last());
        }
        else if (from_->count() > 1)
        {
            to_->setCurrentText(QString());
            computePath();
            refresh();
        }
    }

    void setPath(const QString& from, const QString& to)
    {
        from_->setCurrentText(from);
        to_->setCurrentText(to);
        computePath();
        refresh();
    }

protected:
    void fillNodes()
    {
        QStringList ids;
        for (const auto& n : doc_->model().nodes())
            ids << QString::fromStdString(n.id);
        for (auto* c : {from_, to_})
        {
            const QString keep = c->currentText();
            c->clear();
            c->addItems(ids);
            c->setCurrentText(keep);
        }
        if (from_->currentText().isEmpty() && ids.size() >= 2)
        {
            from_->setCurrentText(ids.first());
            to_->setCurrentText(ids.last());
        }
        nodes_.clear();
    }

    void computePath()
    {
        const auto& m = doc_->model();
        nodes_.clear();
        distances_.clear();
        const std::string a = from_->currentText().trimmed().toStdString();
        const std::string b = to_->currentText().trimmed().toStdString();
        const bool farthest = b.empty();
        if (!m.findNode(a) || (!farthest && !m.findNode(b)))
        {
            path_->setText(tr("Unknown node."));
            return;
        }
        // Shortest path by link length (drawn length when no length is given).
        const auto graph = core::NetworkGraph::build(m);
        const auto path = core::shortestPath(graph, graph.nodeIndex(a), farthest ? -1 : graph.nodeIndex(b));
        if (!path.found())
        {
            path_->setText(tr("The two nodes are not connected."));
            return;
        }
        for (std::size_t i = 0; i < path.nodes.size(); ++i)
        {
            nodes_.push_back(graph.nodeId(path.nodes[i]));
            distances_.push_back(path.distances[i]);
        }
        if (farthest)
            to_->setCurrentText(QString::fromStdString(nodes_.back()));
        path_->setText(tr("Path of %1 nodes, length %2").arg(nodes_.size()).arg(num(distances_.back(), 6)));
    }

    void refresh() override
    {
        if (nodes_.empty())
            return;
        const auto& m = doc_->model();
        const auto units = m.units();
        ChartWidget::Series ground;
        ground.name = m.dialect() == core::Dialect::Swmm ? tr("Invert") : tr("Elevation");
        ground.style = ChartWidget::Series::Style::Area;
        ground.color = QColor(166, 138, 100);
        ground.width = 1.5;
        QVector<std::pair<double, QString>> labels;
        for (std::size_t i = 0; i < nodes_.size(); ++i)
        {
            const double z = elementNumber(m, Category::Node, nodes_[i], {"Elevation", "Head"}).value_or(0.0);
            ground.points.append(QPointF(distances_[i], z));
            labels.append({distances_[i], QString::fromStdString(nodes_[i])});
        }
        QVector<ChartWidget::Series> series;
        // Ground from the project's elevation model and, for SWMM, the node rims and conduit crowns.
        QVector<ChartWidget::Series> terrain;
        ChartWidget::Series dem;
        dem.name = tr("Ground (DEM)");
        dem.color = QColor(96, 72, 44);
        dem.width = 1.6;
        dem.points = terrainProfile(doc_, nodes_, distances_);
        if (!dem.points.isEmpty())
            terrain << dem;
        if (m.dialect() == core::Dialect::Swmm)
        {
            ChartWidget::Series rim, crown;
            rim.name = tr("Rim (invert + maximum depth)");
            rim.color = QColor(150, 110, 60);
            rim.dashed = true;
            rim.width = 1.4;
            crown.name = tr("Conduit crown");
            crown.color = QColor(90, 100, 115);
            crown.width = 1.2;
            const bool offsetsAreElevations = text::toUpper(m.option("LINK_OFFSETS")) == "ELEVATION";
            for (std::size_t i = 0; i < nodes_.size(); ++i)
            {
                const double invert = elementNumber(m, Category::Node, nodes_[i], {"Elevation"}).value_or(0.0);
                const double depth = elementNumber(m, Category::Node, nodes_[i], {"MaxDepth"}).value_or(0.0);
                if (depth > 0)
                    rim.points.append(QPointF(distances_[i], invert + depth));
                if (i + 1 == nodes_.size())
                    continue;
                for (const auto& l : m.links())
                {
                    const bool forward = l.from == nodes_[i] && l.to == nodes_[i + 1];
                    if (l.kind != "Conduit" || !(forward || (l.to == nodes_[i] && l.from == nodes_[i + 1])))
                        continue;
                    const double height = elementNumber(m, Category::Link, l.id, {"Geom1"}).value_or(0.0);
                    const double in = elementNumber(m, Category::Link, l.id, {"InOffset"}).value_or(0.0);
                    const double out = elementNumber(m, Category::Link, l.id, {"OutOffset"}).value_or(0.0);
                    const double next = elementNumber(m, Category::Node, nodes_[i + 1], {"Elevation"}).value_or(0.0);
                    const double a = forward ? in : out, b = forward ? out : in;
                    crown.points.append(QPointF(distances_[i], (offsetsAreElevations ? a : invert + a) + height));
                    crown.points.append(QPointF(distances_[i + 1], (offsetsAreElevations ? b : next + b) + height));
                    break;
                }
            }
            if (!crown.points.isEmpty())
                terrain << crown;
            if (!rim.points.isEmpty())
                terrain << rim;
        }
        const auto* r = doc_->results();
        const auto* t = r ? r->table("node") : nullptr;
        const int head = t ? t->variableIndex("Head") : -1;
        if (t && head >= 0)
        {
            ChartWidget::Series band;
            band.name = tr("Min - max head");
            band.style = ChartWidget::Series::Style::Band;
            band.color = QColor(31, 111, 235);
            ChartWidget::Series hgl;
            hgl.name = tr("Hydraulic grade at %1").arg(QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(doc_->period())])));
            hgl.color = QColor(31, 111, 235);
            hgl.width = 2.4;
            for (std::size_t i = 0; i < nodes_.size(); ++i)
            {
                const int e = t->elementIndex(nodes_[i]);
                if (e < 0)
                    continue;
                double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
                for (int p = 0; p < r->periodCount(); ++p)
                {
                    lo = std::min(lo, static_cast<double>(t->value(p, head, e)));
                    hi = std::max(hi, static_cast<double>(t->value(p, head, e)));
                }
                band.points.append(QPointF(distances_[i], lo));
                band.upper.append(QPointF(distances_[i], hi));
                hgl.points.append(QPointF(distances_[i], t->value(doc_->period(), head, e)));
            }
            series << band << ground << terrain << hgl;
        }
        else
        {
            series << ground << terrain;
        }
        chart_->setXAxis(tr("Distance along the path (%1)").arg(QString::fromStdString(units.label("length"))), false);
        chart_->setXAnnotations(labels);
        chart_->setSeries(series, tr("Elevation (%1)").arg(QString::fromStdString(units.label("elevation"))),
                          QString::fromStdString(nodes_.front()) + "  →  " + QString::fromStdString(nodes_.back()));
    }

    void snapshot(ReportSnapshot& s) const override
    {
        addNote(s, path_);
        addChart(s, chart_);
    }

    QComboBox* from_;
    QComboBox* to_;
    QLabel* path_;
    ChartWidget* chart_;
    std::vector<std::string> nodes_;
    std::vector<double> distances_;
};

// ---------------------------------------------------------------------------
// Frequency (cumulative distribution over all elements at one time)
// ---------------------------------------------------------------------------
class FrequencyReport : public ReportPage
{
    Q_OBJECT
public:
    FrequencyReport(ProjectDocument* doc, QWidget* parent) : ReportPage(doc, parent)
    {
        QHBoxLayout* actions = nullptr;
        layout_->addWidget(reportHeader(tr("Frequency plot"),
                                        tr("Share of elements whose value is at or below each level, at the time "
                                           "selected with the time slider."),
                                        this, &actions));
        auto* exportBtn = actionButton("export", tr("Export CSV"), this);
        actions->addWidget(exportBtn);
        QWidget* b = body();
        auto* bl = static_cast<QVBoxLayout*>(b->layout());
        auto* row = new QHBoxLayout;
        variable_ = new QComboBox(b);
        stats_ = new QLabel(b);
        stats_->setObjectName("muted");
        row->addWidget(new QLabel(tr("Result"), b));
        row->addWidget(variable_);
        row->addSpacing(16);
        row->addWidget(stats_, 1);
        bl->addLayout(row);
        chart_ = new ChartWidget(b);
        bl->addWidget(chart_, 1);
        connect(variable_, &QComboBox::currentIndexChanged, this, [this] { plot(); });
        connect(exportBtn, &QPushButton::clicked, this, [this] { saveText(this, "frequency.csv", chart_->toCsv()); });
        watch(true);
        refresh();
    }

protected:
    void refresh() override
    {
        const auto* r = doc_->results();
        const QString keep = variable_->currentText();
        variable_->blockSignals(true);
        variable_->clear();
        if (r)
        {
            for (const char* table : {"node", "link", "subcatchment"})
            {
                if (const auto* t = r->table(table))
                    for (const auto& v : t->variables)
                        variable_->addItem((std::string(table) == "node" ? tr("Node %1") : std::string(table) == "link" ? tr("Link %1") : tr("Subcatchment %1"))
                                               .arg(QString::fromStdString(v.name)),
                                           QStringList{table, QString::fromStdString(v.name)});
            }
        }
        int idx = variable_->findText(keep);
        if (idx < 0)
            idx = std::max(0, variable_->findData(QStringList{"node", doc_->model().dialect() == core::Dialect::Swmm ? "Depth" : "Pressure"}));
        variable_->setCurrentIndex(idx);
        variable_->blockSignals(false);
        plot();
    }

    void plot()
    {
        const auto* r = doc_->results();
        const QStringList key = variable_->currentData().toStringList();
        const auto* t = (r && key.size() == 2) ? r->table(key[0].toStdString()) : nullptr;
        if (!t)
        {
            chart_->clear(tr("Run a simulation to see the distribution of results."));
            stats_->clear();
            return;
        }
        const int v = t->variableIndex(key[1].toStdString());
        std::vector<double> values;
        for (int e = 0; e < t->elementCount(); ++e)
        {
            double x = t->value(doc_->period(), v, e);
            if (key[0] == "link")
                x = std::fabs(x);
            values.push_back(x);
        }
        if (values.empty())
            return;
        std::sort(values.begin(), values.end());
        ChartWidget::Series s;
        s.name = variable_->currentText();
        for (std::size_t i = 0; i < values.size(); ++i)
            s.points.append(QPointF(values[i], 100.0 * (i + 1) / values.size()));
        double mean = 0;
        for (double x : values)
            mean += x;
        mean /= values.size();
        const auto& var = t->variables[static_cast<std::size_t>(v)];
        stats_->setText(tr("%1 elements  ·  min %2  ·  median %3  ·  mean %4  ·  max %5  %6  ·  at %7")
                            .arg(values.size())
                            .arg(num(values.front()))
                            .arg(num(values[values.size() / 2]))
                            .arg(num(mean))
                            .arg(num(values.back()))
                            .arg(QString::fromStdString(var.unit))
                            .arg(QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(doc_->period())]))));
        chart_->setXAxis(varLabel(var), false);
        chart_->setYRange(0.0, 100.0);
        chart_->setSeries({s}, tr("Percent of elements at or below (%)"));
    }

    void snapshot(ReportSnapshot& s) const override
    {
        addNote(s, stats_);
        addChart(s, chart_);
    }

    QComboBox* variable_;
    QLabel* stats_;
    ChartWidget* chart_;
};

// ---------------------------------------------------------------------------
// Variability: spread of a result over all elements through time
// ---------------------------------------------------------------------------
class VariabilityReport : public ReportPage
{
    Q_OBJECT
public:
    VariabilityReport(ProjectDocument* doc, QWidget* parent) : ReportPage(doc, parent)
    {
        QHBoxLayout* actions = nullptr;
        layout_->addWidget(reportHeader(tr("Variability plot"),
                                        tr("Minimum, 10th-90th percentile band, median and maximum of a result over "
                                           "all elements (or elements of one type) at every time step."),
                                        this, &actions));
        auto* exportBtn = actionButton("export", tr("Export CSV"), this);
        actions->addWidget(exportBtn);
        QWidget* b = body();
        auto* bl = static_cast<QVBoxLayout*>(b->layout());
        auto* row = new QHBoxLayout;
        variable_ = new QComboBox(b);
        kind_ = new QComboBox(b);
        row->addWidget(new QLabel(tr("Result"), b));
        row->addWidget(variable_);
        row->addSpacing(12);
        row->addWidget(new QLabel(tr("Elements"), b));
        row->addWidget(kind_);
        row->addStretch(1);
        bl->addLayout(row);
        chart_ = new ChartWidget(b);
        bl->addWidget(chart_, 1);
        connect(variable_, &QComboBox::currentIndexChanged, this, [this] {
            fillKinds();
            plot();
        });
        connect(kind_, &QComboBox::currentIndexChanged, this, [this] { plot(); });
        connect(exportBtn, &QPushButton::clicked, this, [this] { saveText(this, "variability.csv", chart_->toCsv()); });
        connect(doc_, &ProjectDocument::periodChanged, this, [this](int p) {
            if (doc_->results())
                chart_->setMarker(doc_->results()->times[static_cast<std::size_t>(p)]);
        });
        watch(false);
        refresh();
    }

protected:
    void refresh() override
    {
        const auto* r = doc_->results();
        const QString keep = variable_->currentText();
        variable_->blockSignals(true);
        variable_->clear();
        if (r)
        {
            for (const char* table : {"node", "link", "subcatchment"})
            {
                if (const auto* t = r->table(table))
                    for (const auto& v : t->variables)
                        variable_->addItem((std::string(table) == "node" ? tr("Node %1") : std::string(table) == "link" ? tr("Link %1") : tr("Subcatchment %1"))
                                               .arg(QString::fromStdString(v.name)),
                                           QStringList{table, QString::fromStdString(v.name)});
            }
        }
        int idx = variable_->findText(keep);
        if (idx < 0)
            idx = std::max(0, variable_->findData(QStringList{"node", doc_->model().dialect() == core::Dialect::Swmm ? "Depth" : "Pressure"}));
        variable_->setCurrentIndex(idx);
        variable_->blockSignals(false);
        fillKinds();
        plot();
    }

    void fillKinds()
    {
        const QStringList key = variable_->currentData().toStringList();
        const auto& m = doc_->model();
        std::set<std::string> kinds;
        if (!key.isEmpty() && key[0] == "node")
            for (const auto& n : m.nodes())
                kinds.insert(n.kind);
        else if (!key.isEmpty() && key[0] == "link")
            for (const auto& l : m.links())
                kinds.insert(l.kind);
        const QString keep = kind_->currentData().toString();
        const bool keepAll = kind_->currentIndex() == 0;
        kind_->blockSignals(true);
        kind_->clear();
        kind_->addItem(tr("All"));
        for (const auto& k : kinds)
            kind_->addItem(icons::element(QString::fromStdString(k)), coreText(k), QString::fromStdString(k));
        int idx = keep.isEmpty() ? (keepAll ? 0 : -1) : kind_->findData(keep);
        if (idx < 0)
            idx = std::max(0, kind_->findData(QString("Junction")));
        kind_->setCurrentIndex(idx);
        kind_->blockSignals(false);
    }

    void plot()
    {
        const auto* r = doc_->results();
        const QStringList key = variable_->currentData().toStringList();
        const auto* t = (r && key.size() == 2) ? r->table(key[0].toStdString()) : nullptr;
        if (!t)
        {
            chart_->clear(tr("Run a simulation to see how results vary over the network."));
            return;
        }
        const int v = t->variableIndex(key[1].toStdString());
        const QString kind = kind_->currentIndex() > 0 ? kind_->currentData().toString() : QString();
        std::vector<int> elements;
        const auto& m = doc_->model();
        for (int e = 0; e < t->elementCount(); ++e)
        {
            if (!kind.isEmpty())
            {
                const std::string& id = t->ids[static_cast<std::size_t>(e)];
                const std::string k = key[0] == "node" ? (m.findNode(id) ? m.findNode(id)->kind : "")
                                                       : (m.findLink(id) ? m.findLink(id)->kind : "");
                if (QString::fromStdString(k) != kind)
                    continue;
            }
            elements.push_back(e);
        }
        if (elements.empty())
        {
            chart_->clear(tr("No elements of this type."));
            return;
        }
        ChartWidget::Series band, minS, maxS, median, mean;
        band.name = tr("10th - 90th percentile");
        band.style = ChartWidget::Series::Style::Band;
        band.color = QColor(31, 111, 235);
        minS.name = tr("Minimum");
        minS.color = QColor(43, 103, 199);
        minS.dashed = true;
        minS.width = 1.4;
        maxS.name = tr("Maximum");
        maxS.color = QColor(224, 82, 60);
        maxS.dashed = true;
        maxS.width = 1.4;
        median.name = tr("Median");
        median.color = QColor(31, 111, 235);
        median.width = 2.4;
        mean.name = tr("Mean");
        mean.color = QColor(27, 158, 119);
        mean.width = 1.6;
        std::vector<double> values(elements.size());
        for (int p = 0; p < r->periodCount(); ++p)
        {
            double sum = 0;
            for (std::size_t i = 0; i < elements.size(); ++i)
            {
                double x = t->value(p, v, elements[i]);
                if (key[0] == "link")
                    x = std::fabs(x);
                values[i] = x;
                sum += x;
            }
            std::sort(values.begin(), values.end());
            auto pct = [&](double q) {
                const double pos = q * (values.size() - 1);
                const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
                const std::size_t hi = std::min(values.size() - 1, lo + 1);
                return values[lo] + (values[hi] - values[lo]) * (pos - lo);
            };
            const double time = r->times[static_cast<std::size_t>(p)];
            band.points.append(QPointF(time, pct(0.1)));
            band.upper.append(QPointF(time, pct(0.9)));
            minS.points.append(QPointF(time, values.front()));
            maxS.points.append(QPointF(time, values.back()));
            median.points.append(QPointF(time, pct(0.5)));
            mean.points.append(QPointF(time, sum / values.size()));
        }
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries({band, minS, maxS, mean, median}, varLabel(t->variables[static_cast<std::size_t>(v)]),
                          tr("%1 over %2 elements").arg(variable_->currentText()).arg(elements.size()));
        chart_->setMarker(r->times[static_cast<std::size_t>(doc_->period())]);
    }

    void snapshot(ReportSnapshot& s) const override { addChart(s, chart_); }

    QComboBox* variable_;
    QComboBox* kind_;
    ChartWidget* chart_;
};

// ---------------------------------------------------------------------------
// System flow balance
// ---------------------------------------------------------------------------
class SystemFlowReport : public ReportPage
{
    Q_OBJECT
public:
    SystemFlowReport(ProjectDocument* doc, QWidget* parent) : ReportPage(doc, parent)
    {
        QHBoxLayout* actions = nullptr;
        const bool swmm = doc->model().dialect() == core::Dialect::Swmm;
        layout_->addWidget(reportHeader(tr("System flow"),
                                        swmm ? tr("Total runoff, flow leaving through outfalls and flooding over time.")
                                             : tr("Water produced by sources, consumed by demands and stored in tanks "
                                                  "over time."),
                                        this, &actions));
        auto* exportBtn = actionButton("export", tr("Export CSV"), this);
        actions->addWidget(exportBtn);
        QWidget* b = body();
        auto* bl = static_cast<QVBoxLayout*>(b->layout());
        summary_ = new QLabel(b);
        summary_->setObjectName("muted");
        summary_->setWordWrap(true);
        bl->addWidget(summary_);
        chart_ = new ChartWidget(b);
        bl->addWidget(chart_, 1);
        connect(exportBtn, &QPushButton::clicked, this, [this] { saveText(this, "system-flow.csv", chart_->toCsv()); });
        connect(doc_, &ProjectDocument::periodChanged, this, [this](int p) {
            if (doc_->results())
                chart_->setMarker(doc_->results()->times[static_cast<std::size_t>(p)]);
        });
        watch(false);
        refresh();
    }

protected:
    void refresh() override
    {
        const auto* r = doc_->results();
        if (!r)
        {
            chart_->clear(tr("Run a simulation to see the system flow balance."));
            summary_->clear();
            return;
        }
        const auto& m = doc_->model();
        QVector<ChartWidget::Series> series;
        QString unit;
        auto sumOver = [&](const char* table, const char* variable, auto accept, double sign, const QString& name,
                           const QColor& color) {
            const auto* t = r->table(table);
            if (!t)
                return;
            const int v = t->variableIndex(variable);
            if (v < 0)
                return;
            unit = QString::fromStdString(t->variables[static_cast<std::size_t>(v)].unit);
            ChartWidget::Series s;
            s.name = name;
            s.color = color;
            for (int p = 0; p < r->periodCount(); ++p)
            {
                double total = 0;
                for (int e = 0; e < t->elementCount(); ++e)
                {
                    if (accept(t->ids[static_cast<std::size_t>(e)]))
                        total += sign * t->value(p, v, e);
                }
                s.points.append(QPointF(r->times[static_cast<std::size_t>(p)], total));
            }
            series << s;
        };
        auto kindIs = [&](const char* kind) {
            return [&m, kind](const std::string& id) {
                const auto* n = m.findNode(id);
                return n && n->kind == kind;
            };
        };
        if (m.dialect() == core::Dialect::Swmm)
        {
            sumOver("subcatchment", "Runoff", [](const std::string&) { return true; }, 1.0, tr("Total runoff"), QColor(27, 158, 119));
            sumOver("node", "Total Inflow", kindIs("Outfall"), 1.0, tr("Outfall discharge"), QColor(31, 111, 235));
            sumOver("node", "Flooding", [](const std::string&) { return true; }, 1.0, tr("Flooding"), QColor(224, 82, 60));
        }
        else
        {
            sumOver("node", "Demand", kindIs("Reservoir"), -1.0, tr("Supplied by reservoirs"), QColor(31, 111, 235));
            sumOver("node", "Demand", kindIs("Junction"), 1.0, tr("Consumed by demands"), QColor(224, 82, 60));
            sumOver("node", "Demand", kindIs("Tank"), 1.0, tr("Net tank inflow (filling > 0)"), QColor(27, 158, 119));
        }
        if (series.isEmpty())
        {
            chart_->clear(tr("These results do not include the flows needed for a system balance."));
            return;
        }
        QStringList totals;
        const double step = r->periodCount() > 1 ? r->times[1] - r->times[0] : 0.0;
        for (const auto& s : series)
        {
            double avg = 0;
            for (const auto& pt : s.points)
                avg += pt.y();
            avg /= std::max(1, static_cast<int>(s.points.size()));
            totals << tr("%1: average %2 %3").arg(s.name, num(avg), unit);
        }
        summary_->setText(totals.join("   ·   ") + (step > 0 ? tr("   ·   reporting step %1").arg(QString::fromStdString(text::formatDuration(step))) : QString()));
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries(series, tr("Flow (%1)").arg(unit));
        chart_->setMarker(r->times[static_cast<std::size_t>(doc_->period())]);
    }

    void snapshot(ReportSnapshot& s) const override
    {
        for (const QString& part : summary_->text().split("   ·   ", Qt::SkipEmptyParts))
            s.notes << part.trimmed();
        addChart(s, chart_);
    }

    QLabel* summary_;
    ChartWidget* chart_;
};

// ---------------------------------------------------------------------------
// Energy audit of the pumps (EPANET)
// ---------------------------------------------------------------------------
class EnergyReport : public ReportPage
{
    Q_OBJECT
public:
    EnergyReport(ProjectDocument* doc, QWidget* parent) : ReportPage(doc, parent)
    {
        QHBoxLayout* actions = nullptr;
        layout_->addWidget(reportHeader(tr("Energy report"),
                                        tr("Pump utilization, efficiency, energy use and cost, computed from the "
                                           "reported pump power, flow and head at every reporting step."),
                                        this, &actions));
        auto* exportBtn = actionButton("export", tr("Export CSV"), this);
        actions->addWidget(exportBtn);
        QWidget* b = body();
        auto* bl = static_cast<QVBoxLayout*>(b->layout());
        auto* split = new QSplitter(Qt::Vertical, b);
        table_ = new QTableView(split);
        model_ = new QStandardItemModel(this);
        table_->setModel(model_);
        table_->verticalHeader()->hide();
        table_->setAlternatingRowColors(true);
        table_->setShowGrid(false);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setStretchLastSection(true);
        chart_ = new ChartWidget(split);
        split->addWidget(table_);
        split->addWidget(chart_);
        split->setSizes({220, 320});
        note_ = new QLabel(b);
        note_->setObjectName("muted");
        note_->setWordWrap(true);
        bl->addWidget(split, 1);
        bl->addWidget(note_);
        connect(exportBtn, &QPushButton::clicked, this, [this] {
            QString out;
            QStringList h;
            for (int c = 0; c < model_->columnCount(); ++c)
                h << model_->headerData(c, Qt::Horizontal).toString();
            out += h.join(',') + "\n";
            for (int r = 0; r < model_->rowCount(); ++r)
            {
                QStringList cells;
                for (int c = 0; c < model_->columnCount(); ++c)
                    cells << model_->item(r, c)->text();
                out += cells.join(',') + "\n";
            }
            saveText(this, "energy.csv", out);
        });
        watch(false);
        refresh();
    }

protected:
    void refresh() override
    {
        model_->clear();
        const auto* r = doc_->results();
        const auto& m = doc_->model();
        std::vector<std::string> pumps;
        for (const auto& l : m.links())
            if (l.kind == "Pump")
                pumps.push_back(l.id);
        const auto* t = r ? r->table("link") : nullptr;
        const int power = t ? t->variableIndex("Pump Power") : -1;
        const int flow = t ? t->variableIndex("Flow") : -1;
        const int loss = t ? t->variableIndex("Headloss") : -1;
        if (pumps.empty())
        {
            chart_->clear(tr("This network has no pumps."));
            note_->clear();
            return;
        }
        if (!t || power < 0 || flow < 0)
        {
            chart_->clear(tr("Run a simulation with an EPANET engine to compute the energy report."));
            note_->clear();
            return;
        }
        const auto units = m.units();
        const bool us = units.system == core::UnitSystem::US;
        const double toCms = core::flowToCms(units.flow);
        const double headToM = us ? 0.3048 : 1.0;
        // Prices from [ENERGY]
        double globalPrice = 0, demandCharge = 0, globalEff = 75;
        std::map<std::string, double> pumpPrice;
        if (const auto* s = m.document().find("ENERGY"))
        {
            for (int i = 0; i < s->rowCount(); ++i)
            {
                const auto tok = s->rowTokens(i);
                if (tok.size() >= 3 && text::toUpper(tok[0]) == "GLOBAL" && text::toUpper(tok[1]) == "PRICE")
                    globalPrice = text::toDouble(tok[2]).value_or(0.0);
                else if (tok.size() >= 3 && text::toUpper(tok[0]) == "GLOBAL" && text::toUpper(tok[1]).rfind("EFFIC", 0) == 0)
                    globalEff = text::toDouble(tok[2]).value_or(75.0);
                else if (tok.size() >= 3 && text::toUpper(tok[0]) == "DEMAND" && text::toUpper(tok[1]) == "CHARGE")
                    demandCharge = text::toDouble(tok[2]).value_or(0.0);
                else if (tok.size() >= 4 && text::toUpper(tok[0]) == "PUMP" && text::toUpper(tok[2]) == "PRICE")
                    pumpPrice[tok[1]] = text::toDouble(tok[3]).value_or(0.0);
            }
        }
        (void)globalEff;
        const int periods = r->periodCount();
        const double step = periods > 1 ? r->times[1] - r->times[0] : std::max(1.0, r->reportStep);
        const double span = periods * step / 3600.0; // hours covered by the report
        model_->setHorizontalHeaderLabels({tr("Pump"), tr("Utilization (%)"), tr("Average efficiency (%)"),
                                           us ? tr("kWh per million gallons") : tr("kWh per m³"),
                                           tr("Average kW"), tr("Peak kW"), tr("Energy (kWh)"), tr("Cost per day")});
        QVector<ChartWidget::Series> series;
        double totalEnergy = 0, totalCost = 0, peakAll = 0;
        auto cell = [](const QString& v) {
            auto* item = new QStandardItem(v);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        for (const auto& id : pumps)
        {
            const int e = t->elementIndex(id);
            if (e < 0)
                continue;
            int on = 0;
            double energy = 0, volume = 0, waterEnergy = 0, peak = 0;
            ChartWidget::Series s;
            s.name = QString::fromStdString(id);
            for (int p = 0; p < periods; ++p)
            {
                const double kw = std::max(0.0, static_cast<double>(t->value(p, power, e)));
                const double q = std::fabs(t->value(p, flow, e)) * toCms;
                const double h = loss >= 0 ? std::fabs(t->value(p, loss, e)) * headToM : 0.0;
                if (kw > 1e-9 && q > 1e-12)
                    ++on;
                energy += kw * step / 3600.0;
                volume += q * step;
                waterEnergy += 9.81 * q * h * step / 3600.0;
                peak = std::max(peak, kw);
                s.points.append(QPointF(r->times[static_cast<std::size_t>(p)], kw));
            }
            const double price = pumpPrice.count(id) ? pumpPrice[id] : globalPrice;
            const double perDay = span > 0 ? energy * price * 24.0 / span : 0.0;
            const double volumeUnit = us ? volume / 3785.411784 : volume; // million gallons or m3
            const double perVolume = volumeUnit > 0 ? energy / volumeUnit : 0.0;
            const double avgKw = span > 0 ? energy / span : 0.0;
            QList<QStandardItem*> row;
            row << new QStandardItem(QString::fromStdString(id)) << cell(num(100.0 * on / std::max(1, periods), 4))
                << cell(energy > 0 ? num(100.0 * waterEnergy / energy, 4) : "-") << cell(num(perVolume, 5))
                << cell(num(avgKw, 5)) << cell(num(peak, 5)) << cell(num(energy, 6)) << cell(num(perDay, 5));
            model_->appendRow(row);
            series << s;
            totalEnergy += energy;
            totalCost += perDay;
            peakAll += peak;
        }
        QList<QStandardItem*> total;
        QFont bold = font();
        bold.setBold(true);
        total << new QStandardItem(tr("Total")) << cell("") << cell("") << cell("") << cell(num(span > 0 ? totalEnergy / span : 0, 5))
              << cell(num(peakAll, 5)) << cell(num(totalEnergy, 6)) << cell(num(totalCost, 5));
        for (auto* i : total)
            i->setFont(bold);
        model_->appendRow(total);
        table_->resizeColumnsToContents();
        note_->setText(tr("Energy is integrated over the reporting steps (%1). Costs use the GLOBAL PRICE or the pump PRICE "
                          "of the [ENERGY] section; price patterns are not applied. Demand charge: %2 per maximum kW, "
                          "%3 for this run.")
                           .arg(QString::fromStdString(text::formatDuration(step)))
                           .arg(num(demandCharge))
                           .arg(num(demandCharge * peakAll)));
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries(series, tr("Pump power (kW)"), tr("Pump power"));
    }

    void snapshot(ReportSnapshot& s) const override
    {
        addTable(s, model_);
        if (!s.rows.isEmpty())
            s.numeric[0] = false;
        addChart(s, chart_);
        addNote(s, note_);
    }

    QTableView* table_;
    QStandardItemModel* model_;
    ChartWidget* chart_;
    QLabel* note_;
};

// ---------------------------------------------------------------------------
// Status report: the engine's own report file
// ---------------------------------------------------------------------------
class StatusReport : public ReportPage
{
    Q_OBJECT
public:
    StatusReport(ProjectDocument* doc, const QString& file, QWidget* parent) : ReportPage(doc, parent), file_(file)
    {
        QHBoxLayout* actions = nullptr;
        layout_->addWidget(reportHeader(tr("Status report"),
                                        tr("The report written by the simulation engine: warnings, convergence, "
                                           "status changes and summaries."),
                                        this, &actions));
        auto* reload = actionButton("undo", tr("Reload"), this);
        actions->addWidget(reload);
        edit_ = new QPlainTextEdit(this);
        edit_->setLayoutDirection(Qt::LeftToRight); // engine report text
        edit_->setReadOnly(true);
        edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
        layout_->addWidget(edit_, 1);
        connect(reload, &QPushButton::clicked, this, [this] { refresh(); });
        watch(false);
        refresh();
    }

protected:
    void refresh() override
    {
        QFile f(file_);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            edit_->setPlainText(tr("No report yet. Run a simulation first."));
            return;
        }
        edit_->setPlainText(QString::fromUtf8(f.readAll()));
    }

    void snapshot(ReportSnapshot& s) const override { s.text = edit_->toPlainText(); }

    QString file_;
    QPlainTextEdit* edit_;
};
} // namespace

QList<ReportInfo> availableReports(core::Dialect dialect)
{
    QList<ReportInfo> list = {
        {"tables", QObject::tr("Network tables"), "table", QObject::tr("Properties and results of all elements")},
        {"timeseries", QObject::tr("Time series"), "chart", QObject::tr("Results of several elements over time")},
        {"profile", QObject::tr("Profile plot"), "profile", QObject::tr("Elevation and hydraulic grade along a path")},
        {"frequency", QObject::tr("Frequency plot"), "frequency", QObject::tr("Distribution of a result at one time")},
        {"variability", QObject::tr("Variability plot"), "variability", QObject::tr("Spread of a result over time")},
        {"systemflow", QObject::tr("System flow"), "chart", QObject::tr("Supply, demand and storage over time")},
    };
    if (dialect == core::Dialect::Epanet)
        list.append({"energy", QObject::tr("Energy report"), "energy", QObject::tr("Pump energy use and cost")});
    list.append({"calibration", QObject::tr("Calibration"), "calibration",
                 QObject::tr("Simulated against observed data, with goodness of fit")});
    list.append({"scenarios", QObject::tr("Scenario comparison"), "compare",
                 QObject::tr("Results of the base model and its scenarios side by side")});
    list.append({"status", QObject::tr("Status report"), "report", QObject::tr("The engine's report file")});
    return list;
}

ReportSnapshot reportSnapshot(const QString& kind, ProjectDocument* doc, const QString& reportFile,
                              QWidget* page)
{
    ReportSnapshot s;
    s.kind = kind;
    for (const auto& r : availableReports(doc->model().dialect()))
    {
        if (r.kind == kind)
        {
            s.title = r.title;
            s.description = r.description;
        }
    }
    std::unique_ptr<QWidget> temporary;
    if (!page)
    {
        temporary.reset(createReport(kind, doc, reportFile, nullptr));
        page = temporary.get();
    }
    if (auto* report = dynamic_cast<ReportPage*>(page))
    {
        report->snapshot(s);
    }
    else if (page && !scenarioComparisonSnapshot(page, s))
    {
        // Reports built elsewhere (calibration): their notes, first table and charts.
        for (const auto* label : page->findChildren<QLabel*>())
            if (label->objectName() == "muted" && !label->text().trimmed().isEmpty())
                s.notes << label->text().trimmed();
        if (const auto* view = page->findChild<QTableView*>(); view && view->model())
        {
            const QAbstractItemModel* model = view->model();
            for (int c = 0; c < model->columnCount(); ++c)
            {
                s.header << model->headerData(c, Qt::Horizontal).toString();
                bool numeric = model->rowCount() > 0;
                for (int r = 0; r < std::min(model->rowCount(), 20) && numeric; ++r)
                {
                    const QString cell = model->index(r, c).data().toString();
                    numeric = cell.isEmpty() || text::toDouble(cell.toStdString()).has_value();
                }
                s.numeric << numeric;
            }
            for (int r = 0; r < model->rowCount(); ++r)
            {
                QStringList cells;
                for (int c = 0; c < model->columnCount(); ++c)
                    cells << model->index(r, c).data().toString();
                s.rows << cells;
            }
        }
        for (const auto* chart : page->findChildren<ChartWidget*>())
            if (chart->data().hasPoints())
                s.charts.append(chart->data());
        // Charts over time first, then the others (scatter plots).
        std::stable_partition(s.charts.begin(), s.charts.end(),
                              [](const ChartWidget::Data& d) { return d.timeAxis; });
    }
    return s;
}

bool showProfilePath(QWidget* report, const QString& fromNode, const QString& toNode)
{
    auto* profile = dynamic_cast<ProfileReport*>(report);
    if (!profile)
        return false;
    profile->setPath(fromNode, toNode);
    return true;
}

QWidget* createReport(const QString& kind, ProjectDocument* doc, const QString& reportFile, QWidget* parent)
{
    if (kind == "tables")
        return new TableReport(doc, parent);
    if (kind == "timeseries")
        return new TimeSeriesReport(doc, parent);
    if (kind == "profile")
        return new ProfileReport(doc, parent);
    if (kind == "frequency")
        return new FrequencyReport(doc, parent);
    if (kind == "variability")
        return new VariabilityReport(doc, parent);
    if (kind == "systemflow")
        return new SystemFlowReport(doc, parent);
    if (kind == "energy")
        return new EnergyReport(doc, parent);
    if (kind == "status")
        return new StatusReport(doc, reportFile, parent);
    if (kind == "calibration")
        return createCalibrationReport(doc, parent);
    if (kind == "scenarios")
        return createScenarioComparison(doc, parent);
    return nullptr;
}

} // namespace mirab::app

#include "Reports.moc"
