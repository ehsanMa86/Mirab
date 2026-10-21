/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ReportExport.h
 * Purpose: Report document: project summary, simulation status, map and report tabs as PDF or HTML.
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

#include "ChartWidget.h"
#include "LayoutDialog.h"
#include "ProjectDocument.h"

#include <QCoreApplication>
#include <QDialog>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace mirab::app
{

class MapView;

struct ReportOptions
{
    QString title;
    QString author;
    QString engine; ///< engine chosen for the project, shown in the summary
    bool summary = true;
    bool status = true;
    bool map = true;
    QStringList reports; ///< report kinds, in order
    int maxRows = 200;   ///< table rows per report
    std::string paper = "A4";
    bool landscape = false;
};

/// Builds the report document and writes it as PDF (charts and the map as
/// vector graphics, with a table of contents) or as one self-contained HTML file.
class ReportBuilder
{
    Q_DECLARE_TR_FUNCTIONS(ReportBuilder)
public:
    ReportBuilder(MapView* map, ProjectDocument* doc, PageComposer::ReportSource reports);
    bool exportPdf(const QString& path, const ReportOptions& options, QString* error = nullptr);
    bool exportHtml(const QString& path, const ReportOptions& options, QString* error = nullptr);
    /// Report kinds included by default.
    static QStringList defaultReports(core::Dialect dialect);

    struct Figure
    {
        bool map = false;
        ChartWidget::Data chart;
        QSizeF size; ///< painter units (1/96 inch)
    };
    struct Section
    {
        QString title;
        QString body; ///< HTML; figures appear as <!--figure:N-->
    };

private:
    void build(const ReportOptions& o);
    QString document(const ReportOptions& o, bool pdf) const;
    QString figureHtml(int index, bool pdf) const;

    MapView* map_;
    ProjectDocument* doc_;
    PageComposer::ReportSource reports_;
    QVector<Section> sections_;
    QVector<Figure> figures_;
    double contentWidth_ = 680; ///< painter units
    double contentHeight_ = 900;
    // PDF pagination found by trial layouts
    QStringList tocPages_;        ///< page of each section
    QVector<bool> sectionBreaks_; ///< sections that start on a new page
    QVector<bool> figureBreaks_;  ///< figures that start on a new page
};

/// Draws a report figure (chart, or the map with its legend) into @p rect;
/// @p vector draws the map background as one image (PDF).
void paintFigure(QPainter& p, const QRectF& rect, const ReportBuilder::Figure& figure, MapView* map,
                 double pixelRatio, bool vector = false);

class ReportDialog : public QDialog
{
    Q_OBJECT
public:
    ReportDialog(MapView* map, ProjectDocument* doc, PageComposer::ReportSource reports,
                 const QString& engine, QWidget* parent = nullptr);
    ReportOptions options() const;

private:
    void exportAs(bool pdf);

    MapView* map_;
    ProjectDocument* doc_;
    PageComposer::ReportSource reports_;
    QString engine_;
    QLineEdit* title_;
    QLineEdit* author_;
    QListWidget* sections_;
    QSpinBox* rows_;
    QComboBox* paper_;
    QComboBox* orientation_;
    QLabel* status_;
};

} // namespace mirab::app
