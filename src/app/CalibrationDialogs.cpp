/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/CalibrationDialogs.cpp
 * Purpose: Dialogs for observed data and automated calibration.
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
#include "CalibrationDialogs.h"

#include "ChartWidget.h"
#include "Icons.h"
#include "ObservedData.h"
#include "SolverProcess.h"
#include "mirab/core/Text.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace mirab::app
{

using core::Category;

namespace
{
QString num(double v, int digits = 5)
{
    if (!std::isfinite(v))
        return QStringLiteral("-");
    return QString::fromStdString(text::formatDouble(std::fabs(v) < 1e-12 ? 0.0 : v, digits));
}

QLabel* intro(const QString& textValue, QWidget* parent)
{
    auto* l = new QLabel(textValue, parent);
    l->setWordWrap(true);
    l->setObjectName("muted");
    return l;
}

QPushButton* iconButton(const QString& icon, const QString& textValue, QWidget* parent)
{
    auto* b = new QPushButton(icons::get(icon), textValue, parent);
    b->setIconSize(QSize(16, 16));
    return b;
}

QStringList tableNames(core::Dialect d)
{
    QStringList t = {"node", "link"};
    if (d == core::Dialect::Swmm)
        t << "subcatchment";
    return t;
}

QString tableLabel(const QString& table)
{
    if (table == "node")
        return QObject::tr("Node");
    if (table == "link")
        return QObject::tr("Link");
    if (table == "subcatchment")
        return QObject::tr("Subcatchment");
    return table;
}

enum LinkColumn
{
    LinkUse,
    LinkColumnName,
    LinkType,
    LinkElement,
    LinkResult,
    LinkUnit,
    LinkWeight,
    LinkColumnCount
};
} // namespace

// ---------------------------------------------------------------------------
// Observed data
// ---------------------------------------------------------------------------
ObservedDataDialog::ObservedDataDialog(ProjectDocument* doc, QWidget* parent)
    : QDialog(parent), doc_(doc), data_(ObservedData::of(doc)), config_(data_->config())
{
    setWindowTitle(tr("Observed data"));
    resize(1080, 700);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro(tr("Measurements to compare with the simulation. Each column of a text table (CSV or "
                               "TSV), or each location of an EPANET or SWMM calibration file, is linked to an "
                               "element and a result. Times are dates and times or hours from the start of the "
                               "simulation."),
                            this));
    auto* split = new QSplitter(Qt::Horizontal, this);

    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 8, 0);
    ll->addWidget(new QLabel(tr("Files"), left));
    sources_ = new QListWidget(left);
    ll->addWidget(sources_, 1);
    auto* addBtn = iconButton("new", tr("Add files..."), left);
    auto* removeBtn = iconButton("delete", tr("Remove"), left);
    auto* lb = new QHBoxLayout;
    lb->addWidget(addBtn);
    lb->addWidget(removeBtn);
    ll->addLayout(lb);

    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 0, 0, 0);
    fileLabel_ = new QLabel(right);
    fileLabel_->setObjectName("panelTitle");
    rl->addWidget(fileLabel_);
    auto* formatRow = new QHBoxLayout;
    format_ = new QComboBox(right);
    format_->addItem(tr("Detect from the file"), "auto");
    format_->addItem(tr("Text table (CSV, TSV)"), "csv");
    format_->addItem(tr("EPANET calibration file"), "epanet");
    format_->addItem(tr("SWMM calibration file"), "swmm");
    formatRow->addWidget(new QLabel(tr("Format"), right));
    formatRow->addWidget(format_);
    formatRow->addStretch(1);
    rl->addLayout(formatRow);

    tableBox_ = new QGroupBox(tr("Time"), right);
    auto* grid = new QGridLayout(tableBox_);
    timeColumn_ = new QComboBox(tableBox_);
    timeFormat_ = new QComboBox(tableBox_);
    timeFormat_->addItem(tr("Detect"), "auto");
    timeFormat_->addItem(tr("Date and time"), "datetime");
    timeFormat_->addItem(tr("Elapsed time"), "elapsed");
    timeUnit_ = new QComboBox(tableBox_);
    timeUnit_->addItem(tr("Hours"), "hours");
    timeUnit_->addItem(tr("Minutes"), "minutes");
    timeUnit_->addItem(tr("Seconds"), "seconds");
    timeUnit_->addItem(tr("Days"), "days");
    dateOrder_ = new QComboBox(tableBox_);
    dateOrder_->addItem(tr("Month first (MM/DD/YYYY)"), "MDY");
    dateOrder_->addItem(tr("Day first (DD/MM/YYYY)"), "DMY");
    noData_ = new QLineEdit(tableBox_);
    noData_->setPlaceholderText(tr("e.g. -9999"));
    grid->addWidget(new QLabel(tr("Time column"), tableBox_), 0, 0);
    grid->addWidget(timeColumn_, 0, 1);
    grid->addWidget(new QLabel(tr("Format"), tableBox_), 0, 2);
    grid->addWidget(timeFormat_, 0, 3);
    grid->addWidget(new QLabel(tr("Elapsed unit"), tableBox_), 1, 0);
    grid->addWidget(timeUnit_, 1, 1);
    grid->addWidget(new QLabel(tr("Dates"), tableBox_), 1, 2);
    grid->addWidget(dateOrder_, 1, 3);
    grid->addWidget(new QLabel(tr("Missing value"), tableBox_), 2, 0);
    grid->addWidget(noData_, 2, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    rl->addWidget(tableBox_);

    rl->addWidget(new QLabel(tr("Series"), right));
    links_ = new QTableWidget(0, LinkColumnCount, right);
    links_->setHorizontalHeaderLabels(
        {tr("Use"), tr("Column or location"), tr("Type"), tr("Element"), tr("Result"), tr("Unit"), tr("Weight")});
    links_->verticalHeader()->hide();
    links_->verticalHeader()->setDefaultSectionSize(26);
    links_->horizontalHeader()->setStretchLastSection(true);
    links_->setSelectionBehavior(QAbstractItemView::SelectRows);
    links_->setSelectionMode(QAbstractItemView::SingleSelection);
    links_->setColumnWidth(LinkUse, 40);
    links_->setColumnWidth(LinkColumnName, 170);
    links_->setColumnWidth(LinkType, 110);
    links_->setColumnWidth(LinkElement, 90);
    links_->setColumnWidth(LinkResult, 130);
    links_->setColumnWidth(LinkUnit, 60);
    rl->addWidget(links_, 2);
    preview_ = new ChartWidget(right);
    preview_->setMinimumHeight(190);
    rl->addWidget(preview_, 3);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({250, 830});
    layout->addWidget(split, 1);

    auto* bottom = new QHBoxLayout;
    start_ = new QLineEdit(this);
    start_->setText(QString::fromStdString(config_.simulationStart));
    start_->setPlaceholderText(doc_->model().dialect() == core::Dialect::Swmm
                                   ? tr("START_DATE and START_TIME of the options")
                                   : tr("the start clock time on the day of the first observation"));
    start_->setToolTip(tr("Calendar date and time at which the simulation starts, e.g. 2024-05-01 00:00. Dated "
                          "observations are placed on the simulation clock with it."));
    bottom->addWidget(new QLabel(tr("Simulation starts at"), this));
    bottom->addWidget(start_, 1);
    layout->addLayout(bottom);
    status_ = new QLabel(this);
    status_->setObjectName("muted");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &ObservedDataDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ObservedDataDialog::reject);
    connect(addBtn, &QPushButton::clicked, this, &ObservedDataDialog::addFiles);
    connect(removeBtn, &QPushButton::clicked, this, &ObservedDataDialog::removeSource);
    connect(sources_, &QListWidget::currentRowChanged, this, [this](int row) {
        storeSource();
        showSource(row);
    });
    auto reread = [this] {
        if (loading_)
            return;
        storeSource();
        fillLinks();
        updatePreview();
    };
    connect(format_, &QComboBox::currentIndexChanged, this, reread);
    connect(timeColumn_, &QComboBox::currentIndexChanged, this, reread);
    connect(timeFormat_, &QComboBox::currentIndexChanged, this, reread);
    connect(timeUnit_, &QComboBox::currentIndexChanged, this, reread);
    connect(dateOrder_, &QComboBox::currentIndexChanged, this, reread);
    connect(noData_, &QLineEdit::editingFinished, this, reread);
    connect(start_, &QLineEdit::editingFinished, this, [this] { updatePreview(); });
    connect(links_, &QTableWidget::itemChanged, this, [this] {
        if (loading_)
            return;
        storeSource();
        updatePreview();
    });
    connect(links_, &QTableWidget::currentCellChanged, this, [this] { updatePreview(); });

    for (const auto& s : config_.observed)
        sources_->addItem(QFileInfo(QString::fromStdString(s.file)).fileName());
    if (!config_.observed.empty())
        sources_->setCurrentRow(0);
    else
        showSource(-1);
}

QString ObservedDataDialog::absolutePath(const QString& stored) const
{
    if (QDir::isAbsolutePath(stored) || data_->baseDir().isEmpty())
        return stored;
    return QDir(data_->baseDir()).filePath(stored);
}

QString ObservedDataDialog::fileText(const core::ObservedSource& source)
{
    const QString path = absolutePath(QString::fromStdString(source.file));
    if (!texts_.contains(path))
    {
        QFile f(path);
        texts_[path] = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    return texts_[path];
}

QStringList ObservedDataDialog::variablesFor(const QString& table) const
{
    QStringList out;
    if (const auto* r = doc_->results())
    {
        if (const auto* t = r->table(table.toStdString()))
        {
            for (const auto& v : t->variables)
                out << QString::fromStdString(v.name);
        }
    }
    if (!out.isEmpty())
        return out;
    const bool swmm = doc_->model().dialect() == core::Dialect::Swmm;
    if (table == "node")
        return swmm ? QStringList{"Depth", "Head", "Total Inflow", "Lateral Inflow", "Flooding", "Volume"}
                    : QStringList{"Pressure", "Head", "Demand", "Quality"};
    if (table == "link")
        return swmm ? QStringList{"Flow", "Depth", "Velocity", "Volume", "Capacity"}
                    : QStringList{"Flow", "Velocity", "Headloss", "Quality"};
    return {"Runoff", "Rainfall", "Infiltration Loss", "Groundwater Flow"};
}

void ObservedDataDialog::addFiles()
{
    const QString dir = data_->baseDir().isEmpty() ? QDir::homePath() : data_->baseDir();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add observed data"), dir,
        tr("Observed data (*.csv *.tsv *.txt *.dat);;All files (*)"));
    storeSource();
    for (const QString& f : files)
    {
        core::ObservedSource src;
        src.file = data_->storedPath(f).toStdString();
        config_.observed.push_back(src);
        sources_->addItem(QFileInfo(f).fileName());
    }
    if (!files.isEmpty())
        sources_->setCurrentRow(sources_->count() - 1);
}

void ObservedDataDialog::removeSource()
{
    const int row = sources_->currentRow();
    if (row < 0 || row >= static_cast<int>(config_.observed.size()))
        return;
    current_ = -1;
    config_.observed.erase(config_.observed.begin() + row);
    delete sources_->takeItem(row);
    showSource(sources_->currentRow());
}

void ObservedDataDialog::showSource(int index)
{
    current_ = index;
    const bool valid = index >= 0 && index < static_cast<int>(config_.observed.size());
    for (QWidget* w : std::initializer_list<QWidget*>{format_, tableBox_, links_})
        w->setEnabled(valid);
    if (!valid)
    {
        fileLabel_->setText(tr("Add a CSV table or a calibration file to start."));
        loading_ = true;
        links_->setRowCount(0);
        loading_ = false;
        preview_->clear(tr("No observed data."));
        return;
    }
    const auto& src = config_.observed[static_cast<std::size_t>(index)];
    fileLabel_->setText(QString::fromStdString(src.file));
    loading_ = true;
    format_->setCurrentIndex(std::max(0, format_->findData(QString::fromStdString(src.format))));
    timeFormat_->setCurrentIndex(std::max(0, timeFormat_->findData(QString::fromStdString(src.timeFormat))));
    timeUnit_->setCurrentIndex(std::max(0, timeUnit_->findData(QString::fromStdString(src.timeUnit))));
    dateOrder_->setCurrentIndex(std::max(0, dateOrder_->findData(QString::fromStdString(src.dateOrder))));
    noData_->setText(QString::fromStdString(src.noData));
    // Columns of a text table, for the time column choice.
    timeColumn_->clear();
    timeColumn_->addItem(tr("First column"), QString());
    const QString content = fileText(src);
    if (src.resolvedFormat(content.toStdString()) == "csv")
    {
        for (const auto& h : core::readDelimited(content.toStdString()).header)
            timeColumn_->addItem(QString::fromStdString(h), QString::fromStdString(h));
    }
    timeColumn_->setCurrentIndex(std::max(0, timeColumn_->findData(QString::fromStdString(src.timeColumn))));
    loading_ = false;
    fillLinks();
    storeSource();
    updatePreview();
}

void ObservedDataDialog::fillLinks()
{
    if (current_ < 0 || current_ >= static_cast<int>(config_.observed.size()))
        return;
    const auto& src = config_.observed[static_cast<std::size_t>(current_)];
    const std::string content = fileText(src).toStdString();
    const std::string format = src.resolvedFormat(content);
    tableBox_->setVisible(format == "csv");
    // Every value column or location of the file.
    core::ObservedSource plain = src;
    plain.links.clear();
    std::vector<std::string> columns;
    QString problem;
    try
    {
        const auto series =
            format == "csv" ? core::readObservedTable(content, plain) : core::readCalibrationFile(content, plain);
        for (const auto& s : series)
            columns.push_back(format == "csv" ? s.name : s.element);
    }
    catch (const std::exception& e)
    {
        problem = QString::fromUtf8(e.what());
    }
    if (content.empty())
        problem = tr("The file cannot be read.");
    const auto& model = doc_->model();
    loading_ = true;
    links_->setRowCount(0);
    for (const auto& c : columns)
    {
        core::ObservedLink link;
        bool found = false;
        for (const auto& l : src.links)
        {
            if (l.column == c)
            {
                link = l;
                found = true;
            }
        }
        if (!found)
        {
            link = core::guessLink(c, model);
            link.column = c;
            if (format != "csv")
            {
                if (link.element.empty())
                    link.element = c;
                if (!src.table.empty())
                    link.table = src.table;
                if (!src.variable.empty())
                    link.variable = src.variable;
                if (!src.unit.empty())
                    link.unit = src.unit;
            }
            if (link.table.empty())
                link.table = "node";
            if (link.variable.empty())
                link.variable = core::defaultVariable(model.dialect(), link.table);
            link.enabled = !link.element.empty();
        }
        const int row = links_->rowCount();
        links_->insertRow(row);
        auto* use = new QTableWidgetItem;
        use->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        use->setCheckState(link.enabled ? Qt::Checked : Qt::Unchecked);
        links_->setItem(row, LinkUse, use);
        auto* name = new QTableWidgetItem(QString::fromStdString(c));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        links_->setItem(row, LinkColumnName, name);
        auto* type = new QComboBox(links_);
        for (const QString& t : tableNames(model.dialect()))
            type->addItem(tableLabel(t), t);
        type->setCurrentIndex(std::max(0, type->findData(QString::fromStdString(link.table))));
        links_->setCellWidget(row, LinkType, type);
        links_->setItem(row, LinkElement, new QTableWidgetItem(QString::fromStdString(link.element)));
        auto* result = new QComboBox(links_);
        result->setEditable(true);
        result->addItems(variablesFor(QString::fromStdString(link.table)));
        result->setCurrentText(QString::fromStdString(link.variable));
        links_->setCellWidget(row, LinkResult, result);
        links_->setItem(row, LinkUnit, new QTableWidgetItem(QString::fromStdString(link.unit)));
        links_->setItem(row, LinkWeight, new QTableWidgetItem(num(link.weight)));
        connect(type, &QComboBox::currentIndexChanged, this, [this, type, result] {
            const QString keep = result->currentText();
            result->blockSignals(true);
            result->clear();
            result->addItems(variablesFor(type->currentData().toString()));
            result->setCurrentText(keep);
            result->blockSignals(false);
            if (!loading_)
            {
                storeSource();
                updatePreview();
            }
        });
        connect(result, &QComboBox::currentTextChanged, this, [this] {
            if (!loading_)
            {
                storeSource();
                updatePreview();
            }
        });
    }
    loading_ = false;
    if (links_->rowCount() > 0)
        links_->setCurrentCell(0, LinkColumnName);
    status_->setText(problem);
}

void ObservedDataDialog::storeSource()
{
    if (loading_ || current_ < 0 || current_ >= static_cast<int>(config_.observed.size()))
        return;
    auto& src = config_.observed[static_cast<std::size_t>(current_)];
    src.format = format_->currentData().toString().toStdString();
    src.timeColumn = timeColumn_->currentData().toString().toStdString();
    src.timeFormat = timeFormat_->currentData().toString().toStdString();
    src.timeUnit = timeUnit_->currentData().toString().toStdString();
    src.dateOrder = dateOrder_->currentData().toString().toStdString();
    src.noData = noData_->text().trimmed().toStdString();
    std::vector<core::ObservedLink> links;
    for (int r = 0; r < links_->rowCount(); ++r)
    {
        core::ObservedLink l;
        l.enabled = links_->item(r, LinkUse)->checkState() == Qt::Checked;
        l.column = links_->item(r, LinkColumnName)->text().toStdString();
        if (auto* type = qobject_cast<QComboBox*>(links_->cellWidget(r, LinkType)))
            l.table = type->currentData().toString().toStdString();
        l.element = links_->item(r, LinkElement)->text().trimmed().toStdString();
        if (auto* result = qobject_cast<QComboBox*>(links_->cellWidget(r, LinkResult)))
            l.variable = result->currentText().trimmed().toStdString();
        l.unit = links_->item(r, LinkUnit)->text().trimmed().toStdString();
        l.weight = text::toDouble(links_->item(r, LinkWeight)->text().toStdString()).value_or(1.0);
        links.push_back(l);
    }
    if (!links.empty() || links_->rowCount() == 0)
        src.links = links;
}

void ObservedDataDialog::updatePreview()
{
    const int row = links_->currentRow();
    if (current_ < 0 || current_ >= static_cast<int>(config_.observed.size()) || row < 0)
    {
        preview_->clear(tr("Choose a series to preview it."));
        return;
    }
    core::ObservedSource one = config_.observed[static_cast<std::size_t>(current_)];
    if (row >= static_cast<int>(one.links.size()))
        return;
    core::ObservedLink link = one.links[static_cast<std::size_t>(row)];
    link.enabled = true;
    one.links = {link};
    std::vector<std::string> warnings;
    std::vector<core::ObservedSeries> series;
    try
    {
        series = core::loadObservedSource(one, std::filesystem::u8path(data_->baseDir().toStdString()),
                                          &doc_->model(), &warnings);
    }
    catch (const std::exception& e)
    {
        warnings.push_back(e.what());
    }
    QStringList notes;
    for (const auto& w : warnings)
        notes << QString::fromStdString(w);
    if (series.empty())
    {
        preview_->clear(tr("No values in this series."));
        status_->setText(notes.join("\n"));
        return;
    }
    const auto& s = series.front();
    const auto base = core::timeBaseFor(&doc_->model(), doc_->results(), start_->text().trimmed().toStdString());
    const auto times = core::simulationTimes(s, base);
    ChartWidget::Series obs;
    obs.name = tr("Observed");
    obs.style = ChartWidget::Series::Style::Points;
    obs.color = QColor(224, 82, 60);
    int valid = 0;
    for (std::size_t i = 0; i < times.size() && i < s.values.size(); ++i)
    {
        if (std::isfinite(s.values[i]))
        {
            obs.points.append(QPointF(times[i], s.values[i]));
            ++valid;
        }
    }
    QVector<ChartWidget::Series> plot;
    QString unit = QString::fromStdString(s.unit);
    if (const auto* r = doc_->results())
    {
        if (const auto sim = core::simulatedSeries(*r, s.table, s.element, s.variable))
        {
            ChartWidget::Series line;
            line.name = tr("Simulated");
            line.color = QColor(31, 111, 235);
            const double factor = core::unitFactor(s.unit, sim->unit).value_or(1.0);
            for (auto& p : obs.points)
                p.setY(p.y() * factor);
            for (std::size_t i = 0; i < sim->times.size(); ++i)
                line.points.append(QPointF(sim->times[i], sim->values[i]));
            plot << line;
            unit = QString::fromStdString(sim->unit);
        }
    }
    plot << obs;
    preview_->setXAxis(tr("Time"), true);
    preview_->setSeries(plot, QString::fromStdString(s.variable) + (unit.isEmpty() ? QString() : " (" + unit + ")"),
                        QString::fromStdString(s.element + " " + s.variable));
    QString info = tr("%1 values, %2 missing").arg(valid).arg(static_cast<int>(s.values.size()) - valid);
    if (s.calendar && !s.times.empty())
        info += tr(", from %1 to %2").arg(QString::fromStdString(core::formatDateTime(s.times.front())),
                                          QString::fromStdString(core::formatDateTime(s.times.back())));
    if (!doc_->model().findNode(s.element) && !doc_->model().findLink(s.element) && !doc_->model().findArea(s.element))
        notes.prepend(tr("The model has no element %1.").arg(QString::fromStdString(s.element)));
    status_->setText(info + (notes.isEmpty() ? QString() : "\n" + notes.join("\n")));
}

void ObservedDataDialog::accept()
{
    storeSource();
    config_.simulationStart = start_->text().trimmed().toStdString();
    if (!config_.simulationStart.empty() && !core::parseDateTime(config_.simulationStart))
    {
        QMessageBox::warning(this, tr("Observed data"),
                             tr("The simulation start is not a date and time, e.g. 2024-05-01 00:00."));
        return;
    }
    data_->setConfig(config_);
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------
namespace
{
enum ParamColumn
{
    ParamName,
    ParamSection,
    ParamColumnName,
    ParamElements,
    ParamMode,
    ParamMin,
    ParamMax,
    ParamRows,
    ParamColumnCount
};
} // namespace

CalibrationDialog::CalibrationDialog(ProjectDocument* doc, const QString& engineId, const QString& msxFile,
                                     QWidget* parent)
    : QDialog(parent), doc_(doc), data_(ObservedData::of(doc)), engineId_(engineId), msxFile_(msxFile)
{
    setWindowTitle(tr("Calibrate"));
    resize(1220, 700);
    solver_ = new SolverProcess(this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro(tr("Adjusts model inputs to match the observed data. Each run writes a modified copy "
                               "of the model and simulates it in a separate process; several runs go in "
                               "parallel. The search is Dynamically Dimensioned Search (Tolson and Shoemaker, "
                               "2007). Lower objective values are better."),
                            this));
    auto* split = new QSplitter(Qt::Horizontal, this);
    auto* tabs = new QTabWidget(split);

    // Parameters
    auto* ptab = new QWidget(tabs);
    auto* pl = new QVBoxLayout(ptab);
    params_ = new QTableWidget(0, ParamColumnCount, ptab);
    params_->setHorizontalHeaderLabels({tr("Name"), tr("Section"), tr("Column"), tr("Elements"), tr("Change"),
                                        tr("Min"), tr("Max"), tr("Rows")});
    params_->verticalHeader()->hide();
    params_->verticalHeader()->setDefaultSectionSize(26);
    params_->setSelectionBehavior(QAbstractItemView::SelectRows);
    params_->horizontalHeader()->setMinimumSectionSize(40);
    params_->horizontalHeader()->setSectionResizeMode(ParamName, QHeaderView::Stretch);
    params_->setColumnWidth(ParamSection, 120);
    params_->setColumnWidth(ParamColumnName, 118);
    params_->setColumnWidth(ParamElements, 74);
    params_->setColumnWidth(ParamMode, 102);
    params_->setColumnWidth(ParamMin, 54);
    params_->setColumnWidth(ParamMax, 54);
    params_->setColumnWidth(ParamRows, 50);
    pl->addWidget(params_, 1);
    auto* pb = new QHBoxLayout;
    auto* addParam = iconButton("new", tr("Add"), ptab);
    auto* removeParam = iconButton("delete", tr("Remove"), ptab);
    auto* useSel = new QPushButton(tr("Use selected element"), ptab);
    useSel->setToolTip(tr("Limit the parameter to the element selected on the map"));
    pb->addWidget(addParam);
    pb->addWidget(removeParam);
    pb->addWidget(useSel);
    pb->addStretch(1);
    pl->addLayout(pb);
    pl->addWidget(intro(tr("Elements: \"all\" or IDs separated by commas. Multiply scales the current values "
                           "(the range is a factor); replace sets the value itself."),
                        ptab));
    tabs->addTab(ptab, tr("Parameters"));

    // Objective and run
    auto* otab = new QWidget(tabs);
    auto* ol = new QVBoxLayout(otab);
    terms_ = new QTableWidget(0, 2, otab);
    terms_->setHorizontalHeaderLabels({tr("Metric"), tr("Weight")});
    terms_->verticalHeader()->hide();
    terms_->verticalHeader()->setDefaultSectionSize(26);
    terms_->horizontalHeader()->setStretchLastSection(true);
    terms_->setColumnWidth(0, 200);
    terms_->setMaximumHeight(150);
    ol->addWidget(new QLabel(tr("Objective: weighted sum over every observed series"), otab));
    ol->addWidget(terms_);
    auto* tb = new QHBoxLayout;
    auto* addTermBtn = iconButton("new", tr("Add"), otab);
    auto* removeTermBtn = iconButton("delete", tr("Remove"), otab);
    tb->addWidget(addTermBtn);
    tb->addWidget(removeTermBtn);
    tb->addStretch(1);
    ol->addLayout(tb);
    ol->addWidget(intro(tr("NSE, KGE and R² count as 1 minus the value; PBIAS, peak and volume errors as their "
                           "size in percent divided by 100; RMSE and MAE in result units; peak timing in hours. "
                           "Series weights are set in Observed data."),
                        otab));
    auto* form = new QFormLayout;
    budget_ = new QSpinBox(otab);
    budget_->setRange(1, 100000);
    jobs_ = new QSpinBox(otab);
    jobs_->setRange(1, 64);
    seed_ = new QSpinBox(otab);
    seed_->setRange(1, 1000000);
    method_ = new QComboBox(otab);
    method_->addItem(tr("Search for the best values (DDS)"), "dds");
    method_->addItem(tr("Sensitivity screening (Latin hypercube)"), "lhs");
    form->addRow(tr("Model runs"), budget_);
    form->addRow(tr("Runs in parallel"), jobs_);
    form->addRow(tr("Random seed"), seed_);
    form->addRow(tr("Method"), method_);
    ol->addLayout(form);
    ol->addStretch(1);
    tabs->addTab(otab, tr("Objective and runs"));

    // Progress and results
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 0, 0, 0);
    status_ = new QLabel(tr("Set up the parameters, then press Start."), right);
    status_->setWordWrap(true);
    progress_ = new QProgressBar(right);
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    chart_ = new ChartWidget(right);
    chart_->setMinimumHeight(240);
    chart_->clear(tr("The objective of each run appears here."));
    bestTable_ = new QTableWidget(0, 2, right);
    bestTable_->setHorizontalHeaderLabels({tr("Parameter"), tr("Best value")});
    bestTable_->verticalHeader()->hide();
    bestTable_->horizontalHeader()->setStretchLastSection(true);
    bestTable_->setColumnWidth(0, 220);
    bestTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bestTable_->setMaximumHeight(140);
    rl->addWidget(status_);
    rl->addWidget(progress_);
    rl->addWidget(chart_, 1);
    rl->addWidget(bestTable_);
    split->addWidget(tabs);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({690, 530});
    layout->addWidget(split, 1);

    auto* buttons = new QHBoxLayout;
    startBtn_ = iconButton("run", tr("Start"), this);
    startBtn_->setProperty("primary", true);
    cancelBtn_ = iconButton("stop", tr("Cancel run"), this);
    applyBtn_ = new QPushButton(tr("Apply best parameters"), this);
    applyBtn_->setToolTip(tr("Write the best values into the model as one edit that can be undone"));
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addWidget(startBtn_);
    buttons->addWidget(cancelBtn_);
    buttons->addStretch(1);
    buttons->addWidget(applyBtn_);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    cancelBtn_->setEnabled(false);
    applyBtn_->setEnabled(false);

    // Set-up saved with the project
    const auto cfg = data_->config();
    for (const auto& p : cfg.parameters)
        addParameter(p);
    for (const auto& t : cfg.objective)
        addTerm(t);
    if (cfg.objective.empty())
        addTerm({core::Metric::Nse, 1.0});
    budget_->setValue(cfg.budget > 0 ? cfg.budget : 100);
    jobs_->setValue(cfg.jobs > 0 ? cfg.jobs : std::max(1, std::min(8, QThread::idealThreadCount())));
    seed_->setValue(static_cast<int>(std::max(1u, cfg.seed)));

    connect(addParam, &QPushButton::clicked, this, [this] {
        core::CalibrationParameter p;
        const bool swmm = doc_->model().dialect() == core::Dialect::Swmm;
        p.section = swmm ? "CONDUITS" : "PIPES";
        p.column = "Roughness";
        addParameter(p);
    });
    connect(removeParam, &QPushButton::clicked, this, [this] {
        if (params_->currentRow() >= 0)
            params_->removeRow(params_->currentRow());
    });
    connect(useSel, &QPushButton::clicked, this, [this] {
        const int row = params_->currentRow();
        const Selection s = doc_->selection();
        if (row < 0 || s.isEmpty())
            return;
        auto* item = params_->item(row, ParamElements);
        QString t = item->text().trimmed();
        if (t.isEmpty() || t.compare("all", Qt::CaseInsensitive) == 0)
            t = s.id;
        else if (!t.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts).contains(s.id))
            t += ", " + s.id;
        item->setText(t);
    });
    connect(params_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == ParamElements)
            updateRowCount(item->row());
    });
    connect(addTermBtn, &QPushButton::clicked, this, [this] { addTerm({core::Metric::Kge, 1.0}); });
    connect(removeTermBtn, &QPushButton::clicked, this, [this] {
        if (terms_->currentRow() >= 0)
            terms_->removeRow(terms_->currentRow());
    });
    connect(startBtn_, &QPushButton::clicked, this, &CalibrationDialog::start);
    connect(cancelBtn_, &QPushButton::clicked, solver_, &SolverProcess::cancel);
    connect(applyBtn_, &QPushButton::clicked, this, &CalibrationDialog::applyBest);
    connect(close, &QPushButton::clicked, this, &CalibrationDialog::reject);
    connect(solver_, &SolverProcess::progress, this, [this](double f, const QString& stage) {
        progress_->setValue(static_cast<int>(f * 1000));
        progress_->setFormat(stage);
    });
    connect(solver_, &SolverProcess::log, this, [this](const QString& level, const QString& message) {
        if (level != "info")
            status_->setText(message);
    });
    connect(solver_, &SolverProcess::event, this, &CalibrationDialog::onEvent);
    connect(solver_, &SolverProcess::finished, this, &CalibrationDialog::onFinished);
}

CalibrationDialog::~CalibrationDialog()
{
    cleanUp();
}

void CalibrationDialog::addParameter(const core::CalibrationParameter& p)
{
    const int row = params_->rowCount();
    params_->insertRow(row);
    params_->setItem(row, ParamName, new QTableWidgetItem(QString::fromStdString(p.name)));
    auto* section = new QComboBox(params_);
    const auto& m = doc_->model();
    for (const auto& def : core::sectionDefs(m.dialect()))
    {
        if (def.keyCategory == Category::None || def.multiRowPerKey)
            continue;
        const auto* s = m.document().find(def.name);
        if (!s || s->rowCount() == 0)
            continue;
        const bool numeric = std::any_of(def.columns.begin() + 1, def.columns.end(),
                                         [](const core::ColumnDef& c) { return c.type == core::ColumnType::Number; });
        if (numeric)
            section->addItem(QString::fromStdString(def.name), QString::fromStdString(def.name));
    }
    if (section->findData(QString::fromStdString(p.section)) < 0)
        section->addItem(QString::fromStdString(p.section), QString::fromStdString(p.section));
    section->setCurrentIndex(section->findData(QString::fromStdString(p.section)));
    params_->setCellWidget(row, ParamSection, section);
    auto* column = new QComboBox(params_);
    params_->setCellWidget(row, ParamColumnName, column);
    params_->setItem(row, ParamElements,
                     new QTableWidgetItem(p.elements.empty() ? QString("all")
                                                             : QString::fromStdString(text::join(p.elements, ", "))));
    auto* mode = new QComboBox(params_);
    mode->addItem(tr("Multiply"), "multiply");
    mode->addItem(tr("Replace"), "replace");
    mode->setCurrentIndex(p.mode == core::CalibrationParameter::Mode::Multiply ? 0 : 1);
    params_->setCellWidget(row, ParamMode, mode);
    params_->setItem(row, ParamMin, new QTableWidgetItem(num(p.min)));
    params_->setItem(row, ParamMax, new QTableWidgetItem(num(p.max)));
    auto* rows = new QTableWidgetItem;
    rows->setFlags(Qt::ItemIsEnabled);
    rows->setTextAlignment(Qt::AlignCenter);
    params_->setItem(row, ParamRows, rows);
    fillColumns(row);
    if (column->findText(QString::fromStdString(p.column)) < 0)
        column->addItem(QString::fromStdString(p.column));
    column->setCurrentIndex(column->findText(QString::fromStdString(p.column)));
    updateRowCount(row);
    connect(section, &QComboBox::currentIndexChanged, this, [this, section] {
        for (int r = 0; r < params_->rowCount(); ++r)
        {
            if (params_->cellWidget(r, ParamSection) == section)
            {
                fillColumns(r);
                updateRowCount(r);
            }
        }
    });
    connect(column, &QComboBox::currentTextChanged, this, [this, column] {
        for (int r = 0; r < params_->rowCount(); ++r)
        {
            if (params_->cellWidget(r, ParamColumnName) == column)
                updateRowCount(r);
        }
    });
    connect(mode, &QComboBox::currentIndexChanged, this, [this, mode] {
        for (int r = 0; r < params_->rowCount(); ++r)
        {
            if (params_->cellWidget(r, ParamMode) != mode)
                continue;
            const bool multiply = mode->currentIndex() == 0;
            core::CalibrationParameter p;
            p.section = qobject_cast<QComboBox*>(params_->cellWidget(r, ParamSection))->currentData().toString().toStdString();
            p.column = qobject_cast<QComboBox*>(params_->cellWidget(r, ParamColumnName))->currentText().toStdString();
            p.mode = core::CalibrationParameter::Mode::Replace;
            p.min = -std::numeric_limits<double>::max();
            p.max = std::numeric_limits<double>::max();
            const double current = core::parameterInitialValue(doc_->model(), p);
            params_->item(r, ParamMin)->setText(multiply ? "0.5" : num(current * 0.5));
            params_->item(r, ParamMax)->setText(multiply ? "1.5" : num(current * 1.5));
        }
    });
}

void CalibrationDialog::fillColumns(int row)
{
    auto* section = qobject_cast<QComboBox*>(params_->cellWidget(row, ParamSection));
    auto* column = qobject_cast<QComboBox*>(params_->cellWidget(row, ParamColumnName));
    if (!section || !column)
        return;
    const QString keep = column->currentText();
    column->blockSignals(true);
    column->clear();
    if (const auto* def = core::findSectionDef(doc_->model().dialect(), section->currentData().toString().toStdString()))
    {
        for (std::size_t c = 1; c < def->columns.size(); ++c)
        {
            if (def->columns[c].type == core::ColumnType::Number)
                column->addItem(QString::fromStdString(def->columns[c].name));
        }
    }
    const int idx = column->findText(keep);
    column->setCurrentIndex(idx >= 0 ? idx : 0);
    column->blockSignals(false);
}

void CalibrationDialog::updateRowCount(int row)
{
    auto* section = qobject_cast<QComboBox*>(params_->cellWidget(row, ParamSection));
    auto* column = qobject_cast<QComboBox*>(params_->cellWidget(row, ParamColumnName));
    auto* elements = params_->item(row, ParamElements);
    auto* rows = params_->item(row, ParamRows);
    if (!section || !column || !elements || !rows)
        return;
    core::CalibrationParameter p;
    p.section = section->currentData().toString().toStdString();
    p.column = column->currentText().toStdString();
    const QString e = elements->text().trimmed();
    if (!e.isEmpty() && e.compare("all", Qt::CaseInsensitive) != 0)
        for (const QString& id : e.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts))
            p.elements.push_back(id.toStdString());
    const int n = core::parameterRowCount(doc_->model(), p);
    rows->setText(QString::number(n));
    rows->setForeground(n == 0 ? QColor(200, 50, 50) : QColor(31, 42, 55));
}

void CalibrationDialog::addTerm(const core::ObjectiveTerm& t)
{
    const int row = terms_->rowCount();
    terms_->insertRow(row);
    auto* metric = new QComboBox(terms_);
    for (core::Metric m : core::allMetrics())
        metric->addItem(QString::fromLatin1(core::metricName(m)), static_cast<int>(m));
    metric->setCurrentIndex(std::max(0, metric->findData(static_cast<int>(t.metric))));
    terms_->setCellWidget(row, 0, metric);
    terms_->setItem(row, 1, new QTableWidgetItem(num(t.weight)));
}

bool CalibrationDialog::readSetup(core::CalibrationConfig& cfg, QString* error)
{
    cfg = data_->config();
    cfg.parameters.clear();
    for (int r = 0; r < params_->rowCount(); ++r)
    {
        core::CalibrationParameter p;
        p.name = params_->item(r, ParamName)->text().trimmed().toStdString();
        p.section = qobject_cast<QComboBox*>(params_->cellWidget(r, ParamSection))->currentData().toString().toStdString();
        p.column = qobject_cast<QComboBox*>(params_->cellWidget(r, ParamColumnName))->currentText().trimmed().toStdString();
        const QString e = params_->item(r, ParamElements)->text().trimmed();
        if (!e.isEmpty() && e.compare("all", Qt::CaseInsensitive) != 0)
            for (const QString& id : e.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts))
                p.elements.push_back(id.toStdString());
        p.mode = qobject_cast<QComboBox*>(params_->cellWidget(r, ParamMode))->currentIndex() == 0
                     ? core::CalibrationParameter::Mode::Multiply
                     : core::CalibrationParameter::Mode::Replace;
        const auto lo = text::toDouble(params_->item(r, ParamMin)->text().toStdString());
        const auto hi = text::toDouble(params_->item(r, ParamMax)->text().toStdString());
        if (!lo || !hi || *hi <= *lo)
        {
            *error = tr("Row %1: the minimum must be a number below the maximum.").arg(r + 1);
            return false;
        }
        p.min = *lo;
        p.max = *hi;
        if (core::parameterRowCount(doc_->model(), p) == 0)
        {
            *error = tr("Row %1: no rows of [%2] have a number in column %3 for these elements.")
                         .arg(r + 1)
                         .arg(QString::fromStdString(p.section), QString::fromStdString(p.column));
            return false;
        }
        cfg.parameters.push_back(p);
    }
    cfg.objective.clear();
    for (int r = 0; r < terms_->rowCount(); ++r)
    {
        core::ObjectiveTerm t;
        t.metric = static_cast<core::Metric>(qobject_cast<QComboBox*>(terms_->cellWidget(r, 0))->currentData().toInt());
        t.weight = text::toDouble(terms_->item(r, 1)->text().toStdString()).value_or(1.0);
        if (t.weight > 0)
            cfg.objective.push_back(t);
    }
    cfg.budget = budget_->value();
    cfg.jobs = jobs_->value();
    cfg.seed = static_cast<unsigned>(seed_->value());
    if (cfg.parameters.empty())
    {
        *error = tr("Add at least one parameter.");
        return false;
    }
    if (cfg.observed.empty())
    {
        *error = tr("The project has no observed data. Add it with Project > Observed data.");
        return false;
    }
    return true;
}

void CalibrationDialog::start()
{
    if (solver_->isRunning())
        return;
    core::CalibrationConfig cfg;
    QString error;
    if (!readSetup(cfg, &error))
    {
        QMessageBox::warning(this, tr("Calibrate"), error);
        return;
    }
    data_->setConfig(cfg); // remembered with the project
    if (data_->isEmpty())
    {
        QMessageBox::warning(this, tr("Calibrate"), tr("No observed series could be read:\n%1").arg(data_->warnings().join("\n")));
        return;
    }
    // The run reads a copy of the current model and a set-up with absolute file paths.
    const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString dir = doc_->path().isEmpty() ? tmp : QFileInfo(doc_->path()).absolutePath();
    const QString base = doc_->path().isEmpty() ? QString("untitled") : QFileInfo(doc_->path()).completeBaseName();
    scratchInput_ = QDir(dir).filePath("." + base + ".mirab-calib.inp");
    QFile f(scratchInput_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, tr("Calibrate"), tr("Cannot write %1").arg(scratchInput_));
        return;
    }
    f.write(QByteArray::fromStdString(doc_->model().toText()));
    f.close();
    core::CalibrationConfig run = cfg;
    for (auto& s : run.observed)
        s.file = QDir::cleanPath(QDir(data_->baseDir().isEmpty() ? QDir::currentPath() : data_->baseDir())
                                     .absoluteFilePath(QString::fromStdString(s.file)))
                     .toStdString();
    configFile_ = QDir(tmp).filePath("mirab-calibration-" + QString::number(QCoreApplication::applicationPid()) + ".json");
    QFile c(configFile_);
    if (c.open(QIODevice::WriteOnly | QIODevice::Truncate))
        c.write(QByteArray::fromStdString(run.toJson().dump(2)));
    c.close();
    const QString outDir = doc_->path().isEmpty() ? QDir(tmp).filePath("mirab-calibration")
                                                  : QDir(dir).filePath(base + "-calibration");
    QStringList args = {"calibrate", scratchInput_, "--config", configFile_, "--engine", engineId_,
                        "--jobs", QString::number(cfg.jobs), "--budget", QString::number(cfg.budget),
                        "--seed", QString::number(cfg.seed), "--method", method_->currentData().toString(),
                        "--out-dir", outDir};
    if (!msxFile_.isEmpty())
        args << "--msx" << msxFile_;
    runParams_ = cfg.parameters;
    best_.clear();
    evaluations_.clear();
    bestSoFar_.clear();
    bestTable_->setRowCount(0);
    chart_->clear(tr("Waiting for the first run..."));
    progress_->setValue(0);
    progress_->setFormat(tr("Starting"));
    status_->setText(tr("Calibrating %n parameter(s)...", nullptr, static_cast<int>(cfg.parameters.size())));
    setRunning(true);
    solver_->start(args);
}

void CalibrationDialog::setRunning(bool running)
{
    startBtn_->setEnabled(!running);
    cancelBtn_->setEnabled(running);
    applyBtn_->setEnabled(!running && !best_.empty());
    params_->setEnabled(!running);
    terms_->setEnabled(!running);
}

void CalibrationDialog::onEvent(const QString& name, const QJsonObject& o)
{
    if (name != "evaluation")
        return;
    const int index = o.value("index").toInt();
    const QJsonValue obj = o.value("objective");
    const QJsonValue best = o.value("best");
    if (obj.isDouble())
        evaluations_.append(QPointF(index, obj.toDouble()));
    if (best.isDouble())
    {
        const bool improved = bestSoFar_.isEmpty() || best.toDouble() < bestSoFar_.last().y();
        bestSoFar_.append(QPointF(index, best.toDouble()));
        if (improved && obj.isDouble() && std::fabs(obj.toDouble() - best.toDouble()) < 1e-12)
        {
            best_.clear();
            for (const auto& v : o.value("values").toArray())
                best_.push_back(v.toDouble());
            bestTable_->setRowCount(0);
            for (std::size_t j = 0; j < runParams_.size() && j < best_.size(); ++j)
            {
                bestTable_->insertRow(static_cast<int>(j));
                bestTable_->setItem(static_cast<int>(j), 0,
                                    new QTableWidgetItem(QString::fromStdString(runParams_[j].label())));
                bestTable_->setItem(static_cast<int>(j), 1, new QTableWidgetItem(num(best_[j], 6)));
            }
        }
    }
    status_->setText(tr("Run %1: objective %2, best so far %3")
                         .arg(index)
                         .arg(num(obj.toDouble(std::numeric_limits<double>::quiet_NaN())),
                              num(best.toDouble(std::numeric_limits<double>::quiet_NaN()))));
    plotProgress();
}

void CalibrationDialog::plotProgress()
{
    ChartWidget::Series runs;
    runs.name = tr("Run");
    runs.style = ChartWidget::Series::Style::Points;
    runs.color = QColor(160, 170, 184);
    // Failed or very poor runs would flatten the chart: show them at the top.
    double cap = 0;
    for (const auto& p : bestSoFar_)
        cap = std::max(cap, p.y());
    QVector<double> sorted;
    for (const auto& p : evaluations_)
        sorted.append(p.y());
    std::sort(sorted.begin(), sorted.end());
    if (!sorted.isEmpty())
        cap = std::max(cap, sorted[static_cast<int>(0.9 * (sorted.size() - 1))]);
    for (const auto& p : evaluations_)
        runs.points.append(QPointF(p.x(), std::min(p.y(), cap > 0 ? cap * 1.2 : p.y())));
    ChartWidget::Series best;
    best.name = tr("Best so far");
    best.style = ChartWidget::Series::Style::Step;
    best.color = QColor(31, 111, 235);
    best.width = 2.2;
    best.points = bestSoFar_;
    chart_->setXAxis(tr("Run"), false);
    chart_->setSeries({runs, best}, tr("Objective"), tr("Objective of each run (lower is better)"));
}

void CalibrationDialog::onFinished(bool ok, const QJsonObject& r)
{
    setRunning(false);
    const QJsonObject payload = r.value("payload").toObject();
    if (ok)
    {
        best_.clear();
        for (const auto& v : payload.value("values").toArray())
            best_.push_back(v.toDouble());
        bestTable_->setRowCount(0);
        const QJsonArray params = payload.value("parameters").toArray();
        for (int j = 0; j < params.size(); ++j)
        {
            const QJsonObject p = params[j].toObject();
            bestTable_->insertRow(j);
            bestTable_->setItem(j, 0, new QTableWidgetItem(p.value("label").toString()));
            bestTable_->setItem(j, 1, new QTableWidgetItem(num(p.value("value").toDouble(), 6)));
        }
        // The run used a hidden copy of the model: name the calibrated model after the project.
        const QString bestInput = payload.value("bestInput").toString();
        if (!bestInput.isEmpty() && !doc_->path().isEmpty())
        {
            const QString target =
                QFileInfo(bestInput).dir().filePath(QFileInfo(doc_->path()).completeBaseName() + "-best.inp");
            QFile::remove(target);
            QFile::rename(bestInput, target);
        }
        QString msg = r.value("cancelled").toBool() ? tr("Cancelled after %1 runs.") : tr("Finished %1 runs.");
        msg = msg.arg(payload.value("evaluations").toInt());
        msg += " " + tr("Best objective %1. Log: %2").arg(num(payload.value("objective").toDouble()),
                                                          payload.value("log").toString());
        const QJsonArray sens = payload.value("sensitivity").toArray();
        if (!sens.isEmpty())
        {
            QStringList s;
            for (const auto& v : sens)
                s << tr("%1: %2").arg(v.toObject().value("parameter").toString(),
                                      num(v.toObject().value("spearman").toDouble(), 3));
            msg += "\n" + tr("Rank correlation with the objective: %1").arg(s.join(", "));
        }
        status_->setText(msg);
        progress_->setValue(1000);
        progress_->setFormat(tr("Done"));
    }
    else
    {
        status_->setText(tr("Calibration failed: %1").arg(r.value("message").toString()));
    }
    applyBtn_->setEnabled(!best_.empty());
    cleanUp();
}

void CalibrationDialog::applyBest()
{
    if (best_.empty() || best_.size() != runParams_.size())
        return;
    const auto params = runParams_;
    const auto values = best_;
    QString error;
    if (!doc_->edit(tr("Apply calibrated parameters"),
                    [params, values](core::Model& m, std::string* err) { return core::applyParameters(m, params, values, err); },
                    &error))
    {
        QMessageBox::warning(this, tr("Calibrate"), error);
        return;
    }
    status_->setText(tr("The best values were written into the model. Undo restores the previous values; run a "
                        "simulation to see the calibrated results."));
    applyBtn_->setEnabled(false);
}

void CalibrationDialog::cleanUp()
{
    if (!scratchInput_.isEmpty())
        QFile::remove(scratchInput_);
    if (!configFile_.isEmpty())
        QFile::remove(configFile_);
    scratchInput_.clear();
    configFile_.clear();
}

void CalibrationDialog::reject()
{
    if (solver_->isRunning())
    {
        if (QMessageBox::question(this, tr("Calibrate"), tr("Stop the calibration?")) != QMessageBox::Yes)
            return;
        // Let the solver stop its engine runs and remove its scratch files.
        QEventLoop wait;
        connect(solver_, &SolverProcess::finished, &wait, &QEventLoop::quit);
        QTimer::singleShot(5000, &wait, &QEventLoop::quit);
        solver_->cancel();
        wait.exec();
    }
    QDialog::reject();
}

} // namespace mirab::app
