/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Panels.cpp
 * Purpose: Implementation of the results, browser and text editor panels.
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
#include "Panels.h"

#include "ChartWidget.h"
#include "Icons.h"
#include "Language.h"
#include "SchemaWidgets.h"
#include "ObservedData.h"
#include "mirab/core/Text.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace mirab::app
{

using core::Category;

// ---------------------------------------------------------------------------
// ResultsPanel
// ---------------------------------------------------------------------------
ResultsPanel::ResultsPanel(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    auto* row = new QHBoxLayout;
    title_ = new QLabel(this);
    title_->setObjectName("panelTitle");
    chartVar_ = new QComboBox(this);
    chartVar_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    chartVar_->setToolTip(tr("Result to plot for the selected element"));
    row->addWidget(title_, 1);
    row->addWidget(chartVar_);
    layout->addLayout(row);
    chart_ = new ChartWidget(this);
    layout->addWidget(chart_, 1);

    connect(doc_, &ProjectDocument::periodChanged, this, [this](int p) {
        if (doc_->results())
            chart_->setMarker(doc_->results()->times[static_cast<std::size_t>(p)]);
    });
    connect(chartVar_, &QComboBox::currentTextChanged, this, &ResultsPanel::refreshChart);
    connect(doc_, &ProjectDocument::resultsChanged, this, &ResultsPanel::refreshChart);
    connect(doc_, &ProjectDocument::liveResultsUpdated, this, &ResultsPanel::plot);
    connect(doc_, &ProjectDocument::selectionChanged, this, &ResultsPanel::refreshChart);
    connect(ObservedData::of(doc_), &ObservedData::changed, this, &ResultsPanel::refreshChart);
    refreshChart();
}

void ResultsPanel::refreshChart()
{
    const auto* r = doc_->results();
    const Selection& sel = doc_->selection();
    const char* table = sel.category == Category::Node   ? "node"
                        : sel.category == Category::Link ? "link"
                        : sel.category == Category::Area ? "subcatchment"
                                                         : nullptr;
    const QString keep = chartVar_->currentText();
    chartVar_->blockSignals(true);
    chartVar_->clear();
    const core::ResultTable* t = (r && table) ? r->table(table) : nullptr;
    if (t)
    {
        for (const auto& v : t->variables)
            chartVar_->addItem(QString::fromStdString(v.name));
        int idx = chartVar_->findText(keep);
        if (idx < 0)
            idx = std::max(0, chartVar_->findText(sel.category == Category::Link ? "Flow" : "Pressure"));
        chartVar_->setCurrentIndex(idx);
    }
    chartVar_->setVisible(t != nullptr);
    chartVar_->blockSignals(false);
    plot();
}

void ResultsPanel::plot()
{
    const auto* r = doc_->results();
    const Selection& sel = doc_->selection();
    const char* table = sel.category == Category::Node   ? "node"
                        : sel.category == Category::Link ? "link"
                        : sel.category == Category::Area ? "subcatchment"
                                                         : nullptr;
    const core::ResultTable* t = (r && table) ? r->table(table) : nullptr;
    if (!r)
    {
        title_->clear();
        chart_->clear(tr("Run a simulation, then select a node or link on the map to plot its results over time."));
        return;
    }
    if (!t)
    {
        title_->clear();
        chart_->clear(tr("Select a node, link or subcatchment on the map to plot its results over time."));
        return;
    }
    const int e = t->elementIndex(sel.id.toStdString());
    const int v = t->variableIndex(chartVar_->currentText().toStdString());
    if (e < 0 || v < 0)
    {
        chart_->clear(tr("No results for %1.").arg(sel.id));
        return;
    }
    ChartWidget::Series s;
    s.name = sel.id;
    for (int p = 0; p < r->periodCount(); ++p)
    {
        const float y = t->value(p, v, e);
        if (!std::isnan(y)) // not available while the simulation runs
            s.points.append(QPointF(r->times[static_cast<std::size_t>(p)], y));
    }
    const auto& var = t->variables[static_cast<std::size_t>(v)];
    if (s.points.isEmpty())
    {
        title_->setText(sel.id + "  ·  " + QString::fromStdString(var.name));
        chart_->clear(
            tr("%1 is available when the simulation ends.").arg(QString::fromStdString(var.name)));
        return;
    }
    title_->setText(sel.id + "  ·  " + QString::fromStdString(var.name));
    QVector<ChartWidget::Series> series = {s};
    // Measurements linked to the element are drawn as markers.
    ChartWidget::Series observed;
    observed.points = ObservedData::of(doc_)->pointsFor(sel.category, sel.id, QString::fromStdString(var.name),
                                                        QString::fromStdString(var.unit));
    if (!observed.points.isEmpty())
    {
        observed.name = tr("Observed");
        observed.style = ChartWidget::Series::Style::Points;
        observed.color = QColor(224, 82, 60);
        series << observed;
    }
    chart_->setXAxis(tr("Time"), true);
    chart_->setSeries(series, QString::fromStdString(var.name) + (var.unit.empty() ? QString() : " (" + QString::fromStdString(var.unit) + ")"));
    chart_->setMarker(r->times[static_cast<std::size_t>(doc_->period())]);
}

QString ResultsPanel::csvForSelection() const
{
    const auto* r = doc_->results();
    const Selection& sel = doc_->selection();
    if (!r || sel.isEmpty())
        return {};
    const char* table = sel.category == Category::Node ? "node" : sel.category == Category::Link ? "link" : "subcatchment";
    const auto* t = r->table(table);
    if (!t)
        return {};
    const int e = t->elementIndex(sel.id.toStdString());
    if (e < 0)
        return {};
    QString out = "time_s";
    for (const auto& v : t->variables)
        out += "," + QString::fromStdString(v.name);
    out += "\n";
    for (int p = 0; p < r->periodCount(); ++p)
    {
        out += QString::fromStdString(text::formatDouble(r->times[static_cast<std::size_t>(p)]));
        for (int v = 0; v < t->variableCount(); ++v)
            out += "," + QString::fromStdString(text::formatDouble(t->value(p, v, e), 7));
        out += "\n";
    }
    return out;
}

// ---------------------------------------------------------------------------
// ProjectBrowser
// ---------------------------------------------------------------------------
namespace
{
enum BrowserRole
{
    CategoryRole = Qt::UserRole,
    KindRole = Qt::UserRole + 1,
    DataSectionRole = Qt::UserRole + 2,
    ObjectKindRole = Qt::UserRole + 3 ///< named-object kind of a collection or its items
};

constexpr int kOptionsCategory = -1;
} // namespace

ProjectBrowser::ProjectBrowser(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
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
    summary_ = new QLabel(head);
    summary_->setObjectName("panelSubtitle");
    summary_->setWordWrap(true);
    filter_ = new QLineEdit(head);
    filter_->setPlaceholderText(tr("Search elements by ID"));
    filter_->setClearButtonEnabled(true);
    filter_->addAction(icons::get("find"), QLineEdit::LeadingPosition);
    hl->addWidget(summary_);
    hl->addWidget(filter_);
    layout->addWidget(head);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setIconSize(QSize(18, 18));
    tree_->setIndentation(14);
    tree_->setAnimated(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(tree_, 1);
    connect(filter_, &QLineEdit::textChanged, this, &ProjectBrowser::applyFilter);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (syncing_)
            return;
        // Elements selected in the tree (Ctrl and Shift extend the selection).
        auto isElement = [](const QTreeWidgetItem* it) {
            return it && !it->data(0, CategoryRole).isNull() && it->data(0, DataSectionRole).isNull() &&
                   it->data(0, CategoryRole).toInt() != kOptionsCategory;
        };
        QList<Selection> list;
        for (auto* it : tree_->selectedItems())
        {
            if (isElement(it))
                list.append({static_cast<Category>(it->data(0, CategoryRole).toInt()), it->text(0)});
        }
        QTreeWidgetItem* current = tree_->currentItem();
        if (list.isEmpty())
        {
            if (current && !current->data(0, CategoryRole).isNull() &&
                current->data(0, CategoryRole).toInt() == kOptionsCategory)
                doc_->select({});
            return;
        }
        Selection cur;
        if (isElement(current) && current->isSelected())
            cur = {static_cast<Category>(current->data(0, CategoryRole).toInt()), current->text(0)};
        doc_->setSelection(list, cur);
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (!item || item->data(0, CategoryRole).isNull())
            return;
        if (!item->data(0, DataSectionRole).isNull())
        {
            emit dataActivated(item->data(0, DataSectionRole).toString(), item->text(0));
            return;
        }
        if (item->data(0, CategoryRole).toInt() != kOptionsCategory)
        {
            emit zoomToRequested({static_cast<Category>(item->data(0, CategoryRole).toInt()), item->text(0)});
            emit elementActivated();
        }
        else
        {
            emit optionsActivated();
        }
    });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ProjectBrowser::showContextMenu);
    connect(doc_, &ProjectDocument::modelChanged, this, &ProjectBrowser::rebuild);
    connect(doc_, &ProjectDocument::modelReset, this, &ProjectBrowser::rebuild);
    connect(doc_, &ProjectDocument::selectionChanged, this, &ProjectBrowser::syncSelection);
    rebuild();
}

void ProjectBrowser::setEngine(const engine::EngineDescriptor* engine)
{
    engine_ = engine;
    rebuild();
}

void ProjectBrowser::revealData(const QString& section, const QString& id)
{
    for (auto* item : tree_->findItems(id, Qt::MatchExactly | Qt::MatchRecursive))
    {
        if (item->data(0, DataSectionRole).toString() != section)
            continue;
        syncing_ = true;
        if (item->parent())
            item->parent()->setExpanded(true);
        tree_->setCurrentItem(item);
        tree_->scrollToItem(item);
        syncing_ = false;
        return;
    }
}

void ProjectBrowser::addObject(const QString& kind)
{
    const std::string k = kind.toStdString();
    const auto& m = doc_->model();
    static const QHash<QString, QString> prefixes = {{"Pollutant", "P"},  {"Landuse", "LU"},    {"Aquifer", "AQ"},
                                                     {"Snowpack", "SP"},  {"Hydrograph", "UH"}, {"Transect", "TR"},
                                                     {"Street", "ST"},    {"Inlet", "IN"},      {"LID", "LID"},
                                                     {"Rule", "R"}};
    const QString suggestion = QString::fromStdString(m.uniqueObjectName(k, prefixes.value(kind, "X").toStdString()));
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New %1").arg(objectKindLabel(kind).toLower()), tr("Name:"),
                                               QLineEdit::Normal, suggestion, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    QString err;
    if (!doc_->edit(tr("Add %1 %2").arg(objectKindLabel(kind).toLower(), name),
                    [&](core::Model& model, std::string* e) { return model.addObject(k, name.toStdString(), e); }, &err))
    {
        emit statusMessage(tr("%1 was not added: %2").arg(name, err));
        return;
    }
    if (const auto* def = core::sectionForObject(m.dialect(), k))
        emit dataActivated(QString::fromStdString(def->name), name);
}

void ProjectBrowser::renameObject(const QString& kind, const QString& name)
{
    bool ok = false;
    const QString to = QInputDialog::getText(this, tr("Rename %1").arg(objectKindLabel(kind).toLower()), tr("New name:"),
                                             QLineEdit::Normal, name, &ok)
                           .trimmed();
    if (!ok || to.isEmpty() || to == name)
        return;
    QString err;
    if (!doc_->edit(tr("Rename %1").arg(name),
                    [&](core::Model& model, std::string* e) {
                        return model.renameObject(kind.toStdString(), name.toStdString(), to.toStdString(), e);
                    },
                    &err))
    {
        emit statusMessage(tr("%1 was not renamed: %2").arg(name, err));
        return;
    }
    if (const auto* def = core::sectionForObject(doc_->model().dialect(), kind.toStdString()))
        emit objectRenamed(QString::fromStdString(def->name), name, to);
}

void ProjectBrowser::deleteObject(const QString& kind, const QString& name)
{
    const auto answer = QMessageBox::question(
        this, tr("Delete %1").arg(objectKindLabel(kind).toLower()),
        tr("Delete %1 %2? Data that needs it (inflows, buildup, coverages, ...) is removed too and optional "
           "references to it are cleared. You can undo this.")
            .arg(objectKindLabel(kind).toLower(), name));
    if (answer != QMessageBox::Yes)
        return;
    doc_->edit(tr("Delete %1").arg(name), [&](core::Model& model, std::string*) {
        return model.removeObject(kind.toStdString(), name.toStdString());
    });
}

void ProjectBrowser::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = tree_->itemAt(pos);
    if (!item)
        return;
    QMenu menu(this);
    const QString objectKind = item->data(0, ObjectKindRole).toString();
    if (item->data(0, CategoryRole).isNull())
    {
        if (objectKind.isEmpty())
            return;
        menu.addAction(icons::get("new"), tr("Add %1...").arg(objectKindLabel(objectKind).toLower()), this,
                       [this, objectKind] { addObject(objectKind); });
    }
    else if (!item->data(0, DataSectionRole).isNull())
    {
        const QString section = item->data(0, DataSectionRole).toString();
        const QString id = item->text(0);
        menu.addAction(icons::get("chart"), tr("Open editor"), this, [this, section, id] { emit dataActivated(section, id); });
        if (!objectKind.isEmpty())
        {
            menu.addAction(tr("Rename..."), this, [this, objectKind, id] { renameObject(objectKind, id); });
            menu.addAction(icons::get("delete"), tr("Delete"), this, [this, objectKind, id] { deleteObject(objectKind, id); });
            menu.addSeparator();
            menu.addAction(icons::get("new"), tr("Add %1...").arg(objectKindLabel(objectKind).toLower()), this,
                           [this, objectKind] { addObject(objectKind); });
        }
    }
    else if (item->data(0, CategoryRole).toInt() != kOptionsCategory)
    {
        const Selection clicked{static_cast<Category>(item->data(0, CategoryRole).toInt()), item->text(0)};
        if (doc_->isSelected(clicked))
            doc_->setCurrent(clicked);
        else
            doc_->select(clicked);
        if (doc_->selectionCount() > 1)
            menu.addAction(icons::get("zoom-fit"), tr("Zoom to selection"), this,
                           &ProjectBrowser::zoomToSelectionRequested);
        else
            menu.addAction(icons::get("zoom-fit"), tr("Zoom to"), this, [this] { emit zoomToRequested(doc_->selection()); });
        menu.addAction(icons::get("properties"), tr("Properties"), this, &ProjectBrowser::elementActivated);
        if (item->data(0, CategoryRole).toInt() == static_cast<int>(Category::Area))
            menu.addAction(icons::get("layers"), tr("LID usage..."), this,
                           [this, id = item->text(0)] { emit dataActivated("LID_USAGE", id); });
        menu.addSeparator();
        menu.addAction(icons::get("delete"), tr("Delete"), this, &ProjectBrowser::deleteRequested);
    }
    if (!menu.isEmpty())
        menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void ProjectBrowser::rebuild()
{
    syncing_ = true;
    QSet<QString> expanded;
    bool any = false;
    std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* it) {
        any = true;
        if (it->isExpanded())
            expanded.insert(it->data(0, KindRole).toString());
        for (int j = 0; j < it->childCount(); ++j)
            collect(it->child(j));
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        collect(tree_->topLevelItem(i));
    tree_->clear();
    elementItems_.clear();
    const auto& m = doc_->model();
    const bool swmm = m.dialect() == core::Dialect::Swmm;

    QFont bold = tree_->font();
    bold.setBold(true);
    auto* options = new QTreeWidgetItem(tree_, {tr("Simulation options")});
    options->setIcon(0, icons::get("options"));
    options->setData(0, CategoryRole, kOptionsCategory);
    options->setToolTip(0, tr("Units, hydraulics, quality and time settings of the project"));

    auto group = [&](const QString& name, const QString& key, const QString& icon, Category cat,
                     const std::vector<std::pair<std::string, std::string>>& items) {
        auto* top = new QTreeWidgetItem(tree_, {name + "  (" + QString::number(items.size()) + ")"});
        top->setIcon(0, icons::get(icon));
        top->setFont(0, bold);
        top->setData(0, KindRole, key);
        QMap<QString, QTreeWidgetItem*> kinds;
        for (const auto& [kind, id] : items)
        {
            const QString k = QString::fromStdString(kind);
            QTreeWidgetItem*& sub = kinds[k];
            if (!sub)
            {
                sub = new QTreeWidgetItem(top, {coreText(kind)});
                sub->setIcon(0, icons::element(k == tr("Rain gage") ? "RainGage" : k));
                sub->setData(0, KindRole, key + "/" + k);
            }
            auto* leaf = new QTreeWidgetItem(sub, {QString::fromStdString(id)});
            leaf->setData(0, CategoryRole, static_cast<int>(cat));
            elementItems_.insert(QString::number(static_cast<int>(cat)) + ":" + leaf->text(0), leaf);
        }
        for (auto* sub : kinds)
        {
            sub->setText(0, sub->text(0) + "  (" + QString::number(sub->childCount()) + ")");
            sub->setExpanded(expanded.contains(sub->data(0, KindRole).toString()) ||
                             (!any && sub->childCount() <= 30));
        }
        top->setExpanded(!any || expanded.contains(key));
    };
    std::vector<std::pair<std::string, std::string>> nodes, links, areas, gages;
    for (const auto& n : m.nodes())
        nodes.emplace_back(n.kind, n.id);
    for (const auto& l : m.links())
        links.emplace_back(l.kind, l.id);
    for (const auto& a : m.areas())
        areas.emplace_back("Subcatchment", a.id);
    for (const auto& g : m.gages())
        gages.emplace_back(tr("Rain gage").toStdString(), g.id);
    if (swmm)
    {
        group(tr("Rain gages"), "gages", "RainGage", Category::Gage, gages);
        group(tr("Subcatchments"), "areas", "Subcatchment", Category::Area, areas);
    }
    group(tr("Nodes"), "nodes", "Junction", Category::Node, nodes);
    group(tr("Links"), "links", swmm ? "Conduit" : "Pipe", Category::Link, links);

    // Data sections: patterns, curves, time series, controls
    auto data = [&](const QString& name, const QString& key, const QString& icon, const char* section) {
        const auto* s = m.document().find(section);
        QStringList ids;
        if (s)
        {
            for (int r = 0; r < s->rowCount(); ++r)
            {
                const auto tokens = s->rowTokens(r);
                if (tokens.empty())
                    continue;
                const QString id = QString::fromStdString(tokens[0]);
                if (!ids.contains(id))
                    ids << id;
            }
        }
        auto* top = new QTreeWidgetItem(tree_, {name + "  (" + QString::number(ids.size()) + ")"});
        top->setIcon(0, icons::get(icon));
        top->setFont(0, bold);
        top->setData(0, KindRole, key);
        for (const QString& id : ids)
        {
            auto* leaf = new QTreeWidgetItem(top, {id});
            leaf->setData(0, CategoryRole, 0);
            leaf->setData(0, DataSectionRole, QString::fromLatin1(section));
            leaf->setIcon(0, icons::get(icon));
        }
        top->setExpanded(expanded.contains(key));
    };
    if (swmm)
    {
        data(tr("Time series"), "timeseries", "chart", "TIMESERIES");
        data(tr("Curves"), "curves", "curve", "CURVES");
        data(tr("Patterns"), "patterns", "pattern", "PATTERNS");
        // Named objects: pollutants, land uses, aquifers, ...
        const QColor grey = palette().color(QPalette::Disabled, QPalette::Text);
        for (const auto& kind : core::objectKinds(m.dialect()))
        {
            const auto* def = core::sectionForObject(m.dialect(), kind);
            if (!def)
                continue;
            const QString k = QString::fromStdString(kind);
            const QString section = QString::fromStdString(def->name);
            const auto names = m.objectNames(kind);
            QString why;
            const bool supported = engineSupportsSection(engine_, m.dialect(), def->name, {}, &why);
            auto* top = new QTreeWidgetItem(tree_, {objectKindLabel(k, true) + "  (" + QString::number(names.size()) + ")"});
            top->setIcon(0, objectKindIcon(k));
            top->setFont(0, bold);
            top->setData(0, KindRole, "objects/" + k);
            top->setData(0, ObjectKindRole, k);
            top->setToolTip(0, supported ? tr("[%1]  Right-click to add one.").arg(section) : why);
            if (!supported)
                top->setForeground(0, grey);
            for (const auto& n : names)
            {
                auto* leaf = new QTreeWidgetItem(top, {QString::fromStdString(n)});
                leaf->setData(0, CategoryRole, 0);
                leaf->setData(0, DataSectionRole, section);
                leaf->setData(0, ObjectKindRole, k);
                leaf->setIcon(0, objectKindIcon(k));
                if (!supported)
                {
                    leaf->setForeground(0, grey);
                    leaf->setToolTip(0, why);
                }
            }
            top->setExpanded(expanded.contains("objects/" + k));
        }
    }
    else
    {
        data(tr("Patterns"), "patterns", "pattern", "PATTERNS");
        data(tr("Curves"), "curves", "curve", "CURVES");
    }

    const auto units = m.units();
    QString summary = tr("%1 model  ·  %2 nodes, %3 links")
                          .arg(swmm ? tr("Stormwater (SWMM)") : tr("Water distribution (EPANET)"))
                          .arg(m.nodes().size())
                          .arg(m.links().size());
    if (swmm)
        summary += tr(", %1 subcatchments").arg(m.areas().size());
    summary += tr("  ·  flow in %1").arg(QString::fromStdString(units.flow));
    summary_->setText(summary);
    syncing_ = false;
    applyFilter(filter_->text());
    syncSelection();
}

void ProjectBrowser::applyFilter(const QString& text)
{
    std::function<int(QTreeWidgetItem*)> filter = [&](QTreeWidgetItem* it) -> int {
        if (it->childCount() == 0)
        {
            const bool leaf = !it->data(0, CategoryRole).isNull();
            const bool show = text.isEmpty() || (leaf && it->text(0).contains(text, Qt::CaseInsensitive));
            it->setHidden(!show);
            return show ? 1 : 0;
        }
        int visible = 0;
        for (int i = 0; i < it->childCount(); ++i)
            visible += filter(it->child(i));
        it->setHidden(!text.isEmpty() && visible == 0);
        if (!text.isEmpty() && visible > 0)
            it->setExpanded(true);
        return visible;
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        auto* top = tree_->topLevelItem(i);
        if (top->data(0, CategoryRole).toInt() == kOptionsCategory && !top->data(0, CategoryRole).isNull())
        {
            top->setHidden(!text.isEmpty());
            continue;
        }
        filter(top);
    }
}

void ProjectBrowser::syncSelection()
{
    const Selection& sel = doc_->selection();
    syncing_ = true;
    auto key = [](const Selection& s) { return QString::number(static_cast<int>(s.category)) + ":" + s.id; };
    if (sel.isEmpty())
    {
        tree_->clearSelection();
        tree_->setCurrentItem(tree_->topLevelItemCount() > 0 ? tree_->topLevelItem(0) : nullptr,
                              0, QItemSelectionModel::NoUpdate);
    }
    else
    {
        QItemSelection selection;
        for (const auto& s : doc_->selectedElements())
        {
            if (auto* item = elementItems_.value(key(s)))
            {
                const QModelIndex index = tree_->indexFromItem(item);
                selection.select(index, index);
            }
        }
        tree_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        if (auto* item = elementItems_.value(key(sel)))
        {
            tree_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
            tree_->scrollToItem(item);
        }
    }
    syncing_ = false;
}

// ---------------------------------------------------------------------------
// Text editors
// ---------------------------------------------------------------------------
InpHighlighter::InpHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}

void InpHighlighter::highlightBlock(const QString& text)
{
    static const QRegularExpression section(R"(^\s*\[[^\]]+\])");
    QTextCharFormat header;
    header.setForeground(QColor(30, 110, 200));
    header.setFontWeight(QFont::Bold);
    QTextCharFormat comment;
    comment.setForeground(QColor(120, 130, 140));
    comment.setFontItalic(true);
    const auto m = section.match(text);
    if (m.hasMatch())
        setFormat(0, m.capturedLength(), header);
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i)
    {
        if (text[i] == '"')
            quoted = !quoted;
        if (!quoted && text[i] == ';')
        {
            setFormat(i, text.size() - i, comment);
            break;
        }
    }
}

InpEditor::InpEditor(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 0);
    state_ = new QLabel(tr("The input file exactly as the engines read it."), this);
    applyButton_ = new QPushButton(tr("Apply changes"), this);
    revertButton_ = new QPushButton(tr("Revert"), this);
    bar->addWidget(state_, 1);
    bar->addWidget(revertButton_);
    bar->addWidget(applyButton_);
    layout->addLayout(bar);
    edit_ = new QPlainTextEdit(this);
    edit_->setLayoutDirection(Qt::LeftToRight); // input file text
    edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    new InpHighlighter(edit_->document());
    layout->addWidget(edit_);
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] {
        if (loading_)
            return;
        dirty_ = true;
        applyButton_->setEnabled(true);
        revertButton_->setEnabled(true);
        state_->setText(tr("Edited; apply to update the model."));
    });
    connect(applyButton_, &QPushButton::clicked, this, &InpEditor::apply);
    connect(revertButton_, &QPushButton::clicked, this, &InpEditor::reload);
    connect(doc_, &ProjectDocument::modelChanged, this, [this] {
        if (!dirty_)
            reload();
    });
    connect(doc_, &ProjectDocument::modelReset, this, &InpEditor::reload);
    reload();
}

void InpEditor::reload()
{
    loading_ = true;
    const int pos = edit_->textCursor().position();
    edit_->setPlainText(QString::fromStdString(doc_->model().toText()));
    QTextCursor c = edit_->textCursor();
    c.setPosition(std::min(pos, edit_->document()->characterCount() - 1));
    edit_->setTextCursor(c);
    loading_ = false;
    dirty_ = false;
    applyButton_->setEnabled(false);
    revertButton_->setEnabled(false);
    state_->setText(tr("The input file exactly as the engines read it."));
}

bool InpEditor::apply()
{
    QString err;
    const bool ok = doc_->replaceText(edit_->toPlainText(), &err);
    if (!ok)
    {
        emit statusMessage(tr("The text could not be applied: %1").arg(err));
        return false;
    }
    dirty_ = false;
    reload();
    emit statusMessage(tr("Input text applied."));
    return true;
}

MsxEditor::MsxEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 0);
    label_ = new QLabel(this);
    auto* saveButton = new QPushButton(tr("Save MSX file"), this);
    bar->addWidget(label_, 1);
    bar->addWidget(saveButton);
    layout->addLayout(bar);
    edit_ = new QPlainTextEdit(this);
    edit_->setLayoutDirection(Qt::LeftToRight); // input file text
    edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    new InpHighlighter(edit_->document());
    layout->addWidget(edit_);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        QString err;
        if (save(&err))
            emit statusMessage(tr("Saved %1").arg(path_));
        else
            emit statusMessage(err);
    });
}

bool MsxEditor::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = tr("Cannot open %1").arg(path);
        return false;
    }
    path_ = path;
    edit_->setPlainText(QString::fromUtf8(f.readAll()));
    edit_->document()->setModified(false);
    label_->setText(QFileInfo(path).fileName());
    return true;
}

bool MsxEditor::save(QString* error)
{
    if (path_.isEmpty())
        return true;
    QFile f(path_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (error)
            *error = tr("Cannot write %1").arg(path_);
        return false;
    }
    f.write(edit_->toPlainText().toUtf8());
    edit_->document()->setModified(false);
    return true;
}

bool MsxEditor::isModified() const
{
    return edit_->document()->isModified();
}

} // namespace mirab::app
