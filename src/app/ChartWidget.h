/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/ChartWidget.h
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
#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <optional>
#include <utility>

namespace anahita::app
{

class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    struct Series
    {
        enum class Style
        {
            Line,   ///< polyline through the points
            Step,   ///< step function (pattern multipliers)
            Area,   ///< line filled down to the bottom of the plot (ground profile)
            Band,   ///< filled band between points (lower) and upper
            Bars,   ///< vertical bars at each x
            Points  ///< markers only
        };
        QString name;
        QVector<QPointF> points; ///< x in seconds when the x axis is time
        QVector<QPointF> upper;  ///< upper edge for Band
        QColor color;            ///< automatic when invalid
        Style style = Style::Line;
        double width = 2.0;
        bool dashed = false;
        bool inLegend = true;
    };

    explicit ChartWidget(QWidget* parent = nullptr);
    void setSeries(const QVector<Series>& series, const QString& yLabel, const QString& title = {});
    /// Time axis (seconds shown as hours or days) or a plain numeric axis.
    void setXAxis(const QString& label, bool time);
    /// Labels drawn at given x positions (profile plots: node IDs).
    void setXAnnotations(const QVector<std::pair<double, QString>>& labels);
    void setYRange(std::optional<double> min, std::optional<double> max);
    void setMarker(double x);
    void clear(const QString& placeholder = {});
    /// The plotted data as CSV text (one column per series).
    QString toCsv() const;
    QSize sizeHint() const override { return {520, 260}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    QColor colorOf(int index) const;

    QVector<Series> series_;
    QString yLabel_;
    QString title_;
    QString xLabel_;
    bool timeAxis_ = true;
    QVector<std::pair<double, QString>> annotations_;
    std::optional<double> yMin_, yMax_;
    double marker_ = -1.0;
    QString placeholder_;
    QPointF hover_{-1, -1};
    QRectF plot_;
    double xmin_ = 0, xmax_ = 1, ymin_ = 0, ymax_ = 1;
};

} // namespace anahita::app
