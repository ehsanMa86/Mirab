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
#include "MapView.h"
#include "anahita/core/Text.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace anahita::app
{

using core::Category;

// ---------------------------------------------------------------------------
// ResultsPanel
// ---------------------------------------------------------------------------
ResultsPanel::ResultsPanel(ProjectDocument* doc, MapView* map, QWidget* parent)
    : QWidget(parent), doc_(doc), map_(map)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* row = new QHBoxLayout;
    nodeVar_ = new QComboBox(this);
    linkVar_ = new QComboBox(this);
    areaVar_ = new QComboBox(this);
    areaLabel_ = new QLabel(tr("Subcatchments"), this);
    row->addWidget(new QLabel(tr("Nodes"), this));
    row->addWidget(nodeVar_, 1);
    row->addWidget(new QLabel(tr("Links"), this));
    row->addWidget(linkVar_, 1);
    row->addWidget(areaLabel_);
    row->addWidget(areaVar_, 1);
    layout->addLayout(row);

    auto* time = new QHBoxLayout;
    play_ = new QToolButton(this);
    play_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    play_->setCheckable(true);
    play_->setToolTip(tr("Animate the results"));
    slider_ = new QSlider(Qt::Horizontal, this);
    timeLabel_ = new QLabel(tr("No results"), this);
    timeLabel_->setMinimumWidth(110);
    time->addWidget(play_);
    time->addWidget(slider_, 1);
    time->addWidget(timeLabel_);
    layout->addLayout(time);

    auto* chartRow = new QHBoxLayout;
    chartVar_ = new QComboBox(this);
    chartRow->addWidget(new QLabel(tr("Chart"), this));
    chartRow->addWidget(chartVar_, 1);
    layout->addLayout(chartRow);
    chart_ = new ChartWidget(this);
    layout->addWidget(chart_, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(350);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!doc_->results())
            return;
        int p = doc_->period() + 1;
        if (p >= doc_->results()->periodCount())
            p = 0;
        doc_->setPeriod(p);
    });
    connect(play_, &QToolButton::toggled, this, [this](bool on) {
        on ? timer_->start() : timer_->stop();
        play_->setIcon(style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(slider_, &QSlider::valueChanged, doc_, &ProjectDocument::setPeriod);
    connect(doc_, &ProjectDocument::periodChanged, this, [this](int p) {
        slider_->blockSignals(true);
        slider_->setValue(p);
        slider_->blockSignals(false);
        updateTimeLabel();
        if (doc_->results())
            chart_->setMarker(doc_->results()->times[static_cast<std::size_t>(p)]);
    });
    connect(nodeVar_, &QComboBox::currentTextChanged, map_, &MapView::setNodeVariable);
    connect(linkVar_, &QComboBox::currentTextChanged, map_, &MapView::setLinkVariable);
    connect(areaVar_, &QComboBox::currentTextChanged, map_, &MapView::setAreaVariable);
    connect(chartVar_, &QComboBox::currentTextChanged, this, &ResultsPanel::refreshChart);
    connect(doc_, &ProjectDocument::resultsChanged, this, &ResultsPanel::resultsChanged);
    connect(doc_, &ProjectDocument::selectionChanged, this, &ResultsPanel::refreshChart);
    resultsChanged();
}

void ResultsPanel::resultsChanged()
{
    const auto* r = doc_->results();
    auto fill = [&](QComboBox* box, const char* table, const QString& preferred) {
        const QString keep = box->currentText();
        box->blockSignals(true);
        box->clear();
        box->addItem(tr("(none)"));
        if (r)
        {
            if (const auto* t = r->table(table))
            {
                for (const auto& v : t->variables)
                    box->addItem(QString::fromStdString(v.name));
            }
        }
        int idx = box->findText(keep);
        if (idx <= 0)
            idx = box->findText(preferred);
        box->setCurrentIndex(idx > 0 ? idx : 0);
        box->blockSignals(false);
        emit box->currentTextChanged(box->currentText());
    };
    const bool swmm = doc_->model().dialect() == core::Dialect::Swmm;
    fill(nodeVar_, "node", swmm ? "Depth" : "Pressure");
    fill(linkVar_, "link", "Flow");
    fill(areaVar_, "subcatchment", "Runoff");
    areaVar_->setVisible(swmm);
    areaLabel_->setVisible(swmm);
    slider_->setRange(0, r ? std::max(0, r->periodCount() - 1) : 0);
    slider_->setEnabled(r != nullptr);
    play_->setEnabled(r != nullptr);
    if (!r)
        play_->setChecked(false);
    updateTimeLabel();
    refreshChart();
}

void ResultsPanel::updateTimeLabel()
{
    const auto* r = doc_->results();
    if (!r || r->periodCount() == 0)
    {
        timeLabel_->setText(tr("No results"));
        return;
    }
    timeLabel_->setText(tr("%1  (%2 of %3)")
                            .arg(QString::fromStdString(text::formatDuration(r->times[static_cast<std::size_t>(doc_->period())])))
                            .arg(doc_->period() + 1)
                            .arg(r->periodCount()));
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
        const int idx = chartVar_->findText(keep);
        chartVar_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    chartVar_->blockSignals(false);
    if (!t)
    {
        chart_->clear();
        return;
    }
    const int e = t->elementIndex(sel.id.toStdString());
    const int v = t->variableIndex(chartVar_->currentText().toStdString());
    if (e < 0 || v < 0)
    {
        chart_->clear();
        return;
    }
    ChartWidget::Series s;
    s.name = sel.id;
    for (int p = 0; p < r->periodCount(); ++p)
        s.points.append(QPointF(r->times[static_cast<std::size_t>(p)], t->value(p, v, e)));
    const auto& var = t->variables[static_cast<std::size_t>(v)];
    chart_->setSeries({s}, QString::fromStdString(var.name) + (var.unit.empty() ? QString() : " (" + QString::fromStdString(var.unit) + ")"),
                      sel.id);
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
ProjectBrowser::ProjectBrowser(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter by ID"));
    filter_->setClearButtonEnabled(true);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    layout->addWidget(filter_);
    layout->addWidget(tree_);
    connect(filter_, &QLineEdit::textChanged, this, &ProjectBrowser::applyFilter);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (syncing_ || !item || item->data(0, Qt::UserRole).isNull())
            return;
        doc_->select({static_cast<Category>(item->data(0, Qt::UserRole).toInt()), item->text(0)});
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item && !item->data(0, Qt::UserRole).isNull())
            emit elementActivated();
    });
    connect(doc_, &ProjectDocument::modelChanged, this, &ProjectBrowser::rebuild);
    connect(doc_, &ProjectDocument::modelReset, this, &ProjectBrowser::rebuild);
    connect(doc_, &ProjectDocument::selectionChanged, this, &ProjectBrowser::syncSelection);
    rebuild();
}

void ProjectBrowser::rebuild()
{
    syncing_ = true;
    QSet<QString> expanded;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        auto* top = tree_->topLevelItem(i);
        if (top->isExpanded())
            expanded.insert(top->text(0).section(' ', 0, 0));
        for (int j = 0; j < top->childCount(); ++j)
        {
            if (top->child(j)->isExpanded())
                expanded.insert(top->child(j)->text(0).section(' ', 0, 0));
        }
    }
    tree_->clear();
    const auto& m = doc_->model();
    auto group = [&](const QString& name, Category cat, const std::vector<std::pair<std::string, std::string>>& items) {
        if (items.empty())
            return;
        auto* top = new QTreeWidgetItem(tree_, {name + " (" + QString::number(items.size()) + ")"});
        QMap<QString, QTreeWidgetItem*> kinds;
        for (const auto& [kind, id] : items)
        {
            const QString k = QString::fromStdString(kind);
            QTreeWidgetItem*& sub = kinds[k];
            if (!sub)
                sub = new QTreeWidgetItem(top, {k});
            auto* leaf = new QTreeWidgetItem(sub, {QString::fromStdString(id)});
            leaf->setData(0, Qt::UserRole, static_cast<int>(cat));
        }
        for (auto* sub : kinds)
        {
            sub->setText(0, sub->text(0) + " (" + QString::number(sub->childCount()) + ")");
            sub->setExpanded(expanded.contains(sub->text(0).section(' ', 0, 0)));
        }
        top->setExpanded(expanded.isEmpty() || expanded.contains(name));
    };
    std::vector<std::pair<std::string, std::string>> nodes, links, areas, gages;
    for (const auto& n : m.nodes())
        nodes.emplace_back(n.kind, n.id);
    for (const auto& l : m.links())
        links.emplace_back(l.kind, l.id);
    for (const auto& a : m.areas())
        areas.emplace_back("Subcatchment", a.id);
    for (const auto& g : m.gages())
        gages.emplace_back("Rain gage", g.id);
    group(tr("Nodes"), Category::Node, nodes);
    group(tr("Links"), Category::Link, links);
    group(tr("Subcatchments"), Category::Area, areas);
    group(tr("Rain gages"), Category::Gage, gages);
    syncing_ = false;
    applyFilter(filter_->text());
    syncSelection();
}

void ProjectBrowser::applyFilter(const QString& text)
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        auto* top = tree_->topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j)
        {
            auto* sub = top->child(j);
            int visible = 0;
            for (int k = 0; k < sub->childCount(); ++k)
            {
                const bool show = text.isEmpty() || sub->child(k)->text(0).contains(text, Qt::CaseInsensitive);
                sub->child(k)->setHidden(!show);
                visible += show ? 1 : 0;
            }
            sub->setHidden(visible == 0);
            if (!text.isEmpty() && visible > 0)
                sub->setExpanded(true);
        }
    }
}

void ProjectBrowser::syncSelection()
{
    const Selection& sel = doc_->selection();
    syncing_ = true;
    if (sel.isEmpty())
    {
        tree_->setCurrentItem(nullptr);
    }
    else
    {
        const auto found = tree_->findItems(sel.id, Qt::MatchExactly | Qt::MatchRecursive);
        for (auto* item : found)
        {
            if (item->data(0, Qt::UserRole).toInt() == static_cast<int>(sel.category))
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
