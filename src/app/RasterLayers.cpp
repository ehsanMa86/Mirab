/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/RasterLayers.cpp
 * Purpose: Drawing of raster layers: reprojection to screen pixels, hillshade and colour ramps.
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
#include "RasterLayers.h"

#include "mirab/core/Crs.h"
#include "mirab/core/Elevation.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>

namespace mirab::app
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kGrid = 16; ///< screen pixels between exactly reprojected points
constexpr int kMaxBuffer = 4096;

struct CachedRaster
{
    std::shared_ptr<raster::Raster> raster;
    QDateTime modified;
};

QHash<QString, CachedRaster>& rasterCache()
{
    static QHash<QString, CachedRaster> cache;
    return cache;
}

bool sameSystem(const crs::Crs& a, const crs::Crs& b)
{
    return (a.epsg != 0 && a.epsg == b.epsg) || crs::toString(a) == crs::toString(b);
}

/// Metres per DEM value unit, as the terrain sampler decides it.
double demUnit(const raster::Raster& r, const core::RasterLayerSettings& s)
{
    if (s.verticalUnit > 0)
        return s.verticalUnit;
    const auto& info = r.info();
    if (info.verticalUnitToMeters > 0)
        return info.verticalUnitToMeters;
    if (info.crs && !info.crs->isGeographic())
        return info.crs->unitToMeters;
    return 1.0;
}

/// Hypsometric colours from low ground to high ground.
QRgb ramp(double t)
{
    static const double stops[][4] = {{0.00, 76, 150, 98},   {0.25, 150, 196, 120},
                                      {0.50, 234, 222, 150}, {0.75, 196, 150, 98},
                                      {0.90, 168, 128, 100}, {1.00, 226, 214, 200}};
    t = std::clamp(t, 0.0, 1.0);
    for (int i = 1; i < 6; ++i)
    {
        if (t <= stops[i][0])
        {
            const double f = (t - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0]);
            auto mix = [&](int c)
            { return static_cast<int>(stops[i - 1][c] + f * (stops[i][c] - stops[i - 1][c])); };
            return qRgb(mix(1), mix(2), mix(3));
        }
    }
    return qRgb(226, 214, 200);
}
} // namespace

std::shared_ptr<raster::Raster> openRaster(const QString& path, QString* error)
{
    const QFileInfo fi(path);
    const QString key = fi.absoluteFilePath();
    auto& cache = rasterCache();
    auto it = cache.find(key);
    if (it != cache.end() && it->modified == fi.lastModified() && it->raster)
        return it->raster;
    std::string err;
    std::shared_ptr<raster::Raster> r(raster::Raster::open(key.toStdString(), &err).release());
    if (!r)
    {
        if (error)
            *error = QString::fromStdString(err);
        cache.remove(key);
        return nullptr;
    }
    cache.insert(key, {r, fi.lastModified()});
    return r;
}

QString resolveRasterPath(const ProjectDocument* doc, const std::string& stored)
{
    const QString p = QString::fromStdString(stored);
    if (QFileInfo(p).isAbsolute() || doc->path().isEmpty())
        return p;
    return QFileInfo(doc->path()).dir().filePath(p);
}

std::string storedRasterPath(const ProjectDocument* doc, const QString& absolute)
{
    if (doc->path().isEmpty())
        return absolute.toStdString();
    return QFileInfo(doc->path()).dir().relativeFilePath(absolute).toStdString();
}

bool isElevationLayer(const core::RasterLayerSettings& layer)
{
    return layer.style != "image";
}

QString rasterFileFilter()
{
    return QObject::tr("Rasters (*.tif *.tiff *.asc);;GeoTIFF (*.tif *.tiff);;ESRI ASCII grid "
                       "(*.asc);;All files (*)");
}

std::shared_ptr<core::TerrainSampler> projectTerrain(const ProjectDocument* doc)
{
    const core::RasterLayerSettings* chosen = nullptr;
    for (const auto& l : doc->settings().rasters)
    {
        if (isElevationLayer(l) && (!chosen || (l.visible && !chosen->visible)))
            chosen = &l;
    }
    if (!chosen)
        return nullptr;
    const QString path = resolveRasterPath(doc, chosen->path);
    auto r = openRaster(path);
    if (!r || r->isImagery())
        return nullptr;
    const auto units = doc->model().units();
    static std::shared_ptr<core::TerrainSampler> last;
    static QString lastKey;
    static std::shared_ptr<raster::Raster> lastRaster;
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(path, QString::fromStdString(doc->settings().crs),
                                 QString::fromStdString(units.flow))
                            .arg(units.system == core::UnitSystem::SI ? 1 : 0)
                            .arg(chosen->verticalUnit, 0, 'g', 17);
    if (!last || key != lastKey || r != lastRaster)
    {
        last = std::make_shared<core::TerrainSampler>(r, doc->settings().crs, units,
                                                      chosen->verticalUnit);
        lastKey = key;
        lastRaster = r;
    }
    return last;
}

QVector<QPointF> terrainProfile(const ProjectDocument* doc, const std::vector<std::string>& nodes,
                                const std::vector<double>& distances)
{
    QVector<QPointF> out;
    const auto terrain = projectTerrain(doc);
    if (!terrain || nodes.size() < 2 || distances.size() != nodes.size())
        return out;
    const auto& m = doc->model();
    constexpr int kSteps = 24;
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i)
    {
        const auto* a = m.findNode(nodes[i]);
        const auto* b = m.findNode(nodes[i + 1]);
        if (!a || !b || !a->placed || !b->placed)
            continue;
        std::vector<core::Point> line{a->pos};
        for (const auto& l : m.links())
        {
            if (l.from == a->id && l.to == b->id)
            {
                line.insert(line.end(), l.vertices.begin(), l.vertices.end());
                break;
            }
            if (l.from == b->id && l.to == a->id)
            {
                line.insert(line.end(), l.vertices.rbegin(), l.vertices.rend());
                break;
            }
        }
        line.push_back(b->pos);
        std::vector<double> along{0.0};
        for (std::size_t k = 1; k < line.size(); ++k)
            along.push_back(along.back() +
                            std::hypot(line[k].x - line[k - 1].x, line[k].y - line[k - 1].y));
        const double total = along.back();
        for (int s = i == 0 ? 0 : 1; s <= kSteps; ++s)
        {
            const double t = static_cast<double>(s) / kSteps;
            const double at = t * total;
            std::size_t k = 1;
            while (k + 1 < along.size() && along[k] < at)
                ++k;
            const double seg = along[k] - along[k - 1];
            const double f = seg > 0 ? (at - along[k - 1]) / seg : 0.0;
            const double x = line[k - 1].x + f * (line[k].x - line[k - 1].x);
            const double y = line[k - 1].y + f * (line[k].y - line[k - 1].y);
            if (const auto z = terrain->groundAt(x, y))
                out.append(QPointF(distances[i] + t * (distances[i + 1] - distances[i]), *z));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// RasterRenderer
// ---------------------------------------------------------------------------
RasterRenderer::RasterRenderer(ProjectDocument* doc, QObject* parent)
    : QObject(parent), doc_(doc), timer_(new QTimer(this))
{
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this,
            [this]
            {
                due_ = true;
                emit updated();
            });
}

void RasterRenderer::reload()
{
    layers_.clear();
    problems_.clear();
    for (const auto& s : doc_->settings().rasters)
    {
        Layer l;
        l.settings = s;
        QString err;
        l.raster = openRaster(resolveRasterPath(doc_, s.path), &err);
        if (!l.raster)
        {
            problems_ << err;
            continue;
        }
        if (isElevationLayer(s) && !l.raster->isImagery())
        {
            if (!l.raster->range(0, l.low, l.high))
                l.low = l.high = 0;
            if (l.high <= l.low)
                l.high = l.low + 1;
        }
        layers_.push_back(l);
    }
    ++revision_;
    image_ = QImage();
    emit updated();
}

bool RasterRenderer::hasVisibleLayers() const
{
    for (const auto& l : layers_)
    {
        if (l.settings.visible && l.settings.opacity > 0)
            return true;
    }
    return false;
}

std::optional<QRectF> RasterRenderer::extent(const QString& modelCrs) const
{
    const auto model =
        modelCrs.trimmed().isEmpty() ? std::nullopt : crs::parse(modelCrs.trimmed().toStdString());
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const auto& l : layers_)
    {
        if (!l.settings.visible)
            continue;
        const auto& info = l.raster->info();
        const bool reproject = model && info.crs && !sameSystem(*model, *info.crs);
        for (int i = 0; i <= 4; ++i)
        {
            for (int j = 0; j <= 4; ++j)
            {
                double x = 0, y = 0;
                info.transform.pixelToMap(info.width * i / 4.0, info.height * j / 4.0, x, y);
                if (reproject && !crs::transform(*info.crs, *model, x, y, x, y))
                    continue;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < minX || maxY < minY)
        return std::nullopt;
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

void RasterRenderer::paint(QPainter& p, const QRect& area, const View& view,
                           const QString& modelCrs)
{
    if (!hasVisibleLayers() || area.isEmpty())
        return;
    const QString key = QStringLiteral("%1|%2").arg(revision_).arg(modelCrs);
    const bool exact = !image_.isNull() && key == imageKey_ && view == imageView_;
    if (!exact)
    {
        if (image_.isNull() || key != imageKey_ || due_)
        {
            image_ = render(view, modelCrs);
            imageView_ = view;
            imageKey_ = key;
            due_ = false;
        }
        else
        {
            // Stretch the last picture until the view settles.
            timer_->start(140);
            const double f = view.scale / imageView_.scale;
            const double worldLeft =
                imageView_.centerX - imageView_.size.width() / 2.0 / imageView_.scale;
            const double worldTop =
                imageView_.centerY + imageView_.size.height() / 2.0 / imageView_.scale;
            const QPointF tl(view.size.width() / 2.0 + (worldLeft - view.centerX) * view.scale,
                             view.size.height() / 2.0 - (worldTop - view.centerY) * view.scale);
            p.save();
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRectF(tl, QSizeF(image_.width() * f, image_.height() * f)), image_);
            p.restore();
            return;
        }
    }
    p.drawImage(area.topLeft(), image_);
}

QImage RasterRenderer::render(const View& view, const QString& modelCrs)
{
    QImage out(view.size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    for (auto& layer : layers_)
    {
        if (!layer.settings.visible || layer.settings.opacity <= 0)
            continue;
        const QImage img = renderLayer(layer, view, modelCrs);
        if (img.isNull())
            continue;
        painter.setOpacity(std::clamp(layer.settings.opacity, 0.0, 1.0));
        painter.drawImage(0, 0, img);
    }
    return out;
}

QImage RasterRenderer::renderLayer(Layer& layer, const View& view, const QString& modelCrs)
{
    raster::Raster& r = *layer.raster;
    const auto& info = r.info();
    const int W = view.size.width(), H = view.size.height();
    if (W <= 0 || H <= 0)
        return {};
    const auto model =
        modelCrs.trimmed().isEmpty() ? std::nullopt : crs::parse(modelCrs.trimmed().toStdString());
    const bool reproject = model && info.crs && !sameSystem(*model, *info.crs);

    // Raster pixel coordinates at every kGrid-th screen pixel; interpolated in between.
    const int gw = W / kGrid + 2, gh = H / kGrid + 2;
    std::vector<double> gc(static_cast<std::size_t>(gw * gh), NAN),
        gr(static_cast<std::size_t>(gw * gh), NAN);
    double c0 = 1e300, c1 = -1e300, r0 = 1e300, r1 = -1e300;
    for (int gy = 0; gy < gh; ++gy)
    {
        for (int gx = 0; gx < gw; ++gx)
        {
            double x = view.centerX + (gx * kGrid - W / 2.0) / view.scale;
            double y = view.centerY - (gy * kGrid - H / 2.0) / view.scale;
            if (reproject && !crs::transform(*model, *info.crs, x, y, x, y))
                continue;
            double col = 0, row = 0;
            if (!info.transform.mapToPixel(x, y, col, row) || !std::isfinite(col) ||
                !std::isfinite(row))
                continue;
            const auto k = static_cast<std::size_t>(gy * gw + gx);
            gc[k] = col;
            gr[k] = row;
            c0 = std::min(c0, col);
            c1 = std::max(c1, col);
            r0 = std::min(r0, row);
            r1 = std::max(r1, row);
        }
    }
    c0 = std::max(std::floor(c0) - 2, 0.0);
    r0 = std::max(std::floor(r0) - 2, 0.0);
    c1 = std::min(std::ceil(c1) + 2, static_cast<double>(info.width));
    r1 = std::min(std::ceil(r1) + 2, static_cast<double>(info.height));
    if (c1 <= c0 || r1 <= r0)
        return {};

    // Raster pixels per screen pixel decide the resolution of the window read.
    double step = 0;
    int samples = 0;
    for (int gy = 0; gy < gh; ++gy)
    {
        for (int gx = 0; gx + 1 < gw; ++gx)
        {
            const auto k = static_cast<std::size_t>(gy * gw + gx);
            if (std::isnan(gc[k]) || std::isnan(gc[k + 1]))
                continue;
            step += std::hypot(gc[k + 1] - gc[k], gr[k + 1] - gr[k]) / kGrid;
            ++samples;
        }
    }
    step = samples ? step / samples : 1.0;
    const double pixelsPerCell = std::max(step, 1.0);
    const int bw =
        std::clamp(static_cast<int>(std::ceil((c1 - c0) / pixelsPerCell)), 1, kMaxBuffer);
    const int bh =
        std::clamp(static_cast<int>(std::ceil((r1 - r0) / pixelsPerCell)), 1, kMaxBuffer);
    const double cellW = (c1 - c0) / bw, cellH = (r1 - r0) / bh;

    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    auto gridAt = [&](int sx, int sy, double& col, double& row)
    {
        const int gx = sx / kGrid, gy = sy / kGrid;
        const double fx = (sx - gx * kGrid) / static_cast<double>(kGrid);
        const double fy = (sy - gy * kGrid) / static_cast<double>(kGrid);
        const auto k = static_cast<std::size_t>(gy * gw + gx);
        const double a = gc[k], b = gc[k + 1], c = gc[k + static_cast<std::size_t>(gw)],
                     d = gc[k + static_cast<std::size_t>(gw) + 1];
        if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d))
            return false;
        col = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
        row = (gr[k] * (1 - fx) + gr[k + 1] * fx) * (1 - fy) +
              (gr[k + static_cast<std::size_t>(gw)] * (1 - fx) +
               gr[k + static_cast<std::size_t>(gw) + 1] * fx) *
                  fy;
        return true;
    };

    if (r.isImagery() || !isElevationLayer(layer.settings))
    {
        if (!r.isImagery())
            return {};
        std::vector<std::uint32_t> buf(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh));
        r.readWindowRgba(c0, r0, c1 - c0, r1 - r0, bw, bh, buf.data());
        for (int sy = 0; sy < H; ++sy)
        {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(sy));
            for (int sx = 0; sx < W; ++sx)
            {
                double col = 0, row = 0;
                if (!gridAt(sx, sy, col, row) || col < 0 || row < 0 || col >= info.width ||
                    row >= info.height)
                    continue;
                const int u = static_cast<int>((col - c0) / cellW),
                          v = static_cast<int>((row - r0) / cellH);
                if (u < 0 || v < 0 || u >= bw || v >= bh)
                    continue;
                line[sx] =
                    qPremultiply(buf[static_cast<std::size_t>(v) * static_cast<std::size_t>(bw) +
                                     static_cast<std::size_t>(u)]);
            }
        }
        return img;
    }

    // Elevation: values at screen pixels, then shading and colours.
    std::vector<float> buf(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh));
    r.readWindow(0, c0, r0, c1 - c0, r1 - r0, bw, bh, buf.data());
    auto cell = [&](int u, int v)
    {
        u = std::clamp(u, 0, bw - 1);
        v = std::clamp(v, 0, bh - 1);
        return buf[static_cast<std::size_t>(v) * static_cast<std::size_t>(bw) +
                   static_cast<std::size_t>(u)];
    };
    std::vector<float> z(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), NAN);
    for (int sy = 0; sy < H; ++sy)
    {
        for (int sx = 0; sx < W; ++sx)
        {
            double col = 0, row = 0;
            if (!gridAt(sx, sy, col, row) || col < 0 || row < 0 || col >= info.width ||
                row >= info.height)
                continue;
            const double u = (col - c0) / cellW - 0.5, v = (row - r0) / cellH - 0.5;
            const int iu = static_cast<int>(std::floor(u)), iv = static_cast<int>(std::floor(v));
            const double fu = u - iu, fv = v - iv;
            const float a = cell(iu, iv), b = cell(iu + 1, iv), c = cell(iu, iv + 1),
                        d = cell(iu + 1, iv + 1);
            float value;
            if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d))
                value = cell(static_cast<int>(std::floor(u + 0.5)),
                             static_cast<int>(std::floor(v + 0.5)));
            else
                value = static_cast<float>((a * (1 - fu) + b * fu) * (1 - fv) +
                                           (c * (1 - fu) + d * fu) * fv);
            z[static_cast<std::size_t>(sy) * static_cast<std::size_t>(W) +
              static_cast<std::size_t>(sx)] = value;
        }
    }

    // Size of a screen pixel on the ground, in metres.
    double unit = 1.0;
    if (model)
    {
        unit = model->unitToMeters;
        if (model->isGeographic())
            unit = 111319.49 * std::cos(view.centerY * kPi / 180.0);
    }
    else if (info.crs)
    {
        unit = info.crs->isGeographic() ? 111319.49 * std::cos(view.centerY * kPi / 180.0)
                                        : info.crs->unitToMeters;
    }
    const double cellMetres = unit / view.scale;
    const double zFactor = demUnit(r, layer.settings) * std::max(layer.settings.exaggeration, 0.0);
    const double zenith = (90.0 - 45.0) * kPi / 180.0;
    const double azimuth = (360.0 - 315.0 + 90.0) * kPi / 180.0;
    const bool shade = layer.settings.style != "color";
    const bool colour = layer.settings.style != "hillshade";
    const double span = layer.high - layer.low;
    auto zAt = [&](int x, int y, float centre)
    {
        x = std::clamp(x, 0, W - 1);
        y = std::clamp(y, 0, H - 1);
        const float v = z[static_cast<std::size_t>(y) * static_cast<std::size_t>(W) +
                          static_cast<std::size_t>(x)];
        return std::isnan(v) ? centre : v;
    };
    for (int sy = 0; sy < H; ++sy)
    {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(sy));
        for (int sx = 0; sx < W; ++sx)
        {
            const float e = z[static_cast<std::size_t>(sy) * static_cast<std::size_t>(W) +
                              static_cast<std::size_t>(sx)];
            if (std::isnan(e))
                continue;
            double light = 1.0;
            if (shade)
            {
                // Horn's method on the 3 x 3 neighbourhood (rows grow southwards).
                const double a = zAt(sx - 1, sy - 1, e), b = zAt(sx, sy - 1, e),
                             c = zAt(sx + 1, sy - 1, e);
                const double d = zAt(sx - 1, sy, e), f = zAt(sx + 1, sy, e);
                const double g = zAt(sx - 1, sy + 1, e), h = zAt(sx, sy + 1, e),
                             i = zAt(sx + 1, sy + 1, e);
                const double dzdx =
                    ((c + 2 * f + i) - (a + 2 * d + g)) * zFactor / (8 * cellMetres);
                const double dzdy =
                    ((g + 2 * h + i) - (a + 2 * b + c)) * zFactor / (8 * cellMetres);
                const double slope = std::atan(std::hypot(dzdx, dzdy));
                const double aspect = std::atan2(dzdy, -dzdx);
                light = std::cos(zenith) * std::cos(slope) +
                        std::sin(zenith) * std::sin(slope) * std::cos(azimuth - aspect);
                light = std::clamp(light, 0.0, 1.0);
            }
            QRgb rgb;
            if (colour)
            {
                rgb = ramp((e - layer.low) / span);
                if (shade)
                {
                    const double k = std::clamp(0.3 + 0.7 * light / std::cos(zenith), 0.3, 1.15);
                    auto ch = [&](int v) { return std::clamp(static_cast<int>(v * k), 0, 255); };
                    rgb = qRgb(ch(qRed(rgb)), ch(qGreen(rgb)), ch(qBlue(rgb)));
                }
            }
            else
            {
                // Shading only: flat ground stays clear, slopes facing away from
                // the light darken and slopes facing it lighten what lies below.
                const double relative = light / std::cos(zenith) - 1.0;
                const int alpha = std::clamp(
                    static_cast<int>(std::fabs(relative) * (relative < 0 ? 255 : 150)), 0, 255);
                line[sx] = relative < 0 ? qRgba(0, 0, 0, alpha)
                                        : qRgba(alpha, alpha, alpha, alpha); // premultiplied
                continue;
            }
            line[sx] = rgb;
        }
    }
    return img;
}

} // namespace mirab::app
