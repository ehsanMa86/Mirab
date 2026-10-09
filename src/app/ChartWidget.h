/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/ChartWidget.h
 * Purpose: Lightweight time-series chart drawn with QPainter.
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

namespace anahita::app
{

class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    struct Series
    {
        QString name;
        QVector<QPointF> points; ///< x in seconds, y in any unit
    };

    explicit ChartWidget(QWidget* parent = nullptr);
    void setSeries(const QVector<Series>& series, const QString& yLabel, const QString& title = {});
    void setMarker(double x);
    void clear();
    QSize sizeHint() const override { return {480, 240}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<Series> series_;
    QString yLabel_;
    QString title_;
    double marker_ = -1.0;
};

} // namespace anahita::app
