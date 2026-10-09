/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/engine/src/DynamicLibrary.cpp
 * Purpose: LoadLibraryEx/dlopen wrapper used to load simulation engines.
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
#include "anahita/engine/DynamicLibrary.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <vector>

namespace anahita::engine
{

DynamicLibrary::~DynamicLibrary()
{
    close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_), path_(std::move(other.path_))
{
    other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
{
    if (this != &other)
    {
        close();
        handle_ = other.handle_;
        path_ = std::move(other.path_);
        other.handle_ = nullptr;
    }
    return *this;
}

bool DynamicLibrary::open(const std::filesystem::path& path, std::string* error)
{
    close();
#if defined(_WIN32)
    const std::wstring wpath = std::filesystem::absolute(path).wstring();
    HMODULE h = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h)
    {
        if (error)
            *error = "cannot load " + path.u8string() + " (Windows error " +
                     std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return false;
    }
    handle_ = reinterpret_cast<void*>(h);
#else
    void* h = dlopen(std::filesystem::absolute(path).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h)
    {
        if (error)
        {
            const char* msg = dlerror();
            *error = "cannot load " + path.u8string() + (msg ? std::string(": ") + msg : std::string());
        }
        return false;
    }
    handle_ = h;
#endif
    path_ = path;
    return true;
}

void DynamicLibrary::close()
{
    if (!handle_)
        return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

void* DynamicLibrary::symbol(const char* name) const
{
    if (!handle_)
        return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
}

std::string DynamicLibrary::fileName(const std::string& stem, bool withLibPrefix)
{
    const std::string base = (withLibPrefix ? "lib" : "") + stem;
#if defined(_WIN32)
    return base + ".dll";
#elif defined(__APPLE__)
    return base + ".dylib";
#else
    return base + ".so";
#endif
}

std::filesystem::path executableDirectory()
{
#if defined(_WIN32)
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0)
        return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0)
        return std::filesystem::current_path();
    std::error_code ec;
    auto p = std::filesystem::canonical(buf.data(), ec);
    return ec ? std::filesystem::path(buf.data()).parent_path() : p.parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : p.parent_path();
#endif
}

} // namespace anahita::engine
