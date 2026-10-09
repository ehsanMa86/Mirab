/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
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
#include "anahita/core/Text.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
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

#include <functional>

namespace anahita::app
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
    connect(doc_, &ProjectDocument::selectionChanged, this, &ResultsPanel::refreshChart);
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
        s.points.append(QPointF(r->times[static_cast<std::size_t>(p)], t->value(p, v, e)));
    const auto& var = t->variables[static_cast<std::size_t>(v)];
    title_->setText(sel.id + "  ·  " + QString::fromStdString(var.name));
    chart_->setXAxis(tr("Time"), true);
    chart_->setSeries({s}, QString::fromStdString(var.name) + (var.unit.empty() ? QString() : " (" + QString::fromStdString(var.unit) + ")"));
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
    DataSectionRole = Qt::UserRole + 2
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
    layout->addWidget(tree_, 1);
    connect(filter_, &QLineEdit::textChanged, this, &ProjectBrowser::applyFilter);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (syncing_ || !item || item->data(0, CategoryRole).isNull())
            return;
        const int cat = item->data(0, CategoryRole).toInt();
        if (cat == kOptionsCategory)
        {
            doc_->select({});
            return;
        }
        if (!item->data(0, DataSectionRole).isNull())
            return;
        doc_->select({static_cast<Category>(cat), item->text(0)});
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
            emit zoomToRequested(doc_->selection());
            emit elementActivated();
        }
    });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ProjectBrowser::showContextMenu);
    connect(doc_, &ProjectDocument::modelChanged, this, &ProjectBrowser::rebuild);
    connect(doc_, &ProjectDocument::modelReset, this, &ProjectBrowser::rebuild);
    connect(doc_, &ProjectDocument::selectionChanged, this, &ProjectBrowser::syncSelection);
    rebuild();
}

void ProjectBrowser::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = tree_->itemAt(pos);
    if (!item || item->data(0, CategoryRole).isNull())
        return;
    QMenu menu(this);
    if (!item->data(0, DataSectionRole).isNull())
    {
        const QString section = item->data(0, DataSectionRole).toString();
        const QString id = item->text(0);
        menu.addAction(icons::get("chart"), tr("Open editor"), this, [this, section, id] { emit dataActivated(section, id); });
    }
    else if (item->data(0, CategoryRole).toInt() != kOptionsCategory)
    {
        tree_->setCurrentItem(item);
        menu.addAction(icons::get("zoom-fit"), tr("Zoom to"), this, [this] { emit zoomToRequested(doc_->selection()); });
        menu.addAction(icons::get("properties"), tr("Properties"), this, &ProjectBrowser::elementActivated);
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
                sub = new QTreeWidgetItem(top, {k});
                sub->setIcon(0, icons::element(k == tr("Rain gage") ? "RainGage" : k));
                sub->setData(0, KindRole, key + "/" + k);
            }
            auto* leaf = new QTreeWidgetItem(sub, {QString::fromStdString(id)});
            leaf->setData(0, CategoryRole, static_cast<int>(cat));
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
    if (sel.isEmpty())
    {
        tree_->setCurrentItem(tree_->topLevelItemCount() > 0 ? tree_->topLevelItem(0) : nullptr);
    }
    else
    {
        const auto found = tree_->findItems(sel.id, Qt::MatchExactly | Qt::MatchRecursive);
        for (auto* item : found)
        {
            if (!item->data(0, CategoryRole).isNull() && item->data(0, DataSectionRole).isNull() &&
                item->data(0, CategoryRole).toInt() == static_cast<int>(sel.category))
            {
                tree_->setCurrentItem(item);
                tree_->scrollToItem(item);
                break;
            }
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

} // namespace anahita::app
