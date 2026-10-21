/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ChartWidget.cpp
 * Purpose: Plot widget for time series, profiles, frequency and variability charts.
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
#include "ChartWidget.h"

#include "Theme.h"
#include "mirab/core/Text.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mirab::app
{

namespace
{
const QColor kColors[] = {QColor(31, 111, 235), QColor(224, 82, 60),  QColor(27, 158, 119), QColor(142, 92, 200),
                          QColor(232, 160, 20), QColor(80, 160, 210), QColor(200, 70, 140), QColor(110, 120, 135)};

double niceStep(double range, int ticks)
{
    const double raw = range / std::max(1, ticks);
    if (!(raw > 0))
        return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
    {
        if (raw <= m * mag)
            return m * mag;
    }
    return 10.0 * mag;
}

QString num(double v, int digits = 5)
{
    return QString::fromStdString(text::formatDouble(std::fabs(v) < 1e-12 ? 0.0 : v, digits));
}
} // namespace

bool ChartWidget::Data::hasPoints() const
{
    return std::any_of(series.begin(), series.end(),
                       [](const Series& s) { return !s.points.isEmpty(); });
}

ChartWidget::ChartWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(150);
    setMouseTracking(true);
    d_.xLabel = tr("Time");
}

void ChartWidget::setSeries(const QVector<Series>& series, const QString& yLabel, const QString& title)
{
    d_.series = series;
    d_.yLabel = yLabel;
    d_.title = title;
    update();
}

void ChartWidget::setXAxis(const QString& label, bool time)
{
    d_.xLabel = label;
    d_.timeAxis = time;
    update();
}

void ChartWidget::setXAnnotations(const QVector<std::pair<double, QString>>& labels)
{
    d_.annotations = labels;
    update();
}

void ChartWidget::setYRange(std::optional<double> min, std::optional<double> max)
{
    d_.yMin = min;
    d_.yMax = max;
    update();
}

void ChartWidget::setMarker(double x)
{
    d_.marker = x;
    update();
}

void ChartWidget::clear(const QString& placeholder)
{
    d_.series.clear();
    d_.annotations.clear();
    d_.title.clear();
    d_.placeholder = placeholder;
    update();
}

QString ChartWidget::toCsv() const
{
    QString out = d_.timeAxis ? "time_h" : (d_.xLabel.isEmpty() ? "x" : d_.xLabel);
    for (const auto& s : d_.series)
    {
        out += "," + QString(s.name).replace(',', ';');
        if (s.style == Series::Style::Band)
            out += "," + QString(s.name).replace(',', ';') + " (upper)";
    }
    out += "\n";
    int rows = 0;
    for (const auto& s : d_.series)
        rows = std::max(rows, static_cast<int>(s.points.size()));
    for (int r = 0; r < rows; ++r)
    {
        QString line;
        double x = 0;
        bool haveX = false;
        for (const auto& s : d_.series)
        {
            if (!haveX && r < s.points.size())
            {
                x = s.points[r].x();
                haveX = true;
            }
        }
        line += num(d_.timeAxis ? x / 3600.0 : x, 8);
        for (const auto& s : d_.series)
        {
            line += "," + (r < s.points.size() ? num(s.points[r].y(), 8) : QString());
            if (s.style == Series::Style::Band)
                line += "," + (r < s.upper.size() ? num(s.upper[r].y(), 8) : QString());
        }
        out += line + "\n";
    }
    return out;
}

void ChartWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    draw(p, QRectF(rect()), font(), d_, hover_, true);
}

void ChartWidget::paint(QPainter& p, const QRectF& rect, const QFont& font, const Data& data,
                        bool background)
{
    p.save();
    draw(p, rect, font, data, QPointF(-1e9, -1e9), background);
    p.restore();
}

void ChartWidget::draw(QPainter& p, const QRectF& r, const QFont& font, const Data& d,
                       const QPointF& hover, bool background)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(font);
    if (background)
        p.fillRect(r, theme::kSurface);
    const QColor ink = theme::kText;
    const QColor muted = theme::kTextMuted;
    const QColor grid(232, 236, 241);
    const QFontMetricsF fm(font);
    auto colorOf = [&d](int index)
    {
        const QColor c = d.series[index].color;
        return c.isValid() ? c : kColors[index % 8];
    };

    double xmin = std::numeric_limits<double>::max(), xmax = std::numeric_limits<double>::lowest();
    double ymin = xmin, ymax = xmax;
    for (const auto& s : d.series)
    {
        for (const auto* pts : {&s.points, &s.upper})
        {
            for (const auto& pt : *pts)
            {
                if (!std::isfinite(pt.y()))
                    continue;
                xmin = std::min(xmin, pt.x());
                xmax = std::max(xmax, pt.x());
                ymin = std::min(ymin, pt.y());
                ymax = std::max(ymax, pt.y());
            }
        }
    }
    if (xmin > xmax)
    {
        p.setPen(muted);
        p.drawText(r.adjusted(20, 0, -20, 0), Qt::AlignCenter | Qt::TextWordWrap,
                   d.placeholder.isEmpty()
                       ? tr("Select an element and run a simulation to see its time series.")
                       : d.placeholder);
        return;
    }
    for (const auto& s : d.series)
    {
        if (s.style == Series::Style::Bars)
            ymin = std::min(ymin, 0.0);
    }
    if (d.yMin)
        ymin = *d.yMin;
    if (d.yMax)
        ymax = *d.yMax;
    if (xmax - xmin < 1e-9)
        xmax = xmin + (d.timeAxis ? 3600.0 : 1.0);
    if (ymax - ymin < 1e-9)
    {
        ymin -= 1.0;
        ymax += 1.0;
    }
    const double ystep = niceStep(ymax - ymin, 5);
    if (!d.yMin)
        ymin = std::floor(ymin / ystep) * ystep;
    if (!d.yMax)
        ymax = std::ceil(ymax / ystep) * ystep;
    const bool hours = (xmax - xmin) <= 4 * 86400.0;
    const double xUnit = d.timeAxis ? (hours ? 3600.0 : 86400.0) : 1.0;
    const double xstep = niceStep((xmax - xmin) / xUnit, 8) * xUnit;

    double labelW = 0;
    for (double y = ymin; y <= ymax + ystep * 0.5; y += ystep)
        labelW = std::max(labelW, fm.horizontalAdvance(num(y, 6)));
    const double left = r.left() + labelW + fm.height() + 16;
    const double top =
        r.top() + (d.title.isEmpty() ? std::max(10.0, fm.height() * 0.8) : fm.height() + 16);
    const double bottom = 2 * fm.height() + 14 + (d.annotations.isEmpty() ? 0 : fm.height() + 4);
    const QRectF plot(left, top, r.right() - left - 16, r.bottom() - top - bottom);
    if (plot.width() < 40 || plot.height() < 30)
        return;
    auto X = [&](double x) { return plot.left() + (x - xmin) / (xmax - xmin) * plot.width(); };
    auto Y = [&](double y) { return plot.bottom() - (y - ymin) / (ymax - ymin) * plot.height(); };

    if (!d.title.isEmpty())
    {
        QFont bold = font;
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(ink);
        p.drawText(QRectF(left, r.top() + 6, plot.width(), fm.height() + 2),
                   Qt::AlignLeft | Qt::AlignVCenter, d.title);
        p.setFont(font);
    }
    p.setPen(QPen(grid, 1));
    for (double y = ymin; y <= ymax + ystep * 0.5; y += ystep)
        p.drawLine(QPointF(plot.left(), Y(y)), QPointF(plot.right(), Y(y)));
    for (double x = std::ceil(xmin / xstep) * xstep; x <= xmax + 1e-6; x += xstep)
        p.drawLine(QPointF(X(x), plot.top()), QPointF(X(x), plot.bottom()));
    p.setPen(QPen(theme::kBorderStrong, 1));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.drawLine(plot.bottomLeft(), plot.topLeft());
    p.setPen(muted);
    for (double y = ymin; y <= ymax + ystep * 0.5; y += ystep)
        p.drawText(QRectF(r.left(), Y(y) - fm.height() / 2.0, left - r.left() - 8, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, num(y, 6));
    for (double x = std::ceil(xmin / xstep) * xstep; x <= xmax + 1e-6; x += xstep)
        p.drawText(QRectF(X(x) - 40, plot.bottom() + 4, 80, fm.height()), Qt::AlignHCenter, num(x / xUnit, 4));
    const QString xl = d.timeAxis ? (hours ? tr("Time (hours)") : tr("Time (days)")) : d.xLabel;
    p.drawText(QRectF(plot.left(), r.bottom() - fm.height() - 6, plot.width(), fm.height()),
               Qt::AlignHCenter, xl);
    p.save();
    p.translate(r.left() + fm.height() / 2 + 4, plot.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-plot.height() / 2, -fm.height() / 2, plot.height(), fm.height()),
               Qt::AlignCenter, d.yLabel);
    p.restore();

    // Annotations (profile node IDs)
    if (!d.annotations.isEmpty())
    {
        double lastRight = -1e9;
        for (const auto& [x, label] : d.annotations)
        {
            const double sx = X(x);
            p.setPen(QPen(QColor(200, 207, 216), 1, Qt::DotLine));
            p.drawLine(QPointF(sx, plot.top()), QPointF(sx, plot.bottom()));
            const double w = fm.horizontalAdvance(label);
            if (sx - w / 2.0 > lastRight + 4)
            {
                p.setPen(muted);
                p.drawText(QRectF(sx - w / 2.0, plot.bottom() + fm.height() + 6, w, fm.height()), Qt::AlignCenter, label);
                lastRight = sx + w / 2.0;
            }
        }
    }

    p.save();
    p.setClipRect(plot.adjusted(-1, -1, 1, 1), Qt::IntersectClip);
    for (int i = 0; i < d.series.size(); ++i)
    {
        const auto& s = d.series[i];
        if (s.points.isEmpty())
            continue;
        const QColor c = colorOf(i);
        QPen pen(c, s.width, s.dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        switch (s.style)
        {
        case Series::Style::Band:
        {
            QPainterPath band(QPointF(X(s.points[0].x()), Y(s.points[0].y())));
            for (int k = 1; k < s.points.size(); ++k)
                band.lineTo(X(s.points[k].x()), Y(s.points[k].y()));
            for (int k = s.upper.size() - 1; k >= 0; --k)
                band.lineTo(X(s.upper[k].x()), Y(s.upper[k].y()));
            band.closeSubpath();
            QColor fill = c;
            fill.setAlpha(55);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPath(band);
            break;
        }
        case Series::Style::Bars:
        {
            const double bw = std::max(2.0, plot.width() / std::max(1.0, static_cast<double>(s.points.size())) * 0.7);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            for (const auto& pt : s.points)
            {
                const QRectF bar(QPointF(X(pt.x()) - bw / 2, Y(pt.y())), QPointF(X(pt.x()) + bw / 2, Y(std::max(0.0, ymin))));
                p.drawRect(bar.normalized());
            }
            break;
        }
        case Series::Style::Points:
            p.setPen(QPen(c.darker(115), 1));
            p.setBrush(c);
            for (const auto& pt : s.points)
                p.drawEllipse(QPointF(X(pt.x()), Y(pt.y())), 3, 3);
            break;
        default:
        {
            QPainterPath path(QPointF(X(s.points[0].x()), Y(s.points[0].y())));
            for (int k = 1; k < s.points.size(); ++k)
            {
                if (s.style == Series::Style::Step)
                    path.lineTo(X(s.points[k].x()), Y(s.points[k - 1].y()));
                path.lineTo(X(s.points[k].x()), Y(s.points[k].y()));
            }
            if (s.style == Series::Style::Area)
            {
                QPainterPath fill = path;
                fill.lineTo(X(s.points.last().x()), plot.bottom() + 2);
                fill.lineTo(X(s.points.first().x()), plot.bottom() + 2);
                fill.closeSubpath();
                QColor f = c;
                f.setAlpha(70);
                p.setPen(Qt::NoPen);
                p.setBrush(f);
                p.drawPath(fill);
            }
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            break;
        }
        }
    }
    p.restore();

    if (d.marker >= xmin && d.marker <= xmax)
    {
        p.setPen(QPen(theme::kSelection, 1.5, Qt::DashLine));
        p.drawLine(QPointF(X(d.marker), plot.top()), QPointF(X(d.marker), plot.bottom()));
    }

    // Legend, top right inside the plot
    int legendCount = 0;
    double legendW = 0;
    for (const auto& s : d.series)
    {
        if (s.inLegend && !s.name.isEmpty())
        {
            ++legendCount;
            legendW = std::max(legendW, fm.horizontalAdvance(s.name));
        }
    }
    if (legendCount > 1 || (legendCount == 1 && d.series.size() > 1))
    {
        const double rowH = fm.height() + 3;
        QRectF box(plot.right() - legendW - 44, plot.top() + 6, legendW + 36, legendCount * rowH + 8);
        p.setPen(QPen(theme::kBorder, 1));
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawRoundedRect(box, 6, 6);
        int row = 0;
        for (int i = 0; i < d.series.size(); ++i)
        {
            const auto& s = d.series[i];
            if (!s.inLegend || s.name.isEmpty())
                continue;
            const double y = box.top() + 6 + row * rowH + rowH / 2.0;
            QColor c = colorOf(i);
            if (s.style == Series::Style::Band || s.style == Series::Style::Area)
            {
                c.setAlpha(s.style == Series::Style::Band ? 80 : 255);
                p.fillRect(QRectF(box.left() + 8, y - 4, 16, 8), c);
            }
            else
            {
                p.setPen(QPen(c, 2.5, s.dashed ? Qt::DashLine : Qt::SolidLine));
                p.drawLine(QPointF(box.left() + 8, y), QPointF(box.left() + 24, y));
            }
            p.setPen(ink);
            p.drawText(QPointF(box.left() + 30, y + fm.ascent() / 2.0 - 1), s.name);
            ++row;
        }
    }

    // Hover read-out
    if (plot.contains(hover))
    {
        const double x = xmin + (hover.x() - plot.left()) / plot.width() * (xmax - xmin);
        p.setPen(QPen(QColor(150, 160, 175), 1, Qt::DashLine));
        p.drawLine(QPointF(hover.x(), plot.top()), QPointF(hover.x(), plot.bottom()));
        QStringList lines;
        lines << (d.timeAxis ? QString::fromStdString(text::formatDuration(x)) : num(x, 6));
        QVector<QColor> colors{QColor()};
        for (int i = 0; i < d.series.size(); ++i)
        {
            const auto& s = d.series[i];
            if (s.points.isEmpty() || !s.inLegend)
                continue;
            int k = 0;
            double best = std::numeric_limits<double>::max();
            for (int j = 0; j < s.points.size(); ++j)
            {
                const double dist = std::fabs(s.points[j].x() - x);
                if (dist < best)
                {
                    best = dist;
                    k = j;
                }
            }
            QString v = num(s.points[k].y(), 5);
            if (s.style == Series::Style::Band && k < s.upper.size())
                v += " – " + num(s.upper[k].y(), 5);
            lines << (s.name.isEmpty() ? QString() : s.name + ": ") + v;
            colors << colorOf(i);
            p.setPen(QPen(colorOf(i).darker(110), 1.5));
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(X(s.points[k].x()), Y(s.points[k].y())), 3.5, 3.5);
        }
        double w = 0;
        for (const auto& l : lines)
            w = std::max(w, fm.horizontalAdvance(l));
        QRectF box(hover.x() + 12, plot.top() + 8, w + 24, lines.size() * (fm.height() + 2) + 10);
        if (box.right() > plot.right())
            box.moveRight(hover.x() - 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(31, 42, 55, 225));
        p.drawRoundedRect(box, 5, 5);
        for (int i = 0; i < lines.size(); ++i)
        {
            const double y = box.top() + 5 + i * (fm.height() + 2);
            if (colors[i].isValid())
            {
                p.setBrush(colors[i]);
                p.drawEllipse(QPointF(box.left() + 9, y + fm.height() / 2.0), 3, 3);
            }
            p.setPen(Qt::white);
            p.drawText(QPointF(box.left() + 17, y + fm.ascent()), lines[i]);
            p.setPen(Qt::NoPen);
        }
    }
}

void ChartWidget::mouseMoveEvent(QMouseEvent* e)
{
    hover_ = e->position();
    update();
}

void ChartWidget::leaveEvent(QEvent*)
{
    hover_ = QPointF(-1, -1);
    update();
}

void ChartWidget::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    menu.addAction(tr("Copy image"), this, [this] { QApplication::clipboard()->setPixmap(grab()); });
    menu.addAction(tr("Copy data (CSV)"), this, [this] { QApplication::clipboard()->setText(toCsv()); });
    menu.addAction(tr("Save image..."), this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Save chart"), "chart.png", tr("PNG image (*.png)"));
        if (!f.isEmpty())
            grab().save(f);
    });
    menu.exec(e->globalPos());
}

} // namespace mirab::app
