/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ReportExport.cpp
 * Purpose: Report document: project summary, simulation status, map and report tabs as PDF or HTML.
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
#include "ReportExport.h"

#include "Language.h"
#include "MapDecorations.h"
#include "MapView.h"
#include "Theme.h"
#include "mirab/MirabConfig.h"
#include "mirab/core/PrintLayout.h"
#include "mirab/core/Text.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextObjectInterface>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mirab::app
{

namespace
{
constexpr double kMm = decor::kUnitsPerMm;
constexpr int kFigureType = QTextFormat::UserObject + 41;
constexpr int kFigureIndex = QTextFormat::UserProperty + 41;
const QString kFigureScheme = QStringLiteral("mirab-figure:");

double pt(double points)
{
    return points * 96.0 / 72.0;
}

QString esc(const QString& s)
{
    return s.toHtmlEscaped();
}

QString duration(double seconds)
{
    // Routing steps of a few seconds would read as 0:00.
    if (seconds > 0 && seconds < 60)
        return QStringLiteral("%1 s").arg(QString::fromStdString(text::formatDouble(seconds, 4)));
    return QString::fromStdString(text::formatDuration(seconds));
}

QString kvTable(const QVector<std::pair<QString, QString>>& rows)
{
    QString h = "<table class=\"kv\" width=\"100%\" cellspacing=\"0\" cellpadding=\"4\">";
    for (const auto& [k, v] : rows)
        h += "<tr><td class=\"key\" width=\"32%\">" + esc(k) + "</td><td>" + esc(v) + "</td></tr>";
    return h + "</table>";
}

/// Draws figures (charts and the map) as vector graphics inside a QTextDocument.
class FigureObject : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    FigureObject(const QVector<ReportBuilder::Figure>* figures, MapView* map)
        : figures_(figures), map_(map)
    {
    }
    QSizeF intrinsicSize(QTextDocument*, int, const QTextFormat& format) override
    {
        const int i = format.property(kFigureIndex).toInt();
        return i >= 0 && i < figures_->size() ? (*figures_)[i].size : QSizeF(1, 1);
    }
    void drawObject(QPainter* p, const QRectF& rect, QTextDocument*, int,
                    const QTextFormat& format) override
    {
        const int i = format.property(kFigureIndex).toInt();
        if (i >= 0 && i < figures_->size())
            paintFigure(*p, rect, (*figures_)[i], map_, 2.1, true);
    }

private:
    const QVector<ReportBuilder::Figure>* figures_;
    MapView* map_;
};

const char* kPdfCss = R"(
p.kicker { font-size: 9pt; color: #1f6feb; font-weight: bold; }
p.title { font-size: 24pt; font-weight: bold; color: #1f2a37; }
p.meta { font-size: 9.5pt; color: #607080; }
p.tochead { font-size: 13pt; font-weight: bold; color: #1f2a37; }
h1 { font-size: 15pt; font-weight: bold; color: #1f2a37; }
p.caption, p.muted { font-size: 8.5pt; color: #607080; }
td.key { color: #607080; }
table.data { font-size: 7.5pt; }
th { font-weight: bold; }
pre { font-size: 7pt; }
)";

const char* kHtmlCss = R"(
:root { color-scheme: light; }
body { margin: 0; background: #eef1f5; color: #1f2a37;
  font: 15px/1.55 -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; }
main { max-width: 920px; margin: 0 auto; padding: 44px 40px 80px; background: #fff; box-sizing: border-box; }
.kicker { margin: 0; font-size: 12px; font-weight: 700; letter-spacing: .08em; text-transform: uppercase; color: #1f6feb; }
.title { margin: 6px 0 8px; font-size: 32px; line-height: 1.2; font-weight: 700; }
.meta { margin: 0; color: #607080; }
nav.toc { margin: 28px 0 8px; padding: 14px 20px; background: #f6f8fb; border: 1px solid #e3e8ef; border-radius: 8px; }
nav.toc h2 { margin: 0 0 6px; font-size: 15px; }
nav.toc ol { margin: 0; padding-left: 22px; }
nav.toc a { color: #1f6feb; text-decoration: none; }
h1 { margin: 44px 0 12px; padding-bottom: 6px; font-size: 22px; border-bottom: 2px solid #1f6feb; }
p.muted, p.caption { color: #607080; font-size: 13.5px; }
table { width: 100%; border-collapse: collapse; margin: 10px 0 20px; font-size: 13.5px; }
table.kv td { padding: 6px 8px; border-bottom: 1px solid #e8ecf1; vertical-align: top; }
table.kv td.key { width: 32%; color: #607080; }
table.data { font-size: 12.5px; font-variant-numeric: tabular-nums; }
table.data th { position: sticky; top: 0; background: #f2f5f9; text-align: left; padding: 6px 8px; border-bottom: 1px solid #c7cfd9; }
table.data td { padding: 4px 8px; border-bottom: 1px solid #eef1f5; }
table.data tbody tr:nth-child(even) { background: #fafbfd; }
figure { margin: 16px 0 6px; }
figure img { display: block; max-width: 100%; height: auto; border: 1px solid #e3e8ef; border-radius: 4px; }
pre { font-size: 12px; background: #f7f9fc; border: 1px solid #e3e8ef; border-radius: 6px; padding: 12px; overflow: auto; }
@media (max-width: 640px) { main { padding: 24px 16px 48px; } .title { font-size: 26px; } }
@media print { body { background: #fff; } main { padding: 0; max-width: none; } h1 { break-after: avoid; } figure, table.kv { break-inside: avoid; } }
)";
} // namespace

// ---------------------------------------------------------------------------
// Figures
// ---------------------------------------------------------------------------
void paintFigure(QPainter& p, const QRectF& rect, const ReportBuilder::Figure& figure, MapView* map,
                 double pixelRatio, bool vector)
{
    p.save();
    p.setLayoutDirection(Qt::LayoutDirectionAuto); // see ChartWidget::draw()
    if (!figure.map)
    {
        ChartWidget::paint(p, rect, decor::font(pt(7.5)), figure.chart, true);
        p.setPen(QPen(theme::kBorder, 0.8));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect);
        p.restore();
        return;
    }
    MapView::RenderOptions o;
    o.font = decor::font(pt(7));
    o.pixelRatio = pixelRatio;
    o.tileDetail = 1.3;
    o.rasterBackground = vector;
    map->render(p, rect, o);
    const double m = 3 * kMm;
    decor::legend(&p, rect.topLeft() + QPointF(m, m), map->legend(),
                  map->legend().title.isEmpty() ? map->elementKinds() : QStringList(),
                  decor::font(pt(7)), true, rect.height() * 0.8);
    decor::northArrow(p, QRectF(rect.right() - m - 11 * kMm, rect.top() + m, 11 * kMm, 11 * kMm),
                      map->northAngle(), decor::font(pt(7)), true);
    QString unit;
    const double gpu = map->groundPerUnit(rect, &unit);
    decor::scaleBar(&p, QPointF(rect.left() + m + 2 * kMm, rect.bottom() - m - 1 * kMm), gpu, unit,
                    rect.width() * 0.3, decor::font(pt(7)), true);
    const QString attribution = map->attribution();
    if (!attribution.isEmpty())
    {
        const QFont af = decor::font(pt(5.5));
        const QFontMetricsF afm(af);
        const double w = afm.horizontalAdvance(attribution) + 2 * kMm;
        const QRectF box(rect.right() - w, rect.bottom() - afm.height() - 0.8 * kMm, w,
                         afm.height() + 0.8 * kMm);
        p.fillRect(box, QColor(255, 255, 255, 215));
        p.setFont(af);
        p.setPen(QColor(60, 70, 85));
        p.drawText(box, Qt::AlignCenter, attribution);
    }
    p.setPen(QPen(QColor(70, 80, 95), 0.8));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect);
    p.restore();
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------
ReportBuilder::ReportBuilder(MapView* map, ProjectDocument* doc, PageComposer::ReportSource reports)
    : map_(map), doc_(doc), reports_(std::move(reports))
{
}

QStringList ReportBuilder::defaultReports(core::Dialect dialect)
{
    QStringList kinds = {"systemflow", "variability", "frequency", "profile"};
    if (dialect == core::Dialect::Epanet)
        kinds << "energy";
    kinds << "calibration" << "tables";
    return kinds;
}

void ReportBuilder::build(const ReportOptions& o)
{
    sections_.clear();
    figures_.clear();
    const auto& model = doc_->model();
    const auto* results = doc_->results();
    auto addFigure = [this](Figure f)
    {
        figures_.append(std::move(f));
        return QStringLiteral("<!--figure:%1-->").arg(figures_.size() - 1);
    };

    if (o.summary)
    {
        const auto s = core::summarizeProject(model, doc_->settings());
        const bool epanet = s.dialect == core::Dialect::Epanet;
        QVector<std::pair<QString, QString>> rows;
        rows.append(
            {tr("Title"), s.title.empty() ? tr("(none)") : QString::fromStdString(s.title)});
        if (!doc_->path().isEmpty())
            rows.append({tr("Input file"), QDir::toNativeSeparators(doc_->path())});
        rows.append({tr("Model type"), epanet ? tr("Water distribution (EPANET)")
                                              : tr("Stormwater and drainage (SWMM)")});
        if (!o.engine.isEmpty())
            rows.append({tr("Engine"), o.engine});
        rows.append({tr("Flow units"), QString::fromStdString(s.units.flow)});
        if (epanet && !s.units.pressure.empty())
            rows.append({tr("Pressure units"), QString::fromStdString(s.units.pressure)});
        rows.append({tr("Unit system"), s.units.system == core::UnitSystem::SI
                                            ? tr("SI (metric)")
                                            : tr("US customary")});
        auto counts = [](const std::vector<std::pair<std::string, int>>& list)
        {
            QStringList parts;
            int total = 0;
            for (const auto& [kind, n] : list)
            {
                parts << QStringLiteral("%1 %2").arg(coreText(kind)).arg(n);
                total += n;
            }
            return parts.isEmpty() ? QStringLiteral("0")
                                   : QStringLiteral("%1  (%2)").arg(total).arg(parts.join(", "));
        };
        rows.append({tr("Nodes"), counts(s.nodeKinds)});
        rows.append({tr("Links"), counts(s.linkKinds)});
        if (!epanet)
        {
            rows.append({tr("Subcatchments"), QString::number(s.subcatchments)});
            rows.append({tr("Rain gages"), QString::number(s.rainGages)});
        }
        rows.append({tr("Simulation duration"), duration(s.durationSeconds)});
        if (s.hydraulicStepSeconds > 0)
            rows.append({epanet ? tr("Hydraulic time step") : tr("Routing time step"),
                         duration(s.hydraulicStepSeconds)});
        if (s.reportStepSeconds > 0)
            rows.append({tr("Reporting time step"), duration(s.reportStepSeconds)});
        if (!s.demandModel.empty())
            rows.append({tr("Demand model"), s.demandModel == "PDA" ? tr("Pressure driven (PDA)")
                                                                    : tr("Demand driven (DDA)")});
        QString crs = map_->coordinateSystemName();
        if (crs.isEmpty())
            crs = QString::fromStdString(s.crs);
        rows.append({tr("Coordinate system"), crs.isEmpty() ? tr("Not set") : crs});
        sections_.append({tr("Project summary"), kvTable(rows)});
    }

    if (o.status)
    {
        QString body;
        if (!results)
        {
            body = "<p class=\"muted\">" +
                   esc(tr("No results are loaded. Run a simulation before exporting the report.")) +
                   "</p>";
        }
        else
        {
            const auto run = core::summarizeRun(*results);
            QVector<std::pair<QString, QString>> rows;
            rows.append(
                {tr("Engine"),
                 QString::fromStdString(run.engineName.empty() ? run.engineId : run.engineName)});
            rows.append({tr("Reporting periods"),
                         tr("%1, from %2 to %3")
                             .arg(run.periods)
                             .arg(duration(run.firstSeconds), duration(run.lastSeconds))});
            if (run.reportStepSeconds > 0)
                rows.append({tr("Reporting time step"), duration(run.reportStepSeconds)});
            for (const auto& [name, pct] : run.continuity)
            {
                const QString label =
                    name == "runoff" ? tr("Runoff continuity error")
                    : name == "flow" ? tr("Flow routing continuity error")
                    : name == "quality"
                        ? tr("Quality routing continuity error")
                        : tr("Continuity error (%1)").arg(QString::fromStdString(name));
                const double a = std::fabs(pct);
                const QString rating = a <= 1.0    ? tr("good")
                                       : a <= 10.0 ? tr("acceptable")
                                                   : tr("high, check the model");
                rows.append(
                    {label, QStringLiteral("%1 %  (%2)")
                                .arg(QString::fromStdString(text::formatDouble(pct, 3)), rating)});
            }
            if (run.continuity.empty())
                rows.append({tr("Continuity errors"), tr("Not reported by this engine")});
            rows.append({tr("Messages"),
                         run.messages.empty() ? tr("None") : QString::number(run.messages.size())});
            body = kvTable(rows);
            if (!run.messages.empty())
            {
                body += "<p>" + esc(tr("Messages from the engine:")) + "</p><ul>";
                for (std::size_t i = 0; i < run.messages.size() && i < 60; ++i)
                    body += "<li>" + esc(QString::fromStdString(run.messages[i])) + "</li>";
                body += "</ul>";
            }
        }
        sections_.append({tr("Simulation status"), body});
    }

    if (o.map)
    {
        Figure f;
        f.map = true;
        const double aspect =
            map_->width() > 0
                ? std::clamp(static_cast<double>(map_->height()) / map_->width(), 0.45, 0.9)
                : 0.66;
        f.size = QSizeF(contentWidth_, std::min(contentWidth_ * aspect, contentHeight_ * 0.8));
        const MapView::Legend legend = map_->legend();
        QString caption = tr("The network as shown on the map.");
        if (!legend.title.isEmpty() && results)
            caption = tr("%1 at %2 of the simulation.")
                          .arg(legend.title,
                               duration(results->times[static_cast<std::size_t>(doc_->period())]));
        sections_.append(
            {tr("Map"), "<p class=\"caption\">" + esc(caption) + "</p>" + addFigure(f)});
    }

    for (const QString& kind : o.reports)
    {
        const ReportSnapshot r = reports_ ? reports_(kind) : ReportSnapshot();
        if (r.title.isEmpty() || (kind == "calibration" && r.rows.isEmpty() && r.charts.isEmpty()))
            continue; // no observed data: nothing to compare
        QString body;
        if (!r.description.isEmpty())
            body += "<p class=\"muted\">" + esc(r.description) + "</p>";
        if (r.isEmpty())
            body += "<p>" +
                    esc(tr("Nothing to show yet. Run a simulation, or open the report and make a "
                           "choice.")) +
                    "</p>";
        for (const QString& n : r.notes)
            body += "<p>" + esc(n) + "</p>";
        for (const auto& chart : r.charts)
        {
            Figure f;
            f.chart = chart;
            f.size = QSizeF(contentWidth_, std::min(contentWidth_ * 0.46, contentHeight_ * 0.6));
            body += addFigure(f);
        }
        if (!r.rows.isEmpty())
        {
            const int shown = std::min<int>(o.maxRows, r.rows.size());
            if (shown < r.rows.size())
                body += "<p class=\"muted\">" +
                        esc(tr("The first %1 of %2 rows. Export the report as CSV for all rows.")
                                .arg(shown)
                                .arg(r.rows.size())) +
                        "</p>";
            body += "<table class=\"data\" width=\"100%\" cellspacing=\"0\" cellpadding=\"3\" "
                    "border=\"1\" "
                    "style=\"border-collapse: collapse; border-color: #d5dce5; border-style: "
                    "solid;\"><thead><tr bgcolor=\"#f2f5f9\">";
            for (int c = 0; c < r.header.size(); ++c)
            {
                const bool num = c < r.numeric.size() && r.numeric[c];
                body += QStringLiteral("<th align=\"%1\">").arg(num ? "right" : "left") +
                        esc(r.header[c]) + "</th>";
            }
            body += "</tr></thead><tbody>";
            for (int i = 0; i < shown; ++i)
            {
                body += "<tr>";
                for (int c = 0; c < r.header.size(); ++c)
                {
                    const bool num = c < r.numeric.size() && r.numeric[c];
                    body += QStringLiteral("<td align=\"%1\">").arg(num ? "right" : "left") +
                            esc(c < r.rows[i].size() ? r.rows[i][c] : QString()) + "</td>";
                }
                body += "</tr>";
            }
            body += "</tbody></table>";
        }
        if (!r.text.isEmpty())
        {
            QStringList lines = r.text.split('\n');
            const int limit = std::max(200, o.maxRows * 10);
            if (lines.size() > limit)
            {
                body += "<p class=\"muted\">" +
                        esc(tr("The first %1 of %2 lines of the report file.")
                                .arg(limit)
                                .arg(lines.size())) +
                        "</p>";
                lines = lines.mid(0, limit);
            }
            body += "<pre>" + esc(lines.join('\n')) + "</pre>";
        }
        sections_.append({r.title, body});
    }
}

QString ReportBuilder::figureHtml(int index, bool pdf) const
{
    const Figure& f = figures_[index];
    const int w = static_cast<int>(std::lround(f.size.width()));
    const int h = static_cast<int>(std::lround(f.size.height()));
    if (pdf)
    {
        const bool newPage = index < figureBreaks_.size() && figureBreaks_[index];
        return QStringLiteral(
                   "<p align=\"center\"%1><img src=\"%2%3\" width=\"%4\" height=\"%5\"></p>")
            .arg(newPage ? QStringLiteral(" style=\"page-break-before: always;\"") : QString(),
                 kFigureScheme)
            .arg(index)
            .arg(w)
            .arg(h);
    }
    // Charts and the map as PNG images at twice the size, for sharp display.
    QImage img(QSize(w * 2, h * 2), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(2.0);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        paintFigure(p, QRectF(0, 0, w, h), f, map_, 2.0);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    const QString alt = f.map ? tr("Map of the network")
                              : (f.chart.title.isEmpty() ? f.chart.yLabel : f.chart.title);
    return QStringLiteral("<figure><img src=\"data:image/png;base64,%1\" width=\"%2\" "
                          "height=\"%3\" alt=\"%4\"></figure>")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(w)
        .arg(h)
        .arg(esc(alt));
}

QString ReportBuilder::document(const ReportOptions& o, bool pdf) const
{
    QString h;
    if (pdf)
        h += "<html><body>";
    else
        h += "<!DOCTYPE html>\n<html lang=\"" + QString(currentLanguage()).replace('_', '-') + "\"" +
             (QGuiApplication::layoutDirection() == Qt::RightToLeft ? " dir=\"rtl\"" : "") +
             ">\n<head>\n<meta charset=\"utf-8\">\n"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>" +
             esc(o.title) + "</title>\n<style>" + QString::fromLatin1(kHtmlCss) +
             "</style>\n</head>\n<body>\n<main>\n";

    // Cover
    QStringList meta;
    meta << QLocale().toString(QDate::currentDate(), QLocale::LongFormat);
    if (!o.author.trimmed().isEmpty())
        meta << o.author.trimmed();
    if (!doc_->path().isEmpty())
        meta << QFileInfo(doc_->path()).fileName();
    h += "<p class=\"kicker\">" + esc(tr("Model report")) + "</p>";
    h += "<p class=\"title\">" + esc(o.title) + "</p>";
    h += "<p class=\"meta\">" + esc(meta.join("  ·  ")) + "</p>";

    // Contents
    if (pdf)
    {
        h += "<p class=\"tochead\"><br>" + esc(tr("Contents")) +
             "</p><table width=\"100%\" cellspacing=\"0\" cellpadding=\"3\">";
        for (int i = 0; i < sections_.size(); ++i)
            h += QStringLiteral("<tr><td width=\"8%\">%1</td><td>%2</td><td align=\"right\" "
                                "width=\"12%\">%3</td></tr>")
                     .arg(i + 1)
                     .arg(esc(sections_[i].title), i < tocPages_.size() ? tocPages_[i] : QString());
        h += "</table>";
    }
    else
    {
        h += "<nav class=\"toc\"><h2>" + esc(tr("Contents")) + "</h2><ol>";
        for (int i = 0; i < sections_.size(); ++i)
            h += QStringLiteral("<li><a href=\"#s%1\">%2</a></li>")
                     .arg(i + 1)
                     .arg(esc(sections_[i].title));
        h += "</ol></nav>\n";
    }

    for (int i = 0; i < sections_.size(); ++i)
    {
        const Section& s = sections_[i];
        const bool newPage = i == 0 || (i < sectionBreaks_.size() && sectionBreaks_[i]);
        const QString breakStyle =
            pdf && newPage ? QStringLiteral(" style=\"page-break-before: always;\"") : QString();
        // QTextDocument (PDF) knows anchors by name, browsers by id.
        const QString anchor = pdf ? QStringLiteral("<a name=\"s%1\"></a>").arg(i + 1) : QString();
        const QString number = QString::number(i + 1);
        h += "\n<h1 id=\"s" + number + "\"" + breakStyle + ">" + anchor + number + "&nbsp;&nbsp;" +
             esc(s.title) + "</h1>\n";
        QString body = s.body;
        for (int f = 0; f < figures_.size(); ++f)
        {
            const QString marker = QStringLiteral("<!--figure:%1-->").arg(f);
            if (body.contains(marker))
                body.replace(marker, figureHtml(f, pdf));
        }
        h += body;
    }
    h += pdf ? "</body></html>" : "\n</main>\n</body>\n</html>\n";
    return h;
}

bool ReportBuilder::exportHtml(const QString& path, const ReportOptions& o, QString* error)
{
    contentWidth_ = 838;
    contentHeight_ = 1e6;
    tocPages_.clear();
    sectionBreaks_.clear();
    figureBreaks_.clear();
    build(o);
    for (const Figure& f : figures_)
    {
        if (f.map)
        {
            MapView::RenderOptions ro;
            ro.tileDetail = 1.3;
            map_->waitForBasemap(QRectF(QPointF(0, 0), f.size), ro);
        }
    }
    const QString html = document(o, false);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(html.toUtf8()) < 0)
    {
        if (error)
            *error = tr("%1 could not be written.").arg(path);
        return false;
    }
    return true;
}

bool ReportBuilder::exportPdf(const QString& path, const ReportOptions& o, QString* error)
{
    const core::layout::Paper* paper = core::layout::findPaper(o.paper);
    if (!paper)
        paper = &core::layout::papers().front();
    const double pageW = (o.landscape ? paper->heightMm : paper->widthMm) * kMm;
    const double pageH = (o.landscape ? paper->widthMm : paper->heightMm) * kMm;
    const double side = 20 * kMm, top = 20 * kMm, bottom = 18 * kMm;
    const QSizeF content(pageW - 2 * side, pageH - top - bottom);
    contentWidth_ = content.width();
    contentHeight_ = content.height();
    build(o);
    for (const Figure& f : figures_)
    {
        if (f.map)
        {
            MapView::RenderOptions ro;
            ro.tileDetail = 1.3;
            map_->waitForBasemap(QRectF(QPointF(0, 0), f.size), ro);
        }
    }

    // Lay the document out in units of 1/96 inch on every platform. The
    // document is not edited after pagination (QTextDocument lays out edits
    // lazily), so the table of contents takes two passes.
    QImage reference(1, 1, QImage::Format_RGB32);
    reference.setDotsPerMeterX(3780);
    reference.setDotsPerMeterY(3780);
    FigureObject figures(&figures_, map_);
    auto layout = [&](QTextDocument& doc)
    {
        doc.documentLayout()->setPaintDevice(&reference);
        doc.documentLayout()->registerHandler(kFigureType, &figures);
        doc.setDocumentMargin(0);
        QFont base = QApplication::font();
        base.setPointSizeF(9.5);
        doc.setDefaultFont(base);
        doc.setDefaultStyleSheet(QString::fromLatin1(kPdfCss));
        doc.setHtml(document(o, true));
        // Replace the figure placeholders with vector objects.
        QVector<std::pair<int, int>> spots;
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        {
            for (auto it = b.begin(); !it.atEnd(); ++it)
            {
                const QTextFragment frag = it.fragment();
                if (!frag.charFormat().isImageFormat())
                    continue;
                const QString name = frag.charFormat().toImageFormat().name();
                if (name.startsWith(kFigureScheme))
                    spots.append({frag.position(), name.mid(kFigureScheme.size()).toInt()});
            }
        }
        for (int i = spots.size() - 1; i >= 0; --i)
        {
            QTextCursor c(&doc);
            c.setPosition(spots[i].first);
            c.setPosition(spots[i].first + 1, QTextCursor::KeepAnchor);
            QTextCharFormat f;
            f.setObjectType(kFigureType);
            f.setProperty(kFigureIndex, spots[i].second);
            c.insertText(QString(QChar::ObjectReplacementCharacter), f);
        }
        doc.setPageSize(content);
        doc.pageCount(); // completes the layout
    };
    // Trial layouts find the page of each section for the contents, and add
    // page breaks so that a figure never straddles two pages and a heading
    // never sits at the bottom of a page away from its first figure or table.
    tocPages_ = QStringList(sections_.size(), QStringLiteral("00"));
    sectionBreaks_ = QVector<bool>(sections_.size(), false);
    figureBreaks_ = QVector<bool>(figures_.size(), false);
    for (int pass = 0; pass < 6; ++pass)
    {
        QTextDocument trial;
        layout(trial);
        tocPages_.clear();
        bool moved = false;
        int open = -1; // section whose first figure or table is not reached yet
        int openPage = 0;
        for (QTextBlock b = trial.begin(); b.isValid(); b = b.next())
        {
            const QRectF rect = trial.documentLayout()->blockBoundingRect(b);
            // A line that does not fit moves to the next page while the block stays: use the first
            // line.
            double y = rect.top();
            if (b.layout() && b.layout()->lineCount() > 0)
                y += b.layout()->lineAt(0).y();
            const int page = static_cast<int>(y / content.height());
            if (b.blockFormat().headingLevel() == 1)
            {
                open = static_cast<int>(tocPages_.size());
                openPage = page;
                tocPages_ << QString::number(page + 1);
                continue;
            }
            const bool figure = b.text().contains(QChar::ObjectReplacementCharacter);
            if (!figure && !(open >= 0 && QTextCursor(b).currentTable()))
                continue;
            const bool first = open >= 0;
            const bool headingAlone =
                first && open > 0 && open < sectionBreaks_.size() && !sectionBreaks_[open];
            if (figure)
            {
                const int index = b.begin().fragment().charFormat().property(kFigureIndex).toInt();
                const int lastPage = static_cast<int>((rect.bottom() - 1) / content.height());
                const bool straddles = lastPage > page;
                if ((straddles || page > openPage) && first &&
                    openPage == page - (straddles ? 0 : 1) && headingAlone)
                    sectionBreaks_[open] = moved = true;
                else if (straddles && index >= 0 && index < figureBreaks_.size() &&
                         !figureBreaks_[index])
                    figureBreaks_[index] = moved = true;
            }
            else if (page > openPage && headingAlone)
            {
                sectionBreaks_[open] = moved = true;
            }
            open = -1;
        }
        if (!moved)
            break;
    }
    QTextDocument doc;
    layout(doc);

    QPdfWriter writer(path);
    writer.setResolution(1200);
    writer.setPageLayout(QPageLayout(
        QPageSize(QSizeF(paper->widthMm, paper->heightMm), QPageSize::Millimeter,
                  QString::fromStdString(paper->id)),
        o.landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
    writer.setTitle(o.title);
    writer.setCreator(QStringLiteral("Mirab " MIRAB_VERSION_STRING));
    QPainter p;
    if (!p.begin(&writer))
    {
        if (error)
            *error = tr("%1 could not be written.").arg(path);
        return false;
    }
    const double scale = writer.resolution() / 96.0;
    p.scale(scale, scale);
    const int pages = doc.pageCount();
    const QFont small = decor::font(pt(7.5));
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette.setColor(QPalette::Text, theme::kText);
    for (int i = 0; i < pages; ++i)
    {
        if (i > 0)
            writer.newPage();
        p.save();
        p.translate(side, top - i * content.height());
        const QRectF clip(0, i * content.height(), content.width(), content.height());
        p.setClipRect(clip);
        ctx.clip = clip;
        doc.documentLayout()->draw(&p, ctx);
        p.restore();

        p.setFont(small);
        p.setPen(theme::kTextMuted);
        const double footerY = pageH - bottom + 6 * kMm;
        p.drawText(QRectF(side, footerY, content.width(), 5 * kMm),
                   Qt::AlignLeft | Qt::AlignVCenter, o.title);
        p.drawText(QRectF(side, footerY, content.width(), 5 * kMm),
                   Qt::AlignRight | Qt::AlignVCenter, tr("Page %1 of %2").arg(i + 1).arg(pages));
        p.setPen(QPen(theme::kBorder, 0.6));
        p.drawLine(QPointF(side, footerY), QPointF(side + content.width(), footerY));
    }
    return p.end();
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------
ReportDialog::ReportDialog(MapView* map, ProjectDocument* doc, PageComposer::ReportSource reports,
                           const QString& engine, QWidget* parent)
    : QDialog(parent), map_(map), doc_(doc), reports_(std::move(reports)), engine_(engine)
{
    setWindowTitle(tr("Export report"));
    resize(520, 600);
    auto* layout = new QVBoxLayout(this);
    auto* intro =
        new QLabel(tr("A document with the project summary, the simulation status, the map and the "
                      "reports you "
                      "choose. Open report tabs keep the elements and results chosen in them."),
                   this);
    intro->setWordWrap(true);
    intro->setObjectName("muted");
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    QString title = QString::fromStdString(doc_->model().title()).trimmed();
    if (title.isEmpty())
        title = doc_->path().isEmpty() ? tr("Untitled network")
                                       : QFileInfo(doc_->path()).completeBaseName();
    title_ = new QLineEdit(title, this);
    title_->setCursorPosition(0);
    author_ = new QLineEdit(QSettings().value("report/author").toString(), this);
    author_->setPlaceholderText(tr("Name or organization (optional)"));
    form->addRow(tr("Title"), title_);
    form->addRow(tr("Prepared by"), author_);
    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("Sections"), this));
    sections_ = new QListWidget(this);
    auto add = [this](const QString& text, const QString& key, bool on)
    {
        auto* item = new QListWidgetItem(text, sections_);
        item->setData(Qt::UserRole, key);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    };
    add(tr("Project summary"), "#summary", true);
    add(tr("Simulation status"), "#status", true);
    add(tr("Map"), "#map", true);
    const QStringList defaults = ReportBuilder::defaultReports(doc_->model().dialect());
    for (const auto& r : availableReports(doc_->model().dialect()))
        add(r.title, r.kind, defaults.contains(r.kind));
    layout->addWidget(sections_, 1);

    auto* options = new QFormLayout;
    rows_ = new QSpinBox(this);
    rows_->setRange(10, 100000);
    rows_->setValue(200);
    rows_->setSuffix(tr(" rows"));
    options->addRow(tr("Tables up to"), rows_);
    auto* pageRow = new QHBoxLayout;
    paper_ = new QComboBox(this);
    for (const auto& p : core::layout::papers())
        paper_->addItem(QString::fromStdString(p.id));
    orientation_ = new QComboBox(this);
    orientation_->addItem(tr("Portrait"));
    orientation_->addItem(tr("Landscape"));
    pageRow->addWidget(paper_, 1);
    pageRow->addWidget(orientation_, 1);
    options->addRow(tr("PDF page"), pageRow);
    layout->addLayout(options);

    status_ = new QLabel(this);
    status_->setObjectName("muted");
    status_->setWordWrap(true);
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(this);
    auto* pdf = buttons->addButton(tr("Export PDF..."), QDialogButtonBox::AcceptRole);
    pdf->setProperty("primary", true);
    // Room for the bold text of the primary button.
    pdf->setMinimumWidth(static_cast<int>(pdf->fontMetrics().horizontalAdvance(pdf->text()) * 1.2) +
                         28);
    auto* html = buttons->addButton(tr("Export HTML..."), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(pdf, &QPushButton::clicked, this, [this] { exportAs(true); });
    connect(html, &QPushButton::clicked, this, [this] { exportAs(false); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ReportOptions ReportDialog::options() const
{
    ReportOptions o;
    o.title = title_->text().trimmed();
    o.author = author_->text().trimmed();
    o.engine = engine_;
    o.summary = o.status = o.map = false;
    for (int i = 0; i < sections_->count(); ++i)
    {
        const QListWidgetItem* item = sections_->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        const QString key = item->data(Qt::UserRole).toString();
        if (key == "#summary")
            o.summary = true;
        else if (key == "#status")
            o.status = true;
        else if (key == "#map")
            o.map = true;
        else
            o.reports << key;
    }
    o.maxRows = rows_->value();
    o.paper = paper_->currentText().toStdString();
    o.landscape = orientation_->currentIndex() == 1;
    return o;
}

void ReportDialog::exportAs(bool pdf)
{
    const ReportOptions o = options();
    QSettings().setValue("report/author", o.author);
    const QString base = doc_->path().isEmpty()
                             ? QStringLiteral("report")
                             : QFileInfo(doc_->path()).completeBaseName() + "-report";
    const QString f = QFileDialog::getSaveFileName(
        this, pdf ? tr("Export report as PDF") : tr("Export report as HTML"),
        base + (pdf ? ".pdf" : ".html"),
        pdf ? tr("PDF document (*.pdf)") : tr("Web page (*.html)"));
    if (f.isEmpty())
        return;
    QProgressDialog wait(tr("Building the report..."), QString(), 0, 0, this);
    wait.setWindowModality(Qt::WindowModal);
    wait.setMinimumDuration(200);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ReportBuilder builder(map_, doc_, reports_);
    QString error;
    const bool ok = pdf ? builder.exportPdf(f, o, &error) : builder.exportHtml(f, o, &error);
    QApplication::restoreOverrideCursor();
    if (ok)
        status_->setText(tr("Saved %1").arg(QFileInfo(f).fileName()));
    else
        QMessageBox::warning(this, tr("Export failed"), error);
}

} // namespace mirab::app

#include "ReportExport.moc"
