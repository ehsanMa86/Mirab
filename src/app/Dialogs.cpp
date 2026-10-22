/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Dialogs.cpp
 * Purpose: Implementation of the application dialogs.
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
#include "Dialogs.h"

#include "ChartWidget.h"
#include "EngineDownloader.h"
#include "Language.h"
#include "SolverProcess.h"
#include "mirab/MirabConfig.h"
#include "mirab/core/Crs.h"
#include "mirab/core/Text.h"
#include "mirab/engine/EngineInstall.h"
#include "mirab/resilience/Resilience.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mirab::app
{

// ---------------------------------------------------------------------------
NewProjectDialog::NewProjectDialog(core::Dialect dialect, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(dialect == core::Dialect::Swmm ? tr("New SWMM project") : tr("New EPANET project"));
    auto* form = new QFormLayout(this);
    units_ = new QComboBox(this);
    if (dialect == core::Dialect::Swmm)
        units_->addItems({"CMS", "LPS", "MLD", "CFS", "GPM", "MGD"});
    else
        units_->addItems({"LPS", "LPM", "MLD", "CMH", "CMD", "CMS", "GPM", "CFS", "MGD", "IMGD", "AFD"});
    form->addRow(tr("Flow units"), units_);
    auto* note = new QLabel(tr("SI flow units give metric lengths, diameters and pressures; "
                               "US units give feet, inches and psi."),
                            this);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString NewProjectDialog::flowUnits() const
{
    return units_->currentText();
}

// ---------------------------------------------------------------------------
EnginesDialog::EnginesDialog(const engine::EngineRegistry& registry, QWidget* parent)
    : QDialog(parent), registry_(registry)
{
    setWindowTitle(tr("Simulation engines"));
    resize(920, 480);
    auto* layout = new QVBoxLayout(this);
    table_ = new QTableWidget(this);
    const QStringList headers = {tr("Engine"), tr("Status"), tr("Provider"), tr("License"), tr("Limitations"), tr("Source")};
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(true);
    layout->addWidget(table_);

    auto* row = new QHBoxLayout;
    install_ = new QPushButton(tr("Install SWMM5+"), this);
    install_->setToolTip(tr("Download SWMM5+ from the swmm5plus-capi releases, check every file's SHA-256 sum "
                            "and install it for your user account."));
    installFile_ = new QPushButton(tr("Install from file..."), this);
    installFile_->setToolTip(tr("Install an engine package downloaded earlier (choose its .json manifest)."));
    remove_ = new QPushButton(tr("Remove SWMM5+"), this);
    progress_ = new QProgressBar(this);
    progress_->setVisible(false);
    row->addWidget(install_);
    row->addWidget(installFile_);
    row->addWidget(remove_);
    row->addWidget(progress_, 1);
    layout->addLayout(row);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    downloader_ = new EngineDownloader(this);
    connect(downloader_, &EngineDownloader::progress, this, [this](qint64 done, qint64 total, const QString& stage) {
        progress_->setVisible(true);
        progress_->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0)
            progress_->setValue(static_cast<int>(1000.0 * static_cast<double>(done) / static_cast<double>(total)));
        progress_->setFormat(stage);
        status_->setText(stage);
    });
    connect(downloader_, &EngineDownloader::finished, this, &EnginesDialog::installFinished);
    connect(install_, &QPushButton::clicked, this, [this] {
        install_->setEnabled(false);
        installFile_->setEnabled(false);
        downloader_->start(EngineDownloader::swmm5PlusManifestUrl());
    });
    connect(installFile_, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Engine package manifest"), QDir::homePath(),
                                                       tr("Engine manifest (*.json)"));
        if (f.isEmpty())
            return;
        install_->setEnabled(false);
        installFile_->setEnabled(false);
        downloader_->installLocal(f);
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Remove SWMM5+"), tr("Remove SWMM5+ from your engine folder?")) !=
            QMessageBox::Yes)
            return;
        if (engine::uninstallEngine("swmm5plus-1.0", engine::userEngineDirectory()))
            status_->setText(tr("SWMM5+ was removed."));
        else
            status_->setText(tr("SWMM5+ was not installed by Mirab, so it was left in place."));
        populate();
        emit enginesChanged();
    });
    populate();
}

void EnginesDialog::installFinished(bool ok, const QString& message)
{
    progress_->setVisible(false);
    status_->setText(ok ? tr("Installed. %1").arg(message) : message);
    populate();
    if (ok)
        emit enginesChanged();
}

void EnginesDialog::populate()
{
    table_->setRowCount(0);
    for (const auto& d : registry_.descriptors())
    {
        const int r = table_->rowCount();
        table_->insertRow(r);
        std::string why;
        const bool ok = registry_.isAvailable(d.id, &why);
        QStringList limits;
        for (const auto& [f, s] : d.support)
            limits << tr("%1: %2").arg(coreText(core::featureName(f)),
                                       s == engine::Support::Ignored ? tr("ignored") : tr("not supported"));
        auto* name = new QTableWidgetItem(QString::fromStdString(d.name));
        name->setToolTip(coreText(d.description));
        table_->setItem(r, 0, name);
        auto* status = new QTableWidgetItem(ok ? tr("Installed") : (d.optional ? tr("Optional, not installed") : tr("Missing")));
        status->setToolTip(QString::fromStdString(ok ? registry_.locate(d)->u8string() : why));
        status->setForeground(ok ? QColor(30, 140, 60) : QColor(190, 90, 30));
        table_->setItem(r, 1, status);
        table_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(d.provider)));
        table_->setItem(r, 3, new QTableWidgetItem(QString::fromStdString(d.license)));
        table_->setItem(r, 4, new QTableWidgetItem(limits.isEmpty() ? tr("none") : limits.join(QChar('\n'))));
        table_->setItem(r, 5, new QTableWidgetItem(QString::fromStdString(d.upstreamUrl)));
    }
    table_->resizeColumnsToContents();
    table_->resizeRowsToContents();
    table_->horizontalHeader()->setStretchLastSection(true);

    const bool installed = registry_.isAvailable("swmm5plus-1.0");
    const bool busy = downloader_ && downloader_->isRunning();
    install_->setEnabled(!busy);
    installFile_->setEnabled(!busy);
    install_->setText(installed ? tr("Update SWMM5+") : tr("Install SWMM5+"));
    remove_->setEnabled(installed && !busy);
    if (status_->text().isEmpty())
    {
        status_->setText(tr("Optional engines are installed for your user account in %1.")
                             .arg(QString::fromStdString(engine::userEngineDirectory().u8string())));
    }
}

// ---------------------------------------------------------------------------
namespace
{
QDoubleSpinBox* spin(QWidget* parent, double value, double max, int decimals = 2)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(0.0, max);
    s->setDecimals(decimals);
    s->setValue(value);
    return s;
}
} // namespace

ResilienceDialog::ResilienceDialog(ProjectDocument* doc, const QString& engineId, const QString& inputFile, QWidget* parent)
    : QDialog(parent), doc_(doc), engineId_(engineId), inputFile_(inputFile)
{
    setWindowTitle(tr("Resilience analysis"));
    resize(860, 600);
    const bool si = doc_->model().units().system == core::UnitSystem::SI;
    const QString pUnit = QString::fromStdString(doc_->model().units().label("pressure"));
    const QString qUnit = QString::fromStdString(doc_->model().units().label("flow"));

    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    analysis_ = new QComboBox(this);
    analysis_->addItem(tr("Todini resilience index"), "todini");
    analysis_->addItem(tr("Water service availability"), "service");
    analysis_->addItem(tr("Network topology"), "topology");
    analysis_->addItem(tr("Pipe criticality"), "criticality");
    analysis_->addItem(tr("Fire-flow capacity"), "fireflow");
    top->addWidget(new QLabel(tr("Analysis"), this));
    top->addWidget(analysis_, 1);
    runButton_ = new QPushButton(tr("Run"), this);
    top->addWidget(runButton_);
    layout->addLayout(top);

    params_ = new QStackedWidget(this);
    auto page = [&](QFormLayout*& form) {
        auto* w = new QWidget(params_);
        form = new QFormLayout(w);
        params_->addWidget(w);
        return w;
    };
    QFormLayout* f = nullptr;
    page(f);
    requiredPressure_ = spin(this, si ? 14.0 : 20.0, 1e5);
    requiredPressure_->setSuffix(" " + pUnit);
    f->addRow(tr("Required service pressure"), requiredPressure_);
    f->addRow(new QLabel(tr("Uses the results of the last simulation."), this));
    page(f);
    f->addRow(new QLabel(tr("Delivered versus expected demand from the last simulation. "
                            "Run with DEMAND MODEL PDA for a meaningful result."),
                         this));
    page(f);
    f->addRow(new QLabel(tr("Graph analysis: bridges, articulation points, dead ends and meshedness."), this));
    page(f);
    threshold_ = spin(this, si ? 14.0 : 20.0, 1e5);
    threshold_->setSuffix(" " + pUnit);
    pda_ = new QCheckBox(tr("Pressure-driven demand while pipes are closed"), this);
    pda_->setChecked(true);
    f->addRow(tr("Pressure threshold"), threshold_);
    f->addRow(pda_);
    page(f);
    fireFlow_ = spin(this, si ? 30.0 : 1000.0, 1e7);
    fireFlow_->setSuffix(" " + qUnit);
    residual_ = spin(this, si ? 14.0 : 20.0, 1e5);
    residual_->setSuffix(" " + pUnit);
    hour_ = spin(this, 0.0, 168.0, 1);
    hour_->setSuffix(tr(" h"));
    nodes_ = new QLineEdit(this);
    nodes_->setPlaceholderText(tr("all junctions, or IDs separated by commas"));
    f->addRow(tr("Fire flow"), fireFlow_);
    f->addRow(tr("Minimum residual pressure"), residual_);
    f->addRow(tr("Time of day"), hour_);
    f->addRow(tr("Hydrant nodes"), nodes_);
    layout->addWidget(params_);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);
    progress_ = new QProgressBar(this);
    progress_->setVisible(false);
    layout->addWidget(progress_);
    auto* split = new QSplitter(Qt::Vertical, this);
    table_ = new QTableWidget(split);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->hide();
    chart_ = new ChartWidget(split);
    split->addWidget(table_);
    split->addWidget(chart_);
    layout->addWidget(split, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* csv = buttons->addButton(tr("Copy as CSV"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(csv, &QPushButton::clicked, this, &ResilienceDialog::exportCsv);
    connect(analysis_, qOverload<int>(&QComboBox::currentIndexChanged), params_, &QStackedWidget::setCurrentIndex);
    connect(runButton_, &QPushButton::clicked, this, &ResilienceDialog::run);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto items = table_->selectedItems();
        if (items.isEmpty())
            return;
        const QString id = table_->item(items.first()->row(), 0)->text();
        const std::string sid = id.toStdString();
        if (doc_->model().findNode(sid))
            doc_->select({core::Category::Node, id});
        else if (doc_->model().findLink(sid))
            doc_->select({core::Category::Link, id});
    });

    solver_ = new SolverProcess(this);
    connect(solver_, &SolverProcess::progress, this, [this](double f, const QString& s) {
        progress_->setValue(static_cast<int>(f * 100));
        progress_->setFormat(s + " %p%");
    });
    connect(solver_, &SolverProcess::finished, this, [this](bool ok, const QJsonObject& r) {
        progress_->setVisible(false);
        runButton_->setText(tr("Run"));
        if (!ok)
        {
            summary_->setText(tr("The analysis failed: %1").arg(r.value("message").toString()));
            return;
        }
        showPayload(r.value("payload").toObject());
    });
}

void ResilienceDialog::run()
{
    if (solver_->isRunning())
    {
        solver_->cancel();
        return;
    }
    const QString what = analysis_->currentData().toString();
    const auto& model = doc_->model();
    if (model.dialect() != core::Dialect::Epanet && what != "topology")
    {
        summary_->setText(tr("This analysis applies to water distribution (EPANET) models."));
        return;
    }
    table_->clear();
    table_->setRowCount(0);
    chart_->clear();
    QJsonObject payload;
    payload["analysis"] = what;
    if (what == "todini" || what == "service")
    {
        const auto* results = doc_->results();
        if (!results)
        {
            summary_->setText(tr("Run a simulation first; these indices are computed from its results."));
            return;
        }
        QJsonArray series;
        if (what == "todini")
        {
            const auto r = resilience::todiniIndex(model, *results, requiredPressure_->value());
            for (std::size_t i = 0; i < r.index.size(); ++i)
                series.append(QJsonArray{r.times[i], r.index[i]});
            payload["minimum"] = r.minimum;
            payload["mean"] = r.mean;
            payload["note"] = QString::fromStdString(r.note);
        }
        else
        {
            const auto r = resilience::waterServiceAvailability(*results);
            for (std::size_t i = 0; i < r.system.size(); ++i)
                series.append(QJsonArray{r.times[i], r.system[i]});
            QJsonArray nodes;
            for (const auto& [id, v] : r.nodes)
                nodes.append(QJsonArray{QString::fromStdString(id), v});
            payload["nodes"] = nodes;
            payload["overall"] = r.overall;
            payload["note"] = QString::fromStdString(r.note);
        }
        payload["series"] = series;
        showPayload(payload);
        return;
    }
    if (what == "topology")
    {
        const auto t = resilience::topology(model);
        auto list = [](const std::vector<std::string>& v) {
            QJsonArray a;
            for (const auto& s : v)
                a.append(QString::fromStdString(s));
            return a;
        };
        payload["nodes"] = t.nodes;
        payload["links"] = t.links;
        payload["components"] = t.components;
        payload["averageDegree"] = t.averageDegree;
        payload["meshedness"] = t.meshedness;
        payload["linkDensity"] = t.linkDensity;
        payload["articulationNodes"] = list(t.articulationNodes);
        payload["bridgeLinks"] = list(t.bridgeLinks);
        payload["deadEnds"] = list(t.deadEnds);
        payload["isolatedNodes"] = list(t.isolatedNodes);
        showPayload(payload);
        return;
    }
    QStringList args = {"resilience", what, inputFile_, "--engine", engineId_, "--json"};
    if (what == "criticality")
    {
        args << "--threshold" << QString::number(threshold_->value());
        if (!pda_->isChecked())
            args << "--dda";
    }
    else
    {
        args << "--flow" << QString::number(fireFlow_->value()) << "--residual" << QString::number(residual_->value())
             << "--time" << QString::number(hour_->value());
        if (!nodes_->text().trimmed().isEmpty())
            args << "--nodes" << nodes_->text().remove(' ');
    }
    progress_->setValue(0);
    progress_->setVisible(true);
    runButton_->setText(tr("Cancel"));
    summary_->setText(tr("Running in the solver process..."));
    solver_->start(args);
}

void ResilienceDialog::showPayload(const QJsonObject& p)
{
    const QString what = p.value("analysis").toString();
    auto setTable = [&](const QStringList& headers, const QVector<QStringList>& rows) {
        table_->clear();
        table_->setColumnCount(headers.size());
        table_->setHorizontalHeaderLabels(headers);
        table_->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r)
        {
            for (int c = 0; c < rows[r].size(); ++c)
                table_->setItem(r, c, new QTableWidgetItem(rows[r][c]));
        }
        table_->resizeColumnsToContents();
        table_->horizontalHeader()->setStretchLastSection(true);
    };
    auto num = [](double v, int prec = 4) { return QString::fromStdString(text::formatDouble(v, prec)); };
    QString note = p.value("note").toString();
    if (what == "todini" || what == "service")
    {
        QVector<QStringList> rows;
        ChartWidget::Series s;
        s.name = what == "todini" ? tr("Todini index") : tr("Availability");
        for (const auto& v : p.value("series").toArray())
        {
            const auto a = v.toArray();
            rows.append({QString::fromStdString(text::formatDuration(a[0].toDouble())), num(a[1].toDouble())});
            s.points.append(QPointF(a[0].toDouble(), a[1].toDouble()));
        }
        if (what == "todini")
        {
            summary_->setText(tr("Todini resilience index: minimum %1, mean %2. Values near 1 mean large surplus "
                                 "energy; values near 0 or negative mean the network barely meets the required pressure.")
                                  .arg(num(p.value("minimum").toDouble()), num(p.value("mean").toDouble())) +
                              (note.isEmpty() ? QString() : "\n" + note));
            setTable({tr("Time"), tr("Index")}, rows);
        }
        else
        {
            summary_->setText(tr("Overall water service availability: %1 %.").arg(num(100.0 * p.value("overall").toDouble())) +
                              (note.isEmpty() ? QString() : "\n" + note));
            QVector<QStringList> nodes;
            for (const auto& v : p.value("nodes").toArray())
            {
                const auto a = v.toArray();
                nodes.append({a[0].toString(), num(100.0 * a[1].toDouble())});
            }
            std::sort(nodes.begin(), nodes.end(), [](const QStringList& a, const QStringList& b) {
                return a[1].toDouble() < b[1].toDouble();
            });
            setTable({tr("Node"), tr("Availability (%)")}, nodes);
        }
        chart_->setSeries({s}, s.name);
    }
    else if (what == "topology")
    {
        auto join = [](const QJsonArray& a) {
            QStringList l;
            for (const auto& v : a)
                l << v.toString();
            return l.join(", ");
        };
        summary_->setText(tr("%1 nodes, %2 links, %3 connected component(s).")
                              .arg(p.value("nodes").toInt())
                              .arg(p.value("links").toInt())
                              .arg(p.value("components").toInt()));
        setTable({tr("Measure"), tr("Value")},
                 {{tr("Average node degree"), num(p.value("averageDegree").toDouble())},
                  {tr("Meshedness coefficient"), num(p.value("meshedness").toDouble())},
                  {tr("Link density"), num(p.value("linkDensity").toDouble(), 6)},
                  {tr("Critical nodes (articulation points)"), join(p.value("articulationNodes").toArray())},
                  {tr("Critical links (bridges)"), join(p.value("bridgeLinks").toArray())},
                  {tr("Dead ends"), join(p.value("deadEnds").toArray())},
                  {tr("Isolated nodes"), join(p.value("isolatedNodes").toArray())}});
    }
    else if (what == "criticality")
    {
        QVector<QStringList> rows;
        for (const auto& v : p.value("entries").toArray())
        {
            const auto e = v.toObject();
            rows.append({e.value("link").toString(), QString::number(e.value("nodesBelowThreshold").toInt()),
                         num(100.0 * e.value("unmetDemandFraction").toDouble()), num(e.value("minimumPressure").toDouble()),
                         e.value("worstNode").toString(), e.value("failed").toBool() ? e.value("error").toString() : QString()});
        }
        summary_->setText(tr("Each pipe was closed in turn over the full simulation. Pipes are ranked by the number of "
                             "junctions that drop below %1.")
                              .arg(num(p.value("threshold").toDouble())));
        setTable({tr("Pipe"), tr("Junctions below threshold"), tr("Unmet demand (%)"), tr("Lowest pressure"),
                  tr("Worst node"), tr("Note")},
                 rows);
    }
    else if (what == "fireflow")
    {
        QVector<QStringList> rows;
        int adequate = 0;
        for (const auto& v : p.value("entries").toArray())
        {
            const auto e = v.toObject();
            adequate += e.value("adequate").toBool() ? 1 : 0;
            rows.append({e.value("node").toString(), e.value("adequate").toBool() ? tr("yes") : tr("no"),
                         num(e.value("residualPressure").toDouble()), num(e.value("minimumPressure").toDouble()),
                         e.value("criticalNode").toString(), num(e.value("availableFlow").toDouble()),
                         e.value("failed").toBool() ? e.value("error").toString() : QString()});
        }
        summary_->setText(tr("%1 of %2 hydrant node(s) can deliver %3 while every junction stays above %4.")
                              .arg(adequate)
                              .arg(rows.size())
                              .arg(num(p.value("fireFlow").toDouble()), num(p.value("residual").toDouble())));
        setTable({tr("Node"), tr("Adequate"), tr("Pressure at node"), tr("Lowest pressure"), tr("Critical node"),
                  tr("Available flow"), tr("Note")},
                 rows);
    }
}

void ResilienceDialog::exportCsv()
{
    QString out;
    for (int c = 0; c < table_->columnCount(); ++c)
        out += (c ? "," : "") + table_->horizontalHeaderItem(c)->text();
    out += "\n";
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        for (int c = 0; c < table_->columnCount(); ++c)
            out += (c ? "," : "") + (table_->item(r, c) ? table_->item(r, c)->text() : QString());
        out += "\n";
    }
    QApplication::clipboard()->setText(out);
    summary_->setText(summary_->text() + "\n" + tr("Table copied to the clipboard."));
}

// ---------------------------------------------------------------------------
SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    auto* form = new QFormLayout(this);
    language_ = new QComboBox(this);
    for (const UiLanguage& l : uiLanguages())
        language_->addItem(languageLabel(l), l.code);
    language_->setCurrentIndex(std::max(0, language_->findData(configuredLanguage())));
    form->addRow(withEnglish(tr("Language"), QStringLiteral("Language")), language_);
    auto* row = new QHBoxLayout;
    qgis_ = new QLineEdit(qgisPath(), this);
    qgis_->setPlaceholderText(findQgis().isEmpty() ? tr("QGIS was not found automatically") : findQgis());
    auto* browse = new QPushButton(tr("Browse..."), this);
    row->addWidget(qgis_, 1);
    row->addWidget(browse);
    form->addRow(tr("QGIS program"), row);
    auto* note = new QLabel(tr("Mirab never loads QGIS. \"Open in QGIS\" exports a GeoPackage and starts QGIS "
                               "as a separate program."),
                            this);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("QGIS program"));
        if (!f.isEmpty())
            qgis_->setText(f);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings().setValue("qgis/path", qgis_->text().trimmed());
        const QString language = language_->currentData().toString();
        if (language != configuredLanguage())
        {
            setConfiguredLanguage(language);
            restartRequested_ = askRestartForLanguage(this);
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString SettingsDialog::qgisPath()
{
    return QSettings().value("qgis/path").toString();
}

QString SettingsDialog::findQgis()
{
    const QString configured = qgisPath();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
#if defined(Q_OS_WIN)
    QStringList roots = {"C:/Program Files", "C:/OSGeo4W", "C:/OSGeo4W64"};
    for (const QString& root : roots)
    {
        QDir dir(root);
        if (!dir.exists())
            continue;
        if (QFileInfo::exists(dir.filePath("bin/qgis-bin.exe")))
            return dir.filePath("bin/qgis-bin.exe");
        QStringList entries = dir.entryList({"QGIS*"}, QDir::Dirs, QDir::Name | QDir::Reversed);
        for (const QString& e : entries)
        {
            for (const char* exe : {"bin/qgis-bin.exe", "bin/qgis-ltr-bin.exe", "bin/qgis.bat"})
            {
                const QString p = dir.filePath(e + "/" + exe);
                if (QFileInfo::exists(p))
                    return p;
            }
        }
    }
#elif defined(Q_OS_MACOS)
    QDir apps("/Applications");
    for (const QString& e : apps.entryList({"QGIS*.app"}, QDir::Dirs, QDir::Name | QDir::Reversed))
        return apps.filePath(e);
#endif
    for (const char* name : {"qgis", "qgis-ltr"})
    {
        const QString p = QStandardPaths::findExecutable(name);
        if (!p.isEmpty())
            return p;
    }
    return {};
}

// ---------------------------------------------------------------------------
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("About Mirab"));
    resize(620, 520);
    auto* layout = new QVBoxLayout(this);
    auto* header = new QHBoxLayout;
    auto* icon = new QLabel(this);
    icon->setPixmap(QApplication::windowIcon().pixmap(72, 72));
    auto* title = new QLabel(tr("<h2>Mirab %1</h2><p>Unified modeling environment for water distribution "
                                "and stormwater systems.</p><p>%2<br><a href=\"%3\">%3</a></p>")
                                .arg(MIRAB_VERSION_STRING, QString(MIRAB_COPYRIGHT).toHtmlEscaped(), MIRAB_HOMEPAGE),
                             this);
    title->setOpenExternalLinks(true);
    title->setWordWrap(true);
    header->addWidget(icon);
    header->addWidget(title, 1);
    layout->addLayout(header);
    auto* body = new QTextBrowser(this);
    body->setOpenExternalLinks(true);
    body->setHtml(tr(
        "<p>This program is free software: you can redistribute it and/or modify it under the terms of the GNU "
        "General Public License as published by the Free Software Foundation, either version 3 of the License, or "
        "(at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY "
        "WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See "
        "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">the GNU General Public License</a> for details.</p>"
        "<p><b>Simulation engines and libraries</b> (each under its own license, unmodified):</p><ul>"
        "<li>EPANET 2.2, U.S. Environmental Protection Agency, public domain and MIT</li>"
        "<li>EPANET 2.3, Open Water Analytics community, MIT</li>"
        "<li>EPANET-MSX 2.0, U.S. Environmental Protection Agency, MIT</li>"
        "<li>SWMM 5.2, U.S. Environmental Protection Agency, public domain</li>"
        "<li>SWMM5+ 1.0, CIMM, University of Texas at Austin, public domain (optional)</li>"
        "<li>SQLite, public domain</li>"
        "<li>Qt 6, The Qt Company, LGPL 3</li></ul>"
        "<p>The resilience metrics follow the published methods of Todini (2000), Buhl et al. (2004) and the "
        "service-availability definition used by the Water Network Tool for Resilience (WNTR).</p>"
        "<p>Mirab is not affiliated with or endorsed by the U.S. EPA, Open Water Analytics or CIMM.</p>"));
    layout->addWidget(body, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

// ---------------------------------------------------------------------------
// ResilienceDialog::selectAnalysis
// ---------------------------------------------------------------------------
void ResilienceDialog::selectAnalysis(const QString& key)
{
    const int idx = analysis_->findData(key);
    if (idx >= 0)
        analysis_->setCurrentIndex(idx);
}

// ---------------------------------------------------------------------------
// CoordinateSystemDialog
// ---------------------------------------------------------------------------
CoordinateSystemDialog::CoordinateSystemDialog(const QString& current, const core::Model& model, QWidget* parent)
    : QDialog(parent), model_(model)
{
    setWindowTitle(tr("Coordinate system"));
    resize(640, 560);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Choose the coordinate reference system of the node coordinates. It places the "
                                "network on web basemaps and in GIS exports. Search by name or EPSG code, enter a "
                                "code, or load the .prj file of a shapefile."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search, e.g. UTM 14N, Web Mercator, 32614"));
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);
    list_ = new QListWidget(this);
    for (const auto& e : crs::commonSystems())
    {
        auto* item = new QListWidgetItem(QStringLiteral("EPSG:%1   %2").arg(e.epsg).arg(QString::fromStdString(e.name)), list_);
        item->setData(Qt::UserRole, QStringLiteral("EPSG:%1").arg(e.epsg));
    }
    layout->addWidget(list_, 1);
    auto* row = new QHBoxLayout;
    code_ = new QLineEdit(current, this);
    code_->setPlaceholderText(tr("EPSG code, PROJ string or WKT; empty when unknown"));
    auto* prj = new QPushButton(tr("Load .prj file..."), this);
    auto* none = new QPushButton(tr("Unknown"), this);
    row->addWidget(new QLabel(tr("Definition"), this));
    row->addWidget(code_, 1);
    row->addWidget(prj);
    row->addWidget(none);
    layout->addLayout(row);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setMinimumHeight(40);
    layout->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (int i = 0; i < list_->count(); ++i)
        {
            const QStringList words = t.split(' ', Qt::SkipEmptyParts);
            bool show = true;
            for (const QString& w : words)
                show = show && list_->item(i)->text().contains(w, Qt::CaseInsensitive);
            list_->item(i)->setHidden(!show);
        }
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item)
            code_->setText(item->data(Qt::UserRole).toString());
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] {
        if (ok_->isEnabled())
            accept();
    });
    connect(code_, &QLineEdit::textChanged, this, &CoordinateSystemDialog::validate);
    connect(none, &QPushButton::clicked, this, [this] { code_->clear(); });
    connect(prj, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Projection file"), QDir::homePath(),
                                                       tr("Projection files (*.prj *.wkt);;All files (*)"));
        if (f.isEmpty())
            return;
        QFile file(f);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            code_->setText(QString::fromUtf8(file.readAll()).simplified());
    });
    for (int i = 0; i < list_->count(); ++i)
    {
        if (list_->item(i)->data(Qt::UserRole).toString().compare(current, Qt::CaseInsensitive) == 0)
        {
            list_->setCurrentRow(i);
            list_->scrollToItem(list_->item(i));
        }
    }
    validate();
}

QString CoordinateSystemDialog::crs() const
{
    const QString text = code_->text().trimmed();
    if (text.isEmpty())
        return {};
    bool numeric = false;
    const int code = text.toInt(&numeric);
    if (numeric)
        return QStringLiteral("EPSG:%1").arg(code);
    if (auto c = crs::parse(text.toStdString()); c && c->epsg > 0)
        return QStringLiteral("EPSG:%1").arg(c->epsg);
    return text;
}

void CoordinateSystemDialog::validate()
{
    const QString text = code_->text().trimmed();
    if (text.isEmpty())
    {
        status_->setText(tr("Unknown coordinate system. Basemaps are available only when the coordinates are "
                            "longitudes and latitudes."));
        ok_->setEnabled(true);
        return;
    }
    const auto c = crs::parse(crs().toStdString());
    if (!c)
    {
        status_->setText("<span style='color:#c84b4b'>" + tr("Not recognized. Use an EPSG code from the list, a PROJ "
                                                            "string or the WKT of a .prj file.") + "</span>");
        ok_->setEnabled(false);
        return;
    }
    QString where;
    for (const auto& n : model_.nodes())
    {
        if (!n.placed)
            continue;
        double lon = 0, lat = 0;
        if (crs::toLonLat(*c, n.pos.x, n.pos.y, lon, lat))
            where = tr("Node %1 would be at %2° %3, %4° %5.")
                        .arg(QString::fromStdString(n.id))
                        .arg(std::fabs(lat), 0, 'f', 5)
                        //: compass direction (north / south)
                        .arg(lat >= 0 ? tr("N") : tr("S"))
                        .arg(std::fabs(lon), 0, 'f', 5)
                        //: compass direction (east / west)
                        .arg(lon >= 0 ? tr("E") : tr("W"));
        if (std::fabs(lat) > 90 || std::fabs(lon) > 180 || !std::isfinite(lat) || !std::isfinite(lon))
            where = tr("The coordinates do not fit this system: node %1 falls outside the globe.").arg(QString::fromStdString(n.id));
        break;
    }
    status_->setText("<b>" + QString::fromStdString(c->name.empty() ? crs::toString(*c) : c->name).toHtmlEscaped() +
                     "</b><br>" + where);
    ok_->setEnabled(true);
}

} // namespace mirab::app
