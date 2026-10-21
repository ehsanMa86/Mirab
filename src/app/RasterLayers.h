/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/RasterLayers.h
 * Purpose: Raster layers on the map: elevation models (hillshade, colour ramp) and georeferenced
 * images.
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

#include "ProjectDocument.h"

#include "mirab/core/Raster.h"

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class QPainter;
class QTimer;

namespace mirab::core
{
class TerrainSampler;
}

namespace mirab::app
{

/// Opens a raster through a cache shared by the map, the reports and the
/// dialogs, so a file is opened once however many places use it.
std::shared_ptr<raster::Raster> openRaster(const QString& path, QString* error = nullptr);
/// Absolute path of a raster stored in the project settings.
QString resolveRasterPath(const ProjectDocument* doc, const std::string& stored);
/// Path to store in the settings: relative to the input file when it has been saved.
std::string storedRasterPath(const ProjectDocument* doc, const QString& absolute);
/// True when the layer settings describe an elevation model rather than an image.
bool isElevationLayer(const core::RasterLayerSettings& layer);
/// Ground elevation from the project's first elevation layer (a visible one
/// when there is one), in project units; nullptr when there is none.
std::shared_ptr<core::TerrainSampler> projectTerrain(const ProjectDocument* doc);
/// Ground from the project's elevation model along a chain of connected nodes
/// (following link vertices), as (profile distance, ground) points; the
/// profile distance of each node is given in @p distances. Empty without a DEM.
QVector<QPointF> terrainProfile(const ProjectDocument* doc, const std::vector<std::string>& nodes,
                                const std::vector<double>& distances);
/// File-dialog filter for rasters.
QString rasterFileFilter();

/// Draws the project's raster layers under the network, reprojected to the
/// model coordinate system at screen resolution. The picture is cached per
/// view; while the view changes the last picture is stretched and the new
/// one is drawn once the view settles.
class RasterRenderer : public QObject
{
    Q_OBJECT
public:
    struct View
    {
        double centerX = 0, centerY = 0;
        double scale = 1; ///< pixels per model unit
        QSize size;
        bool operator==(const View& o) const
        {
            return centerX == o.centerX && centerY == o.centerY && scale == o.scale &&
                   size == o.size;
        }
    };

    explicit RasterRenderer(ProjectDocument* doc, QObject* parent = nullptr);

    /// Re-reads the layers from the project settings.
    void reload();
    bool hasVisibleLayers() const;
    /// Problems found while opening the layers (missing files, unsupported formats).
    QStringList problems() const { return problems_; }
    /// Extent of the visible layers in model coordinates (approximate when reprojected).
    std::optional<QRectF> extent(const QString& modelCrs) const;

    void paint(QPainter& p, const QRect& area, const View& view, const QString& modelCrs);

signals:
    void updated();

private:
    struct Layer
    {
        core::RasterLayerSettings settings;
        std::shared_ptr<raster::Raster> raster;
        double low = 0, high = 1; ///< colour ramp range
    };
    QImage render(const View& view, const QString& modelCrs);
    QImage renderLayer(Layer& layer, const View& view, const QString& modelCrs);

    ProjectDocument* doc_;
    std::vector<Layer> layers_;
    QStringList problems_;
    QImage image_;
    View imageView_;
    QString imageKey_;
    int revision_ = 0;
    bool due_ = false;
    QTimer* timer_;
};

} // namespace mirab::app
