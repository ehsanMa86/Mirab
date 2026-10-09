/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/MapView.h
 * Purpose: Interactive network map: drawing, selection, editing tools and result coloring.
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
#include <vector>

namespace anahita::app
{

class MapView : public QWidget
{
    Q_OBJECT
public:
    enum class Tool
    {
        Select,
        Pan,
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

    void setNodeVariable(const QString& name);
    void setLinkVariable(const QString& name);
    void setAreaVariable(const QString& name);
    void setShowLabels(bool on);
    /// Loads an image with a world file (.pgw/.jgw/.wld) or fits it to the network.
    bool setBackdrop(const QString& imagePath, QString* error = nullptr);
    void clearBackdrop();

    /// Snapshot of the map as currently displayed.
    QImage renderImage();

signals:
    void statusMessage(const QString& text);
    void cursorMoved(const QPointF& world);
    void toolFinished();
    void elementActivated();
    void contextMenuForSelection(const QPoint& globalPos);

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
    QColor colorFor(double value) const;
    void recomputeRanges();
    std::optional<double> resultValue(const char* table, int variable, const std::string& id) const;
    void finishArea();
    void cancelPending();
    QString prefixFor(const QString& kind) const;

    ProjectDocument* doc_;
    Tool tool_ = Tool::Select;
    QString kind_;
    double scale_ = 1.0;   ///< pixels per world unit
    core::Point center_;   ///< world point at the widget center
    bool fitted_ = false;

    // interaction state
    QPoint pressPos_;
    QPoint lastPos_;
    bool panning_ = false;
    bool dragging_ = false;
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
    bool showLabels_ = true;

    QImage backdrop_;
    QRectF backdropWorld_;
};

} // namespace anahita::app
