/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/include/mirab/core/Gif.h
 * Purpose: GIF89a encoder and decoder: median-cut palettes, ordered dithering, LZW and animation.
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

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace mirab::core::gif
{

struct Color
{
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }
};
using Palette = std::vector<Color>;

/// Read-only view of 32-bit pixels stored as 0xAARRGGBB values (the layout of
/// QImage::Format_RGB32 and Format_ARGB32). Alpha is ignored.
struct PixelView
{
    const std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0; ///< pixels per row
    std::uint32_t at(int x, int y) const
    {
        return pixels[static_cast<std::size_t>(y) * stride + x];
    }
};

/// Colour statistics of one or more images (5 bits per channel), used to build a palette.
class Histogram
{
public:
    Histogram();
    /// Adds every @p step-th pixel of every @p step-th row.
    void add(const PixelView& image, int step = 1);
    /// Adds one colour with a weight, for example to keep the colours of a legend exact.
    void addColor(Color c, std::uint64_t weight);
    /// Number of distinct 15-bit colour cells seen so far.
    std::size_t cells() const;

    struct Cell
    {
        std::uint64_t count = 0;
        std::uint64_t r = 0, g = 0, b = 0; ///< sums of the 8-bit channel values
    };
    const std::vector<Cell>& data() const { return cells_; }
    /// The distinct 24-bit colours seen, while there are at most 256 of them.
    const std::vector<std::uint32_t>& exactColors() const { return exact_; }
    bool exactOverflow() const { return overflow_; }

private:
    void addExact(std::uint32_t rgb);

    std::vector<Cell> cells_;
    std::vector<std::uint32_t> exact_; ///< sorted
    bool overflow_ = false;
};

/// Palette of at most @p maxColors (1 to 256) colours, by median cut. When the
/// images have no more than @p maxColors distinct colours, they are kept exactly.
Palette medianCut(const Histogram& histogram, int maxColors);

enum class Dither
{
    None,
    Ordered ///< 4x4 Bayer matrix; stable from frame to frame, so animations compress well
};

/// Maps pixels to the nearest palette colour. Pixels that exactly match a
/// palette colour are never dithered, so flat areas stay clean.
class Quantizer
{
public:
    Quantizer(Palette palette, Dither dither);
    const Palette& palette() const { return palette_; }
    /// Fills @p indices with width x height palette indices.
    void map(const PixelView& image, std::vector<std::uint8_t>& indices);
    int nearest(int r, int g, int b) const;

private:
    int exactIndex(std::uint32_t rgb) const;

    Palette palette_;
    Dither dither_;
    std::vector<std::int16_t> cache_;  ///< nearest colour per 18-bit cell, -1 when unknown
    std::vector<std::uint64_t> exact_; ///< bit set of the 24-bit palette colours
};

/// GIF-flavoured LZW code stream (variable code width, at most 12 bits),
/// packed least significant bit first, without the sub-block framing.
std::vector<std::uint8_t> lzwEncode(const std::uint8_t* indices, std::size_t count,
                                    int minCodeSize);
/// Decodes a code stream. Returns false on malformed data.
bool lzwDecode(const std::uint8_t* data, std::size_t size, int minCodeSize,
               std::vector<std::uint8_t>& out);

/// One image of a GIF file.
struct Frame
{
    int x = 0, y = 0, width = 0, height = 0;
    std::vector<std::uint8_t> indices; ///< width x height
    int delayCs = 0;                   ///< hundredths of a second
    int transparent = -1;              ///< transparent palette index, or -1
    int disposal = 0; ///< 0 unspecified, 1 keep, 2 clear to background, 3 restore previous
    Palette local;    ///< local colour table; empty uses the global table
};

/// Low-level GIF89a writer.
class Writer
{
public:
    /// Writes the header and the global colour table. @p loopCount -1 writes
    /// no loop extension (play once), 0 loops forever.
    Writer(std::ostream& out, int width, int height, const Palette& global, int loopCount = 0);
    void addFrame(const Frame& frame);
    /// Writes the trailer. Returns false when the stream failed.
    bool finish();

private:
    std::ostream& out_;
    int width_, height_;
    int globalBits_ = 1;
    bool finished_ = false;
};

/// Writes an animation from full frames: the frames share one palette, and
/// each frame stores only the rectangle that changed, with unchanged pixels
/// transparent. Identical consecutive frames are merged into a longer delay.
class AnimationWriter
{
public:
    struct Options
    {
        int delayCs = 25;
        int loopCount = 0;
        Dither dither = Dither::Ordered;
        bool optimize = true;
    };
    /// @p palette holds at most 255 colours when @p options.optimize is set
    /// (one index is kept for transparency); extra colours are dropped.
    AnimationWriter(std::ostream& out, int width, int height, Palette palette,
                    const Options& options);
    void addFrame(const PixelView& image);
    /// Like addFrame with a delay for this frame only.
    void addFrame(const PixelView& image, int delayCs);
    bool finish();
    int framesWritten() const { return written_; }

private:
    void flush();

    Options options_;
    int width_, height_;
    int transparent_ = -1;
    Quantizer quantizer_;
    Writer writer_;
    std::vector<std::uint8_t> previous_; ///< canvas shown after the last flushed frame
    std::vector<std::uint8_t> current_;
    Frame pending_;
    bool havePending_ = false;
    int written_ = 0;
};

/// Builds a palette for frames of an animation: samples the given images and
/// keeps the extra colours (legend classes) as exactly as possible.
Palette animationPalette(const std::vector<PixelView>& samples, const std::vector<Color>& keyColors,
                         int maxColors);

/// A decoded GIF file (used by the tests and to check exported files).
struct Decoded
{
    struct Image
    {
        Frame frame;                       ///< as stored in the file
        std::vector<std::uint32_t> canvas; ///< composed picture after this frame, 0xAARRGGBB
    };
    int width = 0, height = 0;
    int loopCount = -1; ///< -1 when there is no loop extension
    Palette global;
    std::vector<Image> images;
};

/// Reads a GIF87a/GIF89a file from memory. Returns false with a message on malformed data.
bool decode(std::string_view bytes, Decoded& out, std::string* error = nullptr);

} // namespace mirab::core::gif
