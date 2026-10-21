/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MapDecorations.cpp
 * Purpose: Map furniture for print layouts and animations: legend, scale bar, north arrow and
 * labels.
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
#include "MapDecorations.h"

#include "Theme.h"
#include "mirab/core/PrintLayout.h"
#include "mirab/core/Text.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace mirab::app::decor
{

namespace
{
const QColor kInk(38, 48, 62);

QString number(double v)
{
    return QString::fromStdString(text::formatDouble(v, 6));
}
} // namespace

QFont font(double px, bool bold)
{
    QFont f = QApplication::font();
    f.setPixelSize(std::max(1, static_cast<int>(std::lround(px))));
    f.setBold(bold);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QSizeF legend(QPainter* p, const QPointF& topLeft, const MapView::Legend& legend,
              const QStringList& kinds, const QFont& f, bool framed, double maxHeight)
{
    QFont bold = f;
    bold.setBold(true);
    const QFontMetricsF fm(f), fmb(bold);
    const double pad = framed ? fm.height() * 0.6 : 0.0;
    const double rowH = fm.height() * 1.35;
    const double swatch = fm.height() * 1.5;
    const double gap = fm.height() * 0.5;

    struct Row
    {
        enum Kind
        {
            Heading,
            Class,
            Symbol
        } kind;
        QString text;
        QColor color;
    };
    QVector<Row> rows;
    if (!legend.title.isEmpty())
    {
        rows.append({Row::Heading, legend.title, {}});
        // Highest class on top, as on the map.
        for (int i = legend.classes.size() - 1; i >= 0; --i)
            rows.append({Row::Class, legend.classes[i].label, legend.classes[i].color});
        if (!kinds.isEmpty())
            rows.append(
                {Row::Heading, QCoreApplication::translate("MapDecorations", "Network"), {}});
    }
    for (const QString& k : kinds)
        rows.append({Row::Symbol, k, {}});

    double w = 0, h = 0;
    QVector<double> ys;
    for (const Row& r : rows)
    {
        const double rh = r.kind == Row::Heading ? fmb.height() * 1.3 + (h > 0 ? gap : 0) : rowH;
        if (h + rh + 2 * pad > maxHeight)
            break;
        ys.append(h + (r.kind == Row::Heading && h > 0 ? gap : 0));
        h += rh;
        w = std::max(w, r.kind == Row::Heading ? fmb.horizontalAdvance(r.text)
                                               : swatch + gap + fm.horizontalAdvance(r.text));
    }
    const QSizeF size(w + 2 * pad, h + 2 * pad);
    if (!p || rows.isEmpty())
        return rows.isEmpty() ? QSizeF() : size;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    if (framed)
    {
        p->setPen(QPen(theme::kBorder, 0.8));
        p->setBrush(QColor(255, 255, 255, 238));
        p->drawRoundedRect(QRectF(topLeft, size), pad * 0.8, pad * 0.8);
    }
    const double x = topLeft.x() + pad;
    for (int i = 0; i < ys.size(); ++i)
    {
        const Row& r = rows[i];
        const double y = topLeft.y() + pad + ys[i];
        if (r.kind == Row::Heading)
        {
            p->setFont(bold);
            p->setPen(kInk);
            p->drawText(QPointF(x, y + fmb.ascent()), r.text);
            continue;
        }
        const QRectF box(x, y + rowH * 0.12, swatch, rowH * 0.76);
        if (r.kind == Row::Class)
        {
            p->setPen(Qt::NoPen);
            p->setBrush(r.color);
            p->drawRoundedRect(box.adjusted(0, rowH * 0.06, 0, -rowH * 0.06), 2, 2);
        }
        else
        {
            MapView::drawSymbol(*p, box.center(), swatch * 0.95, r.text);
        }
        p->setFont(f);
        p->setPen(kInk);
        p->drawText(QPointF(x + swatch + gap, y + (rowH - fm.height()) / 2 + fm.ascent()), r.text);
    }
    p->restore();
    return size;
}

QRectF scaleBar(QPainter* p, const QPointF& origin, double groundPerUnit, const QString& unit,
                double maxWidth, const QFont& f, bool framed)
{
    if (!(groundPerUnit > 0) || !(maxWidth > 0))
        return {};
    const double maxGround = maxWidth * groundPerUnit;
    double factor = 1.0;
    QString label = unit;
    if (unit == "m" && core::layout::niceLength(maxGround) >= 1000)
    {
        factor = 1000.0;
        label = "km";
    }
    else if (unit == "ft" && core::layout::niceLength(maxGround) >= 5280)
    {
        factor = 5280.0;
        label = "mi";
    }
    const double length = core::layout::niceLength(maxGround / factor);
    const double width = length * factor / groundPerUnit;
    if (!(width > 1))
        return {};
    const QFontMetricsF fm(f);
    const double barH = std::max(3.0, fm.height() * 0.38);
    const double pad = framed ? fm.height() * 0.5 : 0.0;
    const QString endText = number(length) + " " + label;
    const double endW = fm.horizontalAdvance(endText);
    const double startW = fm.horizontalAdvance("0");
    const QRectF bounds(origin.x() - startW / 2 - pad, origin.y() - barH - fm.height() * 1.15 - pad,
                        width + startW / 2 + endW / 2 + 2 * pad,
                        barH + fm.height() * 1.15 + 2 * pad);
    if (!p)
        return bounds;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    if (framed)
    {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 225));
        p->drawRoundedRect(bounds, pad, pad);
    }
    const int segments = 4;
    for (int i = 0; i < segments; ++i)
    {
        const QRectF seg(origin.x() + width * i / segments, origin.y() - barH, width / segments,
                         barH);
        p->setPen(QPen(kInk, 0.8));
        p->setBrush(i % 2 == 0 ? kInk : QColor(Qt::white));
        p->drawRect(seg);
    }
    p->setFont(f);
    p->setPen(kInk);
    const double textY = origin.y() - barH - fm.height() * 0.25;
    p->drawText(QPointF(origin.x() - startW / 2, textY), "0");
    const QString mid = number(length / 2);
    p->drawText(QPointF(origin.x() + width / 2 - fm.horizontalAdvance(mid) / 2, textY), mid);
    p->drawText(QPointF(origin.x() + width - endW / 2, textY), endText);
    p->restore();
    return bounds;
}

void northArrow(QPainter& p, const QRectF& box, double angle, const QFont& f, bool framed)
{
    const double s = std::min(box.width(), box.height());
    if (s <= 4)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (framed)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 225));
        p.drawRoundedRect(QRectF(box.center() - QPointF(s / 2, s / 2), QSizeF(s, s)), s * 0.12,
                          s * 0.12);
    }
    p.translate(box.center());
    p.rotate(angle);
    const QPointF tip(0, -s * 0.2), notch(0, s * 0.27), left(-s * 0.16, s * 0.42),
        right(s * 0.16, s * 0.42);
    p.setPen(QPen(kInk, std::max(0.6, s * 0.02), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(kInk);
    p.drawPolygon(QPolygonF({tip, left, notch}));
    p.setBrush(Qt::white);
    p.drawPolygon(QPolygonF({tip, right, notch}));
    QFont nf = f;
    nf.setBold(true);
    nf.setPixelSize(std::max(1, static_cast<int>(std::lround(s * 0.26))));
    p.setFont(nf);
    p.setPen(kInk);
    p.drawText(QRectF(-s * 0.3, -s * 0.5, s * 0.6, s * 0.3), Qt::AlignCenter, "N");
    p.restore();
}

QRectF labelBox(QPainter& p, const QPointF& anchor, Qt::Alignment corner, const QString& text,
                const QFont& f)
{
    const QFontMetricsF fm(f);
    const QRectF textRect =
        fm.boundingRect(QRectF(0, 0, 4000, 4000), Qt::AlignLeft | Qt::AlignTop, text);
    const double padX = fm.height() * 0.55, padY = fm.height() * 0.3;
    QRectF box(0, 0, textRect.width() + 2 * padX, textRect.height() + 2 * padY);
    box.moveLeft(corner & Qt::AlignRight ? anchor.x() - box.width() : anchor.x());
    box.moveTop(corner & Qt::AlignBottom ? anchor.y() - box.height() : anchor.y());
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme::kBorder, 0.8));
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawRoundedRect(box, padY * 1.5, padY * 1.5);
    p.setFont(f);
    p.setPen(kInk);
    p.drawText(box.adjusted(padX, padY, -padX, -padY), Qt::AlignLeft | Qt::AlignTop, text);
    p.restore();
    return box;
}

} // namespace mirab::app::decor
