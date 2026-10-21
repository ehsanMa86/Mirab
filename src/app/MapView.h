/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/MapView.h
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
#pragma once

#include "ProjectDocument.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <optional>
#include <utility>
#include <vector>

namespace mirab::app
{

class Basemap;
class RasterRenderer;

class MapView : public QWidget
{
    Q_OBJECT
public:
    enum class Tool
    {
        Select,
        Pan,
        ZoomWindow,
        AddNode,
        AddLink,
        AddArea,
        AddGage
    };

    explicit MapView(ProjectDocument* doc, QWidget* parent = nullptr);

    void setTool(Tool tool, const QString& kind = {});
    Tool tool() const { return tool_; }
    void fitToWindow();
    void zoomBy(double factor);
    /// Centers the view on an element and makes sure it is visible.
    void zoomTo(const Selection& sel);

    void setNodeVariable(const QString& name);
    void setLinkVariable(const QString& name);
    void setAreaVariable(const QString& name);
    void setShowLabels(bool on);
    void setShowArrows(bool on);
    void setShowLegend(bool on);
    /// Loads an image with a world file (.pgw/.jgw/.wld) or fits it to the network.
    bool setBackdrop(const QString& imagePath, QString* error = nullptr);
    void clearBackdrop();

    // Web basemap
    static std::vector<std::pair<QString, QString>> basemapProviders();
    static QString basemapLabel(const QString& id);
    void setBasemap(const QString& id);
    /// Shows a basemap and records it (and a custom service's definition) in the project settings.
    void chooseBasemap(const QString& id);
    QString basemap() const;
    bool basemapUsable(QString* why = nullptr) const;
    /// Re-reads the raster layers from the project settings.
    void reloadRasters();
    /// Ground elevation (project units) from the project's DEM layer.
    std::optional<double> groundElevationAt(const QPointF& world) const;
    /// Problem with the current map service, empty when none.
    QString basemapError() const;

    /// Coordinate system of the model coordinates ("EPSG:32614", WKT, ...).
    void setCoordinateSystem(const QString& crs);
    QString coordinateSystemName() const;

    /// Snapshot of the map as currently displayed.
    QImage renderImage();
    /// Converts a screen position to model coordinates.
    QPointF worldAt(const QPoint& screen) const;

signals:
    void statusMessage(const QString& text);
    void cursorMoved(const QPointF& world);
    void toolFinished();
    void elementActivated();
    void contextMenuForSelection(const QPoint& globalPos);
    void contextMenuForMap(const QPoint& globalPos, const QPointF& world);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QPointF toScreen(const core::Point& p) const;
    QPointF toScreen(double x, double y) const { return toScreen(core::Point{x, y}); }
    core::Point toWorld(const QPointF& s) const;
    Selection hitTest(const QPointF& screen) const;
    void paintTo(QPainter& p, const QRect& area);
    void drawLegend(QPainter& p, const QRect& area);
    void drawScaleBar(QPainter& p, const QRect& area);
    void drawAttribution(QPainter& p, const QRect& area);
    void drawLabel(QPainter& p, const QPointF& at, const QString& text);
    QColor colorFor(double value) const;
    void recomputeRanges();
    /// Widens the legend range with the periods from @p fromPeriod on.
    void extendRanges(int fromPeriod);
    std::optional<double> resultValue(const char* table, int variable, const std::string& id) const;
    int flowVariable() const;
    void finishArea();
    void cancelPending();
    void userMovedView();
    QString prefixFor(const QString& kind) const;
    void updateCoordinateSystem();

    ProjectDocument* doc_;
    Basemap* basemap_;
    RasterRenderer* rasters_;
    QString crsText_;
    Tool tool_ = Tool::Select;
    QString kind_;
    double scale_ = 1.0;   ///< pixels per world unit
    core::Point center_;   ///< world point at the widget center
    bool fitted_ = false;
    bool userView_ = false; ///< the user zoomed or panned since the last fit

    // interaction state
    QPoint pressPos_;
    QPoint lastPos_;
    bool panning_ = false;
    bool dragging_ = false;
    bool rubberBand_ = false;
    QString dragNode_;
    core::Point dragTo_;
    QString linkStart_;
    std::vector<core::Point> pendingVertices_;
    std::vector<core::Point> pendingPolygon_;
    QPointF hoverScreen_;
    Selection hover_;

    // result coloring
    QString nodeVar_, linkVar_, areaVar_;
    int nodeVarIndex_ = -1, linkVarIndex_ = -1, areaVarIndex_ = -1;
    double rangeMin_ = 0.0, rangeMax_ = 1.0;
    QString legendTitle_;
    const char* rangeTable_ = nullptr; ///< table and variable the legend follows
    int rangeVar_ = -1;
    double rangeLo_ = 0.0, rangeHi_ = 0.0; ///< data extremes found so far
    bool showLabels_ = true;
    bool showArrows_ = true;
    bool showLegend_ = true;

    QImage backdrop_;
    QRectF backdropWorld_;
};

} // namespace mirab::app
