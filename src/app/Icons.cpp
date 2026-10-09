/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/Icons.cpp
 * Purpose: Vector line icons drawn in code, crisp at every screen scale.
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
#include "Icons.h"

#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>
#include <functional>

namespace anahita::app::icons
{

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Ink
{
    QColor line;
    QColor accent;
    QColor fill;
};

using Draw = std::function<void(QPainter&, const Ink&)>;

/// Paints a 24 x 24 design at any size, with a lighter ink when disabled.
class LineIconEngine : public QIconEngine
{
public:
    explicit LineIconEngine(Draw draw) : draw_(std::move(draw)) {}
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        Ink ink{QColor(52, 68, 88), QColor(31, 111, 235), QColor(214, 230, 252)};
        if (mode == QIcon::Disabled)
            ink = {QColor(176, 185, 196), QColor(176, 185, 196), QColor(236, 239, 243)};
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft());
        const double s = std::min(rect.width(), rect.height()) / 24.0;
        p->translate((rect.width() - 24 * s) / 2.0, (rect.height() - 24 * s) / 2.0);
        p->scale(s, s);
        draw_(*p, ink);
        p->restore();
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }
    QIconEngine* clone() const override { return new LineIconEngine(draw_); }

private:
    Draw draw_;
};

QPen pen(const QColor& c, double w = 1.7)
{
    return QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void magnifier(QPainter& p, const Ink& k)
{
    p.setPen(pen(k.line));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(10, 10), 6.2, 6.2);
    p.drawLine(QPointF(14.6, 14.6), QPointF(20.5, 20.5));
}

void tray(QPainter& p, const Ink& k)
{
    p.setPen(pen(k.line));
    p.setBrush(Qt::NoBrush);
    QPainterPath t;
    t.moveTo(4, 14);
    t.lineTo(4, 19.5);
    t.lineTo(20, 19.5);
    t.lineTo(20, 14);
    p.drawPath(t);
}

void arrowHead(QPainter& p, QPointF tip, double angleDeg, double size = 3.6)
{
    const double a = angleDeg * kPi / 180.0;
    const QPointF l = tip + QPointF(std::cos(a + 2.5) * size, std::sin(a + 2.5) * size);
    const QPointF r = tip + QPointF(std::cos(a - 2.5) * size, std::sin(a - 2.5) * size);
    p.drawLine(tip, l);
    p.drawLine(tip, r);
}

const QHash<QString, Draw>& table()
{
    static const QHash<QString, Draw> t = {
        {"new",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(Qt::white);
             QPainterPath d;
             d.moveTo(6, 3);
             d.lineTo(14, 3);
             d.lineTo(19, 8);
             d.lineTo(19, 21);
             d.lineTo(6, 21);
             d.closeSubpath();
             p.drawPath(d);
             p.drawPolyline(QPolygonF({QPointF(14, 3), QPointF(14, 8), QPointF(19, 8)}));
             p.setPen(pen(k.accent, 1.9));
             p.drawLine(QPointF(12.5, 11.5), QPointF(12.5, 17.5));
             p.drawLine(QPointF(9.5, 14.5), QPointF(15.5, 14.5));
         }},
        {"open",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(k.fill);
             QPainterPath f;
             f.moveTo(3, 6);
             f.lineTo(9, 6);
             f.lineTo(11, 8);
             f.lineTo(20, 8);
             f.lineTo(20, 19);
             f.lineTo(3, 19);
             f.closeSubpath();
             p.drawPath(f);
             p.setBrush(Qt::white);
             QPainterPath front;
             front.moveTo(3, 19);
             front.lineTo(6, 11);
             front.lineTo(22, 11);
             front.lineTo(19.5, 19);
             front.closeSubpath();
             p.drawPath(front);
         }},
        {"save",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(Qt::white);
             QPainterPath d;
             d.moveTo(4, 4);
             d.lineTo(17, 4);
             d.lineTo(20, 7);
             d.lineTo(20, 20);
             d.lineTo(4, 20);
             d.closeSubpath();
             p.drawPath(d);
             p.setBrush(k.fill);
             p.drawRect(QRectF(7.5, 4, 8, 5));
             p.setBrush(Qt::NoBrush);
             p.drawRect(QRectF(7, 13, 10, 7));
         }},
        {"undo",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(Qt::NoBrush);
             QPainterPath a;
             a.moveTo(5, 10);
             a.lineTo(14, 10);
             a.cubicTo(22, 10, 22, 20, 14, 20);
             a.lineTo(9, 20);
             p.drawPath(a);
             arrowHead(p, QPointF(5, 10), 0, 4);
         }},
        {"redo",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(Qt::NoBrush);
             QPainterPath a;
             a.moveTo(19, 10);
             a.lineTo(10, 10);
             a.cubicTo(2, 10, 2, 20, 10, 20);
             a.lineTo(15, 20);
             p.drawPath(a);
             arrowHead(p, QPointF(19, 10), 180, 4);
         }},
        {"select",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.setBrush(Qt::white);
             p.drawPolygon(QPolygonF({QPointF(6, 3), QPointF(6, 19), QPointF(10, 15), QPointF(13, 21.5),
                                      QPointF(15.5, 20.3), QPointF(12.6, 14), QPointF(18, 14)}));
         }},
        {"pan",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.drawLine(QPointF(12, 3), QPointF(12, 21));
             p.drawLine(QPointF(3, 12), QPointF(21, 12));
             arrowHead(p, QPointF(12, 3), -90, 3);
             arrowHead(p, QPointF(12, 21), 90, 3);
             arrowHead(p, QPointF(3, 12), 180, 3);
             arrowHead(p, QPointF(21, 12), 0, 3);
         }},
        {"zoom-in",
         [](QPainter& p, const Ink& k) {
             magnifier(p, k);
             p.setPen(pen(k.accent));
             p.drawLine(QPointF(10, 7), QPointF(10, 13));
             p.drawLine(QPointF(7, 10), QPointF(13, 10));
         }},
        {"zoom-out",
         [](QPainter& p, const Ink& k) {
             magnifier(p, k);
             p.setPen(pen(k.accent));
             p.drawLine(QPointF(7, 10), QPointF(13, 10));
         }},
        {"zoom-window",
         [](QPainter& p, const Ink& k) {
             p.setPen(QPen(k.accent, 1.4, Qt::DashLine));
             p.setBrush(Qt::NoBrush);
             p.drawRect(QRectF(3, 3, 12, 10));
             p.setPen(pen(k.line));
             p.drawEllipse(QPointF(14, 14), 4.5, 4.5);
             p.drawLine(QPointF(17.3, 17.3), QPointF(21, 21));
         }},
        {"zoom-fit",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.drawPolyline(QPolygonF({QPointF(4, 9), QPointF(4, 4), QPointF(9, 4)}));
             p.drawPolyline(QPolygonF({QPointF(15, 4), QPointF(20, 4), QPointF(20, 9)}));
             p.drawPolyline(QPolygonF({QPointF(20, 15), QPointF(20, 20), QPointF(15, 20)}));
             p.drawPolyline(QPolygonF({QPointF(9, 20), QPointF(4, 20), QPointF(4, 15)}));
             p.setPen(pen(k.accent));
             p.setBrush(k.fill);
             p.drawRect(QRectF(8.5, 8.5, 7, 7));
         }},
        {"run",
         [](QPainter& p, const Ink&) {
             p.setPen(Qt::NoPen);
             p.setBrush(Qt::white);
             p.drawPolygon(QPolygonF({QPointF(7, 4.5), QPointF(19.5, 12), QPointF(7, 19.5)}));
         }},
        {"run-dark",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(QColor(30, 140, 70), 1.4));
             p.setBrush(QColor(40, 170, 90));
             if (k.line.lightness() > 150)
             {
                 p.setPen(pen(k.line, 1.4));
                 p.setBrush(k.fill);
             }
             p.drawPolygon(QPolygonF({QPointF(7, 4.5), QPointF(19.5, 12), QPointF(7, 19.5)}));
         }},
        {"stop",
         [](QPainter& p, const Ink& k) {
             p.setPen(Qt::NoPen);
             p.setBrush(k.line.lightness() > 150 ? k.line : QColor(205, 60, 60));
             p.drawRoundedRect(QRectF(6, 6, 12, 12), 2, 2);
         }},
        {"delete",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(Qt::NoBrush);
             p.drawLine(QPointF(4, 6.5), QPointF(20, 6.5));
             p.drawPolyline(QPolygonF({QPointF(9, 6.5), QPointF(9.5, 3.5), QPointF(14.5, 3.5), QPointF(15, 6.5)}));
             QPainterPath can;
             can.moveTo(6, 6.5);
             can.lineTo(7.2, 20.5);
             can.lineTo(16.8, 20.5);
             can.lineTo(18, 6.5);
             p.drawPath(can);
             p.drawLine(QPointF(10, 10), QPointF(10.3, 17));
             p.drawLine(QPointF(14, 10), QPointF(13.7, 17));
         }},
        {"find", [](QPainter& p, const Ink& k) { magnifier(p, k); }},
        {"labels",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(k.fill);
             QPainterPath tag;
             tag.moveTo(3, 7);
             tag.lineTo(15, 7);
             tag.lineTo(21, 12);
             tag.lineTo(15, 17);
             tag.lineTo(3, 17);
             tag.closeSubpath();
             p.drawPath(tag);
             p.setBrush(Qt::white);
             p.drawEllipse(QPointF(15, 12), 1.5, 1.5);
         }},
        {"basemap",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(k.fill);
             p.drawPolygon(QPolygonF({QPointF(3, 6), QPointF(9, 4), QPointF(15, 6), QPointF(21, 4), QPointF(21, 18),
                                      QPointF(15, 20), QPointF(9, 18), QPointF(3, 20)}));
             p.drawLine(QPointF(9, 4), QPointF(9, 18));
             p.drawLine(QPointF(15, 6), QPointF(15, 20));
         }},
        {"layers",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(Qt::white);
             p.drawPolygon(QPolygonF({QPointF(12, 12), QPointF(21, 16), QPointF(12, 20), QPointF(3, 16)}));
             p.setBrush(k.fill);
             p.drawPolygon(QPolygonF({QPointF(12, 4), QPointF(21, 8.5), QPointF(12, 13), QPointF(3, 8.5)}));
         }},
        {"legend",
         [](QPainter& p, const Ink& k) {
             const QColor c[] = {QColor(43, 103, 199), QColor(40, 180, 90), QColor(220, 60, 50)};
             for (int i = 0; i < 3; ++i)
             {
                 p.setPen(Qt::NoPen);
                 p.setBrush(c[i]);
                 p.drawRoundedRect(QRectF(4, 4.5 + i * 6, 5, 4), 1, 1);
                 p.setPen(pen(k.line));
                 p.drawLine(QPointF(12, 6.5 + i * 6), QPointF(20, 6.5 + i * 6));
             }
         }},
        {"table",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.setBrush(Qt::white);
             p.drawRoundedRect(QRectF(3.5, 4.5, 17, 15), 1.5, 1.5);
             p.setBrush(k.fill);
             p.drawRect(QRectF(3.5, 4.5, 17, 4));
             p.drawLine(QPointF(3.5, 12.5), QPointF(20.5, 12.5));
             p.drawLine(QPointF(3.5, 16), QPointF(20.5, 16));
             p.drawLine(QPointF(10, 4.5), QPointF(10, 19.5));
         }},
        {"chart",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.drawPolyline(QPolygonF({QPointF(4, 3.5), QPointF(4, 20), QPointF(21, 20)}));
             p.setPen(pen(k.accent, 1.9));
             p.drawPolyline(QPolygonF({QPointF(6, 16), QPointF(10, 10), QPointF(14, 13), QPointF(20, 6)}));
         }},
        {"profile",
         [](QPainter& p, const Ink& k) {
             p.setPen(Qt::NoPen);
             p.setBrush(QColor(214, 196, 160));
             QPainterPath ground;
             ground.moveTo(3, 20);
             ground.lineTo(3, 15);
             ground.lineTo(9, 13);
             ground.lineTo(15, 16);
             ground.lineTo(21, 12);
             ground.lineTo(21, 20);
             ground.closeSubpath();
             p.drawPath(ground);
             p.setPen(pen(k.accent, 1.8));
             p.drawPolyline(QPolygonF({QPointF(3, 6), QPointF(9, 7), QPointF(15, 8.5), QPointF(21, 10)}));
             p.setPen(pen(k.line, 1.3));
             p.drawPolyline(QPolygonF({QPointF(3, 15), QPointF(9, 13), QPointF(15, 16), QPointF(21, 12)}));
         }},
        {"frequency",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.drawPolyline(QPolygonF({QPointF(4, 3.5), QPointF(4, 20), QPointF(21, 20)}));
             p.setPen(pen(k.accent, 1.9));
             QPainterPath s;
             s.moveTo(5, 18.5);
             s.cubicTo(13, 18.5, 11, 5, 20, 5);
             p.drawPath(s);
         }},
        {"variability",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.drawPolyline(QPolygonF({QPointF(4, 3.5), QPointF(4, 20), QPointF(21, 20)}));
             p.setPen(Qt::NoPen);
             p.setBrush(k.fill);
             QPainterPath band;
             band.moveTo(5, 12);
             band.cubicTo(10, 4, 14, 9, 20, 6);
             band.lineTo(20, 13);
             band.cubicTo(14, 16, 10, 12, 5, 17);
             band.closeSubpath();
             p.drawPath(band);
             p.setPen(pen(k.accent, 1.7));
             QPainterPath mid;
             mid.moveTo(5, 14.5);
             mid.cubicTo(10, 8, 14, 12.5, 20, 9.5);
             p.drawPath(mid);
         }},
        {"energy",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.4));
             p.setBrush(QColor(255, 196, 60));
             p.drawPolygon(QPolygonF({QPointF(13.5, 2.5), QPointF(5, 13.5), QPointF(11, 13.5), QPointF(9.5, 21.5),
                                      QPointF(19, 9.5), QPointF(13, 9.5)}));
         }},
        {"fireflow",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.4));
             p.setBrush(QColor(232, 88, 60));
             QPainterPath flame;
             flame.moveTo(12, 21);
             flame.cubicTo(5, 21, 4, 13, 9, 9);
             flame.cubicTo(9, 12, 11, 12.5, 11, 12.5);
             flame.cubicTo(10, 7, 13, 4, 14, 2.5);
             flame.cubicTo(15, 7, 20, 9, 19, 15);
             flame.cubicTo(18.5, 19, 15.5, 21, 12, 21);
             p.drawPath(flame);
         }},
        {"report",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(Qt::white);
             p.drawRoundedRect(QRectF(5, 3, 14, 18), 1.5, 1.5);
             p.setPen(pen(k.accent, 1.5));
             for (int i = 0; i < 4; ++i)
                 p.drawLine(QPointF(8, 7.5 + i * 3.3), QPointF(i == 3 ? 13 : 16, 7.5 + i * 3.3));
         }},
        {"import",
         [](QPainter& p, const Ink& k) {
             tray(p, k);
             p.setPen(pen(k.accent));
             p.drawLine(QPointF(12, 3), QPointF(12, 15));
             arrowHead(p, QPointF(12, 15), 90, 4);
         }},
        {"export",
         [](QPainter& p, const Ink& k) {
             tray(p, k);
             p.setPen(pen(k.accent));
             p.drawLine(QPointF(12, 15), QPointF(12, 3));
             arrowHead(p, QPointF(12, 3), -90, 4);
         }},
        {"settings",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.6));
             p.setBrush(Qt::NoBrush);
             QPainterPath gear;
             for (int i = 0; i < 16; ++i)
             {
                 const double a = i * kPi / 8.0;
                 const double r = (i % 2 == 0) ? 9.0 : 7.0;
                 const QPointF q(12 + r * std::cos(a), 12 + r * std::sin(a));
                 i == 0 ? gear.moveTo(q) : gear.lineTo(q);
             }
             gear.closeSubpath();
             p.drawPath(gear);
             p.drawEllipse(QPointF(12, 12), 3, 3);
         }},
        {"engine",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.setBrush(k.fill);
             p.drawRoundedRect(QRectF(6, 6, 12, 12), 2, 2);
             for (int i = 0; i < 3; ++i)
             {
                 const double c = 9 + i * 3;
                 p.drawLine(QPointF(c, 2.5), QPointF(c, 6));
                 p.drawLine(QPointF(c, 18), QPointF(c, 21.5));
                 p.drawLine(QPointF(2.5, c), QPointF(6, c));
                 p.drawLine(QPointF(18, c), QPointF(21.5, c));
             }
         }},
        {"globe",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.setBrush(k.fill);
             p.drawEllipse(QPointF(12, 12), 9, 9);
             p.setBrush(Qt::NoBrush);
             p.drawEllipse(QPointF(12, 12), 4, 9);
             p.drawLine(QPointF(3, 12), QPointF(21, 12));
             p.drawLine(QPointF(4.5, 7.5), QPointF(19.5, 7.5));
             p.drawLine(QPointF(4.5, 16.5), QPointF(19.5, 16.5));
         }},
        {"info",
         [](QPainter& p, const Ink&) {
             p.setPen(Qt::NoPen);
             p.setBrush(QColor(31, 111, 235));
             p.drawEllipse(QPointF(12, 12), 9, 9);
             p.setPen(pen(Qt::white, 2.2));
             p.drawLine(QPointF(12, 11), QPointF(12, 17));
             p.drawPoint(QPointF(12, 7.5));
         }},
        {"warning",
         [](QPainter& p, const Ink&) {
             p.setPen(Qt::NoPen);
             p.setBrush(QColor(232, 160, 20));
             QPainterPath t;
             t.moveTo(12, 3);
             t.lineTo(21.5, 20);
             t.lineTo(2.5, 20);
             t.closeSubpath();
             p.drawPath(t);
             p.setPen(pen(Qt::white, 2.2));
             p.drawLine(QPointF(12, 9), QPointF(12, 14));
             p.drawPoint(QPointF(12, 17));
         }},
        {"error",
         [](QPainter& p, const Ink&) {
             p.setPen(Qt::NoPen);
             p.setBrush(QColor(210, 60, 60));
             p.drawEllipse(QPointF(12, 12), 9, 9);
             p.setPen(pen(Qt::white, 2.2));
             p.drawLine(QPointF(8.5, 8.5), QPointF(15.5, 15.5));
             p.drawLine(QPointF(15.5, 8.5), QPointF(8.5, 15.5));
         }},
        {"play",
         [](QPainter& p, const Ink& k) {
             p.setPen(Qt::NoPen);
             p.setBrush(k.accent);
             p.drawPolygon(QPolygonF({QPointF(8, 5), QPointF(19, 12), QPointF(8, 19)}));
         }},
        {"pause",
         [](QPainter& p, const Ink& k) {
             p.setPen(Qt::NoPen);
             p.setBrush(k.accent);
             p.drawRoundedRect(QRectF(7, 5, 3.5, 14), 1, 1);
             p.drawRoundedRect(QRectF(13.5, 5, 3.5, 14), 1, 1);
         }},
        {"step-back",
         [](QPainter& p, const Ink& k) {
             p.setPen(Qt::NoPen);
             p.setBrush(k.line);
             p.drawRect(QRectF(6, 6, 2, 12));
             p.drawPolygon(QPolygonF({QPointF(18, 6), QPointF(9, 12), QPointF(18, 18)}));
         }},
        {"step-forward",
         [](QPainter& p, const Ink& k) {
             p.setPen(Qt::NoPen);
             p.setBrush(k.line);
             p.drawRect(QRectF(16, 6, 2, 12));
             p.drawPolygon(QPolygonF({QPointF(6, 6), QPointF(15, 12), QPointF(6, 18)}));
         }},
        {"arrows",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.6));
             p.drawLine(QPointF(3, 12), QPointF(21, 12));
             p.setBrush(k.accent);
             p.setPen(Qt::NoPen);
             p.drawPolygon(QPolygonF({QPointF(7, 8), QPointF(12, 12), QPointF(7, 16)}));
             p.drawPolygon(QPolygonF({QPointF(14, 8), QPointF(19, 12), QPointF(14, 16)}));
         }},
        {"options",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.6));
             for (int i = 0; i < 3; ++i)
                 p.drawLine(QPointF(4, 6 + i * 6), QPointF(20, 6 + i * 6));
             p.setBrush(Qt::white);
             p.setPen(pen(k.accent, 1.6));
             p.drawEllipse(QPointF(9, 6), 2.2, 2.2);
             p.drawEllipse(QPointF(16, 12), 2.2, 2.2);
             p.drawEllipse(QPointF(7, 18), 2.2, 2.2);
         }},
        {"pattern",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.4));
             p.drawLine(QPointF(3, 20.5), QPointF(21, 20.5));
             p.setPen(Qt::NoPen);
             p.setBrush(k.accent);
             const double h[] = {6, 10, 15, 11, 7};
             for (int i = 0; i < 5; ++i)
                 p.drawRect(QRectF(4 + i * 3.5, 20 - h[i], 2.6, h[i]));
         }},
        {"curve",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.drawPolyline(QPolygonF({QPointF(4, 3.5), QPointF(4, 20), QPointF(21, 20)}));
             p.setPen(pen(k.accent, 1.9));
             QPainterPath c;
             c.moveTo(5, 7);
             c.cubicTo(12, 7, 16, 11, 20, 18);
             p.drawPath(c);
         }},
        {"controls",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.setBrush(Qt::white);
             p.drawRoundedRect(QRectF(4, 4, 16, 16), 3, 3);
             p.setPen(pen(k.accent, 1.8));
             p.drawPolyline(QPolygonF({QPointF(8, 9), QPointF(10, 11), QPointF(8, 13)}));
             p.drawLine(QPointF(12, 15), QPointF(16, 15));
         }},
        {"home",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line));
             p.setBrush(k.fill);
             p.drawPolygon(QPolygonF({QPointF(4, 11), QPointF(12, 4), QPointF(20, 11), QPointF(18, 11),
                                      QPointF(18, 20), QPointF(6, 20), QPointF(6, 11)}));
         }},
        {"messages",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.6));
             p.setBrush(Qt::white);
             QPainterPath b;
             b.addRoundedRect(QRectF(3.5, 4.5, 17, 12), 3, 3);
             p.drawPath(b);
             p.drawPolyline(QPolygonF({QPointF(8, 16.5), QPointF(7, 20.5), QPointF(12, 16.5)}));
             p.setPen(pen(k.accent, 1.5));
             p.drawLine(QPointF(7, 9), QPointF(17, 9));
             p.drawLine(QPointF(7, 12.5), QPointF(14, 12.5));
         }},
        {"properties",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.setBrush(Qt::white);
             p.drawRoundedRect(QRectF(4, 3.5, 16, 17), 2, 2);
             for (int i = 0; i < 3; ++i)
             {
                 p.setPen(pen(k.accent, 1.5));
                 p.drawLine(QPointF(7, 8 + i * 4.5), QPointF(10, 8 + i * 4.5));
                 p.setPen(pen(k.line, 1.5));
                 p.drawLine(QPointF(12, 8 + i * 4.5), QPointF(17, 8 + i * 4.5));
             }
         }},
        {"project",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.5));
             p.setBrush(k.fill);
             p.drawRoundedRect(QRectF(3, 4, 7, 5), 1, 1);
             p.setBrush(Qt::white);
             p.drawRoundedRect(QRectF(11, 10, 9, 4), 1, 1);
             p.drawRoundedRect(QRectF(11, 16, 9, 4), 1, 1);
             p.setBrush(Qt::NoBrush);
             p.drawPolyline(QPolygonF({QPointF(6.5, 9), QPointF(6.5, 18), QPointF(11, 18)}));
             p.drawLine(QPointF(6.5, 12), QPointF(11, 12));
         }},
        {"close",
         [](QPainter& p, const Ink& k) {
             p.setPen(pen(k.line, 1.6));
             p.drawLine(QPointF(7, 7), QPointF(17, 17));
             p.drawLine(QPointF(17, 7), QPointF(7, 17));
         }},
        {"crs", [](QPainter& p, const Ink& k) { table().value("globe")(p, k); }},
    };
    return t;
}

void drawElement(QPainter& p, const Ink& k, const QString& kind)
{
    const QColor ink = k.line.lightness() > 150 ? k.line : QColor(52, 68, 88);
    const QColor fill = k.line.lightness() > 150 ? k.fill : QColor(66, 133, 230);
    p.setPen(pen(ink, 1.5));
    p.setBrush(fill);
    const QPointF c(12, 12);
    if (kind == "Junction")
        p.drawEllipse(c, 5, 5);
    else if (kind == "Reservoir")
        p.drawPolygon(QPolygonF({QPointF(3.5, 8), QPointF(20.5, 8), QPointF(17, 18.5), QPointF(7, 18.5)}));
    else if (kind == "Tank" || kind == "Storage")
    {
        p.drawRoundedRect(QRectF(7, 4, 10, 16), 1.5, 1.5);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(7, 4, 10, 5));
    }
    else if (kind == "Outfall")
        p.drawPolygon(QPolygonF({QPointF(4, 7), QPointF(20, 7), QPointF(12, 19)}));
    else if (kind == "Divider")
        p.drawPolygon(QPolygonF({QPointF(12, 3.5), QPointF(20.5, 12), QPointF(12, 20.5), QPointF(3.5, 12)}));
    else if (kind == "Subcatchment")
    {
        p.setBrush(QColor(170, 215, 160));
        p.setPen(pen(QColor(60, 140, 70), 1.5));
        p.drawPolygon(QPolygonF({QPointF(4, 6), QPointF(17, 3.5), QPointF(20.5, 17), QPointF(8, 20.5)}));
    }
    else if (kind == "RainGage")
    {
        p.setBrush(QColor(190, 215, 248));
        p.setPen(pen(QColor(40, 110, 200), 1.4));
        QPainterPath cloud;
        cloud.addEllipse(QPointF(8.5, 10), 4.5, 3.8);
        cloud.addEllipse(QPointF(13.5, 7.5), 5.2, 4.6);
        cloud.addEllipse(QPointF(17.5, 10.5), 3.8, 3.2);
        p.drawPath(cloud.simplified());
        p.drawLine(QPointF(9, 16), QPointF(8, 20));
        p.drawLine(QPointF(13, 16), QPointF(12, 20));
        p.drawLine(QPointF(17, 16), QPointF(16, 20));
    }
    else
    {
        p.setPen(pen(ink, 2.4));
        p.drawLine(QPointF(3, 20), QPointF(21, 4));
        if (kind == "Pump")
        {
            p.setPen(pen(ink, 1.5));
            p.drawEllipse(c, 4.6, 4.6);
        }
        else if (kind == "Valve" || kind == "Orifice" || kind == "Weir" || kind == "Outlet")
        {
            p.setPen(pen(ink, 1.4));
            p.drawPolygon(QPolygonF({QPointF(7.5, 8), QPointF(16.5, 16), QPointF(16.5, 8), QPointF(7.5, 16)}));
        }
    }
}
} // namespace

QIcon get(const QString& name)
{
    const auto& t = table();
    const auto it = t.find(name);
    if (it == t.end())
        return element(name);
    return QIcon(new LineIconEngine(it.value()));
}

QIcon element(const QString& kind)
{
    static const QStringList kinds = {"Junction", "Reservoir", "Tank",    "Pipe",  "Pump",   "Valve",
                                      "Outfall",  "Storage",   "Divider", "Conduit", "Orifice", "Weir",
                                      "Outlet",   "Subcatchment", "RainGage"};
    if (!kinds.contains(kind))
        return {};
    return QIcon(new LineIconEngine([kind](QPainter& p, const Ink& k) { drawElement(p, k, kind); }));
}

} // namespace anahita::app::icons
