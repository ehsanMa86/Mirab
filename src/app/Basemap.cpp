/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Basemap.cpp
 * Purpose: Web basemap layer: XYZ, WMS and WMTS tiles drawn in the coordinate system of the model.
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

#include "MapServices.h"
#include "mirab/MirabConfig.h"
#include "mirab/core/Crs.h"
#include "mirab/core/ProjectSettings.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTransform>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <optional>

namespace mirab::app
{

namespace
{
constexpr double kOrigin = crs::kWebMercatorHalfWorld; // half the Web Mercator world width
constexpr int kTile = 256;
constexpr double kMetresPerDegree = 2.0 * 3.14159265358979323846 * 6378137.0 / 360.0;

QJsonObject toJson(const Basemap::Provider& p)
{
    QJsonObject o;
    o["id"] = p.id;
    o["name"] = p.label;
    o["type"] = p.type;
    o["url"] = p.url;
    o["layer"] = p.layer;
    o["style"] = p.style;
    o["format"] = p.format;
    o["crs"] = p.crs;
    o["matrixSet"] = p.matrixSet;
    o["version"] = p.version;
    o["attribution"] = p.attribution;
    return o;
}

Basemap::Provider fromJson(const QJsonObject& o)
{
    Basemap::Provider p;
    p.id = o["id"].toString();
    p.label = o["name"].toString();
    p.type = o["type"].toString("xyz");
    p.url = o["url"].toString();
    p.layer = o["layer"].toString();
    p.style = o["style"].toString();
    p.format = o["format"].toString();
    p.crs = o["crs"].toString();
    p.matrixSet = o["matrixSet"].toString();
    p.version = o["version"].toString();
    p.attribution = o["attribution"].toString();
    p.custom = true;
    if (p.type == "xyz" && p.url.contains("{s}"))
        p.subdomains = {"a", "b", "c"};
    return p;
}

QVector<Basemap::Provider>& customList()
{
    static QVector<Basemap::Provider> list = [] {
        QVector<Basemap::Provider> l;
        const QJsonArray a = QJsonDocument::fromJson(QSettings().value("map/services").toByteArray()).array();
        for (const auto& v : a)
        {
            Basemap::Provider p = fromJson(v.toObject());
            p.saved = true;
            if (!p.id.isEmpty() && !p.url.isEmpty())
                l << p;
        }
        return l;
    }();
    return list;
}

void saveCustomList()
{
    QJsonArray a;
    for (const auto& p : customList())
    {
        if (p.saved)
            a.append(toJson(p));
    }
    QSettings().setValue("map/services", QJsonDocument(a).toJson(QJsonDocument::Compact));
}

const QVector<Basemap::Provider>& builtInProviders()
{
    static const QVector<Basemap::Provider> list = [] {
        auto make = [](const char* id, const QString& label, const char* url, const QStringList& subs,
                       const char* attribution, int maxZoom) {
            Basemap::Provider p;
            p.id = QString::fromLatin1(id);
            p.label = label;
            p.url = QString::fromLatin1(url);
            p.subdomains = subs;
            p.attribution = QString::fromUtf8(attribution);
            p.maxZoom = maxZoom;
            return p;
        };
        return QVector<Basemap::Provider>{
            make("osm", Basemap::tr("OpenStreetMap"), "https://tile.openstreetmap.org/{z}/{x}/{y}.png", {},
                 "© OpenStreetMap contributors", 19),
            make("esri-street", Basemap::tr("Streets (Esri)"),
                 "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}",
                 {}, "Tiles © Esri", 19),
            make("esri-imagery", Basemap::tr("Satellite imagery (Esri)"),
                 "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}", {},
                 "Tiles © Esri, Maxar, Earthstar Geographics and the GIS User Community", 19),
            make("esri-topo", Basemap::tr("Topographic (Esri)"),
                 "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}", {},
                 "Tiles © Esri", 19),
            make("esri-gray", Basemap::tr("Light gray canvas (Esri)"),
                 "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Light_Gray_Base/MapServer/tile/"
                 "{z}/{y}/{x}",
                 {}, "Tiles © Esri", 16),
            make("opentopomap", Basemap::tr("OpenTopoMap"), "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png",
                 {"a", "b", "c"}, "© OpenStreetMap contributors, SRTM © OpenTopoMap (CC-BY-SA)", 17),
        };
    }();
    return list;
}

bool sameSystem(const crs::Crs& a, const crs::Crs& b)
{
    return (a.epsg != 0 && a.epsg == b.epsg) || crs::toString(a) == crs::toString(b);
}

double metresPerUnit(const crs::Crs& c)
{
    return c.isGeographic() ? kMetresPerDegree : c.unitToMeters;
}

QString userAgent()
{
    return QStringLiteral("Mirab/" MIRAB_VERSION_STRING " (+" MIRAB_HOMEPAGE ")");
}
} // namespace

/// One zoom level of a tile grid, in the coordinate system of the requests.
struct Basemap::Level
{
    int z = 0;                   ///< identifies the level in tile keys
    double left = 0, top = 0;    ///< top-left corner of tile (0, 0)
    double spanX = 1, spanY = 1; ///< tile size in grid units
    int cols = 0, rows = 0;      ///< number of tiles; 0 when unbounded
    QString matrixId;            ///< WMTS tile matrix
};

QVector<Basemap::Provider> Basemap::providers()
{
    return builtInProviders() + customList();
}

const Basemap::Provider* Basemap::find(const QString& id)
{
    // CARTO tiles need an API key now; projects saved with it get the Esri light canvas.
    const QString wanted = id == "carto-light" ? QStringLiteral("esri-gray") : id;
    for (const auto& p : builtInProviders())
    {
        if (p.id == wanted)
            return &p;
    }
    for (const auto& p : customList())
    {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

void Basemap::addService(const Provider& provider, bool persist)
{
    Provider p = provider;
    p.custom = true;
    auto& list = customList();
    bool replaced = false;
    for (auto& existing : list)
    {
        if (existing.id == p.id)
        {
            p.saved = persist || existing.saved;
            if (p.matrices.isEmpty())
            {
                p.matrices = existing.matrices;
                p.tileTemplate = existing.tileTemplate;
            }
            existing = p;
            replaced = true;
        }
    }
    if (!replaced)
    {
        p.saved = persist;
        list << p;
    }
    if (persist)
        saveCustomList();
}

void Basemap::removeService(const QString& id)
{
    auto& list = customList();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Provider& p) { return p.id == id; }), list.end());
    saveCustomList();
}

Basemap::Provider Basemap::fromSettings(const core::MapServiceSettings& s)
{
    QJsonObject o;
    o["id"] = QString::fromStdString(s.id);
    o["name"] = QString::fromStdString(s.name);
    o["type"] = QString::fromStdString(s.type.empty() ? std::string("xyz") : s.type);
    o["url"] = QString::fromStdString(s.url);
    o["layer"] = QString::fromStdString(s.layer);
    o["style"] = QString::fromStdString(s.style);
    o["format"] = QString::fromStdString(s.format);
    o["crs"] = QString::fromStdString(s.crs);
    o["matrixSet"] = QString::fromStdString(s.matrixSet);
    o["version"] = QString::fromStdString(s.version);
    o["attribution"] = QString::fromStdString(s.attribution);
    return fromJson(o);
}

void Basemap::toSettings(const Provider& p, core::MapServiceSettings& s)
{
    s.id = p.id.toStdString();
    s.name = p.label.toStdString();
    s.type = p.type.toStdString();
    s.url = p.url.toStdString();
    s.layer = p.layer.toStdString();
    s.style = p.style.toStdString();
    s.format = p.format.toStdString();
    s.crs = p.crs.toStdString();
    s.matrixSet = p.matrixSet.toStdString();
    s.version = p.version.toStdString();
    s.attribution = p.attribution.toStdString();
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
    serviceError_.clear();
    failed_.clear();
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

QString Basemap::tileUrl(const Provider& prov, const Level& level, int x, int y) const
{
    if (prov.type == "wms")
    {
        double x0 = level.left + x * level.spanX, x1 = x0 + level.spanX;
        double y1 = level.top - y * level.spanY, y0 = y1 - level.spanY;
        const QString version = prov.version.isEmpty() ? QStringLiteral("1.3.0") : prov.version;
        const bool v13 = version.startsWith("1.3");
        if (v13 && normaliseCrs(prov.crs) == "EPSG:4326" && !prov.crs.contains("84"))
        {
            std::swap(x0, y0); // latitude first in WMS 1.3.0
            std::swap(x1, y1);
        }
        QUrl url(prov.url);
        QUrlQuery q(url);
        q.addQueryItem("SERVICE", "WMS");
        q.addQueryItem("VERSION", version);
        q.addQueryItem("REQUEST", "GetMap");
        q.addQueryItem("LAYERS", prov.layer);
        q.addQueryItem("STYLES", prov.style);
        q.addQueryItem("FORMAT", prov.format.isEmpty() ? QStringLiteral("image/png") : prov.format);
        q.addQueryItem("TRANSPARENT", "TRUE");
        q.addQueryItem(v13 ? "CRS" : "SRS", prov.crs);
        q.addQueryItem("BBOX", QStringLiteral("%1,%2,%3,%4")
                                   .arg(x0, 0, 'f', 9)
                                   .arg(y0, 0, 'f', 9)
                                   .arg(x1, 0, 'f', 9)
                                   .arg(y1, 0, 'f', 9));
        q.addQueryItem("WIDTH", QString::number(kTile));
        q.addQueryItem("HEIGHT", QString::number(kTile));
        url.setQuery(q);
        return url.toString(QUrl::FullyEncoded);
    }
    if (prov.type == "wmts")
    {
        if (!prov.tileTemplate.isEmpty())
        {
            QString url = prov.tileTemplate;
            url.replace("{TileMatrixSet}", prov.matrixSet, Qt::CaseInsensitive)
                .replace("{TileMatrix}", level.matrixId, Qt::CaseInsensitive)
                .replace("{TileRow}", QString::number(y), Qt::CaseInsensitive)
                .replace("{TileCol}", QString::number(x), Qt::CaseInsensitive)
                .replace("{Style}", prov.style, Qt::CaseInsensitive);
            return url;
        }
        QUrl url(prov.url);
        QUrlQuery q(url);
        q.addQueryItem("SERVICE", "WMTS");
        q.addQueryItem("REQUEST", "GetTile");
        q.addQueryItem("VERSION", "1.0.0");
        q.addQueryItem("LAYER", prov.layer);
        q.addQueryItem("STYLE", prov.style);
        q.addQueryItem("FORMAT", prov.format);
        q.addQueryItem("TILEMATRIXSET", prov.matrixSet);
        q.addQueryItem("TILEMATRIX", level.matrixId);
        q.addQueryItem("TILEROW", QString::number(y));
        q.addQueryItem("TILECOL", QString::number(x));
        url.setQuery(q);
        return url.toString(QUrl::FullyEncoded);
    }
    QString url = prov.url;
    url.replace("{z}", QString::number(level.z))
        .replace("{x}", QString::number(x))
        .replace("{-y}", QString::number((1 << level.z) - 1 - y))
        .replace("{y}", QString::number(y));
    if (!prov.subdomains.isEmpty())
        url.replace("{s}", prov.subdomains.at((x + y) % prov.subdomains.size()));
    return url;
}

void Basemap::request(const Provider& prov, const Level& level, int x, int y)
{
    const QString id = tileId(level.z, x, y);
    if (pending_.contains(id) || failed_.contains(id) || pending_.size() > 48)
        return;
    QNetworkRequest req{QUrl(tileUrl(prov, level, x, y))};
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    pending_.insert(id);
    QNetworkReply* reply = network_->get(req);
    const QString owner = provider_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, owner] {
        reply->deleteLater();
        pending_.remove(id);
        if (reply->error() != QNetworkReply::NoError)
        {
            failed_.insert(id);
            if (owner == provider_)
                serviceError_ = reply->errorString();
            return;
        }
        auto* img = new QImage(QImage::fromData(reply->readAll()));
        if (img->isNull())
        {
            // WMS servers report errors as an XML document.
            delete img;
            failed_.insert(id);
            if (owner == provider_)
                serviceError_ = tr("The map service did not return an image.");
            return;
        }
        if (owner == provider_)
            serviceError_.clear();
        cache_.insert(id, img);
        emit updated();
    });
}

const QImage* Basemap::tile(const Provider& prov, const Level& level, int x, int y, bool fetch)
{
    const QString id = tileId(level.z, x, y);
    if (const QImage* img = cache_.object(id))
        return img;
    if (fetch)
        request(prov, level, x, y);
    return nullptr;
}

void Basemap::loadCapabilities(const Provider& prov)
{
    if (capabilitiesPending_.contains(prov.id))
        return;
    capabilitiesPending_.insert(prov.id);
    capabilitiesLoading_.insert(prov.id);
    QNetworkRequest req(capabilitiesUrl(prov.url, "wmts"));
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply* reply = network_->get(req);
    const QString id = prov.id;
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();
        capabilitiesLoading_.remove(id);
        const ServiceCapabilities caps = parseCapabilities(reply->readAll());
        for (auto& p : customList())
        {
            if (p.id == id && applyWmtsCapabilities(caps, p))
            {
                emit updated();
                return;
            }
        }
        if (id == provider_)
        {
            serviceError_ = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                    : tr("The tile matrix set was not found.");
            emit updated();
        }
    });
}

void Basemap::paint(QPainter& p, const QRectF& viewport,
                    const std::function<QPointF(double, double)>& toScreen,
                    const std::function<QPointF(const QPointF&)>& toWorld, double detail,
                    int maxTiles)
{
    const Provider* found = find(provider_);
    if (!found || !crs_ || viewport.isEmpty())
        return;
    const Provider prov = *found;

    if (prov.type == "wmts" && prov.matrices.isEmpty())
    {
        loadCapabilities(prov); // the tile matrices are not stored with the project
        return;
    }
    // Coordinate system of the tile grid.
    const bool mercator = prov.type == "xyz" || normaliseCrs(prov.crs) == "EPSG:3857";
    const std::optional<crs::Crs> grid =
        mercator ? crs::fromEpsg(3857) : crs::parse(normaliseCrs(prov.crs).toStdString());
    if (!grid)
        return;
    const bool identity = sameSystem(*grid, *crs_);
    auto toGrid = [&](double x, double y, double& gx, double& gy) {
        if (identity)
        {
            gx = x;
            gy = y;
            return true;
        }
        if (mercator)
        {
            double lon = 0, lat = 0;
            if (!crs::toLonLat(*crs_, x, y, lon, lat) || !std::isfinite(lon) || !std::isfinite(lat))
                return false;
            crs::lonLatToWebMercator(std::clamp(lon, -180.0, 180.0), lat, gx, gy);
            return true;
        }
        return crs::transform(*crs_, *grid, x, y, gx, gy) && std::isfinite(gx) && std::isfinite(gy);
    };
    auto fromGrid = [&](double gx, double gy, QPointF& out) {
        double x = gx, y = gy;
        if (!identity && !crs::transform(*grid, *crs_, gx, gy, x, y))
            return false;
        out = toScreen(x, y);
        return std::isfinite(out.x()) && std::isfinite(out.y());
    };

    // Extent of the view in grid coordinates, sampled along a grid (the two
    // systems are not aligned in general).
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    int good = 0;
    for (int i = 0; i <= 6; ++i)
    {
        for (int j = 0; j <= 6; ++j)
        {
            const QPointF s(viewport.left() + viewport.width() * i / 6.0, viewport.top() + viewport.height() * j / 6.0);
            const QPointF w = toWorld(s);
            double gx = 0, gy = 0;
            if (!toGrid(w.x(), w.y(), gx, gy))
                continue;
            minX = std::min(minX, gx);
            maxX = std::max(maxX, gx);
            minY = std::min(minY, gy);
            maxY = std::max(maxY, gy);
            ++good;
        }
    }
    if (good < 4 || maxX <= minX || maxY <= minY)
        return;
    // Grid units per tile pixel; a higher detail picks finer levels (print).
    const double resolution =
        (maxX - minX) / std::max(1.0, viewport.width() * std::max(0.1, detail));

    // Levels: the Web Mercator pyramid, the server's tile matrices, or powers
    // of two of the grid unit (WMS in another system).
    const bool pyramid = mercator && prov.type != "wmts";
    const bool wmts = prov.type == "wmts";
    auto levelFor = [&](int z) {
        Level lv;
        lv.z = z;
        if (pyramid)
        {
            lv.left = -kOrigin;
            lv.top = kOrigin;
            lv.spanX = lv.spanY = 2.0 * kOrigin / std::pow(2.0, z);
            lv.cols = lv.rows = 1 << z;
        }
        else if (wmts)
        {
            const WmtsMatrix& m = prov.matrices[z];
            const double res = m.scaleDenominator * 0.00028 / metresPerUnit(*grid);
            lv.left = m.left;
            lv.top = m.top;
            lv.spanX = m.tileWidth * res;
            lv.spanY = m.tileHeight * res;
            lv.cols = m.matrixWidth;
            lv.rows = m.matrixHeight;
            lv.matrixId = m.id;
        }
        else
        {
            lv.spanX = lv.spanY = kTile * std::pow(2.0, z);
        }
        return lv;
    };
    int z = 0, zMin = 0, zMax = 0;
    if (pyramid)
    {
        zMax = prov.maxZoom;
        z = static_cast<int>(std::round(std::log2(156543.03392804097 / std::max(resolution, 1e-9))));
    }
    else if (wmts)
    {
        zMax = static_cast<int>(prov.matrices.size()) - 1;
        double best = 1e300;
        for (int i = 0; i <= zMax; ++i)
        {
            const double res = prov.matrices[i].scaleDenominator * 0.00028 / metresPerUnit(*grid);
            const double d = std::fabs(std::log(res / resolution));
            if (d < best)
            {
                best = d;
                z = i;
            }
        }
    }
    else
    {
        zMin = -60;
        zMax = 60;
        z = static_cast<int>(std::floor(std::log2(std::max(resolution, 1e-12))));
    }
    z = std::clamp(z, zMin, zMax);
    auto tileRange = [&](const Level& lv, int& x0, int& x1, int& y0, int& y1) {
        x0 = static_cast<int>(std::floor((minX - lv.left) / lv.spanX));
        x1 = static_cast<int>(std::floor((maxX - lv.left) / lv.spanX));
        y0 = static_cast<int>(std::floor((lv.top - maxY) / lv.spanY));
        y1 = static_cast<int>(std::floor((lv.top - minY) / lv.spanY));
        if (lv.cols > 0)
        {
            x0 = std::clamp(x0, 0, lv.cols - 1);
            x1 = std::clamp(x1, 0, lv.cols - 1);
        }
        if (lv.rows > 0)
        {
            y0 = std::clamp(y0, 0, lv.rows - 1);
            y1 = std::clamp(y1, 0, lv.rows - 1);
        }
    };
    auto count = [](int a0, int a1, int b0, int b1) {
        return (static_cast<long long>(a1) - a0 + 1) * (static_cast<long long>(b1) - b0 + 1);
    };
    // Coarser levels have a smaller z in the pyramid and in WMTS, a larger one for powers of two.
    const int coarser = pyramid || wmts ? -1 : 1;
    Level level = levelFor(z);
    int x0, x1, y0, y1;
    tileRange(level, x0, x1, y0, y1);
    while (count(x0, x1, y0, y1) > maxTiles && z + coarser >= zMin && z + coarser <= zMax)
    {
        z += coarser;
        level = levelFor(z);
        tileRange(level, x0, x1, y0, y1);
    }
    if (count(x0, x1, y0, y1) > 400)
        return;

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QTransform base = p.transform();
    for (int ty = y0; ty <= y1; ++ty)
    {
        for (int tx = x0; tx <= x1; ++tx)
        {
            const double left = level.left + tx * level.spanX;
            const double top = level.top - ty * level.spanY;
            QPointF tl, tr, br, bl;
            if (!fromGrid(left, top, tl) || !fromGrid(left + level.spanX, top, tr) ||
                !fromGrid(left + level.spanX, top - level.spanY, br) || !fromGrid(left, top - level.spanY, bl))
                continue;
            const QImage* img = tile(prov, level, tx, ty, true);
            QRectF source = img ? QRectF(img->rect()) : QRectF();
            if (!img && pyramid)
            {
                // Use a cached ancestor tile, magnified, until this one arrives.
                for (int d = 1; d <= 4 && z - d >= 0 && !img; ++d)
                {
                    img = tile(prov, levelFor(z - d), tx >> d, ty >> d, false);
                    if (img)
                    {
                        const double part = static_cast<double>(img->width()) / (1 << d);
                        source = QRectF((tx & ((1 << d) - 1)) * part, (ty & ((1 << d) - 1)) * part, part, part);
                    }
                }
            }
            if (!img)
                continue;
            QTransform t;
            const QPolygonF unit({QPointF(0, 0), QPointF(kTile, 0), QPointF(kTile, kTile), QPointF(0, kTile)});
            if (!QTransform::quadToQuad(unit, QPolygonF({tl, tr, br, bl}), t))
                continue;
            p.setTransform(t * base);
            // Overlap by a fraction of a pixel to avoid hairline seams.
            p.drawImage(QRectF(-0.3, -0.3, kTile + 0.6, kTile + 0.6), *img, source);
        }
    }
    p.restore();
}

} // namespace mirab::app
