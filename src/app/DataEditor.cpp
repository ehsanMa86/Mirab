/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/DataEditor.cpp
 * Purpose: Editor for time patterns, curves and time series, with a live chart.
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
#include "DataEditor.h"

#include "ChartWidget.h"
#include "Icons.h"
#include "mirab/core/Text.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace mirab::app
{

namespace
{
bool isNumber(const std::string& s)
{
    return text::toDouble(s).has_value();
}

/// Rows of a data section that belong to one ID.
std::vector<std::vector<std::string>> rowsOf(const core::Model& m, const QString& section, const QString& id)
{
    std::vector<std::vector<std::string>> out;
    if (const auto* s = m.document().find(section.toStdString()))
    {
        for (auto& tokens : s->allRowTokens())
        {
            if (!tokens.empty() && tokens[0] == id.toStdString())
                out.push_back(std::move(tokens));
        }
    }
    return out;
}
} // namespace

DataEditor::DataEditor(ProjectDocument* doc, const QString& section, const QString& id, QWidget* parent)
    : QWidget(parent), doc_(doc), section_(section), id_(id)
{
    swmm_ = doc_->model().dialect() == core::Dialect::Swmm;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* head = new QWidget(this);
    head->setObjectName("panelHeader");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(14, 10, 14, 10);
    auto* titles = new QVBoxLayout;
    auto* title = new QLabel(section == "PATTERNS" ? tr("Time pattern %1").arg(id)
                             : section == "TIMESERIES" ? tr("Time series %1").arg(id)
                                                       : tr("Curve %1").arg(id),
                             head);
    title->setObjectName("panelTitle");
    state_ = new QLabel(head);
    state_->setObjectName("panelSubtitle");
    titles->addWidget(title);
    titles->addWidget(state_);
    hl->addLayout(titles, 1);
    curveType_ = new QComboBox(head);
    curveType_->addItems({"STORAGE", "SHAPE", "DIVERSION", "TIDAL", "PUMP1", "PUMP2", "PUMP3", "PUMP4", "PUMP5",
                          "RATING", "CONTROL", "WEIR"});
    curveType_->setVisible(swmm_ && section == "CURVES");
    curveType_->setToolTip(tr("Curve type"));
    hl->addWidget(curveType_);
    auto* addRow = new QPushButton(icons::get("new"), tr("Add row"), head);
    auto* delRow = new QPushButton(icons::get("delete"), tr("Delete row"), head);
    revert_ = new QPushButton(icons::get("undo"), tr("Revert"), head);
    apply_ = new QPushButton(tr("Apply"), head);
    apply_->setProperty("primary", true);
    hl->addWidget(addRow);
    hl->addWidget(delRow);
    hl->addWidget(revert_);
    hl->addWidget(apply_);
    layout->addWidget(head);

    auto* split = new QSplitter(Qt::Horizontal, this);
    table_ = new QTableWidget(split);
    table_->verticalHeader()->setDefaultSectionSize(24);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setAlternatingRowColors(true);
    chart_ = new ChartWidget(split);
    split->addWidget(table_);
    split->addWidget(chart_);
    split->setSizes({320, 700});
    layout->addWidget(split, 1);

    connect(table_, &QTableWidget::itemChanged, this, [this] {
        if (loading_)
            return;
        setDirty(true);
        updateChart();
    });
    connect(curveType_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_)
            setDirty(true);
    });
    connect(addRow, &QPushButton::clicked, this, [this] {
        const int r = table_->currentRow() >= 0 ? table_->currentRow() + 1 : table_->rowCount();
        table_->insertRow(r);
        if (section_ == "PATTERNS")
            table_->setItem(r, 0, new QTableWidgetItem("1"));
        setDirty(true);
        updateChart();
    });
    connect(delRow, &QPushButton::clicked, this, [this] {
        if (table_->currentRow() >= 0)
            table_->removeRow(table_->currentRow());
        setDirty(true);
        updateChart();
    });
    connect(apply_, &QPushButton::clicked, this, &DataEditor::apply);
    connect(revert_, &QPushButton::clicked, this, &DataEditor::load);
    connect(doc_, &ProjectDocument::modelChanged, this, [this] {
        if (!apply_->isEnabled())
            load();
    });
    load();
}

double DataEditor::patternStepHours() const
{
    if (const auto* s = doc_->model().document().find("TIMES"))
    {
        for (const auto& tokens : s->allRowTokens())
        {
            if (tokens.size() >= 3 && text::iequals(tokens[0], "PATTERN") && text::iequals(tokens[1], "TIMESTEP"))
            {
                const std::string unit = tokens.size() >= 4 ? tokens[3] : std::string();
                if (auto v = text::parseClockOrDuration(tokens[2], unit))
                    return *v / 3600.0;
            }
        }
    }
    return 1.0;
}

void DataEditor::setDirty(bool on)
{
    apply_->setEnabled(on);
    revert_->setEnabled(on);
    state_->setText(on ? tr("Edited; Apply writes the changes to the project (undo with Ctrl+Z).")
                       : tr("Double-click a cell to edit. Right-click the chart to copy it."));
}

void DataEditor::load()
{
    loading_ = true;
    const auto rows = rowsOf(doc_->model(), section_, id_);
    table_->clear();
    table_->setRowCount(0);
    auto addRow = [&](const QStringList& cells) {
        const int r = table_->rowCount();
        table_->insertRow(r);
        for (int c = 0; c < cells.size(); ++c)
            table_->setItem(r, c, new QTableWidgetItem(cells[c]));
    };
    if (section_ == "PATTERNS")
    {
        table_->setColumnCount(1);
        table_->setHorizontalHeaderLabels({tr("Multiplier")});
        for (const auto& tokens : rows)
            for (std::size_t i = 1; i < tokens.size(); ++i)
                addRow({QString::fromStdString(tokens[i])});
        QStringList periods;
        const double step = patternStepHours();
        for (int r = 0; r < table_->rowCount(); ++r)
            periods << tr("%1 h").arg(QString::fromStdString(text::formatDouble(r * step, 4)));
        table_->setVerticalHeaderLabels(periods);
    }
    else if (section_ == "TIMESERIES")
    {
        table_->setColumnCount(3);
        table_->setHorizontalHeaderLabels({tr("Date (optional)"), tr("Time"), tr("Value")});
        QString date;
        for (const auto& tokens : rows)
        {
            std::size_t i = 1;
            if (i < tokens.size() && text::iequals(tokens[i], "FILE"))
            {
                state_->setText(tr("This time series is read from a file and is edited outside Mirab."));
                continue;
            }
            while (i < tokens.size())
            {
                if (tokens[i].find('/') != std::string::npos)
                {
                    date = QString::fromStdString(tokens[i]);
                    ++i;
                    continue;
                }
                if (i + 1 >= tokens.size())
                    break;
                addRow({date, QString::fromStdString(tokens[i]), QString::fromStdString(tokens[i + 1])});
                date.clear();
                i += 2;
            }
        }
    }
    else
    {
        table_->setColumnCount(2);
        table_->setHorizontalHeaderLabels({tr("X"), tr("Y")});
        for (const auto& tokens : rows)
        {
            std::size_t i = 1;
            if (swmm_ && i < tokens.size() && !isNumber(tokens[i]))
            {
                const int idx = curveType_->findText(QString::fromStdString(text::toUpper(tokens[i])));
                if (idx >= 0)
                    curveType_->setCurrentIndex(idx);
                ++i;
            }
            for (; i + 1 < tokens.size(); i += 2)
                addRow({QString::fromStdString(tokens[i]), QString::fromStdString(tokens[i + 1])});
        }
    }
    table_->horizontalHeader()->setStretchLastSection(true);
    loading_ = false;
    setDirty(false);
    updateChart();
}

void DataEditor::updateChart()
{
    ChartWidget::Series s;
    s.name = id_;
    auto cell = [&](int r, int c) {
        const auto* it = table_->item(r, c);
        return it ? text::toDouble(it->text().toStdString()) : std::nullopt;
    };
    if (section_ == "PATTERNS")
    {
        const double step = patternStepHours();
        s.style = ChartWidget::Series::Style::Step;
        for (int r = 0; r < table_->rowCount(); ++r)
            if (auto v = cell(r, 0))
                s.points.append(QPointF(r * step * 3600.0, *v));
        if (!s.points.isEmpty())
            s.points.append(QPointF(table_->rowCount() * step * 3600.0, s.points.last().y()));
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries({s}, tr("Multiplier"), tr("Pattern %1").arg(id_));
    }
    else if (section_ == "TIMESERIES")
    {
        for (int r = 0; r < table_->rowCount(); ++r)
        {
            const auto* t = table_->item(r, 1);
            auto v = cell(r, 2);
            if (!t || !v)
                continue;
            if (auto secs = text::parseClockOrDuration(t->text().toStdString()))
                s.points.append(QPointF(*secs, *v));
        }
        chart_->setXAxis(tr("Time"), true);
        chart_->setSeries({s}, tr("Value"), tr("Time series %1").arg(id_));
    }
    else
    {
        for (int r = 0; r < table_->rowCount(); ++r)
        {
            auto x = cell(r, 0), y = cell(r, 1);
            if (x && y)
                s.points.append(QPointF(*x, *y));
        }
        std::sort(s.points.begin(), s.points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        if (s.points.size() == 1)
            s.style = ChartWidget::Series::Style::Points;
        chart_->setXAxis(tr("X"), false);
        chart_->setSeries({s}, tr("Y"), tr("Curve %1").arg(id_));
    }
    if (s.points.isEmpty())
        chart_->clear(tr("Add values to see the chart."));
}

void DataEditor::apply()
{
    std::vector<std::vector<std::string>> rows;
    const std::string id = id_.toStdString();
    auto text = [&](int r, int c) {
        const auto* it = table_->item(r, c);
        return it ? it->text().trimmed().toStdString() : std::string();
    };
    if (section_ == "PATTERNS")
    {
        std::vector<std::string> row = {id};
        for (int r = 0; r < table_->rowCount(); ++r)
        {
            const std::string v = text(r, 0);
            if (v.empty())
                continue;
            row.push_back(v);
            if (row.size() == 7)
            {
                rows.push_back(row);
                row = {id};
            }
        }
        if (row.size() > 1)
            rows.push_back(row);
    }
    else if (section_ == "TIMESERIES")
    {
        for (int r = 0; r < table_->rowCount(); ++r)
        {
            if (text(r, 1).empty() || text(r, 2).empty())
                continue;
            std::vector<std::string> row = {id};
            if (!text(r, 0).empty())
                row.push_back(text(r, 0));
            row.push_back(text(r, 1));
            row.push_back(text(r, 2));
            rows.push_back(row);
        }
    }
    else
    {
        bool first = true;
        for (int r = 0; r < table_->rowCount(); ++r)
        {
            if (text(r, 0).empty() || text(r, 1).empty())
                continue;
            std::vector<std::string> row = {id};
            if (swmm_ && first)
                row.push_back(curveType_->currentText().toStdString());
            row.push_back(text(r, 0));
            row.push_back(text(r, 1));
            rows.push_back(row);
            first = false;
        }
    }
    for (const auto& row : rows)
    {
        for (std::size_t i = 1; i < row.size(); ++i)
        {
            const bool timeColumn = section_ == "TIMESERIES" && i + 1 < row.size();
            const bool typeColumn = swmm_ && section_ == "CURVES" && i == 1 && !isNumber(row[i]);
            if (!timeColumn && !typeColumn && !isNumber(row[i]))
            {
                state_->setText("<span style='color:#c84b4b'>" + tr("\"%1\" is not a number.").arg(QString::fromStdString(row[i])) +
                                "</span>");
                return;
            }
        }
    }
    const std::string section = section_.toStdString();
    QString err;
    const bool ok = doc_->edit(tr("Edit %1").arg(id_), [&](core::Model& m, std::string*) {
        core::InpSection& s = m.document().ensure(section);
        // Replace the rows of this ID where the first one was, keeping comments elsewhere.
        int insertAt = -1;
        for (int i = static_cast<int>(s.lines.size()) - 1; i >= 0; --i)
        {
            const std::string& line = s.lines[static_cast<std::size_t>(i)];
            if (!core::InpSection::isDataLine(line))
                continue;
            const auto tokens = text::splitInpLine(line).tokens;
            if (!tokens.empty() && tokens[0] == id)
            {
                s.lines.erase(s.lines.begin() + i);
                insertAt = i;
            }
        }
        if (insertAt < 0)
        {
            for (const auto& row : rows)
                s.appendRow(row);
        }
        else
        {
            for (std::size_t k = 0; k < rows.size(); ++k)
                s.lines.insert(s.lines.begin() + insertAt + static_cast<std::ptrdiff_t>(k),
                               core::InpSection::formatRow(rows[k], {}));
        }
        m.rebuild();
        return true;
    }, &err);
    if (!ok)
    {
        state_->setText(err);
        return;
    }
    setDirty(false);
    load();
}

} // namespace mirab::app
