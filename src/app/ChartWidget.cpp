/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/ChartWidget.cpp
 * Purpose: Painting of time-series charts with axes, grid and legend.
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

#include "anahita/core/Text.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace anahita::app
{

namespace
{
const QColor kColors[] = {QColor(33, 102, 172), QColor(214, 96, 77), QColor(27, 158, 119),
                          QColor(117, 112, 179), QColor(230, 171, 2), QColor(102, 166, 30)};

double niceStep(double range, int ticks)
{
    const double raw = range / std::max(1, ticks);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
    {
        if (raw <= m * mag)
            return m * mag;
    }
    return 10.0 * mag;
}
} // namespace

ChartWidget::ChartWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(160);
}

void ChartWidget::setSeries(const QVector<Series>& series, const QString& yLabel, const QString& title)
{
    series_ = series;
    yLabel_ = yLabel;
    title_ = title;
    update();
}

void ChartWidget::setMarker(double x)
{
    marker_ = x;
    update();
}

void ChartWidget::clear()
{
    series_.clear();
    title_.clear();
    update();
}

void ChartWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));
    const QColor ink = pal.color(QPalette::Text);
    QColor grid = pal.color(QPalette::Mid);
    grid.setAlpha(90);
    const QFontMetrics fm(font());

    double xmin = std::numeric_limits<double>::max(), xmax = std::numeric_limits<double>::lowest();
    double ymin = xmin, ymax = xmax;
    for (const auto& s : series_)
    {
        for (const auto& pt : s.points)
        {
            xmin = std::min(xmin, pt.x());
            xmax = std::max(xmax, pt.x());
            ymin = std::min(ymin, pt.y());
            ymax = std::max(ymax, pt.y());
        }
    }
    if (xmin > xmax)
    {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Select an element and run a simulation to see its time series."));
        return;
    }
    if (xmax - xmin < 1e-9)
        xmax = xmin + 3600.0;
    if (ymax - ymin < 1e-9)
    {
        ymin -= 1.0;
        ymax += 1.0;
    }
    const double ystep = niceStep(ymax - ymin, 5);
    ymin = std::floor(ymin / ystep) * ystep;
    ymax = std::ceil(ymax / ystep) * ystep;
    const bool hours = (xmax - xmin) <= 4 * 86400.0;
    const double xUnit = hours ? 3600.0 : 86400.0;
    const double xstep = niceStep((xmax - xmin) / xUnit, 8) * xUnit;

    const int left = fm.horizontalAdvance("-000000.0") + 14;
    const int top = title_.isEmpty() ? 12 : fm.height() + 14;
    const QRect plot(left, top, width() - left - 16, height() - top - 2 * fm.height() - 14);
    if (plot.width() < 40 || plot.height() < 30)
        return;
    auto X = [&](double x) { return plot.left() + (x - xmin) / (xmax - xmin) * plot.width(); };
    auto Y = [&](double y) { return plot.bottom() - (y - ymin) / (ymax - ymin) * plot.height(); };

    if (!title_.isEmpty())
    {
        p.setPen(ink);
        p.drawText(QRect(0, 4, width(), fm.height()), Qt::AlignHCenter, title_);
    }
    p.setPen(QPen(grid, 1));
    for (double y = ymin; y <= ymax + ystep * 0.5; y += ystep)
    {
        p.drawLine(QPointF(plot.left(), Y(y)), QPointF(plot.right(), Y(y)));
    }
    for (double x = std::ceil(xmin / xstep) * xstep; x <= xmax + 1e-6; x += xstep)
        p.drawLine(QPointF(X(x), plot.top()), QPointF(X(x), plot.bottom()));
    p.setPen(ink);
    p.drawRect(plot);
    for (double y = ymin; y <= ymax + ystep * 0.5; y += ystep)
    {
        const QString label = QString::fromStdString(text::formatDouble(std::fabs(y) < ystep * 1e-6 ? 0.0 : y, 6));
        p.drawText(QRectF(0, Y(y) - fm.height() / 2.0, left - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter, label);
    }
    for (double x = std::ceil(xmin / xstep) * xstep; x <= xmax + 1e-6; x += xstep)
    {
        const QString label = QString::fromStdString(text::formatDouble(x / xUnit, 4));
        p.drawText(QRectF(X(x) - 40, plot.bottom() + 4, 80, fm.height()), Qt::AlignHCenter, label);
    }
    p.drawText(QRect(plot.left(), plot.bottom() + fm.height() + 6, plot.width(), fm.height()), Qt::AlignHCenter,
               hours ? tr("Time (hours)") : tr("Time (days)"));
    p.save();
    p.translate(12, plot.center().y());
    p.rotate(-90);
    p.drawText(QRect(-plot.height() / 2, -fm.height(), plot.height(), fm.height()), Qt::AlignCenter, yLabel_);
    p.restore();

    for (int i = 0; i < series_.size(); ++i)
    {
        const auto& s = series_[i];
        if (s.points.isEmpty())
            continue;
        QPainterPath path(QPointF(X(s.points[0].x()), Y(s.points[0].y())));
        for (int k = 1; k < s.points.size(); ++k)
            path.lineTo(X(s.points[k].x()), Y(s.points[k].y()));
        p.setPen(QPen(kColors[i % 6], 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    if (marker_ >= xmin && marker_ <= xmax)
    {
        p.setPen(QPen(QColor(255, 140, 0), 1.5, Qt::DashLine));
        p.drawLine(QPointF(X(marker_), plot.top()), QPointF(X(marker_), plot.bottom()));
    }
    // Legend
    int lx = plot.left() + 8;
    for (int i = 0; i < series_.size() && series_.size() > 1; ++i)
    {
        p.setPen(QPen(kColors[i % 6], 3));
        p.drawLine(lx, plot.top() + 10, lx + 16, plot.top() + 10);
        p.setPen(ink);
        p.drawText(lx + 20, plot.top() + 10 + fm.ascent() / 2 - 1, series_[i].name);
        lx += 30 + fm.horizontalAdvance(series_[i].name);
    }
}

} // namespace anahita::app
