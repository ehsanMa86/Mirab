/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/Basemap.h
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

namespace anahita::crs
{
struct Crs;
}

namespace anahita::app
{

class Basemap : public QObject
{
    Q_OBJECT
public:
    struct Provider
    {
        QString id;
        QString label;
        QString url;          ///< {z} {x} {y} and optional {s} placeholders
        QStringList subdomains;
        QString attribution;
        int maxZoom = 19;
    };
    static const QVector<Provider>& providers();
    static const Provider* find(const QString& id);

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
    /// screen pixels and model coordinates.
    void paint(QPainter& p, const QRectF& viewport, const std::function<QPointF(double, double)>& toScreen,
               const std::function<QPointF(const QPointF&)>& toWorld);
    QString attribution() const;

    /// Model coordinates of a longitude/latitude, when the CRS is known.
    bool fromLonLat(double lon, double lat, double& x, double& y) const;
    bool toLonLat(double x, double y, double& lon, double& lat) const;

signals:
    void updated();

private:
    struct TileKey
    {
        int z, x, y;
    };
    QString tileId(int z, int x, int y) const;
    const QImage* tile(int z, int x, int y, bool request);
    void request(int z, int x, int y);

    QString provider_;
    std::unique_ptr<crs::Crs> crs_;
    QString reason_;
    QString crsName_;
    QNetworkAccessManager* network_;
    QCache<QString, QImage> cache_;
    QSet<QString> pending_;
    QSet<QString> failed_;
    int generation_ = 0;
};

} // namespace anahita::app
