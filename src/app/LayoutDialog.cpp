/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/LayoutDialog.cpp
 * Purpose: Print layout: a page with the map, legend, scale bar, north arrow, texts and a report
 * chart.
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
#include "LayoutDialog.h"

#include "MapDecorations.h"
#include "MapView.h"
#include "Theme.h"
#include "mirab/MirabConfig.h"
#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mirab::app
{

using core::layout::Template;

namespace
{
constexpr double kMm = decor::kUnitsPerMm;

double pt(double points)
{
    return points * 96.0 / 72.0;
}

QRectF units(const core::layout::RectMm& r)
{
    return QRectF(r.x * kMm, r.y * kMm, r.w * kMm, r.h * kMm);
}

QPageSize pageSizeFor(const std::string& paper)
{
    const QString id = QString::fromStdString(paper).toUpper();
    if (id == "A3")
        return QPageSize(QPageSize::A3);
    if (id == "LETTER")
        return QPageSize(QPageSize::Letter);
    if (id == "TABLOID")
        return QPageSize(QPageSize::Tabloid);
    return QPageSize(QPageSize::A4);
}

QPageLayout pageLayoutFor(const LayoutSettings& s)
{
    return QPageLayout(pageSizeFor(s.page.paper),
                       s.page.landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                       QMarginsF(0, 0, 0, 0));
}

core::layout::PageOptions pageOptions(const LayoutSettings& s)
{
    core::layout::PageOptions o = s.page;
    o.subtitle = !s.subtitle.trimmed().isEmpty();
    return o;
}

MapView::RenderOptions mapOptions(const LayoutSettings& s, double pixelRatio)
{
    MapView::RenderOptions o;
    o.basemap = s.basemap;
    o.font = decor::font(pt(7));
    o.pixelRatio = pixelRatio;
    // The same tiles in the preview and every output, with labels a little
    // smaller than on screen.
    o.tileDetail = 1.3;
    return o;
}

/// Device pixels per painter unit of the map background in PDF files and prints (about 200 dpi).
constexpr double kVectorDetail = 2.1;
} // namespace

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
LayoutSettings LayoutSettings::load(const ProjectDocument* doc, const MapView* map)
{
    LayoutSettings s;
    QSettings st;
    st.beginGroup("layout");
    s.page.paper = st.value("paper", "A4").toString().toStdString();
    s.page.landscape = st.value("landscape", true).toBool();
    s.page.layout =
        st.value("template").toString() == "chart" ? Template::ChartBelow : Template::LegendSidebar;
    s.page.split = st.value("split", 0.75).toDouble();
    s.notes = st.value("notes").toString();
    s.logo = st.value("logo").toString();
    s.report = st.value("report", "systemflow").toString();
    s.basemap = st.value("basemap", true).toBool();
    s.legend = st.value("legend", true).toBool();
    s.scaleBar = st.value("scaleBar", true).toBool();
    s.northArrow = st.value("northArrow", true).toBool();
    s.crsNote = st.value("crsNote", true).toBool();

    s.title = QString::fromStdString(doc->model().title()).trimmed();
    if (s.title.isEmpty())
        s.title = doc->path().isEmpty()
                      ? QCoreApplication::translate("LayoutDialog", "Untitled network")
                      : QFileInfo(doc->path()).completeBaseName();
    const MapView::Legend legend = map->legend();
    if (!legend.title.isEmpty() && doc->results())
        s.subtitle = QCoreApplication::translate("LayoutDialog", "%1 at %2")
                         .arg(legend.title,
                              QString::fromStdString(text::formatDuration(
                                  doc->results()->times[static_cast<std::size_t>(doc->period())])));
    else
        s.subtitle = QFileInfo(doc->path()).fileName();
    s.date = QLocale().toString(QDate::currentDate(), QLocale::LongFormat);
    return s;
}

void LayoutSettings::save() const
{
    QSettings st;
    st.beginGroup("layout");
    st.setValue("paper", QString::fromStdString(page.paper));
    st.setValue("landscape", page.landscape);
    st.setValue("template", page.layout == Template::ChartBelow ? "chart" : "sidebar");
    st.setValue("split", page.split);
    st.setValue("notes", notes);
    st.setValue("logo", logo);
    st.setValue("report", report);
    st.setValue("basemap", basemap);
    st.setValue("legend", legend);
    st.setValue("scaleBar", scaleBar);
    st.setValue("northArrow", northArrow);
    st.setValue("crsNote", crsNote);
}

// ---------------------------------------------------------------------------
// Page composer
// ---------------------------------------------------------------------------
PageComposer::PageComposer(MapView* map, ProjectDocument* doc, ReportSource reports)
    : map_(map), doc_(doc), reports_(std::move(reports))
{
}

QSizeF PageComposer::pageSize(const LayoutSettings& s) const
{
    const auto f = core::layout::computeFrames(pageOptions(s));
    return QSizeF(f.widthMm * kMm, f.heightMm * kMm);
}

QRectF PageComposer::mapRect(const LayoutSettings& s) const
{
    return units(core::layout::computeFrames(pageOptions(s)).map);
}

const ReportSnapshot& PageComposer::report(const QString& kind)
{
    if (kind != cachedKind_ || cached_.kind != kind)
    {
        cached_ = kind.isEmpty() || !reports_ ? ReportSnapshot() : reports_(kind);
        cachedKind_ = kind;
    }
    return cached_;
}

bool PageComposer::basemapLoading(const LayoutSettings& s, double pixelRatio)
{
    return map_->prepareBasemap(mapRect(s), mapOptions(s, pixelRatio));
}

void PageComposer::waitForBasemap(const LayoutSettings& s, double pixelRatio)
{
    map_->waitForBasemap(mapRect(s), mapOptions(s, pixelRatio));
}

void PageComposer::paint(QPainter& p, const LayoutSettings& s, double pixelRatio, bool vector)
{
    const auto f = core::layout::computeFrames(pageOptions(s));
    const double k = std::clamp(std::sqrt(f.widthMm * f.heightMm / (297.0 * 210.0)), 1.0, 1.4);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(QRectF(0, 0, f.widthMm * kMm, f.heightMm * kMm), Qt::white);

    // Header: title, subtitle and logo
    const QRectF header = units(f.header);
    QRectF text = header;
    QImage logo;
    if (!s.logo.isEmpty() && logo.load(s.logo) && !logo.isNull())
    {
        QSizeF size = logo.size();
        size.scale(QSizeF(std::min(55.0 * kMm, header.width() / 3), header.height()),
                   Qt::KeepAspectRatio);
        const QRectF box(header.right() - size.width(),
                         header.top() + (header.height() - size.height()) / 2, size.width(),
                         size.height());
        p.drawImage(box, logo);
        text.setRight(box.left() - 4 * kMm);
    }
    const QFont titleFont = decor::font(pt(19 * k), true);
    const QFontMetricsF tfm(titleFont);
    p.setFont(titleFont);
    p.setPen(theme::kText);
    p.drawText(QRectF(text.left(), text.top(), text.width(), tfm.height()),
               Qt::AlignLeft | Qt::AlignTop, tfm.elidedText(s.title, Qt::ElideRight, text.width()));
    if (!s.subtitle.trimmed().isEmpty())
    {
        const QFont sub = decor::font(pt(10.5 * k));
        const QFontMetricsF sfm(sub);
        p.setFont(sub);
        p.setPen(theme::kTextMuted);
        p.drawText(
            QRectF(text.left(), text.top() + tfm.height() + 0.8 * kMm, text.width(), sfm.height()),
            Qt::AlignLeft | Qt::AlignTop, sfm.elidedText(s.subtitle, Qt::ElideRight, text.width()));
    }
    p.setPen(QPen(theme::kAccent, 0.6 * kMm));
    p.drawLine(QPointF(header.left(), header.bottom() + 1.6 * kMm),
               QPointF(header.right(), header.bottom() + 1.6 * kMm));

    // Map frame
    const QRectF mapR = units(f.map);
    MapView::RenderOptions mo = mapOptions(s, pixelRatio);
    mo.rasterBackground = vector;
    map_->render(p, mapR, mo);
    QString unit;
    const double gpu = map_->groundPerUnit(mapR, &unit);
    const QString attribution = s.basemap ? map_->attribution() : QString();
    if (!attribution.isEmpty())
    {
        const QFont af = decor::font(pt(5.5));
        const QFontMetricsF afm(af);
        const double w = afm.horizontalAdvance(attribution) + 2 * kMm;
        const QRectF box(mapR.right() - w, mapR.bottom() - afm.height() - 0.8 * kMm, w,
                         afm.height() + 0.8 * kMm);
        p.fillRect(box, QColor(255, 255, 255, 215));
        p.setFont(af);
        p.setPen(QColor(60, 70, 85));
        p.drawText(box, Qt::AlignCenter, attribution);
    }
    if (s.page.layout == Template::ChartBelow)
    {
        // Map furniture inside the frame.
        const double m = 3 * kMm;
        if (s.legend)
            decor::legend(&p, mapR.topLeft() + QPointF(m, m), map_->legend(),
                          map_->legend().title.isEmpty() ? map_->elementKinds() : QStringList(),
                          decor::font(pt(7)), true, mapR.height() * 0.75);
        if (s.northArrow)
            decor::northArrow(
                p, QRectF(mapR.right() - m - 12 * kMm, mapR.top() + m, 12 * kMm, 12 * kMm),
                map_->northAngle(), decor::font(pt(7)), true);
        if (s.scaleBar)
            decor::scaleBar(&p, QPointF(mapR.left() + m + 2 * kMm, mapR.bottom() - m - 1 * kMm),
                            gpu, unit, mapR.width() * 0.3, decor::font(pt(7)), true);
    }
    p.setPen(QPen(QColor(70, 80, 95), 0.3 * kMm));
    p.setBrush(Qt::NoBrush);
    p.drawRect(mapR);

    if (s.page.layout == Template::LegendSidebar)
        paintSidebar(p, s, units(f.side));
    else
        paintReport(p, s, units(f.chart));
    paintFooter(p, s, units(f.footer), mapR);
    p.restore();
}

void PageComposer::paintSidebar(QPainter& p, const LayoutSettings& s, const QRectF& side)
{
    double y = side.top();
    const QFont head = decor::font(pt(10), true);
    const QFontMetricsF hfm(head);
    auto heading = [&](const QString& textValue)
    {
        p.setFont(head);
        p.setPen(theme::kText);
        p.drawText(QPointF(side.left(), y + hfm.ascent()), textValue);
        y += hfm.height() + 0.6 * kMm;
        p.setPen(QPen(theme::kBorder, 0.25 * kMm));
        p.drawLine(QPointF(side.left(), y), QPointF(side.right(), y));
        y += 2.5 * kMm;
    };
    if (s.legend)
    {
        heading(QCoreApplication::translate("LayoutDialog", "Legend"));
        const double reserve = (s.northArrow || s.scaleBar ? 26 : 0) * kMm +
                               (s.notes.trimmed().isEmpty() ? 0 : 20 * kMm);
        const QSizeF size = decor::legend(&p, QPointF(side.left(), y), map_->legend(),
                                          map_->elementKinds(), decor::font(pt(7.5)), false,
                                          std::max(20 * kMm, side.bottom() - y - reserve));
        y += size.height() + 5 * kMm;
    }
    if (s.northArrow || s.scaleBar)
    {
        const double arrow = 14 * kMm;
        if (s.northArrow)
            decor::northArrow(p, QRectF(side.left(), y, arrow, arrow), map_->northAngle(),
                              decor::font(pt(8)), false);
        if (s.scaleBar)
        {
            QString unit;
            const double gpu = map_->groundPerUnit(mapRect(s), &unit);
            const double x0 = side.left() + (s.northArrow ? arrow + 5 * kMm : 1 * kMm);
            decor::scaleBar(&p, QPointF(x0, y + arrow * 0.72), gpu, unit,
                            side.right() - x0 - 6 * kMm, decor::font(pt(7)), false);
        }
        y += arrow + 5 * kMm;
    }
    if (!s.notes.trimmed().isEmpty() && y < side.bottom() - 8 * kMm)
    {
        heading(QCoreApplication::translate("LayoutDialog", "Notes"));
        p.setFont(decor::font(pt(8)));
        p.setPen(theme::kText);
        p.drawText(QRectF(side.left(), y, side.width(), side.bottom() - y),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, s.notes.trimmed());
    }
}

int paintTable(QPainter& p, const QRectF& box, const ReportSnapshot& s, const QFont& f)
{
    if (s.header.isEmpty())
        return 0;
    QFont bold = f;
    bold.setBold(true);
    const QFontMetricsF fm(f), fmb(bold);
    const double rowH = fm.height() * 1.45;
    const double pad = fm.height() * 0.45;
    // Column widths from the header and the first rows.
    QVector<double> widths;
    for (int c = 0; c < s.header.size(); ++c)
    {
        double w = fmb.horizontalAdvance(s.header[c]);
        for (int r = 0; r < std::min<int>(200, s.rows.size()); ++r)
            if (c < s.rows[r].size())
                w = std::max(w, fm.horizontalAdvance(s.rows[r][c]));
        widths << std::min(w, 60 * kMm) + 2 * pad;
    }
    int columns = 0;
    double used = 0;
    while (columns < widths.size() && used + widths[columns] <= box.width())
        used += widths[columns++];
    if (columns == 0)
        return 0;
    const double extra = (box.width() - used) / columns;
    for (int c = 0; c < columns; ++c)
        widths[c] += extra;
    int fit = static_cast<int>(std::floor((box.height() - rowH) / rowH));
    const bool more = fit < s.rows.size();
    if (more)
        fit = std::max(0, fit - 1);
    const int shown = std::min<int>(fit, s.rows.size());

    p.save();
    p.setClipRect(box);
    p.fillRect(QRectF(box.left(), box.top(), used + extra * columns, rowH), theme::kSurfaceAlt);
    double x = box.left();
    p.setFont(bold);
    p.setPen(theme::kText);
    for (int c = 0; c < columns; ++c)
    {
        const Qt::Alignment a =
            (c < s.numeric.size() && s.numeric[c] ? Qt::AlignRight : Qt::AlignLeft) |
            Qt::AlignVCenter;
        p.drawText(QRectF(x + pad, box.top(), widths[c] - 2 * pad, rowH), a,
                   fmb.elidedText(s.header[c], Qt::ElideRight, widths[c] - 2 * pad));
        x += widths[c];
    }
    p.setPen(QPen(theme::kBorderStrong, 0.6));
    p.drawLine(QPointF(box.left(), box.top() + rowH),
               QPointF(box.left() + used + extra * columns, box.top() + rowH));
    p.setFont(f);
    for (int r = 0; r < shown; ++r)
    {
        const double y = box.top() + rowH * (r + 1);
        if (r % 2 == 1)
            p.fillRect(QRectF(box.left(), y, used + extra * columns, rowH), QColor(248, 250, 252));
        x = box.left();
        p.setPen(theme::kText);
        for (int c = 0; c < columns; ++c)
        {
            const QString cell = c < s.rows[r].size() ? s.rows[r][c] : QString();
            const Qt::Alignment a =
                (c < s.numeric.size() && s.numeric[c] ? Qt::AlignRight : Qt::AlignLeft) |
                Qt::AlignVCenter;
            p.drawText(QRectF(x + pad, y, widths[c] - 2 * pad, rowH), a,
                       fm.elidedText(cell, Qt::ElideRight, widths[c] - 2 * pad));
            x += widths[c];
        }
    }
    if (more)
    {
        p.setPen(theme::kTextMuted);
        p.drawText(QRectF(box.left() + pad, box.top() + rowH * (shown + 1), box.width(), rowH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QCoreApplication::translate("LayoutDialog", "... %n more row(s)", nullptr,
                                               static_cast<int>(s.rows.size() - shown)));
    }
    p.restore();
    return shown;
}

void PageComposer::paintReport(QPainter& p, const LayoutSettings& s, const QRectF& box)
{
    const ReportSnapshot& r = report(s.report);
    QRectF area = box;
    const QString notes = s.notes.trimmed();
    if (!notes.isEmpty())
    {
        // Free text beside the chart.
        const QRectF textBox(box.right() - box.width() * 0.28, box.top(), box.width() * 0.28,
                             box.height());
        area.setRight(textBox.left() - 4 * kMm);
        p.setFont(decor::font(pt(8)));
        p.setPen(theme::kText);
        p.drawText(textBox.adjusted(2 * kMm, 2 * kMm, 0, 0),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, notes);
        p.setPen(QPen(theme::kBorder, 0.25 * kMm));
        p.drawLine(textBox.topLeft(), textBox.bottomLeft());
    }
    QRectF inner = area.adjusted(0, 0, 0, 0);
    if (!r.title.isEmpty())
    {
        const QFont head = decor::font(pt(9.5), true);
        const QFontMetricsF hfm(head);
        p.setFont(head);
        p.setPen(theme::kText);
        p.drawText(QPointF(inner.left(), inner.top() + hfm.ascent()), r.title);
        inner.setTop(inner.top() + hfm.height() + 0.8 * kMm);
        if (!r.notes.isEmpty() && (!r.charts.isEmpty() || !r.rows.isEmpty()))
        {
            const QFont nf = decor::font(pt(7));
            const QFontMetricsF nfm(nf);
            p.setFont(nf);
            p.setPen(theme::kTextMuted);
            p.drawText(QRectF(inner.left(), inner.top(), inner.width(), nfm.height()),
                       Qt::AlignLeft | Qt::AlignTop,
                       nfm.elidedText(r.notes.first(), Qt::ElideRight, inner.width()));
            inner.setTop(inner.top() + nfm.height() + 1 * kMm);
        }
    }
    if (s.report.isEmpty() || r.isEmpty())
    {
        p.setFont(decor::font(pt(8)));
        p.setPen(theme::kTextMuted);
        p.drawText(
            inner, Qt::AlignCenter | Qt::TextWordWrap,
            s.report.isEmpty()
                ? QCoreApplication::translate("LayoutDialog",
                                              "Choose a report to show a chart or table here.")
                : QCoreApplication::translate(
                      "LayoutDialog", "This report has no content yet. Run a simulation first."));
    }
    else if (!r.charts.isEmpty())
    {
        ChartWidget::paint(p, inner, decor::font(pt(7)), r.charts.first(), false);
    }
    else if (!r.rows.isEmpty())
    {
        paintTable(p, inner, r, decor::font(pt(6.5)));
    }
    else
    {
        const QString body = r.text.isEmpty() ? r.notes.join("\n") : r.text;
        QFont mono = r.text.isEmpty() ? decor::font(pt(8))
                                      : QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (!r.text.isEmpty())
            mono.setPixelSize(static_cast<int>(std::lround(pt(6.5))));
        p.setFont(mono);
        p.setPen(theme::kText);
        p.save();
        p.setClipRect(inner);
        int flags = Qt::AlignLeft | Qt::AlignTop;
        if (r.text.isEmpty())
            flags |= Qt::TextWordWrap;
        p.drawText(inner, flags, body);
        p.restore();
    }
}

void PageComposer::paintFooter(QPainter& p, const LayoutSettings& s, const QRectF& footer,
                               const QRectF& map)
{
    QStringList left;
    if (s.crsNote)
    {
        QString crs = map_->coordinateSystemName();
        if (crs.isEmpty())
            crs = QString::fromStdString(doc_->settings().crs);
        left << (crs.isEmpty()
                     ? QCoreApplication::translate("LayoutDialog", "Coordinate system not set")
                     : QCoreApplication::translate("LayoutDialog", "Coordinate system: %1")
                           .arg(crs));
    }
    QString unit;
    const double gpu = map_->groundPerUnit(map, &unit);
    if (gpu > 0)
    {
        // Paper metres per painter unit: 1/96 inch.
        const double denominator = gpu * (unit == "ft" ? 0.3048 : 1.0) / (0.0254 / 96.0);
        const double rounded = core::layout::roundScale(denominator);
        if (rounded > 0)
            left << QCoreApplication::translate("LayoutDialog", "Scale 1:%1 at %2 size")
                        .arg(QString::fromStdString(core::layout::groupDigits(rounded)),
                             QString::fromStdString(s.page.paper));
    }
    QStringList right;
    if (!s.date.trimmed().isEmpty())
        right << s.date.trimmed();
    if (!doc_->path().isEmpty())
        right << QFileInfo(doc_->path()).fileName();
    const QFont f = decor::font(pt(7));
    p.setFont(f);
    p.setPen(QPen(theme::kBorder, 0.25 * kMm));
    p.drawLine(footer.topLeft() + QPointF(0, 1 * kMm), footer.topRight() + QPointF(0, 1 * kMm));
    p.setPen(theme::kTextMuted);
    const QRectF line = footer.adjusted(0, 1.5 * kMm, 0, 0);
    p.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, left.join("   ·   "));
    p.drawText(line, Qt::AlignRight | Qt::AlignVCenter, right.join("   ·   "));
}

bool PageComposer::exportPdf(const QString& path, const LayoutSettings& s, QString* error)
{
    QPdfWriter writer(path);
    writer.setResolution(1200);
    writer.setPageLayout(pageLayoutFor(s));
    writer.setTitle(s.title);
    writer.setCreator(QStringLiteral("Mirab " MIRAB_VERSION_STRING));
    QPainter p;
    if (!p.begin(&writer))
    {
        if (error)
            *error =
                QCoreApplication::translate("LayoutDialog", "%1 could not be written.").arg(path);
        return false;
    }
    const double scale = writer.resolution() / 96.0;
    p.scale(scale, scale);
    paint(p, s, kVectorDetail, true);
    return p.end();
}

bool PageComposer::exportImage(const QString& path, const LayoutSettings& s, int dpi,
                               QString* error)
{
    const QSizeF page = pageSize(s);
    const double scale = dpi / 96.0;
    QImage img(QSize(static_cast<int>(std::lround(page.width() * scale)),
                     static_cast<int>(std::lround(page.height() * scale))),
               QImage::Format_RGB32);
    if (img.isNull())
    {
        if (error)
            *error = QCoreApplication::translate(
                "LayoutDialog", "The image is too large; choose a lower resolution.");
        return false;
    }
    img.setDotsPerMeterX(static_cast<int>(std::lround(dpi / 0.0254)));
    img.setDotsPerMeterY(static_cast<int>(std::lround(dpi / 0.0254)));
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.scale(scale, scale);
        paint(p, s, scale);
    }
    if (!img.save(path))
    {
        if (error)
            *error =
                QCoreApplication::translate("LayoutDialog", "%1 could not be written.").arg(path);
        return false;
    }
    return true;
}

void PageComposer::print(QPrinter& printer, const LayoutSettings& s)
{
    printer.setFullPage(true);
    QPainter p;
    if (!p.begin(&printer))
        return;
    const QRectF paper = printer.paperRect(QPrinter::DevicePixel);
    const QSizeF page = pageSize(s);
    const double scale = std::min(paper.width() / page.width(), paper.height() / page.height());
    p.scale(scale, scale);
    paint(p, s, std::min(scale, kVectorDetail), true);
    p.end();
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------
LayoutPreview::LayoutPreview(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(360, 260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void LayoutPreview::setImage(const QImage& image)
{
    image_ = image;
    update();
}

void LayoutPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(226, 231, 238));
    if (image_.isNull())
        return;
    const QSizeF size = image_.deviceIndependentSize();
    const QRectF box(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    p.fillRect(box.translated(2, 3), QColor(0, 0, 0, 40));
    p.drawImage(box, image_);
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------
LayoutDialog::LayoutDialog(MapView* map, ProjectDocument* doc, PageComposer::ReportSource reports,
                           QWidget* parent)
    : QDialog(parent), map_(map), doc_(doc), composer_(map, doc, std::move(reports)),
      timer_(new QTimer(this))
{
    setWindowTitle(tr("Print layout"));
    resize(1080, 700);
    const LayoutSettings s = LayoutSettings::load(doc_, map_);

    auto* layout = new QHBoxLayout(this);
    auto* side = new QWidget(this);
    side->setFixedWidth(360);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(0, 0, 0, 0);

    auto* pageBox = new QGroupBox(tr("Page"), side);
    auto* pf = new QFormLayout(pageBox);
    template_ = new QComboBox(pageBox);
    template_->addItem(tr("Legend beside the map"), "sidebar");
    template_->addItem(tr("Chart or table below the map"), "chart");
    template_->setCurrentIndex(s.page.layout == Template::ChartBelow ? 1 : 0);
    pf->addRow(tr("Template"), template_);
    paper_ = new QComboBox(pageBox);
    for (const auto& p : core::layout::papers())
        paper_->addItem(QStringLiteral("%1  (%2 x %3 mm)")
                            .arg(QString::fromStdString(p.id))
                            .arg(p.widthMm, 0, 'f', 0)
                            .arg(p.heightMm, 0, 'f', 0),
                        QString::fromStdString(p.id));
    paper_->setCurrentIndex(std::max(0, paper_->findData(QString::fromStdString(s.page.paper))));
    pf->addRow(tr("Paper"), paper_);
    orientation_ = new QComboBox(pageBox);
    orientation_->addItem(tr("Landscape"));
    orientation_->addItem(tr("Portrait"));
    orientation_->setCurrentIndex(s.page.landscape ? 0 : 1);
    pf->addRow(tr("Orientation"), orientation_);
    split_ = new QSlider(Qt::Horizontal, pageBox);
    split_->setRange(40, 90);
    split_->setValue(static_cast<int>(std::lround(s.page.split * 100)));
    split_->setToolTip(tr("Share of the page body used by the map"));
    pf->addRow(tr("Map size"), split_);
    report_ = new QComboBox(pageBox);
    report_->addItem(tr("None"), QString());
    for (const auto& r : availableReports(doc_->model().dialect()))
        if (r.kind != "status")
            report_->addItem(r.title, r.kind);
    report_->setCurrentIndex(std::max(0, report_->findData(s.report)));
    pf->addRow(tr("Chart or table"), report_);
    sl->addWidget(pageBox);

    auto* textBox = new QGroupBox(tr("Texts"), side);
    auto* tf = new QFormLayout(textBox);
    title_ = new QLineEdit(s.title, textBox);
    title_->setCursorPosition(0);
    subtitle_ = new QLineEdit(s.subtitle, textBox);
    date_ = new QLineEdit(s.date, textBox);
    notes_ = new QPlainTextEdit(s.notes, textBox);
    notes_->setPlaceholderText(tr("Notes, sources or a disclaimer"));
    notes_->setFixedHeight(64);
    logo_ = new QLineEdit(s.logo, textBox);
    logo_->setPlaceholderText(tr("No logo"));
    logo_->setReadOnly(true);
    auto* chooseLogo = new QPushButton(tr("Choose..."), textBox);
    auto* clearLogo = new QPushButton(tr("Clear"), textBox);
    auto* logoRow = new QHBoxLayout;
    logoRow->addWidget(logo_, 1);
    logoRow->addWidget(chooseLogo);
    logoRow->addWidget(clearLogo);
    tf->addRow(tr("Title"), title_);
    tf->addRow(tr("Subtitle"), subtitle_);
    tf->addRow(tr("Date"), date_);
    tf->addRow(tr("Notes"), notes_);
    tf->addRow(tr("Logo"), logoRow);
    sl->addWidget(textBox);

    auto* showBox = new QGroupBox(tr("Show"), side);
    auto* grid = new QGridLayout(showBox);
    basemap_ = new QCheckBox(tr("Basemap"), showBox);
    legend_ = new QCheckBox(tr("Legend"), showBox);
    scaleBar_ = new QCheckBox(tr("Scale bar"), showBox);
    north_ = new QCheckBox(tr("North arrow"), showBox);
    crs_ = new QCheckBox(tr("Coordinate system"), showBox);
    basemap_->setChecked(s.basemap);
    basemap_->setEnabled(!map_->attribution().isEmpty());
    legend_->setChecked(s.legend);
    scaleBar_->setChecked(s.scaleBar);
    north_->setChecked(s.northArrow);
    crs_->setChecked(s.crsNote);
    grid->addWidget(basemap_, 0, 0);
    grid->addWidget(legend_, 0, 1);
    grid->addWidget(scaleBar_, 1, 0);
    grid->addWidget(north_, 1, 1);
    grid->addWidget(crs_, 2, 0, 1, 2);
    sl->addWidget(showBox);
    sl->addStretch(1);

    status_ = new QLabel(side);
    status_->setObjectName("muted");
    status_->setWordWrap(true);
    sl->addWidget(status_);
    auto* pdf = new QPushButton(tr("Export PDF..."), side);
    pdf->setProperty("primary", true);
    auto* png = new QPushButton(tr("Export PNG..."), side);
    dpi_ = new QComboBox(side);
    for (int d : {150, 300, 600})
        dpi_->addItem(tr("%1 dpi").arg(d), d);
    dpi_->setCurrentIndex(1);
    auto* printBtn = new QPushButton(tr("Print..."), side);
    auto* close = new QPushButton(tr("Close"), side);
    for (QPushButton* b : {chooseLogo, clearLogo, png, printBtn, close})
        b->setAutoDefault(false);
    pdf->setDefault(true);
    auto* row1 = new QHBoxLayout;
    row1->addWidget(pdf, 1);
    row1->addWidget(printBtn, 1);
    auto* row2 = new QHBoxLayout;
    row2->addWidget(png, 1);
    row2->addWidget(dpi_);
    sl->addLayout(row1);
    sl->addLayout(row2);
    sl->addWidget(close);

    preview_ = new LayoutPreview(this);
    layout->addWidget(side);
    layout->addWidget(preview_, 1);

    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &LayoutDialog::renderPreview);
    for (QComboBox* c : {template_, paper_, orientation_, report_})
        connect(c, &QComboBox::currentIndexChanged, this, &LayoutDialog::schedulePreview);
    for (QLineEdit* e : {title_, subtitle_, date_, logo_})
        connect(e, &QLineEdit::textChanged, this, &LayoutDialog::schedulePreview);
    connect(notes_, &QPlainTextEdit::textChanged, this, &LayoutDialog::schedulePreview);
    connect(split_, &QSlider::valueChanged, this, &LayoutDialog::schedulePreview);
    for (QCheckBox* c : {basemap_, legend_, scaleBar_, north_, crs_})
        connect(c, &QCheckBox::toggled, this, &LayoutDialog::schedulePreview);
    connect(template_, &QComboBox::currentIndexChanged, this,
            [this] { report_->setEnabled(template_->currentIndex() == 1); });
    report_->setEnabled(template_->currentIndex() == 1);
    connect(chooseLogo, &QPushButton::clicked, this,
            [this]
            {
                const QString f = QFileDialog::getOpenFileName(
                    this, tr("Logo"), QFileInfo(logo_->text()).absolutePath(),
                    tr("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
                if (!f.isEmpty())
                    logo_->setText(f);
            });
    connect(clearLogo, &QPushButton::clicked, logo_, &QLineEdit::clear);
    connect(pdf, &QPushButton::clicked, this, &LayoutDialog::exportPdf);
    connect(png, &QPushButton::clicked, this, &LayoutDialog::exportPng);
    connect(printBtn, &QPushButton::clicked, this, &LayoutDialog::print);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(this, &QDialog::finished, this, [this] { settings().save(); });
    schedulePreview();
}

LayoutSettings LayoutDialog::settings() const
{
    LayoutSettings s;
    s.page.paper = paper_->currentData().toString().toStdString();
    s.page.landscape = orientation_->currentIndex() == 0;
    s.page.layout = template_->currentIndex() == 1 ? Template::ChartBelow : Template::LegendSidebar;
    s.page.split = split_->value() / 100.0;
    s.title = title_->text();
    s.subtitle = subtitle_->text();
    s.date = date_->text();
    s.notes = notes_->toPlainText();
    s.logo = logo_->text();
    s.report = report_->currentData().toString();
    s.basemap = basemap_->isChecked() && basemap_->isEnabled();
    s.legend = legend_->isChecked();
    s.scaleBar = scaleBar_->isChecked();
    s.northArrow = north_->isChecked();
    s.crsNote = crs_->isChecked();
    return s;
}

void LayoutDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    schedulePreview();
}

void LayoutDialog::schedulePreview()
{
    retries_ = 0;
    timer_->start(120);
}

void LayoutDialog::renderPreview()
{
    const LayoutSettings s = settings();
    const QSizeF page = composer_.pageSize(s);
    const QSizeF avail = QSizeF(preview_->size()) - QSizeF(28, 28);
    if (avail.width() < 50 || avail.height() < 50)
        return;
    const double scale = std::min(avail.width() / page.width(), avail.height() / page.height());
    const double dpr = preview_->devicePixelRatioF();
    QImage img(QSize(static_cast<int>(page.width() * scale * dpr),
                     static_cast<int>(page.height() * scale * dpr)),
               QImage::Format_RGB32);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.scale(scale, scale);
        composer_.paint(p, s, scale * dpr);
    }
    preview_->setImage(img);
    // Tiles arrive over the network: draw again once they are in.
    if (composer_.basemapLoading(s, scale * dpr) && retries_++ < 40)
        timer_->start(600);
}

void LayoutDialog::exportPdf()
{
    const LayoutSettings s = settings();
    const QString base = doc_->path().isEmpty() ? QStringLiteral("layout")
                                                : QFileInfo(doc_->path()).completeBaseName();
    const QString f = QFileDialog::getSaveFileName(
        this, tr("Export layout as PDF"), base + "-layout.pdf", tr("PDF document (*.pdf)"));
    if (f.isEmpty())
        return;
    QProgressDialog wait(tr("Loading basemap tiles..."), QString(), 0, 0, this);
    wait.setWindowModality(Qt::WindowModal);
    wait.setMinimumDuration(300);
    composer_.waitForBasemap(s, kVectorDetail);
    QString error;
    if (composer_.exportPdf(f, s, &error))
        status_->setText(tr("Saved %1").arg(QFileInfo(f).fileName()));
    else
        QMessageBox::warning(this, tr("Export failed"), error);
}

void LayoutDialog::exportPng()
{
    const LayoutSettings s = settings();
    const int dpi = dpi_->currentData().toInt();
    const QString base = doc_->path().isEmpty() ? QStringLiteral("layout")
                                                : QFileInfo(doc_->path()).completeBaseName();
    const QString f = QFileDialog::getSaveFileName(this, tr("Export layout as image"),
                                                   base + "-layout.png", tr("PNG image (*.png)"));
    if (f.isEmpty())
        return;
    QProgressDialog wait(tr("Loading basemap tiles..."), QString(), 0, 0, this);
    wait.setWindowModality(Qt::WindowModal);
    wait.setMinimumDuration(300);
    composer_.waitForBasemap(s, dpi / 96.0);
    QString error;
    if (composer_.exportImage(f, s, dpi, &error))
        status_->setText(tr("Saved %1").arg(QFileInfo(f).fileName()));
    else
        QMessageBox::warning(this, tr("Export failed"), error);
}

void LayoutDialog::print()
{
    const LayoutSettings s = settings();
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageLayout(pageLayoutFor(s));
    printer.setDocName(s.title);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    composer_.waitForBasemap(s, kVectorDetail);
    composer_.print(printer, s);
    status_->setText(tr("Sent to %1").arg(printer.printerName()));
}

} // namespace mirab::app
