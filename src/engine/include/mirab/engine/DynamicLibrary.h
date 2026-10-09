/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/engine/include/mirab/engine/DynamicLibrary.h
 * Purpose: Cross-platform loading of engine shared libraries at run time.
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

#include <filesystem>
#include <string>

namespace mirab::engine
{

/// Owns a handle to a shared library opened with LoadLibraryEx or dlopen.
/// Dependencies of the library are resolved from the library's own folder,
/// so an engine and its companion libraries can live together in one place.
class DynamicLibrary
{
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    bool open(const std::filesystem::path& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return handle_ != nullptr; }
    void* symbol(const char* name) const;
    const std::filesystem::path& path() const { return path_; }

    /// File name of a library stem on this platform ("x" -> "x.dll", "x.so", "x.dylib").
    static std::string fileName(const std::string& stem, bool withLibPrefix = false);

    template <typename Fn>
    bool bind(Fn& fn, const char* name, std::string* missing = nullptr) const
    {
        fn = reinterpret_cast<Fn>(symbol(name));
        if (!fn && missing)
        {
            if (!missing->empty())
                *missing += ", ";
            *missing += name;
        }
        return fn != nullptr;
    }

private:
    void* handle_ = nullptr;
    std::filesystem::path path_;
};

/// Folder that contains the running executable.
std::filesystem::path executableDirectory();

} // namespace mirab::engine
