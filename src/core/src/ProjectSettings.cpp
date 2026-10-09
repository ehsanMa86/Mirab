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
    std::ofstream out(sidecarPath(inpFile), std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write " + sidecarPath(inpFile).u8string());
    out << v.dump(2) << "\n";
}

} // namespace mirab::core
