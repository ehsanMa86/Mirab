/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/Basemap.h
 * Purpose: Web basemap layer: XYZ, WMS and WMTS map tiles projected into the coordinate system of the model.
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

#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QPainter;

namespace mirab::crs
{
struct Crs;
}

namespace mirab::core
{
struct MapServiceSettings;
}

namespace mirab::app
{

/// One zoom level of a WMTS tile matrix set (OGC 07-057r7).
struct WmtsMatrix
{
    QString id;
    double scaleDenominator = 0.0;
    double left = 0.0, top = 0.0; ///< top-left corner, x then y
    int tileWidth = 256, tileHeight = 256;
    int matrixWidth = 0, matrixHeight = 0;
};

class Basemap : public QObject
{
    Q_OBJECT
public:
    struct Provider
    {
        QString id;
        QString label;
        QString url;          ///< xyz: {z} {x} {y} (and {s}, {-y}) template; wms/wmts: service address
        QStringList subdomains;
        QString attribution;
        int maxZoom = 19;
        QString type = QStringLiteral("xyz"); ///< "xyz", "wms" or "wmts"
        QString layer, style, format;
        QString crs;          ///< system of the requests, e.g. "EPSG:3857"
        QString matrixSet;    ///< WMTS tile matrix set
        QString version;      ///< WMS version, "1.3.0" or "1.1.1"
        QString tileTemplate; ///< WMTS ResourceURL template, when the server offers one
        QVector<WmtsMatrix> matrices; ///< WMTS levels, read from the capabilities
        bool custom = false;
        bool saved = false; ///< kept in the application settings (not only in the open project)
    };
    /// Built-in providers followed by the user's map services.
    static QVector<Provider> providers();
    static const Provider* find(const QString& id);

    /// User-added services. @p persist also saves them in the application settings.
    static void addService(const Provider& provider, bool persist = true);
    static void removeService(const QString& id);
    static Provider fromSettings(const core::MapServiceSettings& s);
    static void toSettings(const Provider& p, core::MapServiceSettings& s);

    explicit Basemap(QObject* parent = nullptr);
    ~Basemap() override;

    void setProvider(const QString& id);
    QString provider() const { return provider_; }
    bool active() const { return !provider_.isEmpty() && usable(); }

    /// Sets the coordinate system of the model coordinates. Empty text means
    /// unknown: then the extent is checked for longitude/latitude values.
    void setCoordinateSystem(const QString& crsText, double minX, double minY, double maxX, double maxY);
    bool usable() const { return crs_ != nullptr; }
    QString unusableReason() const { return reason_; }
    QString crsName() const { return crsName_; }

    /// Draws the tiles that cover @p viewport. toWorld/toScreen convert between
    /// screen pixels and model coordinates. @p detail is the number of device
    /// pixels per painter unit (higher for print), @p maxTiles caps the download.
    void paint(QPainter& p, const QRectF& viewport,
               const std::function<QPointF(double, double)>& toScreen,
               const std::function<QPointF(const QPointF&)>& toWorld, double detail = 1.0,
               int maxTiles = 160);
    /// True while tiles or the tile matrices of the service are being downloaded.
    bool loading() const { return !pending_.isEmpty() || !capabilitiesLoading_.isEmpty(); }
    QString attribution() const;
    /// Last problem with the current service (capabilities or tiles), empty when none.
    QString serviceError() const { return serviceError_; }

    /// Model coordinates of a longitude/latitude, when the CRS is known.
    bool fromLonLat(double lon, double lat, double& x, double& y) const;
    bool toLonLat(double x, double y, double& lon, double& lat) const;

signals:
    void updated();

private:
    struct Level;
    QString tileId(int z, int x, int y) const;
    const QImage* tile(const Provider& prov, const Level& level, int x, int y, bool request);
    void request(const Provider& prov, const Level& level, int x, int y);
    QString tileUrl(const Provider& prov, const Level& level, int x, int y) const;
    void loadCapabilities(const Provider& prov);

    QString provider_;
    std::unique_ptr<crs::Crs> crs_;
    QString reason_;
    QString crsName_;
    QString serviceError_;
    QNetworkAccessManager* network_;
    QCache<QString, QImage> cache_;
    QSet<QString> pending_;
    QSet<QString> failed_;
    QSet<QString> capabilitiesPending_;
    QSet<QString> capabilitiesLoading_; ///< capabilities requests in flight
};

} // namespace mirab::app
