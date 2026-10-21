/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/PrintLayout.h
 * Purpose: Page geometry of print layouts, map scale helpers and the project summary of reports.
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

#include "mirab/core/Model.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Results.h"
#include "mirab/core/Units.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mirab::core::layout
{

struct Paper
{
    std::string id;  ///< "A4", "A3", "Letter", "Tabloid"
    double widthMm;  ///< portrait width
    double heightMm; ///< portrait height
};

/// Paper sizes offered for layouts.
const std::vector<Paper>& papers();
const Paper* findPaper(std::string_view id);

enum class Template
{
    LegendSidebar, ///< map on the left, legend, scale bar, north arrow and notes on the right
    ChartBelow     ///< map on top with inset legend, a chart or table from a report below
};

struct RectMm
{
    double x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    double right() const { return x + w; }
    double bottom() const { return y + h; }
};

struct PageOptions
{
    std::string paper = "A4";
    bool landscape = true;
    Template layout = Template::LegendSidebar;
    double marginMm = 10.0;
    /// Share of the body given to the map: its width beside the sidebar, or
    /// its height above the chart. Clamped to 0.4 - 0.9.
    double split = 0.75;
    bool subtitle = true;
};

/// Frames of a page in millimetres from the top-left corner of the paper.
struct PageFrames
{
    double widthMm = 0, heightMm = 0;
    RectMm header; ///< title and subtitle
    RectMm map;
    RectMm side;   ///< sidebar (LegendSidebar) - empty otherwise
    RectMm chart;  ///< chart or table (ChartBelow) - empty otherwise
    RectMm footer; ///< coordinate system, scale and date
};

PageFrames computeFrames(const PageOptions& options);

/// Largest 1, 2 or 5 x 10^n that does not exceed @p maxLength (0 for invalid input).
double niceLength(double maxLength);
/// Scale denominator rounded to two significant digits (12,345 -> 12,000).
double roundScale(double denominator);
/// "12,000": thousands separated by commas.
std::string groupDigits(double value);

} // namespace mirab::core::layout

namespace mirab::core
{

/// Facts about a project shown at the start of a report.
struct ProjectSummary
{
    std::string title;
    Dialect dialect = Dialect::Epanet;
    ProjectUnits units;
    std::vector<std::pair<std::string, int>> nodeKinds; ///< element type and count, in model order
    std::vector<std::pair<std::string, int>> linkKinds;
    int subcatchments = 0;
    int rainGages = 0;
    double durationSeconds = 0;
    double reportStepSeconds = 0;
    double hydraulicStepSeconds = 0;
    std::string demandModel; ///< EPANET: DDA or PDA
    std::string crs;         ///< coordinate system text from the project settings
    std::string engineId;    ///< engine chosen for the project
};

ProjectSummary summarizeProject(const Model& model, const ProjectSettings& settings);

/// Facts about a finished simulation.
struct RunSummary
{
    std::string engineId;
    std::string engineName;
    int periods = 0;
    double firstSeconds = 0;
    double lastSeconds = 0;
    double reportStepSeconds = 0;
    /// Continuity errors in percent, by name ("runoff", "flow", "quality").
    std::vector<std::pair<std::string, double>> continuity;
    std::vector<std::string> messages;
};

RunSummary summarizeRun(const ResultSet& results);

} // namespace mirab::core
