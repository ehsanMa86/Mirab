/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Raster.h
 * Purpose: Georeferenced rasters: GeoTIFF and ESRI ASCII grid readers with lazy, cached block
 * access.
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

#include "mirab/core/Crs.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Reading of elevation models and georeferenced imagery without external
/// libraries.
///
/// GeoTIFF: classic and BigTIFF files in either byte order, strips or tiles,
/// chunky or planar samples, compression none, LZW, Deflate and PackBits,
/// predictors 1 (none), 2 (horizontal) and 3 (floating point), 8 to 64-bit
/// integer and floating-point samples, internal overviews, palette images,
/// the GDAL_NODATA tag, and the GeoKeys for EPSG and common user-defined
/// projections. A world file (.tfw) and a .prj file next to the image are
/// used when the TIFF carries no georeferencing. ESRI ASCII grids (.asc) with
/// an optional .prj are read as well.
///
/// References: TIFF Revision 6.0 (Adobe, 1992); Adobe Photoshop TIFF
/// Technical Notes (Deflate); OGC GeoTIFF Standard 1.1 (OGC 19-008r4);
/// BigTIFF file format proposal.
namespace mirab::raster
{

enum class SampleType
{
    UInt8,
    Int8,
    UInt16,
    Int16,
    UInt32,
    Int32,
    Float32,
    Float64
};

/// Pixel to map coordinates, GDAL convention: pixel (0, 0) is the outer
/// corner of the first pixel, so pixel centres are at col + 0.5, row + 0.5.
///   x = originX + col * pixelWidth + row * rowRotation
///   y = originY + col * colRotation + row * pixelHeight
struct GeoTransform
{
    double originX = 0.0, pixelWidth = 1.0, rowRotation = 0.0;
    double originY = 0.0, colRotation = 0.0, pixelHeight = -1.0;

    void pixelToMap(double col, double row, double& x, double& y) const;
    /// False when the transform cannot be inverted.
    bool mapToPixel(double x, double y, double& col, double& row) const;
    /// From the six lines of a world file (A, D, B, E, C, F; C/F at the centre
    /// of the upper-left pixel).
    static GeoTransform fromWorldFile(const double v[6]);
};

struct RasterInfo
{
    std::string format; ///< "GeoTIFF", "TIFF" or "ESRI ASCII grid"
    int width = 0;
    int height = 0;
    int bands = 0;
    SampleType type = SampleType::Float32;
    std::optional<double> noData;
    bool georeferenced = false;
    GeoTransform transform;
    bool pixelIsPoint = false;   ///< the file declared PixelIsPoint (already shifted to corners)
    std::optional<crs::Crs> crs; ///< horizontal coordinate system, when known
    std::string crsDescription;  ///< "EPSG:32611", a name or empty
    /// Length of one vertical unit in metres when the file declares it
    /// (VerticalUnitsGeoKey); 0 when unknown.
    double verticalUnitToMeters = 0.0;
    bool palette = false;           ///< one band of colour indices
    bool rgb = false;               ///< photometric RGB
    bool alpha = false;             ///< the last band is opacity
    int overviews = 0;              ///< reduced-resolution images stored in the file
    std::string compression;        ///< "none", "LZW", "Deflate", "PackBits"
    std::vector<std::string> notes; ///< non-fatal observations made while opening
};

/// A raster opened for reading. Pixel blocks are read from the file when
/// first needed and kept in a small least-recently-used cache, so large files
/// are never loaded whole. All methods are thread safe.
class Raster
{
public:
    ~Raster();
    Raster(const Raster&) = delete;
    Raster& operator=(const Raster&) = delete;

    /// Opens a GeoTIFF (.tif, .tiff) or ESRI ASCII grid (.asc). Returns
    /// nullptr and sets @p error when the file cannot be read.
    static std::unique_ptr<Raster> open(const std::filesystem::path& path,
                                        std::string* error = nullptr);

    const RasterInfo& info() const;
    const std::filesystem::path& path() const;

    /// True when the raster holds 8-bit colour data (RGB, grey or palette)
    /// rather than measurements such as elevation.
    bool isImagery() const;

    /// Value of a pixel of the full-resolution image. NaN for NoData, outside
    /// the image, or when the block cannot be read.
    double value(int col, int row, int band = 0);

    /// Bilinear interpolation between pixel centres at fractional pixel
    /// coordinates (pixel corners at integers). NoData neighbours are left
    /// out and the weights renormalised; nullopt when the nearest pixel is
    /// NoData or the point lies outside the image.
    std::optional<double> samplePixel(double col, double row, int band = 0);
    /// Same at map coordinates in the raster's own coordinate system.
    std::optional<double> sample(double x, double y, int band = 0);

    /// Reads the window [col0, col0 + cols) x [row0, row0 + rows) of the
    /// full-resolution grid resampled (nearest pixel) to outWidth x outHeight
    /// values. The coarsest internal overview that still matches the output
    /// resolution is used. NoData and outside become NaN. Returns false when a
    /// block could not be decoded (the rest of the window is still filled).
    bool readWindow(int band, double col0, double row0, double cols, double rows, int outWidth,
                    int outHeight, float* out);
    /// As readWindow() for imagery: 0xAARRGGBB pixels (fully transparent for
    /// NoData and outside). Grey, RGB(A) and palette images are converted.
    bool readWindowRgba(double col0, double row0, double cols, double rows, int outWidth,
                        int outHeight, std::uint32_t* out);

    /// Minimum and maximum of a band, estimated from at most about 512 x 512
    /// pixels. False when every value is NoData.
    bool range(int band, double& minimum, double& maximum);

    /// Upper bound of the block cache in bytes (default 64 MB).
    void setCacheLimit(std::size_t bytes);
    /// First block decoding error, if any (for diagnostics).
    std::string lastError() const;

    struct Impl;

private:
    explicit Raster(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/// Writes a single-band GeoTIFF with float32 samples (uncompressed strips),
/// for tests and demonstration data. @p epsg 0 writes no coordinate system;
/// @p verticalUnitToMeters other than 0 writes the vertical units key.
bool writeGeoTiff(const std::filesystem::path& path, int width, int height,
                  const std::vector<float>& values, const GeoTransform& transform, int epsg,
                  std::optional<double> noData, double verticalUnitToMeters = 0.0,
                  std::string* error = nullptr);

/// Size in bytes of one sample.
int sampleSize(SampleType type);

} // namespace mirab::raster
