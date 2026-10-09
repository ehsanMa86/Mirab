/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/Basemap.cpp
 * Purpose: Web basemap layer: XYZ map tiles projected into the coordinate system of the model.
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
#include "Basemap.h"

#include "anahita/AnahitaConfig.h"
#include "anahita/core/Crs.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QStandardPaths>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace anahita::app
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kRadius = 6378137.0;
constexpr double kOrigin = 20037508.342789244; // half the Web Mercator world width
constexpr int kTile = 256;

void toMercator(double lon, double lat, double& x, double& y)
{
    lat = std::clamp(lat, -85.05112878, 85.05112878);
    x = lon * kOrigin / 180.0;
    y = std::log(std::tan((90.0 + lat) * kPi / 360.0)) * kRadius;
}

void fromMercator(double x, double y, double& lon, double& lat)
{
    lon = x / kOrigin * 180.0;
    lat = (2.0 * std::atan(std::exp(y / kRadius)) - kPi / 2.0) * 180.0 / kPi;
}
} // namespace

const QVector<Basemap::Provider>& Basemap::providers()
{
    static const QVector<Provider> list = {
        {"osm", tr("OpenStreetMap"), "https://tile.openstreetmap.org/{z}/{x}/{y}.png", {},
         QStringLiteral("© OpenStreetMap contributors"), 19},
        {"carto-light", tr("Light (CARTO Positron)"), "https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png",
         {"a", "b", "c", "d"}, QStringLiteral("© OpenStreetMap contributors © CARTO"), 20},
        {"esri-street", tr("Streets (Esri)"),
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}", {},
         QStringLiteral("Tiles © Esri"), 19},
        {"esri-imagery", tr("Satellite imagery (Esri)"),
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}", {},
         QStringLiteral("Tiles © Esri, Maxar, Earthstar Geographics and the GIS User Community"), 19},
        {"esri-topo", tr("Topographic (Esri)"),
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}", {},
         QStringLiteral("Tiles © Esri"), 19},
        {"esri-gray", tr("Light gray canvas (Esri)"),
         "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Light_Gray_Base/MapServer/tile/{z}/{y}/{x}",
         {}, QStringLiteral("Tiles © Esri"), 16},
        {"opentopomap", tr("OpenTopoMap"), "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png", {"a", "b", "c"},
         QStringLiteral("© OpenStreetMap contributors, SRTM © OpenTopoMap (CC-BY-SA)"), 17},
    };
    return list;
}

const Basemap::Provider* Basemap::find(const QString& id)
{
    for (const auto& p : providers())
    {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

Basemap::Basemap(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this))
{
    cache_.setMaxCost(700);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!dir.isEmpty())
    {
        auto* disk = new QNetworkDiskCache(this);
        disk->setCacheDirectory(QDir(dir).filePath("tiles"));
        disk->setMaximumCacheSize(300LL * 1024 * 1024);
        network_->setCache(disk);
    }
    reason_ = tr("Set the coordinate system of the map coordinates to show a basemap.");
}

Basemap::~Basemap() = default;

void Basemap::setProvider(const QString& id)
{
    provider_ = find(id) ? id : QString();
    emit updated();
}

void Basemap::setCoordinateSystem(const QString& crsText, double minX, double minY, double maxX, double maxY)
{
    crs_.reset();
    crsName_.clear();
    if (!crsText.trimmed().isEmpty())
    {
        if (auto c = crs::parse(crsText.trimmed().toStdString()))
        {
            crs_ = std::make_unique<crs::Crs>(*c);
            crsName_ = QString::fromStdString(c->name.empty() ? crs::toString(*c) : c->name);
            reason_.clear();
        }
        else
        {
            reason_ = tr("The coordinate system \"%1\" is not supported.").arg(crsText);
        }
    }
    else if (auto g = crs::guessFromExtent(minX, minY, maxX, maxY))
    {
        crs_ = std::make_unique<crs::Crs>(*g);
        crsName_ = tr("Longitude/latitude (assumed)");
        reason_.clear();
    }
    else
    {
        reason_ = tr("Set the coordinate system of the map coordinates to show a basemap.");
    }
    emit updated();
}

bool Basemap::toLonLat(double x, double y, double& lon, double& lat) const
{
    return crs_ && crs::toLonLat(*crs_, x, y, lon, lat);
}

bool Basemap::fromLonLat(double lon, double lat, double& x, double& y) const
{
    return crs_ && crs::fromLonLat(*crs_, lon, lat, x, y);
}

QString Basemap::attribution() const
{
    const Provider* p = find(provider_);
    return p ? p->attribution : QString();
}

QString Basemap::tileId(int z, int x, int y) const
{
    return QStringLiteral("%1/%2/%3/%4").arg(provider_).arg(z).arg(x).arg(y);
}

void Basemap::request(int z, int x, int y)
{
    const Provider* prov = find(provider_);
    if (!prov)
        return;
    const QString id = tileId(z, x, y);
    if (pending_.contains(id) || failed_.contains(id) || pending_.size() > 48)
        return;
    QString url = prov->url;
    url.replace("{z}", QString::number(z)).replace("{x}", QString::number(x)).replace("{y}", QString::number(y));
    if (!prov->subdomains.isEmpty())
        url.replace("{s}", prov->subdomains.at((x + y) % prov->subdomains.size()));
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Anahita/" ANAHITA_VERSION_STRING " (+" ANAHITA_HOMEPAGE ")"));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    pending_.insert(id);
    QNetworkReply* reply = network_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();
        pending_.remove(id);
        if (reply->error() != QNetworkReply::NoError)
        {
            failed_.insert(id);
            return;
        }
        auto* img = new QImage(QImage::fromData(reply->readAll()));
        if (img->isNull())
        {
            delete img;
            failed_.insert(id);
            return;
        }
        cache_.insert(id, img);
        emit updated();
    });
}

const QImage* Basemap::tile(int z, int x, int y, bool fetch)
{
    const QString id = tileId(z, x, y);
    if (const QImage* img = cache_.object(id))
        return img;
    if (fetch)
        request(z, x, y);
    return nullptr;
}

void Basemap::paint(QPainter& p, const QRectF& viewport, const std::function<QPointF(double, double)>& toScreen,
                    const std::function<QPointF(const QPointF&)>& toWorld)
{
    const Provider* prov = find(provider_);
    if (!prov || !crs_ || viewport.isEmpty())
        return;

    // Extent of the view in Web Mercator, sampled along a grid (the model CRS
    // and Web Mercator are not aligned in general).
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    int good = 0;
    for (int i = 0; i <= 6; ++i)
    {
        for (int j = 0; j <= 6; ++j)
        {
            const QPointF s(viewport.left() + viewport.width() * i / 6.0, viewport.top() + viewport.height() * j / 6.0);
            const QPointF w = toWorld(s);
            double lon = 0, lat = 0;
            if (!crs::toLonLat(*crs_, w.x(), w.y(), lon, lat) || !std::isfinite(lon) || !std::isfinite(lat))
                continue;
            double mx = 0, my = 0;
            toMercator(std::clamp(lon, -180.0, 180.0), lat, mx, my);
            minX = std::min(minX, mx);
            maxX = std::max(maxX, mx);
            minY = std::min(minY, my);
            maxY = std::max(maxY, my);
            ++good;
        }
    }
    if (good < 4 || maxX <= minX || maxY <= minY)
        return;
    const double resolution = (maxX - minX) / std::max(1.0, viewport.width());
    int z = static_cast<int>(std::round(std::log2(156543.03392804097 / std::max(resolution, 1e-6))));
    z = std::clamp(z, 0, prov->maxZoom);
    auto tileRange = [&](int zoom, int& x0, int& x1, int& y0, int& y1) {
        const double span = 2.0 * kOrigin / std::pow(2.0, zoom);
        const int n = 1 << zoom;
        x0 = std::clamp(static_cast<int>(std::floor((minX + kOrigin) / span)), 0, n - 1);
        x1 = std::clamp(static_cast<int>(std::floor((maxX + kOrigin) / span)), 0, n - 1);
        y0 = std::clamp(static_cast<int>(std::floor((kOrigin - maxY) / span)), 0, n - 1);
        y1 = std::clamp(static_cast<int>(std::floor((kOrigin - minY) / span)), 0, n - 1);
    };
    int x0, x1, y0, y1;
    tileRange(z, x0, x1, y0, y1);
    while (z > 0 && (x1 - x0 + 1) * (y1 - y0 + 1) > 160)
    {
        --z;
        tileRange(z, x0, x1, y0, y1);
    }

    const double span = 2.0 * kOrigin / std::pow(2.0, z);
    auto corner = [&](double mx, double my, QPointF& out) {
        double lon = 0, lat = 0, x = 0, y = 0;
        fromMercator(mx, my, lon, lat);
        if (!crs::fromLonLat(*crs_, lon, lat, x, y))
            return false;
        out = toScreen(x, y);
        return std::isfinite(out.x()) && std::isfinite(out.y());
    };

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QTransform base = p.transform();
    const QPolygonF unit({QPointF(0, 0), QPointF(kTile, 0), QPointF(kTile, kTile), QPointF(0, kTile)});
    for (int ty = y0; ty <= y1; ++ty)
    {
        for (int tx = x0; tx <= x1; ++tx)
        {
            const double left = -kOrigin + tx * span;
            const double top = kOrigin - ty * span;
            QPointF tl, tr, br, bl;
            if (!corner(left, top, tl) || !corner(left + span, top, tr) || !corner(left + span, top - span, br) ||
                !corner(left, top - span, bl))
                continue;
            QTransform t;
            if (!QTransform::quadToQuad(unit, QPolygonF({tl, tr, br, bl}), t))
                continue;
            const QImage* img = tile(z, tx, ty, true);
            QRectF source(0, 0, kTile, kTile);
            if (!img)
            {
                // Use a cached ancestor tile, magnified, until this one arrives.
                for (int d = 1; d <= 4 && z - d >= 0 && !img; ++d)
                {
                    img = tile(z - d, tx >> d, ty >> d, false);
                    if (img)
                    {
                        const double part = static_cast<double>(kTile) / (1 << d);
                        source = QRectF((tx & ((1 << d) - 1)) * part, (ty & ((1 << d) - 1)) * part, part, part);
                    }
                }
            }
            if (!img)
                continue;
            p.setTransform(t * base);
            // Overlap by a fraction of a pixel to avoid hairline seams.
            p.drawImage(QRectF(-0.3, -0.3, kTile + 0.6, kTile + 0.6), *img, source);
        }
    }
    p.restore();
}

} // namespace anahita::app
