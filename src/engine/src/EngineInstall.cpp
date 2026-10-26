/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/src/EngineInstall.cpp
 * Purpose: Verifies and installs engine packages described by "mirab-engine/1" manifests.
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
#include "mirab/engine/EngineInstall.h"

#include "mirab/core/Hash.h"
#include "mirab/core/Json.h"
#include "mirab/core/Text.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mirab::engine
{

std::string platformId()
{
#if defined(__x86_64__) || defined(_M_X64)
    const char* arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const char* arch = "arm64";
#else
    const char* arch = "unknown";
#endif
#if defined(_WIN32)
    return std::string("windows-") + arch;
#elif defined(__APPLE__)
    return std::string("macos-") + arch;
#else
    return std::string("linux-") + arch;
#endif
}

std::filesystem::path userEngineDirectory()
{
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"))
        return std::filesystem::u8path(local) / "Mirab" / "engines";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::u8path(home) / "Library" / "Application Support" / "Mirab" / "engines";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::filesystem::u8path(xdg) / "mirab" / "engines";
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::u8path(home) / ".local" / "share" / "mirab" / "engines";
#endif
    return std::filesystem::temp_directory_path() / "mirab-engines";
}

std::string defaultSwmm5PlusManifestUrl()
{
    if (const char* env = std::getenv("MIRAB_SWMM5PLUS_MANIFEST_URL"); env && *env)
        return env;
    return "https://github.com/ehsanMa86/swmm5plus-capi/releases/latest/download/swmm5plus-capi-" + platformId() +
           ".json";
}

EngineManifest EngineManifest::parse(const std::string& textValue)
{
    json::Value v;
    try
    {
        v = json::Value::parse(textValue);
    }
    catch (const json::ParseError& e)
    {
        throw std::runtime_error(std::string("the engine manifest is not valid JSON: ") + e.what());
    }
    if (v.get("format").asString() != "mirab-engine/1")
        throw std::runtime_error("unsupported engine manifest format \"" + v.get("format").asString() + "\"");
    EngineManifest m;
    m.engine = v.get("engine").asString();
    // The id names the install record inside the engine folder, so it must be
    // a plain name: letters, digits, ".", "_" and "-", and not "." or "..".
    if (m.engine.empty() || m.engine == "." || m.engine == ".." ||
        m.engine.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") !=
            std::string::npos)
        throw std::runtime_error("the engine manifest has an invalid engine id \"" + m.engine + "\"");
    m.name = v.get("name").asString(m.engine);
    m.version = v.get("version").asString();
    m.packageVersion = v.get("packageVersion").asString();
    m.platform = v.get("platform").asString();
    m.apiVersion = static_cast<int>(v.get("apiVersion").asNumber());
    for (const auto& f : v.get("files").items())
    {
        File file;
        file.name = f.get("name").asString();
        file.asset = f.get("asset").asString(file.name);
        file.size = static_cast<long long>(f.get("size").asNumber(-1));
        file.sha256 = text::toLower(f.get("sha256").asString());
        // File names must be plain names: never paths that could escape the engine folder.
        if (file.name.empty() || file.name.find_first_of("/\\:") != std::string::npos || file.name == "." ||
            file.name == ".." || file.asset.find_first_of("/\\:") != std::string::npos)
            throw std::runtime_error("the engine manifest contains an invalid file name \"" + file.name + "\"");
        if (file.sha256.size() != 64)
            throw std::runtime_error("the engine manifest lacks a SHA-256 checksum for " + file.name);
        m.files.push_back(file);
    }
    if (m.engine.empty() || m.files.empty())
        throw std::runtime_error("the engine manifest does not list an engine and its files");
    return m;
}

EngineManifest EngineManifest::load(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + file.u8string());
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse(buf.str());
}

void installFromManifest(const EngineManifest& m, const std::filesystem::path& sourceDir,
                         const std::filesystem::path& targetDir)
{
    namespace fs = std::filesystem;
    if (m.platform != platformId())
        throw std::runtime_error("this " + m.name + " package is for " + m.platform + ", but this computer is " +
                                 platformId());
    if (m.engine == "swmm5plus-1.0" && m.apiVersion != 1 && m.apiVersion != 2)
        throw std::runtime_error("this " + m.name + " package uses C interface version " +
                                 std::to_string(m.apiVersion) + "; this Mirab supports versions 1 and 2");

    // 1. Verify everything before touching the engine folder.
    for (const auto& f : m.files)
    {
        const fs::path src = sourceDir / fs::u8path(f.asset);
        std::error_code ec;
        if (!fs::is_regular_file(src, ec))
            throw std::runtime_error("missing file " + f.asset);
        if (f.size >= 0 && static_cast<long long>(fs::file_size(src)) != f.size)
            throw std::runtime_error(f.asset + " has the wrong size; the download may be incomplete");
        if (core::sha256File(src) != f.sha256)
            throw std::runtime_error(f.asset + " failed its SHA-256 check; it was not installed");
    }

    // 2. Copy through temporary names, then rename into place.
    fs::create_directories(targetDir);
    for (const auto& f : m.files)
    {
        const fs::path tmp = targetDir / fs::u8path(f.name + ".part");
        fs::copy_file(sourceDir / fs::u8path(f.asset), tmp, fs::copy_options::overwrite_existing);
    }
    json::Value record = json::Value::object();
    record.set("format", "mirab-engine-installed/1");
    record.set("engine", m.engine);
    record.set("name", m.name);
    record.set("version", m.version);
    record.set("packageVersion", m.packageVersion);
    record.set("platform", m.platform);
    json::Value names = json::Value::array();
    for (const auto& f : m.files)
    {
        const fs::path tmp = targetDir / fs::u8path(f.name + ".part");
        const fs::path dst = targetDir / fs::u8path(f.name);
        std::error_code ec;
        fs::remove(dst, ec);
        fs::rename(tmp, dst);
        names.push(f.name);
    }
    record.set("files", names);
    std::ofstream out(targetDir / fs::u8path(m.engine + ".installed.json"), std::ios::binary | std::ios::trunc);
    out << record.dump(2) << "\n";
}

bool uninstallEngine(const std::string& engineId, const std::filesystem::path& targetDir)
{
    namespace fs = std::filesystem;
    const fs::path recordFile = targetDir / fs::u8path(engineId + ".installed.json");
    std::ifstream in(recordFile, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    in.close();
    const json::Value record = json::Value::parse(buf.str());
    for (const auto& name : record.get("files").items())
    {
        const std::string n = name.asString();
        if (n.empty() || n.find_first_of("/\\:") != std::string::npos)
            continue;
        std::error_code ec;
        fs::remove(targetDir / fs::u8path(n), ec);
    }
    std::error_code ec;
    fs::remove(recordFile, ec);
    return true;
}

} // namespace mirab::engine
