/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ScenarioCompare.cpp
 * Purpose: Scenario comparison report: results of several scenarios side by side.
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
#include "ScenarioCompare.h"

#include "ChartWidget.h"
#include "Icons.h"
#include "Theme.h"
#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace mirab::app
{

namespace
{
using core::Category;

const char* tableOf(Category c)
{
    return c == Category::Link ? "link" : c == Category::Area ? "subcatchment" : "node";
}

QString num(double v, int digits = 5)
{
    return QString::fromStdString(text::formatDouble(std::fabs(v) < 1e-12 ? 0.0 : v, digits));
}

QString signedNum(double v, int digits = 4)
{
    const QString s = num(v, digits);
    return v > 0 && s != "0" ? "+" + s : s;
}

enum class Statistic
{
    Now,
    Maximum,
    Minimum,
    Average
};

/// One result of one element, reduced over time by a statistic.
std::optional<double> reduce(const core::ResultSet& r, const char* table, const std::string& variable,
                             const std::string& id, Statistic stat, double time)
{
    const core::ResultTable* t = r.table(table);
    const int v = t ? t->variableIndex(variable) : -1;
    const int e = t ? t->elementIndex(id) : -1;
    if (v < 0 || e < 0 || r.periodCount() == 0)
        return std::nullopt;
    if (stat == Statistic::Now)
    {
        const auto it = std::lower_bound(r.times.begin(), r.times.end(), time - 0.5);
        if (it == r.times.end() || std::fabs(*it - time) > 0.5)
            return std::nullopt;
        const double x = t->value(static_cast<int>(it - r.times.begin()), v, e);
        return std::isnan(x) ? std::nullopt : std::optional<double>(x);
    }
    double acc = stat == Statistic::Maximum ? -std::numeric_limits<double>::max()
                 : stat == Statistic::Minimum ? std::numeric_limits<double>::max()
                                              : 0.0;
    int n = 0;
    for (int p = 0; p < r.periodCount(); ++p)
    {
        const double x = t->value(p, v, e);
        if (std::isnan(x))
            continue;
        ++n;
        if (stat == Statistic::Maximum)
            acc = std::max(acc, x);
        else if (stat == Statistic::Minimum)
            acc = std::min(acc, x);
        else
            acc += x;
    }
    if (n == 0)
        return std::nullopt;
    return stat == Statistic::Average ? acc / n : acc;
}

QString csvField(QString s)
{
    if (s.contains(',') || s.contains('"'))
        s = '"' + s.replace('"', "\"\"") + '"';
    return s;
}

class ScenarioComparison : public QWidget
{
public:
    ScenarioComparison(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Header, as on the other report pages.
        auto* head = new QWidget(this);
        head->setObjectName("panelHeader");
        head->setAttribute(Qt::WA_StyledBackground, true);
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(14, 10, 14, 10);
        auto* titles = new QVBoxLayout;
        titles->setSpacing(1);
        auto* title = new QLabel(tr("Scenario comparison"), head);
        title->setObjectName("panelTitle");
        auto* subtitle = new QLabel(tr("Results of the base model and its scenarios side by side. The first checked "
                                       "scenario is the reference; differences are the others minus it."),
                                    head);
        subtitle->setObjectName("panelSubtitle");
        subtitle->setWordWrap(true);
        titles->addWidget(title);
        titles->addWidget(subtitle);
        hl->addLayout(titles, 1);
        auto* exportButton = new QPushButton(icons::get("export"), tr("Export CSV"), head);
        exportButton->setIconSize(QSize(16, 16));
        hl->addWidget(exportButton);
        layout->addWidget(head);

        auto* body = new QWidget(this);
        auto* bl = new QVBoxLayout(body);
        bl->setContentsMargins(12, 10, 12, 12);
        auto* split = new QSplitter(Qt::Horizontal, body);
        bl->addWidget(split, 1);
        layout->addWidget(body, 1);

        auto* side = new QWidget(split);
        auto* sl = new QVBoxLayout(side);
        sl->setContentsMargins(0, 0, 8, 0);
        sl->addWidget(new QLabel(tr("Scenarios"), side));
        list_ = new QListWidget(side);
        list_->setMaximumHeight(170);
        sl->addWidget(list_);
        hint_ = new QLabel(side);
        hint_->setObjectName("muted");
        hint_->setWordWrap(true);
        sl->addWidget(hint_);
        category_ = new QComboBox(side);
        category_->addItem(icons::element("Junction"), tr("Nodes"), static_cast<int>(Category::Node));
        category_->addItem(icons::element("Pipe"), tr("Links"), static_cast<int>(Category::Link));
        if (doc_->model().dialect() == core::Dialect::Swmm)
            category_->addItem(icons::element("Subcatchment"), tr("Subcatchments"), static_cast<int>(Category::Area));
        variable_ = new QComboBox(side);
        statistic_ = new QComboBox(side);
        statistic_->addItem(tr("At the time shown on the map"), static_cast<int>(Statistic::Now));
        statistic_->addItem(tr("Maximum over time"), static_cast<int>(Statistic::Maximum));
        statistic_->addItem(tr("Minimum over time"), static_cast<int>(Statistic::Minimum));
        statistic_->addItem(tr("Average over time"), static_cast<int>(Statistic::Average));
        changedOnly_ = new QCheckBox(tr("Only elements that change"), side);
        sl->addSpacing(6);
        sl->addWidget(new QLabel(tr("Result"), side));
        sl->addWidget(category_);
        sl->addWidget(variable_);
        sl->addWidget(statistic_);
        sl->addWidget(changedOnly_);
        sl->addSpacing(6);
        sl->addWidget(new QLabel(tr("Time series of"), side));
        element_ = new QLineEdit(side);
        element_->setPlaceholderText(tr("Element ID"));
        auto* useSelected = new QPushButton(icons::get("select"), tr("Use the selected element"), side);
        sl->addWidget(element_);
        sl->addWidget(useSelected);
        sl->addStretch(1);

        auto* right = new QSplitter(Qt::Vertical, split);
        summary_ = new QTableWidget(right);
        summary_->verticalHeader()->hide();
        summary_->setShowGrid(false);
        summary_->setAlternatingRowColors(true);
        summary_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        summary_->setSelectionMode(QAbstractItemView::NoSelection);
        summary_->horizontalHeader()->setStretchLastSection(true);
        model_ = new QStandardItemModel(this);
        model_->setSortRole(Qt::UserRole);
        proxy_ = new QSortFilterProxyModel(this);
        proxy_->setSourceModel(model_);
        proxy_->setSortRole(Qt::UserRole);
        view_ = new QTableView(right);
        view_->setModel(proxy_);
        view_->setSortingEnabled(true);
        view_->setAlternatingRowColors(true);
        view_->setShowGrid(false);
        view_->verticalHeader()->hide();
        view_->verticalHeader()->setDefaultSectionSize(24);
        view_->setSelectionBehavior(QAbstractItemView::SelectRows);
        view_->setSelectionMode(QAbstractItemView::SingleSelection);
        view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        chart_ = new ChartWidget(right);
        right->addWidget(summary_);
        right->addWidget(view_);
        right->addWidget(chart_);
        right->setSizes({140, 300, 260});
        split->addWidget(side);
        split->addWidget(right);
        split->setStretchFactor(1, 1);
        split->setSizes({250, 900});

        connect(list_, &QListWidget::itemChanged, this, [this] { refresh(); });
        connect(category_, &QComboBox::currentIndexChanged, this, [this] {
            fillVariables();
            refreshTable();
            plot();
        });
        connect(variable_, &QComboBox::currentTextChanged, this, [this] {
            refreshTable();
            plot();
        });
        connect(statistic_, &QComboBox::currentIndexChanged, this, [this] { refreshTable(); });
        connect(changedOnly_, &QCheckBox::toggled, this, [this] { refreshTable(); });
        connect(element_, &QLineEdit::editingFinished, this, [this] { plot(); });
        connect(useSelected, &QPushButton::clicked, this, [this] { followSelection(); });
        connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& idx) {
            if (!idx.isValid())
                return;
            const QString id = proxy_->index(idx.row(), 0).data(Qt::DisplayRole).toString();
            element_->setText(id);
            plot();
            doc_->select({static_cast<Category>(category_->currentData().toInt()), id});
        });
        connect(exportButton, &QPushButton::clicked, this, [this] { exportCsv(); });
        connect(doc_, &ProjectDocument::resultsChanged, this, [this] { refresh(); });
        connect(doc_, &ProjectDocument::scenariosChanged, this, [this] { refresh(); });
        connect(doc_, &ProjectDocument::activeScenarioChanged, this, [this] { refresh(); });
        connect(doc_, &ProjectDocument::modelReset, this, [this] { refresh(); });
        connect(doc_, &ProjectDocument::periodChanged, this, [this] {
            if (static_cast<Statistic>(statistic_->currentData().toInt()) == Statistic::Now)
                refreshTable();
            markTime();
        });
        const Selection s = doc_->selection();
        if (!s.isEmpty() && s.category != Category::Gage)
        {
            const int idx = category_->findData(static_cast<int>(s.category));
            if (idx >= 0)
                category_->setCurrentIndex(idx);
            element_->setText(s.id);
        }
        refresh();
    }

    void snapshot(ReportSnapshot& s) const
    {
        if (runs_.size() < 2)
        {
            s.notes << tr("Run at least two scenarios to compare them (Simulation > Run scenarios).");
            return;
        }
        s.notes << tr("Reference: %1. %2 of %3, %4.")
                       .arg(runs_.front().label, variable_->currentText(), category_->currentText().toLower(),
                            statistic_->currentText().toLower());
        // Key figures, one line per scenario.
        for (int r = 0; r < summary_->rowCount(); ++r)
        {
            QStringList parts;
            for (int c = 1; c < summary_->columnCount(); ++c)
            {
                if (summary_->item(r, c) && summary_->horizontalHeaderItem(c))
                    parts << summary_->horizontalHeaderItem(c)->text() + ": " + summary_->item(r, c)->text();
            }
            if (summary_->item(r, 0))
                s.notes << summary_->item(r, 0)->text() + " - " + parts.join("; ");
        }
        for (int c = 0; c < proxy_->columnCount(); ++c)
        {
            s.header << proxy_->headerData(c, Qt::Horizontal).toString();
            s.numeric << (c > 0);
        }
        for (int r = 0; r < proxy_->rowCount(); ++r)
        {
            QStringList cells;
            for (int c = 0; c < proxy_->columnCount(); ++c)
                cells << proxy_->index(r, c).data().toString();
            s.rows << cells;
        }
        if (chart_->data().hasPoints())
            s.charts.append(chart_->data());
    }

private:
    struct Run
    {
        QString name; ///< empty: the base model
        QString label;
        std::shared_ptr<const core::ResultSet> results;
    };

    void followSelection()
    {
        const Selection s = doc_->selection();
        if (s.isEmpty() || s.category == Category::Gage)
            return;
        const int idx = category_->findData(static_cast<int>(s.category));
        if (idx >= 0)
            category_->setCurrentIndex(idx);
        element_->setText(s.id);
        plot();
    }

    /// Rebuilds the scenario list, keeping the checks, then everything else.
    void refresh()
    {
        std::map<QString, bool> checked;
        for (int i = 0; i < list_->count(); ++i)
            checked[list_->item(i)->data(Qt::UserRole).toString()] = list_->item(i)->checkState() == Qt::Checked;
        const bool first = list_->count() == 0;
        list_->blockSignals(true);
        list_->clear();
        QStringList names = {QString()};
        for (const auto& s : doc_->scenarios())
            names << QString::fromStdString(s.name);
        int withResults = 0;
        for (const QString& n : names)
        {
            auto* item = new QListWidgetItem(icons::get(n.isEmpty() ? "project" : "scenarios"),
                                             n.isEmpty() ? tr("Base model") : n, list_);
            item->setData(Qt::UserRole, n);
            const bool has = doc_->resultsFor(n) != nullptr;
            withResults += has ? 1 : 0;
            item->setFlags(has ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::ItemIsUserCheckable);
            const auto it = checked.find(n);
            const bool on = has && (first || it == checked.end() ? true : it->second);
            item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            if (!has)
                item->setToolTip(tr("Not run yet: use Simulation > Run scenarios"));
        }
        list_->blockSignals(false);
        runs_.clear();
        for (int i = 0; i < list_->count(); ++i)
        {
            QListWidgetItem* item = list_->item(i);
            if (item->checkState() != Qt::Checked)
                continue;
            const QString n = item->data(Qt::UserRole).toString();
            runs_.push_back({n, item->text(), doc_->resultsFor(n)});
        }
        hint_->setText(withResults < 2 ? tr("Run at least two scenarios to compare them (Simulation > Run scenarios).")
                       : runs_.size() < 2 ? tr("Check at least two scenarios.")
                                          : tr("Reference: %1").arg(runs_.front().label));
        fillVariables();
        refreshSummary();
        refreshTable();
        plot();
    }

    void fillVariables()
    {
        const QString keep = variable_->currentText();
        variable_->blockSignals(true);
        variable_->clear();
        const char* table = tableOf(static_cast<Category>(category_->currentData().toInt()));
        QStringList names;
        for (const auto& r : runs_)
        {
            if (const auto* t = r.results ? r.results->table(table) : nullptr)
                for (const auto& v : t->variables)
                {
                    const QString n = QString::fromStdString(v.name);
                    if (!names.contains(n))
                        names << n;
                }
        }
        variable_->addItems(names);
        const bool swmm = doc_->model().dialect() == core::Dialect::Swmm;
        int idx = variable_->findText(keep);
        if (idx < 0)
            idx = variable_->findText(table == std::string("node") ? (swmm ? "Depth" : "Pressure") : "Flow");
        variable_->setCurrentIndex(std::max(0, idx));
        variable_->blockSignals(false);
    }

    /// Key figures: one row per scenario, one column per figure.
    void refreshSummary()
    {
        summary_->clear();
        summary_->setRowCount(0);
        std::vector<std::vector<core::ResultSummaryItem>> figures;
        for (const auto& r : runs_)
            figures.push_back(core::resultSummary(*r.results, doc_->scenarioModel(r.name)));
        if (figures.empty())
        {
            summary_->setColumnCount(0);
            return;
        }
        const auto& ref = figures.front();
        summary_->setColumnCount(static_cast<int>(ref.size()) + 1);
        QStringList headers = {tr("Scenario")};
        for (const auto& f : ref)
            headers << QString::fromStdString(f.name) + (f.unit.empty() ? QString() : " (" + QString::fromStdString(f.unit) + ")");
        summary_->setHorizontalHeaderLabels(headers);
        summary_->setRowCount(static_cast<int>(figures.size()));
        for (std::size_t s = 0; s < figures.size(); ++s)
        {
            const int row = static_cast<int>(s);
            summary_->setItem(row, 0, new QTableWidgetItem(runs_[s].label));
            for (std::size_t i = 0; i < ref.size(); ++i)
            {
                const auto it = std::find_if(figures[s].begin(), figures[s].end(),
                                             [&](const core::ResultSummaryItem& x) { return x.name == ref[i].name; });
                if (it == figures[s].end())
                    continue;
                QString text = num(it->value, 5);
                auto* item = new QTableWidgetItem;
                if (s > 0)
                {
                    const double d = it->value - ref[i].value;
                    text += "  (" + signedNum(d);
                    if (std::fabs(ref[i].value) > 1e-12)
                        text += ", " + signedNum(100.0 * d / std::fabs(ref[i].value), 3) + "%";
                    text += ")";
                    if (std::fabs(d) > 1e-9 * std::max(1.0, std::fabs(ref[i].value)))
                        item->setForeground(d > 0 ? theme::kDanger : theme::kAccent);
                }
                item->setText(text);
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                summary_->setItem(row, static_cast<int>(i) + 1, item);
            }
        }
        summary_->resizeColumnsToContents();
        // Room for every scenario without scrolling.
        summary_->setMinimumHeight(summary_->horizontalHeader()->height() +
                                   summary_->verticalHeader()->defaultSectionSize() * std::min(6, summary_->rowCount()) + 6);
    }

    double currentTime() const
    {
        const auto* r = doc_->results();
        if (r && doc_->period() < r->periodCount())
            return r->times[static_cast<std::size_t>(doc_->period())];
        return !runs_.empty() && !runs_.front().results->times.empty() ? runs_.front().results->times.front() : 0.0;
    }

    void refreshTable()
    {
        model_->clear();
        if (runs_.size() < 2 || variable_->currentText().isEmpty())
        {
            model_->setHorizontalHeaderLabels({tr("Check at least two scenarios with results")});
            return;
        }
        const auto category = static_cast<Category>(category_->currentData().toInt());
        const char* table = tableOf(category);
        const std::string variable = variable_->currentText().toStdString();
        const auto stat = static_cast<Statistic>(statistic_->currentData().toInt());
        const double time = currentTime();
        // Every element of every run, in the order of the reference.
        std::vector<std::string> ids;
        std::map<std::string, bool> seen;
        for (const auto& r : runs_)
        {
            if (const auto* t = r.results->table(table))
                for (const auto& id : t->ids)
                    if (seen.emplace(id, true).second)
                        ids.push_back(id);
        }
        QStringList headers = {tr("ID")};
        for (std::size_t s = 0; s < runs_.size(); ++s)
        {
            headers << runs_[s].label;
            if (s > 0)
                headers << tr("%1 − %2").arg(runs_[s].label, runs_.front().label) << tr("Change %");
        }
        model_->setHorizontalHeaderLabels(headers);
        auto cell = [](const QString& text, const QVariant& sort) {
            auto* item = new QStandardItem(text);
            item->setData(sort, Qt::UserRole);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        const bool changedOnly = changedOnly_->isChecked();
        for (const auto& id : ids)
        {
            QList<QStandardItem*> row;
            auto* idItem = new QStandardItem(QString::fromStdString(id));
            idItem->setData(QString::fromStdString(id), Qt::UserRole);
            row << idItem;
            const auto ref = reduce(*runs_.front().results, table, variable, id, stat, time);
            bool changed = false;
            for (std::size_t s = 0; s < runs_.size(); ++s)
            {
                const auto v = s == 0 ? ref : reduce(*runs_[s].results, table, variable, id, stat, time);
                row << (v ? cell(num(*v), *v) : cell({}, QVariant()));
                if (s == 0)
                    continue;
                if (v && ref)
                {
                    const double d = *v - *ref;
                    changed = changed || std::fabs(d) > 1e-6 * std::max(1.0, std::fabs(*ref));
                    auto* diff = cell(signedNum(d), d);
                    if (std::fabs(d) > 1e-6 * std::max(1.0, std::fabs(*ref)))
                        diff->setForeground(d > 0 ? theme::kDanger : theme::kAccent);
                    row << diff;
                    if (std::fabs(*ref) > 1e-12)
                    {
                        const double pct = 100.0 * d / std::fabs(*ref);
                        row << cell(signedNum(pct, 3), pct);
                    }
                    else
                    {
                        row << cell({}, QVariant());
                    }
                }
                else
                {
                    // In one run only: an element added or removed by a scenario.
                    changed = true;
                    row << cell(tr("n/a"), QVariant()) << cell({}, QVariant());
                }
            }
            if (changedOnly && !changed)
            {
                qDeleteAll(row);
                continue;
            }
            model_->appendRow(row);
        }
        view_->resizeColumnsToContents();
        // Largest increases first.
        view_->sortByColumn(3, Qt::DescendingOrder);
    }

    void plot()
    {
        const QString id = element_->text().trimmed();
        if (runs_.empty() || id.isEmpty())
        {
            chart_->clear(runs_.empty() ? tr("Run scenarios to compare them.")
                                        : tr("Pick an element in the table or on the map."));
            return;
        }
        const char* table = tableOf(static_cast<Category>(category_->currentData().toInt()));
        const std::string variable = variable_->currentText().toStdString();
        QVector<ChartWidget::Series> series;
        QString unit;
        for (std::size_t s = 0; s < runs_.size(); ++s)
        {
            const auto& r = *runs_[s].results;
            const auto* t = r.table(table);
            const int v = t ? t->variableIndex(variable) : -1;
            const int e = t ? t->elementIndex(id.toStdString()) : -1;
            if (v < 0 || e < 0)
                continue;
            unit = QString::fromStdString(t->variables[static_cast<std::size_t>(v)].unit);
            ChartWidget::Series line;
            line.name = runs_[s].label;
            line.dashed = s == 0;
            for (int p = 0; p < r.periodCount(); ++p)
            {
                const double x = t->value(p, v, e);
                if (!std::isnan(x))
                    line.points.append(QPointF(r.times[static_cast<std::size_t>(p)], x));
            }
            series << line;
        }
        if (series.isEmpty())
        {
            chart_->clear(tr("%1 has no %2 results.").arg(id, variable_->currentText()));
            return;
        }
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries(series, variable_->currentText() + (unit.isEmpty() ? QString() : " (" + unit + ")"),
                          tr("%1 in each scenario").arg(id));
        markTime();
    }

    void markTime()
    {
        if (doc_->results() && doc_->period() < doc_->results()->periodCount())
            chart_->setMarker(doc_->results()->times[static_cast<std::size_t>(doc_->period())]);
    }

    void exportCsv()
    {
        QStringList lines;
        QStringList head;
        for (int c = 0; c < summary_->columnCount(); ++c)
            head << csvField(summary_->horizontalHeaderItem(c) ? summary_->horizontalHeaderItem(c)->text() : QString());
        lines << head.join(',');
        for (int r = 0; r < summary_->rowCount(); ++r)
        {
            QStringList row;
            for (int c = 0; c < summary_->columnCount(); ++c)
                row << csvField(summary_->item(r, c) ? summary_->item(r, c)->text() : QString());
            lines << row.join(',');
        }
        lines << QString();
        head.clear();
        for (int c = 0; c < model_->columnCount(); ++c)
            head << csvField(model_->horizontalHeaderItem(c) ? model_->horizontalHeaderItem(c)->text() : QString());
        lines << head.join(',');
        for (int r = 0; r < proxy_->rowCount(); ++r)
        {
            QStringList row;
            for (int c = 0; c < proxy_->columnCount(); ++c)
            {
                // Full precision numbers, as sorted.
                const QVariant v = proxy_->index(r, c).data(Qt::UserRole);
                row << csvField(c > 0 && v.isValid() ? QString::number(v.toDouble(), 'g', 10)
                                                     : proxy_->index(r, c).data(Qt::DisplayRole).toString());
            }
            lines << row.join(',');
        }
        const QString f = QFileDialog::getSaveFileName(this, tr("Export"), "scenario-comparison.csv", tr("CSV (*.csv)"));
        if (f.isEmpty())
            return;
        QFile out(f);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write((lines.join('\n') + '\n').toUtf8());
    }

    ProjectDocument* doc_;
    std::vector<Run> runs_;
    QListWidget* list_;
    QLabel* hint_;
    QComboBox* category_;
    QComboBox* variable_;
    QComboBox* statistic_;
    QCheckBox* changedOnly_;
    QLineEdit* element_;
    QTableWidget* summary_;
    QStandardItemModel* model_;
    QSortFilterProxyModel* proxy_;
    QTableView* view_;
    ChartWidget* chart_;
};
} // namespace

QWidget* createScenarioComparison(ProjectDocument* doc, QWidget* parent)
{
    return new ScenarioComparison(doc, parent);
}

bool scenarioComparisonSnapshot(const QWidget* page, ReportSnapshot& s)
{
    const auto* comparison = dynamic_cast<const ScenarioComparison*>(page);
    if (!comparison)
        return false;
    comparison->snapshot(s);
    return true;
}

} // namespace mirab::app
