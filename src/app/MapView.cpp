/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/MapView.cpp
 * Purpose: Implementation of the network map widget.
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
#include "MapView.h"

#include "anahita/core/Text.h"

#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace anahita::app
{

using core::Category;
using core::Point;

namespace
{
const QColor kRamp[] = {QColor(43, 103, 199), QColor(0, 170, 210), QColor(40, 180, 90),
                        QColor(240, 200, 30), QColor(220, 50, 40)};

double segDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 0 ? ((p - a).x() * ab.x() + (p - a).y() * ab.y()) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF c = a + t * ab;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

bool insidePolygon(const std::vector<Point>& poly, const Point& p)
{
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
            in = !in;
    }
    return in;
}
} // namespace

MapView::MapView(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(doc_, &ProjectDocument::modelReset, this, [this] {
        fitted_ = false;
        cancelPending();
        recomputeRanges();
        fitToWindow();
    });
    connect(doc_, &ProjectDocument::modelChanged, this, [this] {
        recomputeRanges();
        update();
    });
    connect(doc_, &ProjectDocument::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(doc_, &ProjectDocument::resultsChanged, this, [this] {
        setNodeVariable(nodeVar_);
        setLinkVariable(linkVar_);
        setAreaVariable(areaVar_);
    });
    connect(doc_, &ProjectDocument::periodChanged, this, qOverload<>(&QWidget::update));
}

void MapView::setTool(Tool tool, const QString& kind)
{
    cancelPending();
    tool_ = tool;
    kind_ = kind;
    switch (tool)
    {
    case Tool::Pan:
        setCursor(Qt::OpenHandCursor);
        break;
    case Tool::Select:
        setCursor(Qt::ArrowCursor);
        break;
    default:
        setCursor(Qt::CrossCursor);
        break;
    }
    if (tool == Tool::AddLink)
        emit statusMessage(tr("Click the start node, optional bend points, then the end node."));
    else if (tool == Tool::AddArea)
        emit statusMessage(tr("Click the outline corners; double-click or press Enter to finish."));
    else if (tool == Tool::AddNode || tool == Tool::AddGage)
        emit statusMessage(tr("Click on the map to place the %1.").arg(kind.isEmpty() ? tr("rain gage") : kind.toLower()));
    update();
}

void MapView::cancelPending()
{
    linkStart_.clear();
    pendingVertices_.clear();
    pendingPolygon_.clear();
    dragging_ = false;
    dragNode_.clear();
    update();
}

QPointF MapView::toScreen(const Point& p) const
{
    return QPointF(width() / 2.0 + (p.x - center_.x) * scale_, height() / 2.0 - (p.y - center_.y) * scale_);
}

Point MapView::toWorld(const QPointF& s) const
{
    return Point{center_.x + (s.x() - width() / 2.0) / scale_, center_.y - (s.y() - height() / 2.0) / scale_};
}

void MapView::fitToWindow()
{
    core::Bounds b = doc_->model().bounds();
    if (!backdropWorld_.isNull())
    {
        b.include({backdropWorld_.left(), backdropWorld_.top()});
        b.include({backdropWorld_.right(), backdropWorld_.bottom()});
    }
    if (!b.valid)
    {
        b.include({0, 0});
        b.include({10000, 10000});
    }
    double w = std::max(b.width(), 1e-6), h = std::max(b.height(), 1e-6);
    if (b.width() < 1e-6 && b.height() < 1e-6)
        w = h = 100.0;
    center_ = {(b.minX + b.maxX) / 2.0, (b.minY + b.maxY) / 2.0};
    const double sx = (width() - 60) / w;
    const double sy = (height() - 60) / h;
    scale_ = std::max(1e-9, std::min(sx > 0 ? sx : 1.0, sy > 0 ? sy : 1.0));
    fitted_ = true;
    update();
}

void MapView::zoomBy(double factor)
{
    scale_ = std::clamp(scale_ * factor, 1e-9, 1e9);
    update();
}

void MapView::setShowLabels(bool on)
{
    showLabels_ = on;
    update();
}

void MapView::setNodeVariable(const QString& name)
{
    nodeVar_ = name;
    nodeVarIndex_ = -1;
    if (const auto* r = doc_->results())
    {
        if (const auto* t = r->table("node"))
            nodeVarIndex_ = t->variableIndex(name.toStdString());
    }
    recomputeRanges();
    update();
}

void MapView::setLinkVariable(const QString& name)
{
    linkVar_ = name;
    linkVarIndex_ = -1;
    if (const auto* r = doc_->results())
    {
        if (const auto* t = r->table("link"))
            linkVarIndex_ = t->variableIndex(name.toStdString());
    }
    recomputeRanges();
    update();
}

void MapView::setAreaVariable(const QString& name)
{
    areaVar_ = name;
    areaVarIndex_ = -1;
    if (const auto* r = doc_->results())
    {
        if (const auto* t = r->table("subcatchment"))
            areaVarIndex_ = t->variableIndex(name.toStdString());
    }
    recomputeRanges();
    update();
}

void MapView::recomputeRanges()
{
    // The legend follows the node variable when one is shown, otherwise the link variable.
    const auto* r = doc_->results();
    legendTitle_.clear();
    rangeMin_ = 0.0;
    rangeMax_ = 1.0;
    if (!r)
        return;
    const char* table = nullptr;
    int var = -1;
    if (nodeVarIndex_ >= 0)
    {
        table = "node";
        var = nodeVarIndex_;
    }
    else if (linkVarIndex_ >= 0)
    {
        table = "link";
        var = linkVarIndex_;
    }
    else if (areaVarIndex_ >= 0)
    {
        table = "subcatchment";
        var = areaVarIndex_;
    }
    if (!table)
        return;
    const auto* t = r->table(table);
    double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
    for (int p = 0; p < r->periodCount(); ++p)
    {
        for (int e = 0; e < t->elementCount(); ++e)
        {
            double v = t->value(p, var, e);
            if (std::string(table) == "link")
                v = std::fabs(v);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    if (lo > hi)
        return;
    if (hi - lo < 1e-12)
        hi = lo + 1.0;
    rangeMin_ = lo;
    rangeMax_ = hi;
    const auto& v = t->variables[static_cast<std::size_t>(var)];
    legendTitle_ = QString::fromStdString(v.name) + (v.unit.empty() ? QString() : " (" + QString::fromStdString(v.unit) + ")");
}

QColor MapView::colorFor(double value) const
{
    const double t = std::clamp((value - rangeMin_) / (rangeMax_ - rangeMin_), 0.0, 1.0);
    const int idx = std::min(4, static_cast<int>(t * 5.0));
    return kRamp[idx];
}

std::optional<double> MapView::resultValue(const char* table, int variable, const std::string& id) const
{
    const auto* r = doc_->results();
    if (!r || variable < 0)
        return std::nullopt;
    const auto* t = r->table(table);
    if (!t)
        return std::nullopt;
    const int e = t->elementIndex(id);
    if (e < 0)
        return std::nullopt;
    return t->value(doc_->period(), variable, e);
}

bool MapView::setBackdrop(const QString& imagePath, QString* error)
{
    QImage img(imagePath);
    if (img.isNull())
    {
        if (error)
            *error = tr("The image could not be read.");
        return false;
    }
    QFileInfo fi(imagePath);
    QRectF world;
    for (const QString& ext : {QStringLiteral("pgw"), QStringLiteral("jgw"), QStringLiteral("tfw"),
                               QStringLiteral("wld"), fi.suffix() + "w"})
    {
        QFile wf(fi.path() + "/" + fi.completeBaseName() + "." + ext);
        if (!wf.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream ts(&wf);
        double v[6];
        bool ok = true;
        for (double& d : v)
        {
            const auto parsed = text::toDouble(ts.readLine().toStdString());
            ok = ok && parsed.has_value();
            d = parsed.value_or(0.0);
        }
        if (ok && v[0] != 0.0 && v[3] != 0.0)
        {
            const double left = v[4] - v[0] / 2.0;
            const double top = v[5] - v[3] / 2.0;
            world = QRectF(QPointF(left, top + v[3] * img.height()), QPointF(left + v[0] * img.width(), top));
            break;
        }
    }
    if (world.isNull())
    {
        core::Bounds b = doc_->model().bounds();
        if (!b.valid)
        {
            b.include({0, 0});
            b.include({static_cast<double>(img.width()), static_cast<double>(img.height())});
        }
        world = QRectF(QPointF(b.minX, b.minY), QPointF(b.maxX, b.maxY));
        emit statusMessage(tr("No world file found; the image was stretched over the network extent."));
    }
    backdrop_ = img;
    backdropWorld_ = world.normalized();
    update();
    return true;
}

void MapView::clearBackdrop()
{
    backdrop_ = QImage();
    backdropWorld_ = QRectF();
    update();
}

Selection MapView::hitTest(const QPointF& s) const
{
    const auto& m = doc_->model();
    double best = 9.0;
    Selection hit;
    for (const auto& n : m.nodes())
    {
        if (!n.placed)
            continue;
        const QPointF q = toScreen(n.pos);
        const double d = std::hypot(q.x() - s.x(), q.y() - s.y());
        if (d < best)
        {
            best = d;
            hit = {Category::Node, QString::fromStdString(n.id)};
        }
    }
    for (const auto& g : m.gages())
    {
        if (!g.placed)
            continue;
        const QPointF q = toScreen(g.pos);
        const double d = std::hypot(q.x() - s.x(), q.y() - s.y());
        if (d < best)
        {
            best = d;
            hit = {Category::Gage, QString::fromStdString(g.id)};
        }
    }
    if (!hit.isEmpty())
        return hit;
    best = 6.0;
    for (const auto& l : m.links())
    {
        const auto* a = m.findNode(l.from);
        const auto* b = m.findNode(l.to);
        if (!a || !b || !a->placed || !b->placed)
            continue;
        QPointF prev = toScreen(a->pos);
        auto test = [&](const QPointF& next) {
            const double d = segDistance(s, prev, next);
            if (d < best)
            {
                best = d;
                hit = {Category::Link, QString::fromStdString(l.id)};
            }
            prev = next;
        };
        for (const auto& v : l.vertices)
            test(toScreen(v));
        test(toScreen(b->pos));
    }
    if (!hit.isEmpty())
        return hit;
    const Point w = toWorld(s);
    for (const auto& a : m.areas())
    {
        if (a.polygon.size() >= 3 && insidePolygon(a.polygon, w))
            return {Category::Area, QString::fromStdString(a.id)};
    }
    return {};
}

QString MapView::prefixFor(const QString& kind) const
{
    static const QHash<QString, QString> prefixes = {
        {"Junction", "J"}, {"Reservoir", "R"}, {"Tank", "T"}, {"Outfall", "O"}, {"Storage", "SU"},
        {"Divider", "D"},  {"Pipe", "P"},      {"Pump", "PU"}, {"Valve", "V"},  {"Conduit", "C"},
        {"Orifice", "OR"}, {"Weir", "W"},      {"Outlet", "OL"}};
    return prefixes.value(kind, kind.left(1).toUpper());
}

void MapView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    paintTo(p, rect());
}

QImage MapView::renderImage()
{
    return grab().toImage();
}

void MapView::paintTo(QPainter& p, const QRect& area)
{
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    p.fillRect(area, pal.color(QPalette::Base));
    const auto& m = doc_->model();
    const Selection& sel = doc_->selection();
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    const QColor ink = dark ? QColor(220, 225, 232) : QColor(40, 48, 60);
    const QColor selColor(255, 140, 0);

    if (!backdrop_.isNull())
    {
        const QPointF tl = toScreen(backdropWorld_.left(), backdropWorld_.bottom());
        const QPointF br = toScreen(backdropWorld_.right(), backdropWorld_.top());
        p.setOpacity(0.85);
        p.drawImage(QRectF(tl, br).normalized(), backdrop_);
        p.setOpacity(1.0);
    }

    // Subcatchments
    for (const auto& a : m.areas())
    {
        if (a.polygon.size() < 3)
            continue;
        QPolygonF poly;
        for (const auto& v : a.polygon)
            poly << toScreen(v);
        QColor fill = dark ? QColor(70, 120, 80, 110) : QColor(150, 205, 150, 120);
        if (auto v = resultValue("subcatchment", areaVarIndex_, a.id); v && nodeVarIndex_ < 0 && linkVarIndex_ < 0)
        {
            fill = colorFor(*v);
            fill.setAlpha(150);
        }
        const bool selected = sel.category == Category::Area && sel.id.toStdString() == a.id;
        p.setPen(QPen(selected ? selColor : QColor(60, 140, 70), selected ? 2.5 : 1.2));
        p.setBrush(fill);
        p.drawPolygon(poly);
        if (const auto* out = m.findNode(a.outlet); out && out->placed)
        {
            QPointF c(0, 0);
            for (const auto& q : poly)
                c += q;
            c /= poly.size();
            p.setPen(QPen(QColor(60, 140, 70), 1, Qt::DashLine));
            p.drawLine(c, toScreen(out->pos));
        }
    }

    // Links
    const double lw = std::clamp(scale_ * 1.5, 1.6, 4.0);
    for (const auto& l : m.links())
    {
        const auto* a = m.findNode(l.from);
        const auto* b = m.findNode(l.to);
        if (!a || !b || !a->placed || !b->placed)
            continue;
        QPolygonF line;
        line << toScreen(a->pos);
        if (!dragNode_.isEmpty() && dragging_ && a->id == dragNode_.toStdString())
            line.first() = toScreen(dragTo_);
        for (const auto& v : l.vertices)
            line << toScreen(v);
        line << toScreen(b->pos);
        if (!dragNode_.isEmpty() && dragging_ && b->id == dragNode_.toStdString())
            line.last() = toScreen(dragTo_);
        QColor c = ink;
        if (auto v = resultValue("link", linkVarIndex_, l.id))
            c = colorFor(std::fabs(*v));
        const bool selected = sel.category == Category::Link && sel.id.toStdString() == l.id;
        p.setPen(QPen(selected ? selColor : c, selected ? lw + 2.0 : lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);
        if (l.kind == "Pump" || l.kind == "Valve" || l.kind == "Orifice" || l.kind == "Weir" || l.kind == "Outlet")
        {
            const QPointF mid = (line.first() + line.last()) / 2.0;
            p.setBrush(pal.color(QPalette::Base));
            p.setPen(QPen(selected ? selColor : c, 1.5));
            if (l.kind == "Pump")
                p.drawEllipse(mid, 5.5, 5.5);
            else
            {
                QPolygonF bow;
                bow << mid + QPointF(-6, -4) << mid + QPointF(6, 4) << mid + QPointF(6, -4) << mid + QPointF(-6, 4);
                p.drawPolygon(bow);
            }
        }
    }

    // Pending link and polygon
    if (tool_ == Tool::AddLink && !linkStart_.isEmpty())
    {
        if (const auto* a = m.findNode(linkStart_.toStdString()))
        {
            QPolygonF line;
            line << toScreen(a->pos);
            for (const auto& v : pendingVertices_)
                line << toScreen(v);
            line << hoverScreen_;
            p.setPen(QPen(selColor, 1.5, Qt::DashLine));
            p.drawPolyline(line);
        }
    }
    if (tool_ == Tool::AddArea && !pendingPolygon_.empty())
    {
        QPolygonF poly;
        for (const auto& v : pendingPolygon_)
            poly << toScreen(v);
        poly << hoverScreen_;
        p.setPen(QPen(QColor(60, 140, 70), 1.5, Qt::DashLine));
        p.setBrush(QColor(150, 205, 150, 60));
        p.drawPolygon(poly);
    }

    // Nodes
    const double r = std::clamp(scale_ * 3.0, 3.5, 7.0);
    const bool labels = showLabels_ && (m.nodes().size() < 400 || scale_ > 0.5);
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
    p.setFont(f);
    for (const auto& n : m.nodes())
    {
        if (!n.placed)
            continue;
        QPointF c = toScreen(n.pos);
        if (dragging_ && n.id == dragNode_.toStdString())
            c = toScreen(dragTo_);
        if (!area.adjusted(-20, -20, 20, 20).contains(c.toPoint()))
            continue;
        QColor fill = dark ? QColor(150, 160, 175) : QColor(90, 100, 115);
        if (auto v = resultValue("node", nodeVarIndex_, n.id))
            fill = colorFor(*v);
        const bool selected = sel.category == Category::Node && sel.id.toStdString() == n.id;
        p.setPen(QPen(selected ? selColor : ink, selected ? 2.5 : 1.0));
        p.setBrush(fill);
        if (n.kind == "Junction")
            p.drawEllipse(c, r, r);
        else if (n.kind == "Reservoir")
        {
            QPolygonF shape;
            shape << c + QPointF(-r * 1.6, -r) << c + QPointF(r * 1.6, -r) << c + QPointF(r * 1.1, r * 1.2)
                  << c + QPointF(-r * 1.1, r * 1.2);
            p.drawPolygon(shape);
        }
        else if (n.kind == "Tank" || n.kind == "Storage")
            p.drawRect(QRectF(c.x() - r * 1.3, c.y() - r * 1.6, r * 2.6, r * 3.2));
        else if (n.kind == "Outfall")
        {
            QPolygonF tri;
            tri << c + QPointF(-r * 1.4, -r) << c + QPointF(r * 1.4, -r) << c + QPointF(0, r * 1.4);
            p.drawPolygon(tri);
        }
        else
        {
            QPolygonF dia;
            dia << c + QPointF(0, -r * 1.4) << c + QPointF(r * 1.4, 0) << c + QPointF(0, r * 1.4) << c + QPointF(-r * 1.4, 0);
            p.drawPolygon(dia);
        }
        if (labels)
        {
            p.setPen(ink);
            p.drawText(c + QPointF(r + 3, -r - 1), QString::fromStdString(n.id));
        }
    }

    // Rain gages
    for (const auto& g : m.gages())
    {
        if (!g.placed)
            continue;
        const QPointF c = toScreen(g.pos);
        const bool selected = sel.category == Category::Gage && sel.id.toStdString() == g.id;
        p.setPen(QPen(selected ? selColor : QColor(40, 110, 200), selected ? 2.5 : 1.5));
        p.setBrush(QColor(170, 205, 245));
        QPainterPath cloud;
        cloud.addEllipse(c + QPointF(-7, -3), 6, 5);
        cloud.addEllipse(c + QPointF(0, -6), 7, 6);
        cloud.addEllipse(c + QPointF(7, -3), 6, 5);
        p.drawPath(cloud.simplified());
        for (int i = -1; i <= 1; ++i)
            p.drawLine(c + QPointF(i * 5, 4), c + QPointF(i * 5 - 2, 10));
        if (labels)
        {
            p.setPen(ink);
            p.drawText(c + QPointF(12, -6), QString::fromStdString(g.id));
        }
    }

    drawLegend(p, area);
}

void MapView::drawLegend(QPainter& p, const QRect& area)
{
    if (legendTitle_.isEmpty())
        return;
    const QFontMetrics fm(p.font());
    const int w = std::max(150, fm.horizontalAdvance(legendTitle_) + 20);
    const int h = 5 * (fm.height() + 2) + fm.height() + 16;
    QRect box(area.left() + 10, area.bottom() - h - 10, w, h);
    QColor bg = palette().color(QPalette::Window);
    bg.setAlpha(225);
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(bg);
    p.drawRoundedRect(box, 6, 6);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(box.adjusted(8, 6, -8, 0), Qt::AlignLeft | Qt::AlignTop, legendTitle_);
    for (int i = 0; i < 5; ++i)
    {
        const double lo = rangeMin_ + (rangeMax_ - rangeMin_) * i / 5.0;
        const double hi = rangeMin_ + (rangeMax_ - rangeMin_) * (i + 1) / 5.0;
        const int y = box.top() + 10 + fm.height() + (4 - i) * (fm.height() + 2);
        p.fillRect(QRect(box.left() + 10, y + 2, 18, fm.height() - 4), kRamp[i]);
        p.drawText(QPoint(box.left() + 36, y + fm.ascent()),
                   QString::fromStdString(text::formatDouble(lo, 3)) + " - " +
                       QString::fromStdString(text::formatDouble(hi, 3)));
    }
}

void MapView::resizeEvent(QResizeEvent*)
{
    if (!fitted_)
        fitToWindow();
}

void MapView::wheelEvent(QWheelEvent* e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0)
        return;
    const QPointF pos = e->position();
    const Point before = toWorld(pos);
    scale_ = std::clamp(scale_ * std::pow(1.2, steps), 1e-9, 1e9);
    const Point after = toWorld(pos);
    center_.x += before.x - after.x;
    center_.y += before.y - after.y;
    update();
}

void MapView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    pressPos_ = e->pos();
    lastPos_ = e->pos();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && tool_ == Tool::Pan))
    {
        panning_ = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton)
    {
        if (tool_ != Tool::Select)
        {
            setTool(Tool::Select);
            emit toolFinished();
            return;
        }
        const Selection hit = hitTest(e->position());
        if (!hit.isEmpty())
        {
            doc_->select(hit);
            emit contextMenuForSelection(e->globalPosition().toPoint());
        }
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    const Point w = toWorld(e->position());
    const auto& m = doc_->model();
    switch (tool_)
    {
    case Tool::Select:
    {
        const Selection hit = hitTest(e->position());
        doc_->select(hit);
        if (hit.category == Category::Node || hit.category == Category::Gage)
        {
            dragNode_ = hit.id;
            dragging_ = false;
        }
        else if (hit.isEmpty())
        {
            panning_ = true;
        }
        break;
    }
    case Tool::AddNode:
    {
        const QString id = QString::fromStdString(m.uniqueId(Category::Node, prefixFor(kind_).toStdString()));
        QString err;
        const std::string k = kind_.toStdString();
        if (doc_->edit(tr("Add %1 %2").arg(kind_.toLower(), id),
                       [&](core::Model& mm, std::string* er) { return mm.addNode(k, id.toStdString(), w, er); }, &err))
            doc_->select({Category::Node, id});
        else
            emit statusMessage(err);
        break;
    }
    case Tool::AddGage:
    {
        const QString id = QString::fromStdString(m.uniqueId(Category::Gage, "RG"));
        QString err;
        if (doc_->edit(tr("Add rain gage %1").arg(id),
                       [&](core::Model& mm, std::string* er) { return mm.addGage(id.toStdString(), w, er); }, &err))
            doc_->select({Category::Gage, id});
        else
            emit statusMessage(err);
        break;
    }
    case Tool::AddLink:
    {
        const Selection hit = hitTest(e->position());
        if (hit.category == Category::Node)
        {
            if (linkStart_.isEmpty())
            {
                linkStart_ = hit.id;
                pendingVertices_.clear();
                emit statusMessage(tr("Start node %1; click the end node.").arg(hit.id));
            }
            else if (hit.id != linkStart_)
            {
                const QString id = QString::fromStdString(m.uniqueId(Category::Link, prefixFor(kind_).toStdString()));
                const std::string k = kind_.toStdString(), a = linkStart_.toStdString(), b = hit.id.toStdString();
                const auto verts = pendingVertices_;
                QString err;
                if (doc_->edit(tr("Add %1 %2").arg(kind_.toLower(), id),
                               [&](core::Model& mm, std::string* er) {
                                   if (!mm.addLink(k, id.toStdString(), a, b, er))
                                       return false;
                                   return verts.empty() || mm.setVertices(id.toStdString(), verts);
                               },
                               &err))
                    doc_->select({Category::Link, id});
                else
                    emit statusMessage(err);
                linkStart_.clear();
                pendingVertices_.clear();
            }
        }
        else if (!linkStart_.isEmpty())
        {
            pendingVertices_.push_back(w);
        }
        update();
        break;
    }
    case Tool::AddArea:
        pendingPolygon_.push_back(w);
        update();
        break;
    case Tool::Pan:
        break;
    }
}

void MapView::mouseMoveEvent(QMouseEvent* e)
{
    hoverScreen_ = e->position();
    emit cursorMoved(QPointF(toWorld(e->position()).x, toWorld(e->position()).y));
    if (panning_)
    {
        const QPoint d = e->pos() - lastPos_;
        center_.x -= d.x() / scale_;
        center_.y += d.y() / scale_;
        lastPos_ = e->pos();
        update();
        return;
    }
    if (!dragNode_.isEmpty() && (e->buttons() & Qt::LeftButton))
    {
        if (!dragging_ && (e->pos() - pressPos_).manhattanLength() > 4)
            dragging_ = true;
        if (dragging_)
        {
            dragTo_ = toWorld(e->position());
            update();
        }
        return;
    }
    if (tool_ == Tool::AddLink || tool_ == Tool::AddArea)
        update();
    const Selection h = hitTest(e->position());
    if (!(h == hover_))
    {
        hover_ = h;
        if (!h.isEmpty())
        {
            QString tip = h.id;
            const char* table = h.category == Category::Node ? "node" : h.category == Category::Link ? "link" : "subcatchment";
            if (const auto* r = doc_->results())
            {
                if (const auto* t = r->table(table))
                {
                    const int el = t->elementIndex(h.id.toStdString());
                    if (el >= 0)
                    {
                        for (int v = 0; v < t->variableCount() && v < 8; ++v)
                            tip += "\n" + QString::fromStdString(t->variables[v].name) + ": " +
                                   QString::fromStdString(text::formatDouble(t->value(doc_->period(), v, el), 5)) + " " +
                                   QString::fromStdString(t->variables[v].unit);
                    }
                }
            }
            QToolTip::showText(e->globalPosition().toPoint(), tip, this);
        }
        else
        {
            QToolTip::hideText();
        }
    }
}

void MapView::mouseReleaseEvent(QMouseEvent* e)
{
    if (panning_)
    {
        panning_ = false;
        setCursor(tool_ == Tool::Pan ? Qt::OpenHandCursor : (tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor));
        if (tool_ == Tool::Select && (e->pos() - pressPos_).manhattanLength() < 3)
            doc_->select({});
        return;
    }
    if (dragging_ && !dragNode_.isEmpty())
    {
        const std::string id = dragNode_.toStdString();
        const Point to = dragTo_;
        const bool gage = doc_->model().findGage(id) != nullptr && doc_->model().findNode(id) == nullptr;
        doc_->edit(tr("Move %1").arg(dragNode_), [&](core::Model& mm, std::string*) {
            return gage ? mm.moveGage(id, to) : mm.moveNode(id, to);
        });
    }
    dragging_ = false;
    dragNode_.clear();
}

void MapView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (tool_ == Tool::AddArea)
    {
        finishArea();
        return;
    }
    if (tool_ == Tool::Select && !hitTest(e->position()).isEmpty())
        emit elementActivated();
}

void MapView::finishArea()
{
    // The double click added a duplicate final point; drop it.
    if (pendingPolygon_.size() >= 2)
    {
        const auto& a = pendingPolygon_[pendingPolygon_.size() - 1];
        const auto& b = pendingPolygon_[pendingPolygon_.size() - 2];
        if (std::hypot(a.x - b.x, a.y - b.y) * scale_ < 4.0)
            pendingPolygon_.pop_back();
    }
    if (pendingPolygon_.size() < 3)
    {
        emit statusMessage(tr("A subcatchment needs at least three corners."));
        return;
    }
    const auto& m = doc_->model();
    core::Point c{0, 0};
    for (const auto& v : pendingPolygon_)
    {
        c.x += v.x;
        c.y += v.y;
    }
    c.x /= pendingPolygon_.size();
    c.y /= pendingPolygon_.size();
    std::string outlet;
    double best = std::numeric_limits<double>::max();
    for (const auto& n : m.nodes())
    {
        if (!n.placed)
            continue;
        const double d = std::hypot(n.pos.x - c.x, n.pos.y - c.y);
        if (d < best)
        {
            best = d;
            outlet = n.id;
        }
    }
    const QString id = QString::fromStdString(m.uniqueId(Category::Area, "S"));
    const auto poly = pendingPolygon_;
    QString err;
    if (doc_->edit(tr("Add subcatchment %1").arg(id),
                   [&](core::Model& mm, std::string* er) { return mm.addArea(id.toStdString(), poly, outlet, er); }, &err))
        doc_->select({Category::Area, id});
    else
        emit statusMessage(err);
    pendingPolygon_.clear();
    update();
}

void MapView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
    {
        if (tool_ != Tool::Select)
        {
            setTool(Tool::Select);
            emit toolFinished();
        }
        else
        {
            doc_->select({});
        }
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && tool_ == Tool::AddArea)
    {
        finishArea();
        return;
    }
    QWidget::keyPressEvent(e);
}

void MapView::leaveEvent(QEvent*)
{
    hover_ = {};
    QToolTip::hideText();
}

} // namespace anahita::app
