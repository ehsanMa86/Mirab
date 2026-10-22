/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/CalibrationReport.cpp
 * Purpose: Calibration report: simulated versus observed charts and goodness-of-fit table.
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
#include "CalibrationReport.h"

#include "ChartWidget.h"
#include "Icons.h"
#include "ObservedData.h"
#include "mirab/core/Calibration.h"
#include "mirab/core/Text.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mirab::app
{

int qualityBand(const QString& metric, double v)
{
    if (!std::isfinite(v))
        return -1;
    auto higher = [v](double vg, double g, double s) { return v > vg ? 3 : v > g ? 2 : v > s ? 1 : 0; };
    auto lower = [v](double vg, double g, double s) {
        const double a = std::fabs(v);
        return a < vg ? 3 : a < g ? 2 : a < s ? 1 : 0;
    };
    // NSE and PBIAS after Moriasi et al. (2007); R2 after Moriasi et al. (2015).
    if (metric == "NSE")
        return higher(0.75, 0.65, 0.50);
    if (metric == "KGE")
        return higher(0.75, 0.60, 0.50);
    if (metric == "R2")
        return higher(0.85, 0.75, 0.60);
    if (metric == "PBIAS")
        return lower(10, 15, 25);
    if (metric == "VolumeError")
        return lower(5, 10, 20);
    if (metric == "PeakError")
        return lower(10, 20, 30);
    return -1;
}

QColor qualityColor(int band)
{
    switch (band)
    {
    case 3:
        return QColor(214, 240, 222);
    case 2:
        return QColor(232, 245, 220);
    case 1:
        return QColor(253, 240, 208);
    case 0:
        return QColor(250, 221, 218);
    default:
        return {};
    }
}

QString qualityName(int band)
{
    switch (band)
    {
    case 3:
        return QObject::tr("Very good");
    case 2:
        return QObject::tr("Good");
    case 1:
        return QObject::tr("Satisfactory");
    case 0:
        return QObject::tr("Unsatisfactory");
    default:
        return {};
    }
}

namespace
{
QString num(double v, int digits = 4)
{
    if (!std::isfinite(v))
        return QStringLiteral("-");
    return QString::fromStdString(text::formatDouble(std::fabs(v) < 1e-12 ? 0.0 : v, digits));
}

class CalibrationReport : public QWidget
{
    Q_OBJECT
public:
    CalibrationReport(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc), data_(ObservedData::of(doc))
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* head = new QWidget(this);
        head->setObjectName("panelHeader");
        head->setAttribute(Qt::WA_StyledBackground, true);
        auto* hl = new QHBoxLayout(head);
        hl->setContentsMargins(14, 10, 14, 10);
        auto* titles = new QVBoxLayout;
        titles->setSpacing(1);
        auto* t = new QLabel(tr("Calibration"), head);
        t->setObjectName("panelTitle");
        auto* d = new QLabel(tr("Simulated results against observed data. Click a series to see its time series and "
                                "scatter plot; colours rate the fit from very good (green) to unsatisfactory (red)."),
                             head);
        d->setObjectName("panelSubtitle");
        d->setWordWrap(true);
        titles->addWidget(t);
        titles->addWidget(d);
        hl->addLayout(titles, 1);
        auto* observedBtn = button("table", tr("Observed data..."), head);
        auto* calibrateBtn = button("calibration", tr("Calibrate..."), head);
        auto* exportBtn = button("export", tr("Export CSV"), head);
        hl->addWidget(observedBtn);
        hl->addWidget(calibrateBtn);
        hl->addWidget(exportBtn);
        layout->addWidget(head);

        auto* body = new QWidget(this);
        auto* bl = new QVBoxLayout(body);
        bl->setContentsMargins(12, 10, 12, 12);
        summary_ = new QLabel(body);
        summary_->setObjectName("muted");
        summary_->setWordWrap(true);
        bl->addWidget(summary_);
        auto* split = new QSplitter(Qt::Vertical, body);
        model_ = new QStandardItemModel(this);
        model_->setSortRole(Qt::UserRole);
        proxy_ = new QSortFilterProxyModel(this);
        proxy_->setSourceModel(model_);
        proxy_->setSortRole(Qt::UserRole);
        table_ = new QTableView(split);
        table_->setModel(proxy_);
        table_->setSortingEnabled(true);
        table_->sortByColumn(-1, Qt::AscendingOrder); // keep the order of the files until a header is clicked
        table_->setShowGrid(false);
        table_->verticalHeader()->hide();
        table_->verticalHeader()->setDefaultSectionSize(24);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setStretchLastSection(true);
        auto* charts = new QSplitter(Qt::Horizontal, split);
        series_ = new ChartWidget(charts);
        scatter_ = new ChartWidget(charts);
        charts->addWidget(series_);
        charts->addWidget(scatter_);
        charts->setStretchFactor(0, 3);
        charts->setStretchFactor(1, 2);
        split->addWidget(table_);
        split->addWidget(charts);
        split->setStretchFactor(1, 1);
        split->setSizes({200, 380});
        bl->addWidget(split, 1);
        layout->addWidget(body, 1);

        connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
                [this](const QModelIndex&) { plotSelected(); });
        connect(exportBtn, &QPushButton::clicked, this, &CalibrationReport::exportCsv);
        connect(observedBtn, &QPushButton::clicked, this, [this] { trigger("observedDataAction"); });
        connect(calibrateBtn, &QPushButton::clicked, this, [this] { trigger("calibrateAction"); });
        connect(doc_, &ProjectDocument::resultsChanged, this, [this] { refresh(); });
        connect(doc_, &ProjectDocument::modelReset, this, [this] { refresh(); });
        connect(data_, &ObservedData::changed, this, [this] { refresh(); });
        refresh();
    }

private:
    static QPushButton* button(const QString& icon, const QString& textValue, QWidget* parent)
    {
        auto* b = new QPushButton(icons::get(icon), textValue, parent);
        b->setIconSize(QSize(16, 16));
        return b;
    }

    void trigger(const char* action)
    {
        if (auto* a = window()->findChild<QAction*>(action))
            a->trigger();
    }

    void refresh()
    {
        fits_.clear();
        model_->clear();
        const auto& observed = data_->series();
        const auto* results = doc_->results();
        QStringList notes;
        for (const auto& w : data_->warnings())
            notes << w;
        if (observed.empty())
        {
            summary_->setText(tr("No observed data yet. Add measurement files with Project > Observed data.") +
                              (notes.isEmpty() ? QString() : "\n" + notes.join("\n")));
            series_->clear(tr("Add observed data to compare it with the simulation."));
            scatter_->clear(QString(" "));
            return;
        }
        if (!results)
        {
            summary_->setText(tr("%n observed series. Run a simulation to compare them with the results.", nullptr,
                                 static_cast<int>(observed.size())) +
                              (notes.isEmpty() ? QString() : "\n" + notes.join("\n")));
            series_->clear(tr("Run a simulation to compare it with the observed data."));
            scatter_->clear(QString(" "));
            fillTable(observed);
            return;
        }
        base_ = data_->timeBase();
        fits_ = core::evaluateFit(observed, *results, base_);
        const auto cfg = data_->config();
        const double objective = core::objectiveValue(cfg.objective, observed, fits_);
        QStringList terms;
        for (const auto& term : cfg.objective)
            terms << QString::fromLatin1(core::metricName(term.metric));
        int ok = 0;
        for (const auto& f : fits_)
            ok += f.ok ? 1 : 0;
        summary_->setText(tr("%1 of %2 series compared  ·  calibration objective %3 (%4, lower is better)")
                              .arg(ok)
                              .arg(observed.size())
                              .arg(num(objective, 5))
                              .arg(terms.isEmpty() ? QString("NSE") : terms.join(", ")) +
                          (notes.isEmpty() ? QString() : "\n" + notes.join("\n")));
        fillTable(observed);
        if (model_->rowCount() > 0)
            table_->selectRow(0);
        plotSelected();
    }

    void fillTable(const std::vector<core::ObservedSeries>& observed)
    {
        QStringList headers = {tr("Series"), tr("Element"), tr("Result"), tr("Count")};
        for (core::Metric m : core::allMetrics())
            headers << metricHeader(m);
        headers << tr("Rating");
        model_->setHorizontalHeaderLabels(headers);
        auto cell = [](const QString& textValue, const QVariant& sortKey, bool right = false) {
            auto* item = new QStandardItem(textValue);
            item->setData(sortKey, Qt::UserRole);
            if (right)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        for (std::size_t i = 0; i < observed.size(); ++i)
        {
            const auto& s = observed[i];
            QList<QStandardItem*> row;
            auto* name = cell(QString::fromStdString(s.name), QString::fromStdString(s.name));
            name->setData(static_cast<int>(i), Qt::UserRole + 1);
            name->setToolTip(QString::fromStdString(s.source));
            row << name;
            row << cell(QString::fromStdString(s.element), QString::fromStdString(s.element));
            row << cell(QString::fromStdString(s.variable), QString::fromStdString(s.variable));
            const core::SeriesFit* f = i < fits_.size() ? &fits_[i] : nullptr;
            if (!f || !f->ok)
            {
                const int valid = static_cast<int>(std::count_if(s.values.begin(), s.values.end(),
                                                                 [](double v) { return std::isfinite(v); }));
                row << cell(QString::number(valid), valid, true);
                for (std::size_t k = 0; k < core::allMetrics().size(); ++k)
                    row << cell(QString(), std::numeric_limits<double>::lowest(), true);
                auto* why = cell(f ? QString::fromStdString(f->error) : tr("No results"), QString());
                why->setForeground(QColor(140, 60, 60));
                row << why;
                for (auto* item : row)
                    item->setToolTip(f ? QString::fromStdString(f->error) : QString());
                model_->appendRow(row);
                continue;
            }
            row << cell(QString::number(f->metrics.count), f->metrics.count, true);
            int rating = 3;
            for (core::Metric m : core::allMetrics())
            {
                const double v = core::metricValue(m, f->metrics);
                auto* item = cell(num(v), std::isfinite(v) ? QVariant(v) : QVariant(std::numeric_limits<double>::lowest()), true);
                const int band = qualityBand(QString::fromLatin1(core::metricName(m)), v);
                if (band >= 0)
                {
                    item->setBackground(qualityColor(band));
                    item->setToolTip(qualityName(band));
                    if (m == core::Metric::Nse || m == core::Metric::Pbias)
                        rating = std::min(rating, band);
                }
                row << item;
            }
            auto* r = cell(qualityName(rating), rating);
            r->setBackground(qualityColor(rating));
            row << r;
            model_->appendRow(row);
        }
        table_->resizeColumnsToContents();
        for (int c = 0; c < model_->columnCount(); ++c)
            table_->setColumnWidth(c, std::max(64, std::min(240, table_->columnWidth(c) + 14)));
    }

    QString metricHeader(core::Metric m) const
    {
        switch (m)
        {
        case core::Metric::Nse:
            return tr("NSE");
        case core::Metric::Kge:
            return tr("KGE");
        case core::Metric::Pbias:
            return tr("PBIAS (%)");
        case core::Metric::Rmse:
            return tr("RMSE");
        case core::Metric::Mae:
            return tr("MAE");
        case core::Metric::R2:
            return tr("R²");
        case core::Metric::PeakError:
            return tr("Peak error (%)");
        case core::Metric::PeakTiming:
            return tr("Peak timing (h)");
        case core::Metric::VolumeError:
            return tr("Volume error (%)");
        }
        return {};
    }

    int selectedSeries() const
    {
        const QModelIndex idx = table_->currentIndex();
        if (!idx.isValid())
            return -1;
        return proxy_->index(idx.row(), 0).data(Qt::UserRole + 1).toInt();
    }

    void plotSelected()
    {
        const int i = selectedSeries();
        const auto* results = doc_->results();
        if (i < 0 || static_cast<std::size_t>(i) >= fits_.size() || !results)
            return;
        const auto& f = fits_[static_cast<std::size_t>(i)];
        const auto& s = data_->series()[static_cast<std::size_t>(i)];
        if (!f.ok)
        {
            series_->clear(tr("%1 cannot be compared: %2").arg(QString::fromStdString(s.name), QString::fromStdString(f.error)));
            scatter_->clear(QString(" "));
            return;
        }
        const QString unit = QString::fromStdString(f.unit);
        const QString yLabel = QString::fromStdString(s.variable) + (unit.isEmpty() ? QString() : " (" + unit + ")");
        ChartWidget::Series sim;
        sim.name = tr("Simulated");
        sim.color = QColor(31, 111, 235);
        if (const auto full = core::simulatedSeries(*results, s.table, s.element, s.variable))
        {
            for (std::size_t k = 0; k < full->times.size(); ++k)
                sim.points.append(QPointF(full->times[k], full->values[k]));
        }
        ChartWidget::Series obs;
        obs.name = tr("Observed");
        obs.style = ChartWidget::Series::Style::Points;
        obs.color = QColor(224, 82, 60);
        for (std::size_t k = 0; k < f.aligned.times.size(); ++k)
            obs.points.append(QPointF(f.aligned.times[k], f.aligned.observed[k]));
        series_->setXAxis(tr("Time"), true);
        series_->setSeries({sim, obs}, yLabel,
                           QString::fromStdString(s.name) + "  ·  " +
                               tr("NSE %1, KGE %2").arg(num(f.metrics.nse, 3), num(f.metrics.kge, 3)));
        if (results->periodCount() > 0)
            series_->setMarker(results->times[static_cast<std::size_t>(std::min(doc_->period(), results->periodCount() - 1))]);

        // Scatter of simulated against observed, with the 1:1 line and the least-squares fit.
        ChartWidget::Series points;
        points.name = tr("Observations");
        points.style = ChartWidget::Series::Style::Points;
        points.color = QColor(31, 111, 235);
        double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
        double mo = 0, ms = 0;
        const auto& a = f.aligned;
        for (std::size_t k = 0; k < a.observed.size(); ++k)
        {
            points.points.append(QPointF(a.observed[k], a.simulated[k]));
            lo = std::min({lo, a.observed[k], a.simulated[k]});
            hi = std::max({hi, a.observed[k], a.simulated[k]});
            mo += a.observed[k];
            ms += a.simulated[k];
        }
        const double n = static_cast<double>(a.observed.size());
        mo /= n;
        ms /= n;
        double sxy = 0, sxx = 0;
        for (std::size_t k = 0; k < a.observed.size(); ++k)
        {
            sxy += (a.observed[k] - mo) * (a.simulated[k] - ms);
            sxx += (a.observed[k] - mo) * (a.observed[k] - mo);
        }
        ChartWidget::Series oneToOne;
        oneToOne.name = tr("1:1");
        oneToOne.color = QColor(110, 120, 135);
        oneToOne.dashed = true;
        oneToOne.width = 1.4;
        oneToOne.points = {QPointF(lo, lo), QPointF(hi, hi)};
        QVector<ChartWidget::Series> scatter = {oneToOne, points};
        if (sxx > 0)
        {
            const double slope = sxy / sxx;
            const double icpt = ms - slope * mo;
            ChartWidget::Series fit;
            fit.name = tr("Fit: y = %1x %2 %3").arg(num(slope, 3), icpt < 0 ? "-" : "+", num(std::fabs(icpt), 3));
            fit.color = QColor(27, 158, 119);
            fit.width = 1.8;
            fit.points = {QPointF(lo, icpt + slope * lo), QPointF(hi, icpt + slope * hi)};
            scatter << fit;
        }
        scatter_->setXAxis(tr("Observed (%1)").arg(unit), false);
        scatter_->setSeries(scatter, tr("Simulated (%1)").arg(unit), tr("R² %1").arg(num(f.metrics.r2, 3)));
    }

    void exportCsv()
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
        const QString f = QFileDialog::getSaveFileName(this, tr("Export"), "calibration-metrics.csv", tr("CSV (*.csv)"));
        if (f.isEmpty())
            return;
        QFile file(f);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            file.write(out.toUtf8());
    }

    ProjectDocument* doc_;
    ObservedData* data_;
    core::TimeBase base_;
    std::vector<core::SeriesFit> fits_;
    QLabel* summary_;
    QStandardItemModel* model_;
    QSortFilterProxyModel* proxy_;
    QTableView* table_;
    ChartWidget* series_;
    ChartWidget* scatter_;
};
} // namespace

QWidget* createCalibrationReport(ProjectDocument* doc, QWidget* parent)
{
    return new CalibrationReport(doc, parent);
}

} // namespace mirab::app

#include "CalibrationReport.moc"
