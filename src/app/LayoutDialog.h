/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/LayoutDialog.h
 * Purpose: Print layout: a page with the map, legend, scale bar, north arrow, texts and a report
 * chart.
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
#include "Reports.h"
#include "mirab/core/PrintLayout.h"

#include <QDialog>
#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPrinter;
class QSlider;
class QTimer;

namespace mirab::app
{

class MapView;

struct LayoutSettings
{
    core::layout::PageOptions page;
    QString title;
    QString subtitle;
    QString notes;
    QString logo; ///< image file shown in the header
    QString date;
    QString report; ///< report kind shown below the map (map with chart template)
    bool basemap = true;
    bool legend = true;
    bool scaleBar = true;
    bool northArrow = true;
    bool crsNote = true;

    /// Last used settings, with the title, subtitle and date of the current project and map.
    static LayoutSettings load(const ProjectDocument* doc, const MapView* map);
    void save() const;
};

/// Draws layout pages. Painter units are 1/96 inch; callers scale the painter
/// to the device resolution, so the same code makes the preview, PDF, PNG and print.
class PageComposer
{
public:
    using ReportSource = std::function<ReportSnapshot(const QString& kind)>;
    PageComposer(MapView* map, ProjectDocument* doc, ReportSource reports);

    /// Page size in painter units.
    QSizeF pageSize(const LayoutSettings& s) const;
    /// Paints the page. @p pixelRatio is the number of device pixels per unit;
    /// @p vector draws the map background as one image (PDF and print).
    void paint(QPainter& p, const LayoutSettings& s, double pixelRatio, bool vector = false);
    /// Requests the basemap tiles of the map frame; true while some are loading.
    bool basemapLoading(const LayoutSettings& s, double pixelRatio);
    /// Waits for the basemap tiles of the map frame.
    void waitForBasemap(const LayoutSettings& s, double pixelRatio);

    bool exportPdf(const QString& path, const LayoutSettings& s, QString* error);
    bool exportImage(const QString& path, const LayoutSettings& s, int dpi, QString* error);
    void print(QPrinter& printer, const LayoutSettings& s);

private:
    QRectF mapRect(const LayoutSettings& s) const;
    const ReportSnapshot& report(const QString& kind);
    void paintSidebar(QPainter& p, const LayoutSettings& s, const QRectF& side);
    void paintReport(QPainter& p, const LayoutSettings& s, const QRectF& box);
    void paintFooter(QPainter& p, const LayoutSettings& s, const QRectF& footer, const QRectF& map);

    MapView* map_;
    ProjectDocument* doc_;
    ReportSource reports_;
    QString cachedKind_;
    ReportSnapshot cached_;
};

/// Paints a table of a report snapshot into @p box; returns the number of rows shown.
int paintTable(QPainter& p, const QRectF& box, const ReportSnapshot& s, const QFont& f);

/// Shows the page as it will be printed.
class LayoutPreview : public QWidget
{
    Q_OBJECT
public:
    explicit LayoutPreview(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    QSize sizeHint() const override { return {640, 460}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage image_;
};

class LayoutDialog : public QDialog
{
    Q_OBJECT
public:
    LayoutDialog(MapView* map, ProjectDocument* doc, PageComposer::ReportSource reports,
                 QWidget* parent = nullptr);
    LayoutSettings settings() const;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void schedulePreview();
    void renderPreview();
    void exportPdf();
    void exportPng();
    void print();

    MapView* map_;
    ProjectDocument* doc_;
    PageComposer composer_;
    QComboBox* template_;
    QComboBox* paper_;
    QComboBox* orientation_;
    QSlider* split_;
    QLineEdit* title_;
    QLineEdit* subtitle_;
    QLineEdit* date_;
    QPlainTextEdit* notes_;
    QLineEdit* logo_;
    QComboBox* report_;
    QCheckBox* basemap_;
    QCheckBox* legend_;
    QCheckBox* scaleBar_;
    QCheckBox* north_;
    QCheckBox* crs_;
    QComboBox* dpi_;
    QLabel* status_;
    LayoutPreview* preview_;
    QTimer* timer_;
    int retries_ = 0;
};

} // namespace mirab::app
