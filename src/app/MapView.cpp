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
#include "RasterLayers.h"
#include "Theme.h"
#include "mirab/core/Crs.h"
#include "mirab/core/Editing.h"
#include "mirab/core/Elevation.h"
#include "mirab/core/Geometry.h"
#include "mirab/core/Text.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>
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

// Blue below zero, grey around it, red above: the difference ramp.
const QColor kDiverging[] = {QColor(33, 102, 172), QColor(120, 175, 214), QColor(214, 218, 224),
                             QColor(240, 150, 110), QColor(190, 40, 45)};

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
    return core::pointInPolygon(poly, p);
}

double haversineMeters(double lon1, double lat1, double lon2, double lat2)
{
    const double r = 6371008.8;
    const double p1 = lat1 * kPi / 180, p2 = lat2 * kPi / 180;
    const double dp = p2 - p1, dl = (lon2 - lon1) * kPi / 180;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * r * std::asin(std::min(1.0, std::sqrt(a)));
}

/// @p f made @p points smaller, but not below @p minPoints (pixel-sized fonts too).
QFont smaller(QFont f, double points, double minPoints)
{
    if (f.pixelSize() > 0)
        f.setPixelSize(
            std::max(1, static_cast<int>(std::lround(
                            std::max(minPoints, f.pixelSize() * 0.75 - points) / 0.75))));
    else
        f.setPointSizeF(std::max(minPoints, f.pointSizeF() - points));
    return f;
}

double niceStep(double raw)
{
    const double e = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / e;
    return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * e;
}
} // namespace

MapView::MapView(ProjectDocument* doc, QWidget* parent)
    : QWidget(parent), doc_(doc), basemap_(new Basemap(this)), rasters_(new RasterRenderer(doc, this))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(basemap_, &Basemap::updated, this, qOverload<>(&QWidget::update));
    connect(rasters_, &RasterRenderer::updated, this, qOverload<>(&QWidget::update));
    connect(doc_, &ProjectDocument::modelReset, this, [this] {
        fitted_ = false;
        userView_ = false;
        cancelPending();
        recomputeRanges();
        updateCoordinateSystem();
        const auto& service = doc_->settings().mapService;
        if (service.valid() && !Basemap::find(QString::fromStdString(service.id)))
            Basemap::addService(Basemap::fromSettings(service), false);
        basemap_->setProvider(QString::fromStdString(doc_->settings().basemap));
        rasters_->reload();
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
    connect(doc_, &ProjectDocument::liveResultsUpdated, this, [this](int firstNew) {
        extendRanges(firstNew);
        update();
    });
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

void MapView::chooseBasemap(const QString& id)
{
    setBasemap(id);
    auto& settings = doc_->settings();
    settings.basemap = id.toStdString();
    if (const auto* p = Basemap::find(id); p && p->custom)
        Basemap::toSettings(*p, settings.mapService);
    doc_->markSettingsChanged();
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

void MapView::reloadRasters()
{
    rasters_->reload();
    update();
}

std::optional<double> MapView::groundElevationAt(const QPointF& world) const
{
    const auto terrain = projectTerrain(doc_);
    if (!terrain)
        return std::nullopt;
    return terrain->groundAt(world.x(), world.y());
}

QString MapView::basemapError() const
{
    return basemap_->active() ? basemap_->serviceError() : QString();
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
    case Tool::SplitLink:
        setCursor(Qt::PointingHandCursor);
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
    else if (tool == Tool::SelectPolygon)
        emit statusMessage(tr("Click the corners of the area to select, or drag around it; double-click or press "
                              "Enter to finish. Shift adds to the selection, Ctrl removes from it."));
    else if (tool == Tool::MeasureLength)
        emit statusMessage(tr("Click points along the line to measure; double-click or press Enter to finish. "
                              "Right-click or Esc to stop measuring."));
    else if (tool == Tool::MeasureArea)
        emit statusMessage(tr("Click the corners of the area to measure; double-click or press Enter to finish. "
                              "Right-click or Esc to stop measuring."));
    else if (tool == Tool::SplitLink)
        emit statusMessage(tr("Click a pipe or conduit where it should be split. Right-click or Esc to finish."));
    update();
}

void MapView::cancelPending()
{
    linkStart_.clear();
    pendingVertices_.clear();
    pendingPolygon_.clear();
    dragging_ = false;
    rubberBand_ = false;
    selectBand_ = false;
    lasso_.clear();
    lassoDragged_ = false;
    measure_.clear();
    measureDone_ = false;
    dragNode_.clear();
    update();
}

QPointF MapView::toScreen(const Point& p) const
{
    const QPointF c = viewRect().center();
    return QPointF(c.x() + (p.x - center_.x) * scale_, c.y() - (p.y - center_.y) * scale_);
}

Point MapView::toWorld(const QPointF& s) const
{
    const QPointF c = viewRect().center();
    return Point{center_.x + (s.x() - c.x()) / scale_, center_.y - (s.y() - c.y()) / scale_};
}

QPointF MapView::worldAt(const QPoint& screen) const
{
    const Point w = toWorld(screen);
    return {w.x, w.y};
}

void MapView::fitToWindow()
{
    core::Bounds b = doc_->model().bounds();
    if (!b.valid)
    {
        // An empty project: show the raster layers instead.
        if (const auto e = rasters_->extent(crsText_))
        {
            b.include({e->left(), e->top()});
            b.include({e->right(), e->bottom()});
        }
    }
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

void MapView::includeElement(core::Bounds& b, const Selection& sel) const
{
    for (const auto& p : core::elementGeometry(doc_->model(), sel.ref()))
        b.include(p);
}

void MapView::zoomToBounds(const core::Bounds& b)
{
    if (!b.valid)
        return;
    center_ = {(b.minX + b.maxX) / 2.0, (b.minY + b.maxY) / 2.0};
    const double margin = 60.0;
    if (b.width() > 0 || b.height() > 0)
    {
        const double sx = b.width() > 0 ? std::max(1.0, width() - 2 * margin) / b.width() : 1e9;
        const double sy = b.height() > 0 ? std::max(1.0, height() - 2 * margin) / b.height() : 1e9;
        const double needed = std::min(sx, sy);
        if (scale_ > needed)
            scale_ = needed;
    }
    userMovedView();
    update();
}

void MapView::zoomTo(const Selection& sel)
{
    core::Bounds b;
    includeElement(b, sel);
    if (!b.valid)
        return;
    // A single element: keep it in the middle half of the view.
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

void MapView::zoomToSelection()
{
    const auto& items = doc_->selectedElements();
    if (items.size() == 1)
    {
        zoomTo(items.first());
        return;
    }
    core::Bounds b;
    for (const auto& s : items)
        includeElement(b, s);
    zoomToBounds(b);
}

void MapView::selectInPolygon(const std::vector<core::Point>& polygon, bool crossing,
                              Qt::KeyboardModifiers modifiers)
{
    QList<Selection> found;
    for (const auto& e : core::elementsInPolygon(doc_->model(), polygon, crossing))
        found.append(Selection::of(e));
    if (modifiers & Qt::ShiftModifier)
        doc_->addToSelection(found);
    else if (modifiers & Qt::ControlModifier)
        doc_->removeFromSelection(found);
    else
        doc_->setSelection(found);
    emit statusMessage(tr("%n element(s) in the area", nullptr, static_cast<int>(found.size())));
}

void MapView::clickSelect(const Selection& hit, Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::ControlModifier)
        doc_->toggleSelected(hit);
    else if (modifiers & Qt::ShiftModifier)
        doc_->addToSelection({hit});
    else
        doc_->select(hit);
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

void MapView::setDifference(std::shared_ptr<const core::ResultSet> reference, const QString& label)
{
    reference_ = std::move(reference);
    referenceLabel_ = label;
    recomputeRanges();
    update();
}

const QColor* MapView::ramp() const
{
    return reference_ ? kDiverging : kRamp;
}

std::optional<double> MapView::referenceValue(const char* table, int variable, int element, int period) const
{
    const auto* r = doc_->results();
    const auto* t = r ? r->table(table) : nullptr;
    const auto* rt = reference_ ? reference_->table(table) : nullptr;
    if (!t || !rt || variable < 0 || variable >= t->variableCount() || period >= r->periodCount())
        return std::nullopt;
    const int rv = rt->variableIndex(t->variables[static_cast<std::size_t>(variable)].name);
    const int re = rt->elementIndex(t->ids[static_cast<std::size_t>(element)]);
    if (rv < 0 || re < 0 || reference_->times.empty())
        return std::nullopt;
    // The reference period at the same simulation time.
    const auto& times = reference_->times;
    const double time = r->times[static_cast<std::size_t>(period)];
    const auto it = std::lower_bound(times.begin(), times.end(), time - 0.5);
    if (it == times.end() || std::fabs(*it - time) > 0.5)
        return std::nullopt;
    const double v = rt->value(static_cast<int>(it - times.begin()), rv, re);
    if (std::isnan(v))
        return std::nullopt;
    return v;
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
    rangeTable_ = nullptr;
    rangeVar_ = -1;
    rangeLo_ = std::numeric_limits<double>::max();
    rangeHi_ = std::numeric_limits<double>::lowest();
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
    rangeTable_ = table;
    rangeVar_ = var;
    extendRanges(0);
}

void MapView::extendRanges(int fromPeriod)
{
    const auto* r = doc_->results();
    const auto* t = r && rangeTable_ ? r->table(rangeTable_) : nullptr;
    if (!t || rangeVar_ < 0 || rangeVar_ >= t->variableCount())
        return;
    const bool magnitude = std::strcmp(rangeTable_, "link") == 0;
    double lo = rangeLo_, hi = rangeHi_;
    for (int p = std::max(0, fromPeriod); p < r->periodCount(); ++p)
    {
        for (int e = 0; e < t->elementCount(); ++e)
        {
            double v = t->value(p, rangeVar_, e);
            if (std::isnan(v)) // not available during a run
                continue;
            if (magnitude)
                v = std::fabs(v);
            if (reference_)
            {
                const auto ref = referenceValue(rangeTable_, rangeVar_, e, p);
                if (!ref)
                    continue;
                // A range symmetric around zero, so that grey means "no change".
                v = std::fabs(v - (magnitude ? std::fabs(*ref) : *ref));
                lo = std::min(lo, -v);
            }
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    rangeLo_ = lo;
    rangeHi_ = hi;
    if (lo > hi)
        return;
    if (hi - lo < 1e-12)
        hi = lo + 1.0;
    rangeMin_ = lo;
    rangeMax_ = hi;
    if (reference_ && hi - lo > 1e-12)
    {
        const double m = std::max(std::fabs(lo), std::fabs(hi));
        rangeMin_ = -m;
        rangeMax_ = m;
    }
    const auto& v = t->variables[static_cast<std::size_t>(rangeVar_)];
    legendTitle_ = QString::fromStdString(v.name) + (v.unit.empty() ? QString() : " (" + QString::fromStdString(v.unit) + ")");
    if (reference_)
        legendTitle_ = tr("Change in %1 from %2").arg(legendTitle_, referenceLabel_);
}

double MapView::classEdge(int i) const
{
    // Differences: a narrow grey class around zero, so that small changes show.
    static const double diverging[] = {-1.0, -0.5, -0.05, 0.05, 0.5, 1.0};
    if (reference_)
        return rangeMax_ * diverging[i];
    return rangeMin_ + (rangeMax_ - rangeMin_) * i / 5.0;
}

QColor MapView::colorFor(double value) const
{
    int idx = 0;
    while (idx < 4 && value > classEdge(idx + 1))
        ++idx;
    return ramp()[idx];
}

std::optional<double> MapView::resultValue(const char* table, int variable, const std::string& id) const
{
    const auto v = rawValue(table, variable, id);
    if (!v || !reference_)
        return v;
    const auto* t = doc_->results()->table(table);
    const auto ref = referenceValue(table, variable, t->elementIndex(id), currentPeriod());
    if (!ref)
        return std::nullopt;
    if (std::strcmp(table, "link") == 0)
        return std::fabs(*v) - std::fabs(*ref); // links are colored by magnitude
    return *v - *ref;
}

std::optional<double> MapView::rawValue(const char* table, int variable, const std::string& id) const
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
    const double v = t->value(currentPeriod(), variable, e);
    if (std::isnan(v))
        return std::nullopt;
    return v;
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
    paintFont_ = font();
    RenderOptions o;
    o.overlays = true;
    o.highlights = true;
    o.screenOverlays = true;
    paintTo(p, QRectF(rect()), o);
}

void MapView::setOverlay(const QString& name, Overlay overlay)
{
    overlays_[name] = std::move(overlay);
    update();
}

void MapView::removeOverlay(const QString& name)
{
    if (overlays_.erase(name) > 0)
        update();
}

void MapView::drawOverlays(QPainter& p, const QRectF& area, const RenderOptions& o, bool aboveLinks)
{
    const QRectF visible = area.adjusted(-20, -20, 20, 20);
    for (const auto& [name, layer] : overlays_)
    {
        if (layer.aboveLinks != aboveLinks || !(layer.inExports || o.screenOverlays))
            continue;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(layer.lineColor, layer.lineWidth, layer.lineStyle, Qt::RoundCap, Qt::RoundJoin));
        for (const auto& line : layer.lines)
        {
            QPolygonF poly;
            for (const auto& pt : line)
                poly << toScreen(pt);
            if (poly.size() >= 2 && poly.boundingRect().adjusted(-1, -1, 1, 1).intersects(visible))
                p.drawPolyline(poly);
        }
        if (layer.points.empty())
            continue;
        p.setPen(Qt::NoPen);
        p.setBrush(layer.pointColor);
        const double r = layer.pointSize / 2;
        for (const auto& pt : layer.points)
        {
            const QPointF c = toScreen(pt);
            if (visible.contains(c))
                p.drawEllipse(c, r, r);
        }
    }
    p.setBrush(Qt::NoBrush);
}

// Fits the current view into a target rectangle for the lifetime of the scope.
struct MapView::ViewScope
{
    ViewScope(MapView* map, const QRectF& target, const RenderOptions& o)
        : m(map), scale(map->scale_), font(map->paintFont_), view(map->viewOverride_),
          period(map->periodOverride_), highlights(map->highlights_), detail(map->detail_),
          pixelRatio(map->pixelRatio_)
    {
        if (m->width() > 0 && m->height() > 0 && target.width() > 0 && target.height() > 0)
            m->scale_ =
                m->scale_ * std::min(target.width() / m->width(), target.height() / m->height());
        m->viewOverride_ = target;
        m->periodOverride_ = o.period;
        m->highlights_ = o.highlights;
        m->paintFont_ = o.font.family().isEmpty() ? m->font() : o.font;
        m->detail_ = o.tileDetail;
        m->pixelRatio_ = o.pixelRatio;
    }
    ~ViewScope()
    {
        m->scale_ = scale;
        m->paintFont_ = font;
        m->viewOverride_ = view;
        m->periodOverride_ = period;
        m->highlights_ = highlights;
        m->detail_ = detail;
        m->pixelRatio_ = pixelRatio;
    }
    ViewScope(const ViewScope&) = delete;
    ViewScope& operator=(const ViewScope&) = delete;

    MapView* m;
    double scale;
    QFont font;
    std::optional<QRectF> view;
    int period;
    bool highlights;
    double detail;
    double pixelRatio;
};

void MapView::render(QPainter& p, const QRectF& target, const RenderOptions& options)
{
    ViewScope scope(this, target, options);
    p.save();
    p.setClipRect(target, Qt::IntersectClip);
    if (options.rasterBackground && options.background)
    {
        const double ratio = std::clamp(options.pixelRatio, 0.5, 4.0);
        QImage image((target.size() * ratio).toSize(), QImage::Format_RGB32);
        if (!image.isNull())
        {
            image.setDevicePixelRatio(ratio);
            image.fill(theme::kMapBackground);
            {
                QPainter bp(&image);
                bp.translate(-target.topLeft());
                RenderOptions back = options;
                back.network = false;
                back.overlays = false;
                paintTo(bp, target, back);
            }
            p.drawImage(target, image);
            RenderOptions front = options;
            front.background = false;
            paintTo(p, target, front);
            p.restore();
            return;
        }
    }
    paintTo(p, target, options);
    p.restore();
}

bool MapView::prepareBasemap(const QRectF& target, const RenderOptions& options)
{
    if (!options.background || !options.basemap || !basemap_->active())
        return false;
    ViewScope scope(this, target, options);
    QImage scratch(4, 4, QImage::Format_RGB32);
    QPainter p(&scratch);
    basemap_->paint(
        p, target, [this](double x, double y) { return toScreen(x, y); },
        [this](const QPointF& s)
        {
            const Point w = toWorld(s);
            return QPointF(w.x, w.y);
        },
        detail_, detail_ > 1.0 ? 400 : 160);
    return basemap_->loading();
}

void MapView::waitForBasemap(const QRectF& target, const RenderOptions& options, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (prepareBasemap(target, options) && clock.elapsed() < timeoutMs)
    {
        QEventLoop loop;
        QTimer::singleShot(150, &loop, &QEventLoop::quit);
        connect(basemap_, &Basemap::updated, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
}

QVector<QColor> MapView::rampColors() const
{
    return QVector<QColor>(ramp(), ramp() + 5);
}

MapView::Legend MapView::legend() const
{
    Legend l;
    l.title = legendTitle_;
    if (l.title.isEmpty())
        return l;
    for (int i = 0; i < 5; ++i)
    {
        const double lo = classEdge(i);
        const double hi = classEdge(i + 1);
        l.classes.append({ramp()[i], QString::fromStdString(text::formatDouble(lo, 3)) + "  –  " +
                                        QString::fromStdString(text::formatDouble(hi, 3))});
    }
    return l;
}

QStringList MapView::elementKinds() const
{
    QStringList kinds;
    const auto& m = doc_->model();
    for (const auto& n : m.nodes())
        if (n.placed && !kinds.contains(QString::fromStdString(n.kind)))
            kinds << QString::fromStdString(n.kind);
    for (const auto& l : m.links())
        if (!kinds.contains(QString::fromStdString(l.kind)))
            kinds << QString::fromStdString(l.kind);
    if (!m.areas().empty())
        kinds << QStringLiteral("Subcatchment");
    if (!m.gages().empty())
        kinds << QStringLiteral("Rain gage");
    return kinds;
}

void MapView::drawSymbol(QPainter& p, const QPointF& c, double size, const QString& kind)
{
    // The shapes and default colors of paintTo(), scaled to @p size.
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double r = size * 0.32;
    const QColor ink(78, 92, 112), nodeInk(44, 58, 78);
    p.setPen(QPen(nodeInk, 1.2));
    p.setBrush(kind == "Junction"  ? QColor(255, 255, 255)
               : kind == "Outfall" ? QColor(120, 120, 200)
                                   : QColor(66, 133, 230));
    if (kind == "Junction")
        p.drawEllipse(c, r, r);
    else if (kind == "Reservoir")
        p.drawPolygon(QPolygonF({c + QPointF(-r * 1.5, -r), c + QPointF(r * 1.5, -r),
                                 c + QPointF(r, r * 1.1), c + QPointF(-r, r * 1.1)}));
    else if (kind == "Tank" || kind == "Storage")
    {
        p.drawRoundedRect(QRectF(c.x() - r * 1.1, c.y() - r * 1.4, r * 2.2, r * 2.8), 1.2, 1.2);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(c.x() - r * 1.1, c.y() - r * 1.4, r * 2.2, r * 0.9));
    }
    else if (kind == "Outfall")
        p.drawPolygon(QPolygonF(
            {c + QPointF(-r * 1.3, -r), c + QPointF(r * 1.3, -r), c + QPointF(0, r * 1.3)}));
    else if (kind == "Divider")
        p.drawPolygon(QPolygonF({c + QPointF(0, -r * 1.3), c + QPointF(r * 1.3, 0),
                                 c + QPointF(0, r * 1.3), c + QPointF(-r * 1.3, 0)}));
    else if (kind == "Subcatchment")
    {
        p.setPen(QPen(QColor(60, 140, 70), 1.2));
        p.setBrush(QColor(150, 205, 150, 110));
        p.drawRect(QRectF(c.x() - size * 0.45, c.y() - size * 0.3, size * 0.9, size * 0.6));
    }
    else if (kind == "Rain gage")
    {
        p.setPen(QPen(QColor(40, 110, 200), 1.2));
        p.setBrush(QColor(190, 215, 248));
        p.drawEllipse(c, r * 1.3, r);
    }
    else
    {
        // Links: a line, with the marker of pumps and valves.
        p.setPen(QPen(ink, std::max(1.5, size * 0.12), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c - QPointF(size * 0.5, 0), c + QPointF(size * 0.5, 0));
        p.setPen(QPen(ink.darker(110), 1.2));
        p.setBrush(Qt::white);
        if (kind == "Pump")
        {
            p.drawEllipse(c, r * 1.2, r * 1.2);
            p.setBrush(ink.darker(110));
            p.drawPolygon(QPolygonF({c + QPointF(r * 0.8, 0), c + QPointF(-r * 0.5, r * 0.6),
                                     c + QPointF(-r * 0.5, -r * 0.6)}));
        }
        else if (kind == "Valve" || kind == "Orifice" || kind == "Weir" || kind == "Outlet")
        {
            p.drawPolygon(
                QPolygonF({c + QPointF(-r * 1.3, -r * 0.8), c + QPointF(r * 1.3, r * 0.8),
                           c + QPointF(r * 1.3, -r * 0.8), c + QPointF(-r * 1.3, r * 0.8)}));
        }
    }
    p.restore();
}

double MapView::groundPerUnit(const QRectF& target, QString* unit)
{
    ViewScope scope(this, target, RenderOptions());
    return groundPerPixel(QPointF(target.center().x() - 50, target.center().y()), unit);
}

double MapView::northAngle() const
{
    double lon = 0, lat = 0, x = 0, y = 0;
    if (!basemap_->toLonLat(center_.x, center_.y, lon, lat) ||
        !basemap_->fromLonLat(lon, std::min(89.0, lat + 0.01), x, y))
        return 0.0;
    const double dx = x - center_.x, dy = y - center_.y;
    if (!(std::hypot(dx, dy) > 0) || !std::isfinite(dx) || !std::isfinite(dy))
        return 0.0;
    return std::atan2(dx, dy) * 180.0 / kPi;
}

QString MapView::attribution() const
{
    return basemap_->active() ? basemap_->attribution() : QString();
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

void MapView::paintTo(QPainter& p, const QRectF& area, const RenderOptions& o)
{
    p.setRenderHint(QPainter::Antialiasing);
    const auto& m = doc_->model();
    const Selection hover = highlights_ ? hover_ : Selection{};
    const bool drag = highlights_ && dragging_ && !dragNode_.isEmpty();
    const QColor ink(78, 92, 112);
    const QColor nodeInk(44, 58, 78);
    const QColor selColor = theme::kSelection;
    const QColor hoverColor(31, 111, 235, 90);

    const bool base = basemap_->active() && o.basemap;
    if (o.background)
        p.fillRect(area, theme::kMapBackground);
    if (o.background && base)
    {
        basemap_->paint(
            p, area, [this](double x, double y) { return toScreen(x, y); },
            [this](const QPointF& s)
            {
                const Point w = toWorld(s);
                return QPointF(w.x, w.y);
            },
            detail_, detail_ > 1.0 ? 400 : 160);
        // A light veil keeps the network readable over busy imagery.
        p.fillRect(area, QColor(255, 255, 255, basemap_->provider() == "esri-imagery" ? 30 : 70));
    }
    else if (o.background && highlights_)
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

    if (o.background && highlights_)
        rasters_->paint(p, area.toRect(), {center_.x, center_.y, scale_, area.toRect().size()},
                        crsText_);
    else if (o.background)
        rasters_->paintExact(p, area, {center_.x, center_.y, scale_, area.toRect().size()},
                             crsText_, pixelRatio_);

    if (o.background && !backdrop_.isNull())
    {
        const QPointF tl = toScreen(backdropWorld_.left(), backdropWorld_.bottom());
        const QPointF br = toScreen(backdropWorld_.right(), backdropWorld_.top());
        p.setOpacity(0.85);
        p.drawImage(QRectF(tl, br).normalized(), backdrop_);
        p.setOpacity(1.0);
    }

    drawOverlays(p, area, o, false);

    if (!o.network)
    {
        if (o.overlays)
            drawScaleBar(p, area);
        return;
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
        const bool selected = highlights_ && doc_->isSelected(Category::Area, a.id);
        const bool hovered = hover.category == Category::Area && hover.id.toStdString() == a.id;
        if (selected)
            fill = QColor(255, 196, 120, 150);
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
        if (drag && a->id == dragNode_.toStdString())
            line.first() = toScreen(dragTo_);
        for (const auto& v : l.vertices)
            line << toScreen(v);
        line << toScreen(b->pos);
        if (drag && b->id == dragNode_.toStdString())
            line.last() = toScreen(dragTo_);
        if (!line.boundingRect().adjusted(-10, -10, 10, 10).intersects(area))
            continue;
        QColor c = ink;
        if (auto v = resultValue("link", linkVarIndex_, l.id))
            c = colorFor(reference_ ? *v : std::fabs(*v));
        const bool selected = highlights_ && doc_->isSelected(Category::Link, l.id);
        const bool hovered = hover.category == Category::Link && hover.id.toStdString() == l.id;
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
            if (auto f = rawValue("link", flowVar, l.id); f && std::fabs(*f) > 1e-9)
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
    if (highlights_ && tool_ == Tool::AddLink && !linkStart_.isEmpty())
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
    if (highlights_ && tool_ == Tool::AddArea && !pendingPolygon_.empty())
    {
        QPolygonF poly;
        for (const auto& v : pendingPolygon_)
            poly << toScreen(v);
        poly << hoverScreen_;
        p.setPen(QPen(QColor(60, 140, 70), 1.5, Qt::DashLine));
        p.setBrush(QColor(150, 205, 150, 60));
        p.drawPolygon(poly);
    }
    if (highlights_ && rubberBand_)
    {
        p.setPen(QPen(theme::kAccent, 1.2, Qt::DashLine));
        p.setBrush(QColor(31, 111, 235, 30));
        p.drawRect(QRectF(pressPos_, hoverScreen_).normalized());
    }
    if (highlights_ && selectBand_ &&
        (std::fabs(hoverScreen_.x() - pressPos_.x()) >= 4 || std::fabs(hoverScreen_.y() - pressPos_.y()) >= 4))
    {
        // Left to right selects what lies inside, right to left what it touches.
        const bool crossing = hoverScreen_.x() < pressPos_.x();
        p.setPen(QPen(selColor, 1.3, crossing ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(crossing ? QColor(46, 178, 96, 30) : QColor(255, 140, 0, 30));
        p.drawRect(QRectF(pressPos_, hoverScreen_).normalized());
    }
    if (highlights_ && tool_ == Tool::SelectPolygon && !lasso_.empty())
    {
        QPolygonF poly;
        for (const auto& v : lasso_)
            poly << toScreen(v);
        if (!lassoDragged_)
            poly << hoverScreen_;
        p.setPen(QPen(selColor, 1.4, Qt::DashLine));
        p.setBrush(QColor(255, 140, 0, 30));
        p.drawPolygon(poly);
    }
    if (highlights_ && (tool_ == Tool::MeasureLength || tool_ == Tool::MeasureArea) && !measure_.empty())
    {
        QPolygonF line;
        for (const auto& v : measure_)
            line << toScreen(v);
        if (!measureDone_)
            line << hoverScreen_;
        const QColor mc(200, 40, 120);
        p.setPen(QPen(mc, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (tool_ == Tool::MeasureArea)
        {
            p.setBrush(QColor(200, 40, 120, 35));
            p.drawPolygon(line);
        }
        else
        {
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(line);
        }
        p.setBrush(Qt::white);
        p.setPen(QPen(mc, 1.6));
        for (const auto& v : measure_)
            p.drawEllipse(toScreen(v), 3.5, 3.5);
        drawOverlayText(p, (measureDone_ ? line.last() : hoverScreen_) + QPointF(14, 18), measureText());
    }
    if (highlights_ && tool_ == Tool::SplitLink)
    {
        QString link;
        Point at;
        if (splitTarget(hoverScreen_, link, at))
        {
            const QPointF c = toScreen(at);
            p.setPen(QPen(QColor(200, 40, 120), 2.0));
            p.setBrush(Qt::white);
            p.drawEllipse(c, 6, 6);
            p.drawLine(c + QPointF(-10, 10), c + QPointF(10, -10));
        }
    }

    drawOverlays(p, area, o, true);

    // Nodes
    const double r = std::clamp(scale_ * 2.4, 3.2, 6.5);
    const bool labels = showLabels_ && (m.nodes().size() < 400 || scale_ > 0.5);
    p.setFont(smaller(paintFont_, 1.0, 7.5));
    for (const auto& n : m.nodes())
    {
        if (!n.placed)
            continue;
        QPointF c = toScreen(n.pos);
        if (drag && n.id == dragNode_.toStdString())
            c = toScreen(dragTo_);
        if (!area.adjusted(-20, -20, 20, 20).contains(c))
            continue;
        QColor fill = n.kind == "Junction" ? QColor(255, 255, 255) : QColor(66, 133, 230);
        if (n.kind == "Outfall")
            fill = QColor(120, 120, 200);
        if (auto v = resultValue("node", nodeVarIndex_, n.id))
            fill = colorFor(*v);
        const bool selected = highlights_ && doc_->isSelected(Category::Node, n.id);
        const bool hovered = hover.category == Category::Node && hover.id.toStdString() == n.id;
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
        const bool selected = highlights_ && doc_->isSelected(Category::Gage, g.id);
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

    if (!o.overlays)
        return;
    if (showLegend_)
        drawLegend(p, area);
    drawScaleBar(p, area);
    if (base)
        drawAttribution(p, area);
}

void MapView::drawOverlayText(QPainter& p, const QPointF& at, const QString& text)
{
    if (text.isEmpty())
        return;
    QFont f = font();
    p.setFont(f);
    const QFontMetrics fm(f);
    const QStringList lines = text.split('\n');
    int w = 0;
    for (const auto& l : lines)
        w = std::max(w, fm.horizontalAdvance(l));
    QRectF box(at, QSizeF(w + 16, lines.size() * fm.height() + 10));
    // Keep the box on the map.
    if (box.right() > width() - 4)
        box.moveRight(at.x() - 28);
    if (box.bottom() > height() - 4)
        box.moveBottom(at.y() - 28);
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawRoundedRect(box, 6, 6);
    p.setPen(theme::kText);
    for (int i = 0; i < lines.size(); ++i)
        p.drawText(QPointF(box.left() + 8, box.top() + 5 + fm.ascent() + i * fm.height()), lines[i]);
}

QString MapView::formatLength(double value, bool metres) const
{
    const bool us = doc_->model().units().system == core::UnitSystem::US;
    if (!metres)
        return QString::fromStdString(text::formatDouble(value, 6)) + " " + (us ? "ft" : "m");
    if (us)
    {
        const double ft = value / 0.3048;
        QString t = QString::number(ft, 'f', 1) + " ft";
        if (ft >= 5280)
            t += QString("  (%1 mi)").arg(ft / 5280.0, 0, 'f', 3);
        return t;
    }
    QString t = QString::number(value, 'f', 2) + " m";
    if (value >= 1000)
        t += QString("  (%1 km)").arg(value / 1000.0, 0, 'f', 3);
    return t;
}

QString MapView::formatArea(double value, bool metres) const
{
    const bool us = doc_->model().units().system == core::UnitSystem::US;
    const QString sq = QString(QChar(0x00B2));
    if (!metres)
        return QString::fromStdString(text::formatDouble(value, 6)) + " " + (us ? "ft" : "m") + sq;
    if (us)
    {
        const double ft2 = value / (0.3048 * 0.3048);
        return QString::number(ft2, 'f', 0) + " ft" + sq + QString("  (%1 ac)").arg(ft2 / 43560.0, 0, 'f', 3);
    }
    return QString::number(value, 'f', 1) + " m" + sq + QString("  (%1 ha)").arg(value / 10000.0, 0, 'f', 4);
}

QString MapView::measureText() const
{
    if (measure_.empty())
        return {};
    std::vector<Point> pts = measure_;
    if (!measureDone_)
        pts.push_back(toWorld(hoverScreen_));
    const auto crs = crs::parse(doc_->settings().crs);
    const crs::Crs* system = crs && crs->valid() ? &*crs : nullptr;
    QString t;
    if (tool_ == Tool::MeasureArea)
    {
        const auto m = core::measure(pts, true, system);
        t = tr("Area %1").arg(formatArea(m.area, m.metres)) + "\n" +
            tr("Perimeter %1").arg(formatLength(m.length, m.metres));
    }
    else
    {
        const auto m = core::measure(pts, false, system);
        t = tr("Length %1").arg(formatLength(m.length, m.metres));
        if (pts.size() > 2)
        {
            const auto last = core::measure({pts[pts.size() - 2], pts.back()}, false, system);
            t += "\n" + tr("Last segment %1").arg(formatLength(last.length, last.metres));
        }
    }
    if (!system)
        t += "\n" + tr("Map units: choose a coordinate system for true distances.");
    return t;
}

bool MapView::splitTarget(const QPointF& screen, QString& link, Point& at) const
{
    const Selection hit = hitTest(screen);
    if (hit.category != Category::Link)
        return false;
    const auto& m = doc_->model();
    const auto* l = m.findLink(hit.id.toStdString());
    if (!l || (l->kind != "Pipe" && l->kind != "Conduit"))
        return false;
    const auto line = core::linkPolyline(m, *l);
    if (line.size() < 2)
        return false;
    link = hit.id;
    at = core::locateOnPolyline(line, toWorld(screen)).point;
    return true;
}

void MapView::splitAt(const QPointF& screen)
{
    QString link;
    Point at;
    if (!splitTarget(screen, link, at))
    {
        const Selection hit = hitTest(screen);
        emit statusMessage(hit.category == Category::Link ? tr("Only pipes and conduits can be split.")
                                                          : tr("Click on a pipe or conduit to split it."));
        return;
    }
    QString err;
    core::SplitResult result;
    const std::string id = link.toStdString();
    if (doc_->edit(tr("Split %1").arg(link),
                   [&](core::Model& mm, std::string* e) { return core::splitLink(mm, id, at, &result, e); }, &err))
    {
        const QString node = QString::fromStdString(result.node);
        doc_->setSelection({{Category::Link, link}, {Category::Node, node},
                            {Category::Link, QString::fromStdString(result.link)}},
                           {Category::Node, node});
        emit statusMessage(
            tr("%1 was split at the new node %2; the second part is %3.").arg(link, node, QString::fromStdString(result.link)));
    }
    else
    {
        emit statusMessage(tr("%1 was not split: %2").arg(link, err));
    }
}

void MapView::drawLegend(QPainter& p, const QRectF& area)
{
    if (legendTitle_.isEmpty())
        return;
    QFont f = paintFont_;
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
        const double lo = classEdge(i);
        const double hi = classEdge(i + 1);
        rows << QString::fromStdString(text::formatDouble(lo, 3)) + "  –  " + QString::fromStdString(text::formatDouble(hi, 3));
        textW = std::max(textW, fm.horizontalAdvance(rows.last()) + 30);
    }
    const int w = textW + 28;
    const int h = fmb.height() + 5 * rowH + 22;
    const QRectF box(area.left() + 12, area.top() + 12, w, h);
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(QColor(255, 255, 255, 238));
    p.drawRoundedRect(box, 8, 8);
    p.setFont(bold);
    p.setPen(theme::kText);
    p.drawText(box.adjusted(12, 9, -10, 0), Qt::AlignLeft | Qt::AlignTop, legendTitle_);
    p.setFont(f);
    for (int i = 0; i < 5; ++i)
    {
        const double y = box.top() + 14 + fmb.height() + (4 - i) * rowH;
        p.setPen(Qt::NoPen);
        p.setBrush(ramp()[i]);
        p.drawRoundedRect(QRectF(box.left() + 12, y + 3, 18, rowH - 7), 3, 3);
        p.setPen(theme::kText);
        p.drawText(QPointF(box.left() + 38, y + fm.ascent() + 1), rows[i]);
    }
}

double MapView::groundPerPixel(const QPointF& a, QString* unit) const
{
    // Ground distance of 100 screen pixels, in meters when the CRS is known.
    const QPointF b = a + QPointF(100, 0);
    const Point wa = toWorld(a), wb = toWorld(b);
    double perPixel = 0;
    double lon1, lat1, lon2, lat2;
    const bool us = doc_->model().units().system == core::UnitSystem::US;
    if (basemap_->usable() && basemap_->toLonLat(wa.x, wa.y, lon1, lat1) && basemap_->toLonLat(wb.x, wb.y, lon2, lat2))
    {
        perPixel = haversineMeters(lon1, lat1, lon2, lat2) / 100.0;
        if (us)
            perPixel /= 0.3048; // feet and miles for US customary models
    }
    else
    {
        perPixel = std::hypot(wb.x - wa.x, wb.y - wa.y) / 100.0;
    }
    if (unit)
        *unit = us ? "ft" : "m";
    return perPixel > 0 && std::isfinite(perPixel) ? perPixel : 0.0;
}

void MapView::drawScaleBar(QPainter& p, const QRectF& area)
{
    const QPointF a(area.left() + 40, area.bottom() - 30);
    QString unit;
    const double perPixel = groundPerPixel(a, &unit);
    if (!(perPixel > 0))
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
    const QFontMetrics fm(paintFont_);
    const QRectF box(a.x() - 10, a.y() - fm.height() - 12, px + 20, fm.height() + 22);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawRoundedRect(box, 6, 6);
    p.setPen(QPen(theme::kText, 1.6));
    p.drawLine(a, a + QPointF(px, 0));
    p.drawLine(a + QPointF(0, -5), a + QPointF(0, 2));
    p.drawLine(a + QPointF(px, -5), a + QPointF(px, 2));
    p.setFont(paintFont_);
    p.drawText(QRectF(a.x(), a.y() - fm.height() - 6, px, fm.height()), Qt::AlignCenter, label);
}

void MapView::drawAttribution(QPainter& p, const QRectF& area)
{
    QString text = basemap_->attribution();
    const QString error = basemapError();
    if (!error.isEmpty())
        text = tr("Map service: %1").arg(error) + (text.isEmpty() ? QString() : "   " + text);
    if (text.isEmpty())
        return;
    const QFont f = smaller(paintFont_, 1.5, 7.0);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 12;
    const QRectF box(area.right() - w - 4, area.bottom() - fm.height() - 6, w, fm.height() + 4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawRect(box);
    p.setPen(error.isEmpty() ? QColor(60, 70, 85) : QColor(170, 40, 30));
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
            // Right-clicking a selected element keeps the selection for the menu.
            if (doc_->isSelected(hit))
                doc_->setCurrent(hit);
            else
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
        pressModifiers_ = e->modifiers();
        if (hit.isEmpty() || hit.category == Category::Area)
        {
            // Dragging over an empty part of the map (or inside a subcatchment)
            // selects with a rectangle; a click selects what is under it.
            selectBand_ = true;
            bandClick_ = hit;
            hoverScreen_ = e->position();
            break;
        }
        clickSelect(hit, e->modifiers());
        if ((hit.category == Category::Node || hit.category == Category::Gage) &&
            !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
        {
            dragNode_ = hit.id;
            dragging_ = false;
        }
        break;
    }
    case Tool::SelectPolygon:
        pressModifiers_ = e->modifiers();
        lasso_.push_back(w);
        lassoDragged_ = false;
        update();
        break;
    case Tool::MeasureLength:
    case Tool::MeasureArea:
        if (measureDone_)
        {
            measure_.clear();
            measureDone_ = false;
        }
        measure_.push_back(w);
        update();
        break;
    case Tool::SplitLink:
        splitAt(e->position());
        break;
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
    if (rubberBand_ || selectBand_)
    {
        update();
        return;
    }
    if (tool_ == Tool::SelectPolygon && (e->buttons() & Qt::LeftButton) && !lasso_.empty())
    {
        // Freehand lasso while the button is held.
        const QPointF last = toScreen(lasso_.back());
        if (std::hypot(last.x() - e->position().x(), last.y() - e->position().y()) > 4.0)
        {
            lasso_.push_back(toWorld(e->position()));
            lassoDragged_ = true;
        }
        update();
        return;
    }
    if (tool_ == Tool::SelectPolygon || tool_ == Tool::MeasureLength || tool_ == Tool::MeasureArea)
    {
        update();
        return;
    }
    if (tool_ == Tool::SplitLink)
        update();
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
                        {
                            const double x = t->value(doc_->period(), v, el);
                            // NaN: not available while the simulation runs.
                            const QString shown =
                                std::isnan(x) ? tr("n/a") : QString::fromStdString(text::formatDouble(x, 5));
                            tip += "<tr><td>" + QString::fromStdString(t->variables[v].name) + "&nbsp;&nbsp;</td><td align='right'>" +
                                   shown +
                                   "</td><td>&nbsp;" + QString::fromStdString(t->variables[v].unit).toHtmlEscaped() +
                                   "</td></tr>";
                        }
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
    if (selectBand_)
    {
        selectBand_ = false;
        const QRectF r = QRectF(pressPos_, e->position()).normalized();
        if (r.width() < 4 && r.height() < 4)
        {
            // A click on an empty spot clears the selection (unless adding to it).
            if (!bandClick_.isEmpty())
                clickSelect(bandClick_, pressModifiers_);
            else if (!(pressModifiers_ & (Qt::ShiftModifier | Qt::ControlModifier)))
                doc_->select({});
        }
        else
        {
            const Point a = toWorld(r.topLeft()), b = toWorld(r.bottomRight());
            selectInPolygon({{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}}, e->position().x() < pressPos_.x(),
                            pressModifiers_);
        }
        update();
        return;
    }
    if (tool_ == Tool::SelectPolygon && lassoDragged_)
    {
        finishLasso();
        return;
    }
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
    if (tool_ == Tool::SelectPolygon)
    {
        finishLasso();
        return;
    }
    if (tool_ == Tool::MeasureLength || tool_ == Tool::MeasureArea)
    {
        // The double click added a duplicate final point.
        if (measure_.size() >= 2)
        {
            const auto& a = measure_[measure_.size() - 1];
            const auto& b = measure_[measure_.size() - 2];
            if (std::hypot(a.x - b.x, a.y - b.y) * scale_ < 4.0)
                measure_.pop_back();
        }
        measureDone_ = true;
        emit statusMessage(measureText().replace('\n', QStringLiteral("   ")));
        update();
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

void MapView::finishLasso()
{
    if (lasso_.size() >= 2)
    {
        const auto& a = lasso_[lasso_.size() - 1];
        const auto& b = lasso_[lasso_.size() - 2];
        if (std::hypot(a.x - b.x, a.y - b.y) * scale_ < 4.0)
            lasso_.pop_back();
    }
    if (lasso_.size() >= 3)
        selectInPolygon(lasso_, false, pressModifiers_);
    else
        emit statusMessage(tr("An area needs at least three corners."));
    lasso_.clear();
    lassoDragged_ = false;
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
        if (tool_ == Tool::SelectPolygon)
        {
            finishLasso();
            return;
        }
        if ((tool_ == Tool::MeasureLength || tool_ == Tool::MeasureArea) && !measure_.empty())
        {
            measureDone_ = true;
            emit statusMessage(measureText().replace('\n', QStringLiteral("   ")));
            update();
            return;
        }
        break;
    case Qt::Key_Backspace:
        if (tool_ == Tool::SelectPolygon && !lasso_.empty())
            lasso_.pop_back();
        else if ((tool_ == Tool::MeasureLength || tool_ == Tool::MeasureArea) && !measure_.empty() && !measureDone_)
            measure_.pop_back();
        update();
        return;
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
