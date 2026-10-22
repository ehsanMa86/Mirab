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

#include <QFont>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <map>
#include <memory>
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
        AddGage,
        SelectPolygon, ///< lasso: click corners or drag freehand
        MeasureLength,
        MeasureArea,
        SplitLink ///< split a pipe or conduit where it is clicked
    };

    explicit MapView(ProjectDocument* doc, QWidget* parent = nullptr);

    void setTool(Tool tool, const QString& kind = {});
    Tool tool() const { return tool_; }
    void fitToWindow();
    void zoomBy(double factor);
    /// Centers the view on an element and makes sure it is visible.
    void zoomTo(const Selection& sel);
    /// Fits the view to every selected element.
    void zoomToSelection();
    /// Selects the elements inside a polygon (map coordinates). Shift adds
    /// to the selection and Ctrl removes from it.
    void selectInPolygon(const std::vector<core::Point>& polygon, bool crossing,
                         Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    void setNodeVariable(const QString& name);
    void setLinkVariable(const QString& name);
    void setAreaVariable(const QString& name);
    void setShowLabels(bool on);
    void setShowArrows(bool on);
    void setShowLegend(bool on);
    /// Colors by the shown results minus @p reference at the same time (for
    /// links, the difference of magnitudes); null shows the results again.
    void setDifference(std::shared_ptr<const core::ResultSet> reference, const QString& label);
    bool showsDifference() const { return reference_ != nullptr; }
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

    /// Lines and points in model coordinates drawn with the network, for
    /// reference layers and previews (another model, links between models).
    struct Overlay
    {
        std::vector<std::vector<core::Point>> lines;
        std::vector<core::Point> points;
        QColor lineColor = QColor(120, 128, 140);
        double lineWidth = 1.0; ///< painter units
        Qt::PenStyle lineStyle = Qt::SolidLine;
        QColor pointColor = QColor(120, 128, 140);
        double pointSize = 3.0;  ///< diameter in painter units
        bool aboveLinks = false; ///< over the links (under the nodes) instead of under the network
        bool inExports = false;  ///< also drawn by render(), otherwise on screen only
    };
    /// Adds or replaces the overlay called @p name.
    void setOverlay(const QString& name, Overlay overlay);
    void removeOverlay(const QString& name);
    bool hasOverlay(const QString& name) const { return overlays_.count(name) > 0; }

    /// Coordinate system of the model coordinates ("EPSG:32614", WKT, ...).
    void setCoordinateSystem(const QString& crs);
    QString coordinateSystemName() const;

    /// Snapshot of the map as currently displayed.
    QImage renderImage();

    /// What render() draws. Sizes are in painter units; a unit is a screen
    /// pixel at 96 dpi, and the painter is scaled for print resolutions.
    struct RenderOptions
    {
        int period = -1;        ///< reporting period; -1 follows the time slider
        bool background = true; ///< background, basemap and backdrop image
        bool basemap = true;    ///< web basemap tiles, when one is chosen
        bool network = true;
        bool overlays = false;   ///< the on-screen legend, scale bar and attribution
        bool highlights = false; ///< selection, hover and pending edits
        bool screenOverlays = false; ///< overlays (setOverlay) that are not marked for exports
        QFont font;              ///< label font; the widget font when not set
        double pixelRatio = 1.0; ///< device pixels per painter unit
        /// Basemap tile pixels per painter unit: 1 matches the screen; higher
        /// values give sharper imagery with smaller labels.
        double tileDetail = 1.0;
        /// Draws the background as one image at pixelRatio (smaller PDF files
        /// than one image per basemap tile); the network stays vector.
        bool rasterBackground = false;
    };
    /// Draws the current view into @p target, keeping the visible extent.
    void render(QPainter& p, const QRectF& target, const RenderOptions& options);
    /// Requests the basemap tiles render() needs; true while some are still loading.
    bool prepareBasemap(const QRectF& target, const RenderOptions& options);
    /// Waits (processing events) until the basemap tiles are loaded or the time runs out.
    void waitForBasemap(const QRectF& target, const RenderOptions& options, int timeoutMs = 20000);

    struct LegendClass
    {
        QColor color;
        QString label;
    };
    struct Legend
    {
        QString title; ///< result and unit; empty when the map shows no results
        QVector<LegendClass> classes;
    };
    /// Color classes of the result shown on the map.
    Legend legend() const;
    /// Element types drawn on the map, for a symbol legend.
    QStringList elementKinds() const;
    /// Draws the map symbol of an element type centred on @p c.
    static void drawSymbol(QPainter& p, const QPointF& c, double size, const QString& kind);
    /// Ground length of one painter unit when the view is fitted into @p target;
    /// @p unit receives "m" or "ft". 0 when unknown.
    double groundPerUnit(const QRectF& target, QString* unit);
    /// Clockwise angle in degrees from the top of the map to true north (0 when unknown).
    double northAngle() const;
    /// Copyright notice of the basemap, when one is shown.
    QString attribution() const;
    /// Colors of the result classes, from low to high.
    QVector<QColor> rampColors() const;
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
    struct ViewScope;
    QRectF viewRect() const { return viewOverride_ ? *viewOverride_ : QRectF(rect()); }
    int currentPeriod() const { return periodOverride_ >= 0 ? periodOverride_ : doc_->period(); }
    double groundPerPixel(const QPointF& a, QString* unit) const;
    QPointF toScreen(const core::Point& p) const;
    QPointF toScreen(double x, double y) const { return toScreen(core::Point{x, y}); }
    core::Point toWorld(const QPointF& s) const;
    Selection hitTest(const QPointF& screen) const;
    void paintTo(QPainter& p, const QRectF& area, const RenderOptions& o);
    void drawLegend(QPainter& p, const QRectF& area);
    void drawScaleBar(QPainter& p, const QRectF& area);
    void drawAttribution(QPainter& p, const QRectF& area);
    void drawLabel(QPainter& p, const QPointF& at, const QString& text);
    void drawOverlays(QPainter& p, const QRectF& area, const RenderOptions& o, bool aboveLinks);
    QColor colorFor(double value) const;
    /// Lower edge of legend class @p i (0 to 4); class 5 is the top of the range.
    double classEdge(int i) const;
    void recomputeRanges();
    /// Widens the legend range with the periods from @p fromPeriod on.
    void extendRanges(int fromPeriod);
    /// Value used for coloring: the result, or its difference from the reference.
    std::optional<double> resultValue(const char* table, int variable, const std::string& id) const;
    std::optional<double> rawValue(const char* table, int variable, const std::string& id) const;
    /// Reference value of an element at the reference period matching @p period.
    std::optional<double> referenceValue(const char* table, int variable, int element, int period) const;
    const QColor* ramp() const;
    int flowVariable() const;
    void finishArea();
    void finishLasso();
    void cancelPending();
    void zoomToBounds(const core::Bounds& b);
    void includeElement(core::Bounds& b, const Selection& sel) const;
    void clickSelect(const Selection& hit, Qt::KeyboardModifiers modifiers);
    void splitAt(const QPointF& screen);
    /// The link under the cursor and the point of it closest to the cursor, for the split tool.
    bool splitTarget(const QPointF& screen, QString& link, core::Point& at) const;
    QString measureText() const;
    QString formatLength(double value, bool metres) const;
    QString formatArea(double value, bool metres) const;
    void drawOverlayText(QPainter& p, const QPointF& at, const QString& text);
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
    bool selectBand_ = false; ///< rectangle selection with the select tool
    Selection bandClick_;     ///< element to select when the rectangle turns out to be a click
    Qt::KeyboardModifiers pressModifiers_;
    std::vector<core::Point> lasso_;
    bool lassoDragged_ = false;
    std::vector<core::Point> measure_;
    bool measureDone_ = false;
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

    std::shared_ptr<const core::ResultSet> reference_;
    QString referenceLabel_;

    QImage backdrop_;
    QRectF backdropWorld_;
    std::map<QString, Overlay> overlays_;

    // render() state
    QFont paintFont_;
    std::optional<QRectF> viewOverride_;
    int periodOverride_ = -1;
    bool highlights_ = true;
    double detail_ = 1.0;     ///< basemap tile detail
    double pixelRatio_ = 1.0; ///< device pixels per painter unit (raster layers)
};

} // namespace mirab::app
