/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/SelectionTools.cpp
 * Purpose: Select by query dialog, traces and shortest paths on the project selection.
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
#include "SelectionTools.h"

#include "Icons.h"
#include "Language.h"
#include "mirab/core/Crs.h"
#include "mirab/core/Geometry.h"
#include "mirab/core/Query.h"
#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>

namespace mirab::app
{

using core::Category;

namespace
{
QString periodText(const ProjectDocument* doc)
{
    const auto* r = doc->results();
    if (!r || doc->period() >= r->periodCount())
        return {};
    return QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(doc->period())]));
}
} // namespace

QList<Selection> traceSelection(const ProjectDocument* doc, core::TraceDirection direction,
                                const TraceSettings& settings, QString* summary)
{
    const auto& m = doc->model();
    const auto g = core::NetworkGraph::build(m);
    std::vector<int> nodes, links;
    QList<Selection> starts;
    for (const auto& s : doc->selectedElements())
    {
        const std::string id = s.id.toStdString();
        if (s.category == Category::Node && g.nodeIndex(id) >= 0)
            nodes.push_back(g.nodeIndex(id));
        else if (s.category == Category::Link && g.linkIndex(id) >= 0)
            links.push_back(g.linkIndex(id));
        else if (s.category == Category::Area && direction != core::TraceDirection::Upstream)
        {
            // A subcatchment drains to its outlet node, possibly through other subcatchments.
            std::string outlet = id;
            for (int hops = 0; hops < 1000; ++hops)
            {
                const auto* a = m.findArea(outlet);
                if (!a)
                    break;
                outlet = a->outlet;
            }
            if (g.nodeIndex(outlet) >= 0)
                nodes.push_back(g.nodeIndex(outlet));
        }
        else
            continue;
        starts.append(s);
    }
    if (starts.isEmpty())
    {
        if (summary)
            *summary = QObject::tr("Select a node, link or subcatchment to trace from.");
        return {};
    }

    core::TraceOptions o;
    o.direction = direction;
    o.stopAtClosed = settings.stopAtClosed;
    bool flows = false;
    const auto* r = doc->results();
    const auto* t = r ? r->table("link") : nullptr;
    if (settings.followFlows && t && doc->period() < r->periodCount())
    {
        const int q = t->variableIndex("Flow");
        const int status = t->variableIndex("Status");
        if (q >= 0)
        {
            flows = true;
            o.flows.assign(static_cast<std::size_t>(g.linkCount()), std::nan(""));
        }
        if (status >= 0)
            o.closed.assign(static_cast<std::size_t>(g.linkCount()), false);
        for (int i = 0; i < g.linkCount(); ++i)
        {
            const int e = t->elementIndex(g.linkId(i));
            if (e < 0)
                continue;
            if (q >= 0)
                o.flows[static_cast<std::size_t>(i)] = t->value(doc->period(), q, e);
            if (status >= 0)
            {
                const double v = t->value(doc->period(), status, e);
                o.closed[static_cast<std::size_t>(i)] = std::isfinite(v) ? v < 0.5 : g.link(i).closed;
            }
        }
    }
    const auto result = core::trace(g, nodes, links, o);

    QList<Selection> out = starts;
    std::unordered_set<std::string> reached;
    for (int n : result.nodes)
    {
        out.append({Category::Node, QString::fromStdString(g.nodeId(n))});
        reached.insert(g.nodeId(n));
    }
    for (int l : result.links)
        out.append({Category::Link, QString::fromStdString(g.linkId(l))});
    int areas = 0;
    if (direction != core::TraceDirection::Downstream)
    {
        // Subcatchments that drain to the nodes reached (directly or through other subcatchments).
        std::set<std::string> taken;
        for (bool grew = true; grew;)
        {
            grew = false;
            for (const auto& a : m.areas())
            {
                if (taken.count(a.id) || !(reached.count(a.outlet) || taken.count(a.outlet)))
                    continue;
                taken.insert(a.id);
                out.append({Category::Area, QString::fromStdString(a.id)});
                ++areas;
                grew = true;
            }
        }
    }
    if (summary)
    {
        const QString from = starts.size() == 1 ? starts.first().id : QObject::tr("%n elements", nullptr,
                                                                                 static_cast<int>(starts.size()));
        QString what = QObject::tr("%n node(s)", nullptr, static_cast<int>(result.nodes.size())) + ", " +
                       QObject::tr("%n link(s)", nullptr, static_cast<int>(result.links.size()));
        if (areas > 0)
            what += ", " + QObject::tr("%n subcatchment(s)", nullptr, areas);
        const QString kind = direction == core::TraceDirection::Downstream ? QObject::tr("Downstream of %1: %2")
                             : direction == core::TraceDirection::Upstream ? QObject::tr("Upstream of %1: %2")
                                                                           : QObject::tr("Connected to %1: %2");
        *summary = kind.arg(from, what);
        if (flows && direction != core::TraceDirection::Connected)
            *summary += QObject::tr(" (flow directions at %1)").arg(periodText(doc));
    }
    return out;
}

QList<Selection> shortestPathSelection(const ProjectDocument* doc, QString* message)
{
    QStringList nodes;
    for (const auto& s : doc->selectedElements())
        if (s.category == Category::Node)
            nodes << s.id;
    if (nodes.size() < 2)
    {
        if (message)
            *message = QObject::tr("Select two nodes (the first and the last picked are the ends of the path).");
        return {};
    }
    const auto g = core::NetworkGraph::build(doc->model());
    const auto path =
        core::shortestPath(g, g.nodeIndex(nodes.first().toStdString()), g.nodeIndex(nodes.last().toStdString()));
    if (!path.found())
    {
        if (message)
            *message = QObject::tr("%1 and %2 are not connected.").arg(nodes.first(), nodes.last());
        return {};
    }
    QList<Selection> out;
    for (std::size_t i = 0; i < path.nodes.size(); ++i)
    {
        out.append({Category::Node, QString::fromStdString(g.nodeId(path.nodes[i]))});
        if (i < path.links.size())
            out.append({Category::Link, QString::fromStdString(g.linkId(path.links[i]))});
    }
    if (message)
        *message = QObject::tr("Shortest path from %1 to %2: %n link(s), length %3 %4", nullptr,
                               static_cast<int>(path.links.size()))
                       .arg(nodes.first(), nodes.last(), QString::fromStdString(text::formatDouble(path.length(), 6)),
                            QString::fromStdString(doc->model().units().label("length")));
    return out;
}

double toMapUnits(const ProjectDocument* doc, double distance)
{
    const auto c = crs::parse(doc->settings().crs);
    if (!c || !c->valid())
        return distance;
    const double metres = distance * (doc->model().units().system == core::UnitSystem::US ? 0.3048 : 1.0);
    if (c->isGeographic())
        return metres / 111320.0; // degrees, near enough for a search radius
    return metres / c->unitToMeters;
}

// ---------------------------------------------------------------------------
// Select by query
// ---------------------------------------------------------------------------
QueryDialog::QueryDialog(ProjectDocument* doc, QWidget* parent) : QDialog(parent), doc_(doc)
{
    setWindowTitle(tr("Select by query"));
    setMinimumWidth(520);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Select elements whose properties or results meet conditions, for example "
                                "<i>Diameter &lt; 200 AND Roughness &lt; 100</i>, <i>Pressure &lt; 20</i> or "
                                "<i>ID contains PS</i>. Combine conditions with AND, OR, NOT and parentheses."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    auto* what = new QHBoxLayout;
    category_ = new QComboBox(this);
    category_->addItem(icons::element("Junction"), tr("Nodes"), static_cast<int>(Category::Node));
    category_->addItem(icons::element(doc_->model().dialect() == core::Dialect::Swmm ? "Conduit" : "Pipe"),
                       tr("Links"), static_cast<int>(Category::Link));
    if (doc_->model().dialect() == core::Dialect::Swmm)
    {
        category_->addItem(icons::element("Subcatchment"), tr("Subcatchments"), static_cast<int>(Category::Area));
        category_->addItem(icons::element("RainGage"), tr("Rain gages"), static_cast<int>(Category::Gage));
    }
    category_->addItem(tr("All elements"), static_cast<int>(Category::None));
    kind_ = new QComboBox(this);
    what->addWidget(category_, 1);
    what->addWidget(kind_, 1);
    form->addRow(tr("Find"), what);
    expression_ = new QLineEdit(this);
    expression_->setPlaceholderText(tr("Conditions, e.g. Diameter < 200 AND Roughness < 100 (empty: all)"));
    expression_->setLayoutDirection(Qt::LeftToRight); // expressions use English names
    expression_->setClearButtonEnabled(true);
    form->addRow(tr("Where"), expression_);
    fields_ = new QListWidget(this);
    fields_->setFlow(QListView::LeftToRight);
    fields_->setWrapping(true);
    fields_->setSpacing(2);
    fields_->setMaximumHeight(96);
    fields_->setToolTip(tr("Double-click a field to add it to the conditions."));
    form->addRow(tr("Fields"), fields_);
    resultsNote_ = new QLabel(this);
    resultsNote_->setObjectName("muted");
    resultsNote_->setWordWrap(true);
    form->addRow(QString(), resultsNote_);

    auto* nearRow = new QHBoxLayout;
    near_ = new QCheckBox(tr("Only within"), this);
    distance_ = new QDoubleSpinBox(this);
    distance_->setRange(0, 1e9);
    distance_->setDecimals(2);
    distance_->setValue(doc_->model().units().system == core::UnitSystem::US ? 300 : 100);
    distance_->setSuffix(" " + QString::fromStdString(doc_->model().units().label("length")));
    nearRow->addWidget(near_);
    nearRow->addWidget(distance_);
    nearRow->addWidget(new QLabel(tr("of the current selection"), this), 1);
    near_->setEnabled(doc_->selectionCount() > 0);
    distance_->setEnabled(false);
    form->addRow(QString(), nearRow);

    mode_ = new QComboBox(this);
    mode_->addItem(tr("Replace the selection"));
    mode_->addItem(tr("Add to the selection"));
    mode_->addItem(tr("Remove from the selection"));
    mode_->addItem(tr("Keep only matches in the selection"));
    form->addRow(tr("Result"), mode_);
    layout->addLayout(form);

    preview_ = new QLabel(this);
    preview_->setWordWrap(true);
    layout->addWidget(preview_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* select = buttons->addButton(tr("Select"), QDialogButtonBox::ApplyRole);
    select->setProperty("primary", true);
    select->setDefault(true);
    layout->addWidget(buttons);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, &QueryDialog::updatePreview);
    connect(category_, &QComboBox::currentIndexChanged, this, [this] {
        fillKinds();
        fillFields();
        timer_->start();
    });
    connect(kind_, &QComboBox::currentIndexChanged, this, [this] { timer_->start(); });
    connect(expression_, &QLineEdit::textChanged, this, [this] { timer_->start(); });
    connect(expression_, &QLineEdit::returnPressed, this, &QueryDialog::apply);
    connect(near_, &QCheckBox::toggled, this, [this](bool on) {
        distance_->setEnabled(on);
        timer_->start();
    });
    connect(distance_, &QDoubleSpinBox::valueChanged, this, [this] { timer_->start(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { timer_->start(); });
    connect(fields_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        QString t = expression_->text().trimmed();
        if (!t.isEmpty() && !t.endsWith('('))
            t += " AND ";
        expression_->setText(t + item->text() + " ");
        expression_->setFocus();
    });
    connect(select, &QPushButton::clicked, this, &QueryDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(doc_, &ProjectDocument::selectionChanged, this, [this] {
        near_->setEnabled(doc_->selectionCount() > 0);
        timer_->start();
    });
    connect(doc_, &ProjectDocument::periodChanged, this, [this] {
        fillFields();
        timer_->start();
    });
    connect(doc_, &ProjectDocument::resultsChanged, this, [this] {
        fillFields();
        timer_->start();
    });

    // Start with the kind of the current element.
    const Selection cur = doc_->selection();
    if (!cur.isEmpty())
        category_->setCurrentIndex(std::max(0, category_->findData(static_cast<int>(cur.category))));
    fillKinds();
    fillFields();
    updatePreview();
}

void QueryDialog::setExpression(const QString& text)
{
    expression_->setText(text);
    updatePreview();
}

void QueryDialog::fillKinds()
{
    const auto cat = static_cast<Category>(category_->currentData().toInt());
    kind_->blockSignals(true);
    kind_->clear();
    kind_->addItem(tr("All types"));
    if (cat == Category::Node || cat == Category::Link)
    {
        for (const auto& k : core::elementKinds(doc_->model().dialect(), cat))
            kind_->addItem(icons::element(QString::fromStdString(k)), coreText(k), QString::fromStdString(k));
    }
    kind_->setEnabled(kind_->count() > 1);
    kind_->blockSignals(false);
}

void QueryDialog::fillFields()
{
    fields_->clear();
    const auto cat = static_cast<Category>(category_->currentData().toInt());
    std::vector<std::string> names = {"ID", "Type"};
    if (cat != Category::None)
        names = core::FieldTable(doc_->model(), cat, doc_->results(), doc_->period()).fieldNames();
    for (const auto& n : names)
        fields_->addItem(QString::fromStdString(n));
    const QString at = periodText(doc_);
    resultsNote_->setText(at.isEmpty() ? tr("Run a simulation to query results as well.")
                                       : tr("Result values are those at %1, the time shown on the map.").arg(at));
}

bool QueryDialog::evaluate(QList<Selection>& out, QString* message) const
{
    std::string err;
    const auto query = core::Query::parse(expression_->text().toStdString(), &err);
    if (!query)
    {
        if (message)
            *message = QString::fromStdString(err);
        return false;
    }
    const auto cat = static_cast<Category>(category_->currentData().toInt());
    const std::string kind = kind_->currentIndex() > 0 ? kind_->currentData().toString().toStdString() : std::string();
    // Fields that no element of the category has are probably typing errors.
    if (cat != Category::None)
    {
        const auto names = core::FieldTable(doc_->model(), cat, doc_->results(), doc_->period()).fieldNames();
        for (const auto& f : query->fields())
        {
            std::string name = text::trim(f);
            if (text::istartsWith(name, "result."))
                name = name.substr(7);
            const bool known = text::iequals(name, "Kind") ||
                               std::any_of(names.begin(), names.end(),
                                           [&](const std::string& n) { return text::iequals(n, name); });
            if (!known)
            {
                if (message)
                    *message = tr("Unknown field \"%1\".").arg(QString::fromStdString(f));
                return false;
            }
        }
    }
    const auto found = core::selectByQuery(doc_->model(), *query, cat, kind, doc_->results(), doc_->period());
    std::set<core::ElementRef> nearSet;
    const bool near = near_->isChecked() && near_->isEnabled();
    if (near)
    {
        std::vector<core::ElementRef> from;
        for (const auto& s : doc_->selectedElements())
            from.push_back(s.ref());
        for (const auto& e : core::elementsWithinDistance(doc_->model(), from, toMapUnits(doc_, distance_->value())))
            nearSet.insert(e);
    }
    out.clear();
    for (const auto& e : found)
    {
        if (!near || nearSet.count(e))
            out.append(Selection::of(e));
    }
    return true;
}

void QueryDialog::updatePreview()
{
    QList<Selection> found;
    QString message;
    if (!evaluate(found, &message))
    {
        preview_->setText(message);
        preview_->setStyleSheet("color: #b3261e;");
        expression_->setStyleSheet("border-color: #c84b4b;");
        return;
    }
    expression_->setStyleSheet({});
    preview_->setStyleSheet({});
    preview_->setText(tr("%n element(s) match.", nullptr, static_cast<int>(found.size())) +
                      (doc_->selectionCount() > 0 ? "  " + tr("%n selected now.", nullptr, doc_->selectionCount())
                                                  : QString()));
}

void QueryDialog::apply()
{
    QList<Selection> found;
    QString message;
    if (!evaluate(found, &message))
    {
        updatePreview();
        return;
    }
    switch (mode_->currentIndex())
    {
    case 1:
        doc_->addToSelection(found);
        break;
    case 2:
        doc_->removeFromSelection(found);
        break;
    case 3:
    {
        std::set<core::ElementRef> matched;
        for (const auto& s : found)
            matched.insert(s.ref());
        QList<Selection> keep;
        for (const auto& s : doc_->selectedElements())
            if (matched.count(s.ref()))
                keep.append(s);
        doc_->setSelection(keep, doc_->selection());
        break;
    }
    default:
        doc_->setSelection(found);
        break;
    }
    emit statusMessage(tr("%n element(s) selected.", nullptr, doc_->selectionCount()));
    updatePreview();
}

} // namespace mirab::app
