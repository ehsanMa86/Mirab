/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MapView.cpp
 * Purpose: Network map: drawing, basemap, result coloring and interactive editing.
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

#include "Basemap.h"
#include "Theme.h"
#include "mirab/core/Text.h"

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

namespace mirab::app
{

using core::Category;
using core::Point;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Color classes of the legend, from low to high values.
const QColor kRamp[] = {QColor(43, 103, 199), QColor(0, 166, 214), QColor(46, 178, 96), QColor(242, 188, 26),
                        QColor(224, 64, 48)};

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

double haversineMeters(double lon1, double lat1, double lon2, double lat2)
{
    const double r = 6371008.8;
    const double p1 = lat1 * kPi / 180, p2 = lat2 * kPi / 180;
    const double dp = p2 - p1, dl = (lon2 - lon1) * kPi / 180;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * r * std::asin(std::min(1.0, std::sqrt(a)));
}

double niceStep(double raw)
{
    const double e = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / e;
    return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * e;
}
} // namespace

MapView::MapView(ProjectDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc), basemap_(new Basemap(this))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(basemap_, &Basemap::updated, this, qOverload<>(&QWidget::update));
    connect(doc_, &ProjectDocument::modelReset, this, [this] {
        fitted_ = false;
        userView_ = false;
        cancelPending();
        recomputeRanges();
        updateCoordinateSystem();
        basemap_->setProvider(QString::fromStdString(doc_->settings().basemap));
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

// ---------------------------------------------------------------------------
// Basemap and coordinate system
// ---------------------------------------------------------------------------
std::vector<std::pair<QString, QString>> MapView::basemapProviders()
{
    std::vector<std::pair<QString, QString>> list;
    for (const auto& p : Basemap::providers())
        list.emplace_back(p.id, p.label);
    return list;
}

QString MapView::basemapLabel(const QString& id)
{
    const auto* p = Basemap::find(id);
    return p ? p->label : QString();
}

void MapView::setBasemap(const QString& id)
{
    basemap_->setProvider(id);
    update();
}

QString MapView::basemap() const
{
    return basemap_->provider();
}

bool MapView::basemapUsable(QString* why) const
{
    if (why)
        *why = basemap_->unusableReason();
    return basemap_->usable();
}

void MapView::setCoordinateSystem(const QString& crs)
{
    crsText_ = crs;
    updateCoordinateSystem();
    update();
}

QString MapView::coordinateSystemName() const
{
    return basemap_->crsName();
}

void MapView::updateCoordinateSystem()
{
    crsText_ = QString::fromStdString(doc_->settings().crs);
    const core::Bounds b = doc_->model().bounds();
    if (b.valid)
        basemap_->setCoordinateSystem(crsText_, b.minX, b.minY, b.maxX, b.maxY);
    else
        basemap_->setCoordinateSystem(crsText_, 1e9, 1e9, -1e9, -1e9);
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------
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
        emit statusMessage(tr("Click the start node, optional bend points, then the end node. Right-click or Esc to finish."));
    else if (tool == Tool::AddArea)
        emit statusMessage(tr("Click the outline corners; double-click or press Enter to finish."));
    else if (tool == Tool::AddNode || tool == Tool::AddGage)
        emit statusMessage(tr("Click on the map to place the %1. Right-click or Esc to finish.")
                               .arg(kind.isEmpty() ? tr("rain gage") : kind.toLower()));
    else if (tool == Tool::ZoomWindow)
        emit statusMessage(tr("Drag a rectangle to zoom into."));
    update();
}

void MapView::cancelPending()
{
    linkStart_.clear();
    pendingVertices_.clear();
    pendingPolygon_.clear();
    dragging_ = false;
    rubberBand_ = false;
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

QPointF MapView::worldAt(const QPoint& screen) const
{
    const Point w = toWorld(screen);
    return {w.x, w.y};
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
    const double margin = 70.0;
    const double sx = (width() - 2 * margin) / w;
    const double sy = (height() - 2 * margin) / h;
    scale_ = std::max(1e-9, std::min(sx > 0 ? sx : 1.0, sy > 0 ? sy : 1.0));
    fitted_ = true;
    userView_ = false;
    update();
}

void MapView::userMovedView()
{
    userView_ = true;
}

void MapView::zoomBy(double factor)
{
    scale_ = std::clamp(scale_ * factor, 1e-9, 1e9);
    userMovedView();
    update();
}

void MapView::zoomTo(const Selection& sel)
{
    const auto& m = doc_->model();
    const std::string id = sel.id.toStdString();
    core::Bounds b;
    if (sel.category == Category::Node)
    {
        if (const auto* n = m.findNode(id); n && n->placed)
            b.include(n->pos);
    }
    else if (sel.category == Category::Link)
    {
        if (const auto* l = m.findLink(id))
        {
            for (const auto* n : {m.findNode(l->from), m.findNode(l->to)})
            {
                if (n && n->placed)
                    b.include(n->pos);
            }
            for (const auto& v : l->vertices)
                b.include(v);
        }
    }
    else if (sel.category == Category::Area)
    {
        if (const auto* a = m.findArea(id))
        {
            for (const auto& v : a->polygon)
                b.include(v);
        }
    }
    else if (sel.category == Category::Gage)
    {
        if (const auto* g = m.findGage(id); g && g->placed)
            b.include(g->pos);
    }
    if (!b.valid)
        return;
    center_ = {(b.minX + b.maxX) / 2.0, (b.minY + b.maxY) / 2.0};
    const double extent = std::max(b.width(), b.height());
    if (extent > 0)
    {
        const double needed = std::min(width(), height()) * 0.5 / extent;
        if (scale_ > needed)
            scale_ = needed;
    }
    userMovedView();
    update();
}

void MapView::setShowLabels(bool on)
{
    showLabels_ = on;
    update();
}

void MapView::setShowArrows(bool on)
{
    showArrows_ = on;
    update();
}

void MapView::setShowLegend(bool on)
{
    showLegend_ = on;
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

int MapView::flowVariable() const
{
    const auto* r = doc_->results();
    if (!r)
        return -1;
    const auto* t = r->table("link");
    return t ? t->variableIndex("Flow") : -1;
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

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void MapView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    paintTo(p, rect());
}

QImage MapView::renderImage()
{
    return grab().toImage();
}

void MapView::drawLabel(QPainter& p, const QPointF& at, const QString& text)
{
    // Text with a light halo, readable on basemaps and on dense networks.
    QPainterPath path;
    path.addText(at, p.font(), text);
    p.setPen(QPen(QColor(255, 255, 255, 220), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(38, 50, 66));
    p.drawPath(path);
}

void MapView::paintTo(QPainter& p, const QRect& area)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(area, theme::kMapBackground);
    const auto& m = doc_->model();
    const Selection& sel = doc_->selection();
    const QColor ink(78, 92, 112);
    const QColor nodeInk(44, 58, 78);
    const QColor selColor = theme::kSelection;
    const QColor hoverColor(31, 111, 235, 90);

    const bool base = basemap_->active();
    if (base)
    {
        basemap_->paint(p, QRectF(area), [this](double x, double y) { return toScreen(x, y); },
                        [this](const QPointF& s) {
                            const Point w = toWorld(s);
                            return QPointF(w.x, w.y);
                        });
        // A light veil keeps the network readable over busy imagery.
        p.fillRect(area, QColor(255, 255, 255, basemap_->provider() == "esri-imagery" ? 30 : 70));
    }
    else
    {
        // Subtle grid gives a sense of scale and motion while panning.
        const double step = niceStep(80.0 / scale_);
        const Point tl = toWorld(QPointF(area.left(), area.top()));
        const Point br = toWorld(QPointF(area.right(), area.bottom()));
        p.setPen(QPen(QColor(234, 238, 243), 1.0));
        if (step * scale_ > 12)
        {
            for (double x = std::floor(tl.x / step) * step; x <= br.x; x += step)
                p.drawLine(QPointF(toScreen(x, 0).x(), area.top()), QPointF(toScreen(x, 0).x(), area.bottom()));
            for (double y = std::floor(br.y / step) * step; y <= tl.y; y += step)
                p.drawLine(QPointF(area.left(), toScreen(0, y).y()), QPointF(area.right(), toScreen(0, y).y()));
        }
    }

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
        QColor fill(150, 205, 150, 110);
        if (auto v = resultValue("subcatchment", areaVarIndex_, a.id); v && nodeVarIndex_ < 0 && linkVarIndex_ < 0)
        {
            fill = colorFor(*v);
            fill.setAlpha(150);
        }
        const bool selected = sel.category == Category::Area && sel.id.toStdString() == a.id;
        const bool hovered = hover_.category == Category::Area && hover_.id.toStdString() == a.id;
        p.setPen(QPen(selected ? selColor : QColor(60, 140, 70), selected ? 2.5 : hovered ? 2.0 : 1.2));
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
    const double lw = std::clamp(scale_ * 1.2, 1.8, 4.0);
    const int flowVar = showArrows_ ? flowVariable() : -1;
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
        if (!line.boundingRect().adjusted(-10, -10, 10, 10).intersects(area))
            continue;
        QColor c = ink;
        if (auto v = resultValue("link", linkVarIndex_, l.id))
            c = colorFor(std::fabs(*v));
        const bool selected = sel.category == Category::Link && sel.id.toStdString() == l.id;
        const bool hovered = hover_.category == Category::Link && hover_.id.toStdString() == l.id;
        p.setBrush(Qt::NoBrush);
        if (selected || hovered)
        {
            p.setPen(QPen(selected ? QColor(255, 140, 0, 110) : hoverColor, lw + 7.0, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            p.drawPolyline(line);
        }
        p.setPen(QPen(selected ? selColor : c, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(line);

        // Flow direction arrow at the middle of the longest segment.
        if (flowVar >= 0)
        {
            if (auto f = resultValue("link", flowVar, l.id); f && std::fabs(*f) > 1e-9)
            {
                int best = 0;
                double bestLen = 0;
                for (int i = 0; i + 1 < line.size(); ++i)
                {
                    const double len = std::hypot(line[i + 1].x() - line[i].x(), line[i + 1].y() - line[i].y());
                    if (len > bestLen)
                    {
                        bestLen = len;
                        best = i;
                    }
                }
                if (bestLen > 28)
                {
                    QPointF s0 = line[best], s1 = line[best + 1];
                    if (*f < 0)
                        std::swap(s0, s1);
                    const QPointF dir = (s1 - s0) / bestLen;
                    const double off = (l.kind == "Pipe" || l.kind == "Conduit") ? 0.5 : 0.78;
                    const QPointF tip = s0 + (s1 - s0) * off + dir * 4.5;
                    const QPointF nrm(-dir.y(), dir.x());
                    const double size = std::max(4.5, lw * 2.0);
                    QPolygonF head;
                    head << tip << tip - dir * size + nrm * size * 0.6 << tip - dir * size - nrm * size * 0.6;
                    p.setPen(Qt::NoPen);
                    p.setBrush(selected ? selColor : c.darker(115));
                    p.drawPolygon(head);
                }
            }
        }

        if (l.kind == "Pump" || l.kind == "Valve" || l.kind == "Orifice" || l.kind == "Weir" || l.kind == "Outlet")
        {
            const QPointF mid = (line.first() + line.last()) / 2.0;
            p.setBrush(Qt::white);
            p.setPen(QPen(selected ? selColor : c.darker(110), 1.6));
            if (l.kind == "Pump")
            {
                p.drawEllipse(mid, 6.5, 6.5);
                p.setBrush(selected ? selColor : c.darker(110));
                const QPointF d = line.last() - line.first();
                const double len = std::max(1.0, std::hypot(d.x(), d.y()));
                const QPointF u = d / len, n(-u.y(), u.x());
                QPolygonF tri;
                tri << mid + u * 4.0 << mid - u * 2.5 + n * 3.0 << mid - u * 2.5 - n * 3.0;
                p.drawPolygon(tri);
            }
            else
            {
                QPolygonF bow;
                bow << mid + QPointF(-7, -4.5) << mid + QPointF(7, 4.5) << mid + QPointF(7, -4.5) << mid + QPointF(-7, 4.5);
                p.drawPolygon(bow);
            }
        }
    }

    // Pending link, polygon and rubber band
    if (tool_ == Tool::AddLink && !linkStart_.isEmpty())
    {
        if (const auto* a = m.findNode(linkStart_.toStdString()))
        {
            QPolygonF line;
            line << toScreen(a->pos);
            for (const auto& v : pendingVertices_)
                line << toScreen(v);
            line << hoverScreen_;
            p.setPen(QPen(theme::kAccent, 1.8, Qt::DashLine));
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
    if (rubberBand_)
    {
        p.setPen(QPen(theme::kAccent, 1.2, Qt::DashLine));
        p.setBrush(QColor(31, 111, 235, 30));
        p.drawRect(QRectF(pressPos_, hoverScreen_).normalized());
    }

    // Nodes
    const double r = std::clamp(scale_ * 2.4, 3.2, 6.5);
    const bool labels = showLabels_ && (m.nodes().size() < 400 || scale_ > 0.5);
    QFont f = font();
    f.setPointSizeF(std::max(7.5, f.pointSizeF() - 1.0));
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
        QColor fill = n.kind == "Junction" ? QColor(255, 255, 255) : QColor(66, 133, 230);
        if (n.kind == "Outfall")
            fill = QColor(120, 120, 200);
        if (auto v = resultValue("node", nodeVarIndex_, n.id))
            fill = colorFor(*v);
        const bool selected = sel.category == Category::Node && sel.id.toStdString() == n.id;
        const bool hovered = hover_.category == Category::Node && hover_.id.toStdString() == n.id;
        if (selected || hovered)
        {
            p.setPen(Qt::NoPen);
            p.setBrush(selected ? QColor(255, 140, 0, 90) : hoverColor);
            p.drawEllipse(c, r * 2.6, r * 2.6);
        }
        p.setPen(QPen(selected ? selColor : nodeInk, selected ? 2.4 : 1.4));
        p.setBrush(fill);
        if (n.kind == "Junction")
            p.drawEllipse(c, r, r);
        else if (n.kind == "Reservoir")
        {
            QPolygonF shape;
            shape << c + QPointF(-r * 1.8, -r * 1.1) << c + QPointF(r * 1.8, -r * 1.1) << c + QPointF(r * 1.2, r * 1.3)
                  << c + QPointF(-r * 1.2, r * 1.3);
            p.drawPolygon(shape);
        }
        else if (n.kind == "Tank" || n.kind == "Storage")
        {
            p.drawRoundedRect(QRectF(c.x() - r * 1.4, c.y() - r * 1.7, r * 2.8, r * 3.4), 1.5, 1.5);
            p.setBrush(Qt::white);
            p.drawRect(QRectF(c.x() - r * 1.4, c.y() - r * 1.7, r * 2.8, r * 1.1));
        }
        else if (n.kind == "Outfall")
        {
            QPolygonF tri;
            tri << c + QPointF(-r * 1.5, -r * 1.1) << c + QPointF(r * 1.5, -r * 1.1) << c + QPointF(0, r * 1.5);
            p.drawPolygon(tri);
        }
        else
        {
            QPolygonF dia;
            dia << c + QPointF(0, -r * 1.5) << c + QPointF(r * 1.5, 0) << c + QPointF(0, r * 1.5) << c + QPointF(-r * 1.5, 0);
            p.drawPolygon(dia);
        }
        if (labels)
            drawLabel(p, c + QPointF(r + 4, -r - 2), QString::fromStdString(n.id));
    }

    // Rain gages
    for (const auto& g : m.gages())
    {
        if (!g.placed)
            continue;
        const QPointF c = toScreen(g.pos);
        const bool selected = sel.category == Category::Gage && sel.id.toStdString() == g.id;
        p.setPen(QPen(selected ? selColor : QColor(40, 110, 200), selected ? 2.5 : 1.5));
        p.setBrush(QColor(190, 215, 248));
        QPainterPath cloud;
        cloud.addEllipse(c + QPointF(-7, -3), 6, 5);
        cloud.addEllipse(c + QPointF(0, -6), 7, 6);
        cloud.addEllipse(c + QPointF(7, -3), 6, 5);
        p.drawPath(cloud.simplified());
        for (int i = -1; i <= 1; ++i)
            p.drawLine(c + QPointF(i * 5, 4), c + QPointF(i * 5 - 2, 10));
        if (labels)
            drawLabel(p, c + QPointF(12, -8), QString::fromStdString(g.id));
    }

    if (showLegend_)
        drawLegend(p, area);
    drawScaleBar(p, area);
    if (base)
        drawAttribution(p, area);
}

void MapView::drawLegend(QPainter& p, const QRect& area)
{
    if (legendTitle_.isEmpty())
        return;
    QFont f = font();
    p.setFont(f);
    const QFontMetrics fm(f);
    QFont bold = f;
    bold.setBold(true);
    const QFontMetrics fmb(bold);
    const int rowH = fm.height() + 4;
    QStringList rows;
    int textW = fmb.horizontalAdvance(legendTitle_);
    for (int i = 0; i < 5; ++i)
    {
        const double lo = rangeMin_ + (rangeMax_ - rangeMin_) * i / 5.0;
        const double hi = rangeMin_ + (rangeMax_ - rangeMin_) * (i + 1) / 5.0;
        rows << QString::fromStdString(text::formatDouble(lo, 3)) + "  –  " + QString::fromStdString(text::formatDouble(hi, 3));
        textW = std::max(textW, fm.horizontalAdvance(rows.last()) + 30);
    }
    const int w = textW + 28;
    const int h = fmb.height() + 5 * rowH + 22;
    const QRect box(area.left() + 12, area.top() + 12, w, h);
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(QColor(255, 255, 255, 238));
    p.drawRoundedRect(box, 8, 8);
    p.setFont(bold);
    p.setPen(theme::kText);
    p.drawText(box.adjusted(12, 9, -10, 0), Qt::AlignLeft | Qt::AlignTop, legendTitle_);
    p.setFont(f);
    for (int i = 0; i < 5; ++i)
    {
        const int y = box.top() + 14 + fmb.height() + (4 - i) * rowH;
        p.setPen(Qt::NoPen);
        p.setBrush(kRamp[i]);
        p.drawRoundedRect(QRect(box.left() + 12, y + 3, 18, rowH - 7), 3, 3);
        p.setPen(theme::kText);
        p.drawText(QPoint(box.left() + 38, y + fm.ascent() + 1), rows[i]);
    }
}

void MapView::drawScaleBar(QPainter& p, const QRect& area)
{
    // Ground distance of 100 screen pixels, in meters when the CRS is known.
    const QPointF a(area.left() + 40, area.bottom() - 30);
    const QPointF b = a + QPointF(100, 0);
    const Point wa = toWorld(a), wb = toWorld(b);
    double perPixel = 0;
    QString unit;
    double lon1, lat1, lon2, lat2;
    if (basemap_->usable() && basemap_->toLonLat(wa.x, wa.y, lon1, lat1) && basemap_->toLonLat(wb.x, wb.y, lon2, lat2))
    {
        perPixel = haversineMeters(lon1, lat1, lon2, lat2) / 100.0;
        unit = "m";
        if (doc_->model().units().system == core::UnitSystem::US)
        {
            perPixel /= 0.3048; // feet and miles for US customary models
            unit = "ft";
        }
    }
    else
    {
        perPixel = std::hypot(wb.x - wa.x, wb.y - wa.y) / 100.0;
        unit = doc_->model().units().system == core::UnitSystem::SI ? "m" : "ft";
    }
    if (!(perPixel > 0) || !std::isfinite(perPixel))
        return;
    double length = niceStep(perPixel * 110.0);
    QString label;
    if (unit == "m" && length >= 1000)
        label = QString::fromStdString(text::formatDouble(length / 1000.0, 3)) + " km";
    else if (unit == "ft" && length >= 5280)
        label = QString::fromStdString(text::formatDouble(length / 5280.0, 3)) + " mi";
    else
        label = QString::fromStdString(text::formatDouble(length, 3)) + " " + unit;
    const double px = length / perPixel;
    const QFontMetrics fm(font());
    const QRectF box(a.x() - 10, a.y() - fm.height() - 12, px + 20, fm.height() + 22);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawRoundedRect(box, 6, 6);
    p.setPen(QPen(theme::kText, 1.6));
    p.drawLine(a, a + QPointF(px, 0));
    p.drawLine(a + QPointF(0, -5), a + QPointF(0, 2));
    p.drawLine(a + QPointF(px, -5), a + QPointF(px, 2));
    p.setFont(font());
    p.drawText(QRectF(a.x(), a.y() - fm.height() - 6, px, fm.height()), Qt::AlignCenter, label);
}

void MapView::drawAttribution(QPainter& p, const QRect& area)
{
    const QString text = basemap_->attribution();
    if (text.isEmpty())
        return;
    QFont f = font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.5));
    p.setFont(f);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 12;
    const QRect box(area.right() - w - 4, area.bottom() - fm.height() - 6, w, fm.height() + 4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawRect(box);
    p.setPen(QColor(60, 70, 85));
    p.drawText(box, Qt::AlignCenter, text);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------
void MapView::resizeEvent(QResizeEvent*)
{
    if (!fitted_ || !userView_)
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
    userMovedView();
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
        else
        {
            const Point w = toWorld(e->position());
            emit contextMenuForMap(e->globalPosition().toPoint(), QPointF(w.x, w.y));
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
    case Tool::ZoomWindow:
        rubberBand_ = true;
        hoverScreen_ = e->position();
        break;
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
        if ((e->pos() - pressPos_).manhattanLength() > 2)
            userMovedView();
        update();
        return;
    }
    if (rubberBand_)
    {
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
        update();
        if (!h.isEmpty())
        {
            QString kind;
            if (h.category == Category::Node)
                kind = QString::fromStdString(doc_->model().findNode(h.id.toStdString())->kind);
            else if (h.category == Category::Link)
                kind = QString::fromStdString(doc_->model().findLink(h.id.toStdString())->kind);
            else if (h.category == Category::Area)
                kind = tr("Subcatchment");
            else
                kind = tr("Rain gage");
            QString tip = "<b>" + kind + " " + h.id.toHtmlEscaped() + "</b>";
            const char* table = h.category == Category::Node ? "node" : h.category == Category::Link ? "link" : "subcatchment";
            if (const auto* r = doc_->results())
            {
                if (const auto* t = r->table(table))
                {
                    const int el = t->elementIndex(h.id.toStdString());
                    if (el >= 0)
                    {
                        tip += "<table style='margin-top:4px'>";
                        for (int v = 0; v < t->variableCount() && v < 8; ++v)
                            tip += "<tr><td>" + QString::fromStdString(t->variables[v].name) + "&nbsp;&nbsp;</td><td align='right'>" +
                                   QString::fromStdString(text::formatDouble(t->value(doc_->period(), v, el), 5)) +
                                   "</td><td>&nbsp;" + QString::fromStdString(t->variables[v].unit).toHtmlEscaped() +
                                   "</td></tr>";
                        tip += "</table>";
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
    if (rubberBand_)
    {
        rubberBand_ = false;
        const QRectF r = QRectF(pressPos_, e->position()).normalized();
        if (r.width() > 6 && r.height() > 6)
        {
            const Point c = toWorld(r.center());
            center_ = c;
            scale_ = std::clamp(scale_ * std::min(width() / r.width(), height() / r.height()), 1e-9, 1e9);
            userMovedView();
        }
        update();
        return;
    }
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
    const double panStep = 80.0 / scale_;
    switch (e->key())
    {
    case Qt::Key_Escape:
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
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (tool_ == Tool::AddArea)
        {
            finishArea();
            return;
        }
        break;
    case Qt::Key_Left:
        center_.x -= panStep;
        break;
    case Qt::Key_Right:
        center_.x += panStep;
        break;
    case Qt::Key_Up:
        center_.y += panStep;
        break;
    case Qt::Key_Down:
        center_.y -= panStep;
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(1.4);
        return;
    case Qt::Key_Minus:
        zoomBy(1 / 1.4);
        return;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    userMovedView();
    update();
}

void MapView::leaveEvent(QEvent*)
{
    hover_ = {};
    QToolTip::hideText();
    update();
}

} // namespace mirab::app
