/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/ProjectSettings.cpp
 * Purpose: Reads and writes the per-project settings sidecar file.
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
#include "mirab/core/ProjectSettings.h"

#include "mirab/core/Json.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mirab::core
{

std::filesystem::path ProjectSettings::sidecarPath(const std::filesystem::path& inpFile)
{
    std::filesystem::path p = inpFile;
    p += ".mirab.json";
    return p;
}

ProjectSettings ProjectSettings::loadFor(const std::filesystem::path& inpFile)
{
    ProjectSettings s;
    std::ifstream in(sidecarPath(inpFile), std::ios::binary);
    if (!in)
        return s;
    std::ostringstream buf;
    buf << in.rdbuf();
    try
    {
        const json::Value v = json::Value::parse(buf.str());
        s.engineId = v.get("engine").asString();
        s.useMsx = v.get("useMsx").asBool();
        s.msxFile = v.get("msxFile").asString();
        s.crs = v.get("crs").asString();
        s.backdropImage = v.get("backdropImage").asString();
        s.basemap = v.get("basemap").asString();
        s.pressureThreshold = v.get("pressureThreshold").asNumber();
        for (const auto& r : v.get("rasters").items())
        {
            RasterLayerSettings layer;
            layer.path = r.get("path").asString();
            layer.style = r.get("style").asString("hillshade");
            layer.opacity = r.get("opacity").asNumber(0.7);
            layer.visible = r.get("visible").asBool(true);
            layer.verticalUnit = r.get("verticalUnit").asNumber(0.0);
            layer.exaggeration = r.get("exaggeration").asNumber(1.0);
            if (!layer.path.empty())
                s.rasters.push_back(layer);
        }
        if (const json::Value* m = v.find("mapService"); m && m->isObject())
        {
            MapServiceSettings& ms = s.mapService;
            ms.id = m->get("id").asString();
            ms.name = m->get("name").asString();
            ms.type = m->get("type").asString();
            ms.url = m->get("url").asString();
            ms.layer = m->get("layer").asString();
            ms.style = m->get("style").asString();
            ms.format = m->get("format").asString();
            ms.crs = m->get("crs").asString();
            ms.matrixSet = m->get("matrixSet").asString();
            ms.version = m->get("version").asString();
            ms.attribution = m->get("attribution").asString();
        }
        if (v.get("calibration").isObject())
            s.calibration = v.get("calibration");
        s.scenarios = scenariosFromJson(v.get("scenarios"));
    }
    catch (const json::ParseError&)
    {
        return ProjectSettings{};
    }
    return s;
}

void ProjectSettings::saveFor(const std::filesystem::path& inpFile) const
{
    json::Value v = json::Value::object();
    v.set("format", "mirab-project-settings/1");
    v.set("engine", engineId);
    v.set("useMsx", useMsx);
    v.set("msxFile", msxFile);
    v.set("crs", crs);
    v.set("backdropImage", backdropImage);
    if (!basemap.empty())
        v.set("basemap", basemap);
    v.set("pressureThreshold", pressureThreshold);
    if (!rasters.empty())
    {
        json::Value list = json::Value::array();
        for (const auto& r : rasters)
        {
            json::Value o = json::Value::object();
            o.set("path", r.path);
            o.set("style", r.style);
            o.set("opacity", r.opacity);
            o.set("visible", r.visible);
            if (r.verticalUnit > 0)
                o.set("verticalUnit", r.verticalUnit);
            if (r.exaggeration != 1.0)
                o.set("exaggeration", r.exaggeration);
            list.push(o);
        }
        v.set("rasters", list);
    }
    if (mapService.valid() && basemap == mapService.id)
    {
        json::Value m = json::Value::object();
        for (const auto& [key, value] :
             {std::pair<const char*, const std::string*>{"id", &mapService.id}, {"name", &mapService.name},
              {"type", &mapService.type}, {"url", &mapService.url}, {"layer", &mapService.layer},
              {"style", &mapService.style}, {"format", &mapService.format}, {"crs", &mapService.crs},
              {"matrixSet", &mapService.matrixSet}, {"version", &mapService.version},
              {"attribution", &mapService.attribution}})
        {
            if (!value->empty())
                m.set(key, *value);
        }
        v.set("mapService", m);
    }
    if (calibration.isObject())
        v.set("calibration", calibration);
    if (!scenarios.empty())
        v.set("scenarios", scenariosToJson(scenarios));
    std::ofstream out(sidecarPath(inpFile), std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + sidecarPath(inpFile).u8string());
    out << v.dump(2) << "\n";
}

} // namespace mirab::core
