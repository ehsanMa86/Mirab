/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/PrintLayout.cpp
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
#include "mirab/core/PrintLayout.h"

#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>

namespace mirab::core::layout
{

const std::vector<Paper>& papers()
{
    static const std::vector<Paper> list = {
        {"A4", 210.0, 297.0},
        {"A3", 297.0, 420.0},
        {"Letter", 215.9, 279.4},
        {"Tabloid", 279.4, 431.8},
    };
    return list;
}

const Paper* findPaper(std::string_view id)
{
    for (const auto& p : papers())
    {
        if (text::toUpper(p.id) == text::toUpper(id))
            return &p;
    }
    return nullptr;
}

PageFrames computeFrames(const PageOptions& o)
{
    const Paper* paper = findPaper(o.paper);
    if (!paper)
        paper = &papers().front();
    PageFrames f;
    f.widthMm = o.landscape ? paper->heightMm : paper->widthMm;
    f.heightMm = o.landscape ? paper->widthMm : paper->heightMm;
    const double margin = std::clamp(o.marginMm, 0.0, std::min(f.widthMm, f.heightMm) / 4.0);
    const double gap = 4.0;
    const double split = std::clamp(o.split, 0.4, 0.9);
    // Text heights grow a little with the paper so A3 titles do not look lost.
    const double size = std::sqrt((f.widthMm * f.heightMm) / (297.0 * 210.0));
    const double headerH = (o.subtitle ? 17.0 : 11.0) * std::clamp(size, 1.0, 1.4);
    const double footerH = 6.0;
    const double left = margin, top = margin, width = f.widthMm - 2 * margin;
    f.header = {left, top, width, headerH};
    f.footer = {left, f.heightMm - margin - footerH, width, footerH};
    const RectMm body{left, top + headerH + gap, width, f.footer.y - gap - (top + headerH + gap)};
    if (o.layout == Template::LegendSidebar)
    {
        const double mapW = (body.w - gap) * split;
        f.map = {body.x, body.y, mapW, body.h};
        f.side = {body.x + mapW + gap, body.y, body.w - mapW - gap, body.h};
    }
    else
    {
        const double mapH = (body.h - gap) * split;
        f.map = {body.x, body.y, body.w, mapH};
        f.chart = {body.x, body.y + mapH + gap, body.w, body.h - mapH - gap};
    }
    return f;
}

double niceLength(double maxLength)
{
    if (!(maxLength > 0) || !std::isfinite(maxLength))
        return 0.0;
    const double mag = std::pow(10.0, std::floor(std::log10(maxLength)));
    for (double m : {5.0, 2.0, 1.0})
    {
        if (m * mag <= maxLength * (1 + 1e-12))
            return m * mag;
    }
    return mag;
}

double roundScale(double denominator)
{
    if (!(denominator > 0) || !std::isfinite(denominator))
        return 0.0;
    const double mag = std::pow(10.0, std::floor(std::log10(denominator)) - 1);
    return std::max(1.0, std::round(denominator / mag) * mag);
}

std::string groupDigits(double value)
{
    std::string digits = std::to_string(static_cast<long long>(std::llround(std::fabs(value))));
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i)
    {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return (value < 0 ? "-" : "") + out;
}

} // namespace mirab::core::layout

namespace mirab::core
{

namespace
{
void countKind(std::vector<std::pair<std::string, int>>& list, const std::string& kind)
{
    for (auto& [k, n] : list)
    {
        if (k == kind)
        {
            ++n;
            return;
        }
    }
    list.emplace_back(kind, 1);
}
} // namespace

ProjectSummary summarizeProject(const Model& model, const ProjectSettings& settings)
{
    ProjectSummary s;
    s.title = model.title();
    s.dialect = model.dialect();
    s.units = model.units();
    for (const auto& n : model.nodes())
        countKind(s.nodeKinds, n.kind);
    for (const auto& l : model.links())
        countKind(s.linkKinds, l.kind);
    s.subcatchments = static_cast<int>(model.areas().size());
    s.rainGages = static_cast<int>(model.gages().size());
    s.durationSeconds = model.durationSeconds();
    s.reportStepSeconds = model.reportStepSeconds();
    s.hydraulicStepSeconds = model.hydraulicStepSeconds();
    if (model.dialect() == Dialect::Epanet)
        s.demandModel = model.demandModel();
    s.crs = settings.crs;
    s.engineId = settings.engineId;
    return s;
}

RunSummary summarizeRun(const ResultSet& results)
{
    RunSummary s;
    s.engineId = results.engineId;
    s.engineName = results.engineName;
    s.periods = results.periodCount();
    if (!results.times.empty())
    {
        s.firstSeconds = results.times.front();
        s.lastSeconds = results.times.back();
    }
    s.reportStepSeconds = results.reportStep > 0     ? results.reportStep
                          : results.times.size() > 1 ? results.times[1] - results.times[0]
                                                     : 0.0;
    const std::string suffix = "ContinuityErrorPct";
    for (const auto& [key, value] : results.metadata)
    {
        if (key.size() <= suffix.size() ||
            key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        if (const auto v = text::toDouble(value))
            s.continuity.emplace_back(key.substr(0, key.size() - suffix.size()), *v);
    }
    // Water balance first, then flow, then quality, as engines report them.
    auto rank = [](const std::string& name)
    {
        return name == "runoff" ? 0 : name == "flow" ? 1 : name == "quality" ? 2 : 3;
    };
    std::stable_sort(s.continuity.begin(), s.continuity.end(),
                     [&](const auto& a, const auto& b) { return rank(a.first) < rank(b.first); });
    s.messages = results.messages;
    return s;
}

} // namespace mirab::core
