/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Raster.cpp
 * Purpose: Implementation of the GeoTIFF and ESRI ASCII grid readers, block cache and sampling.
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
#include "mirab/core/Raster.h"

#include "mirab/core/Inflate.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace mirab::raster
{

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool hostIsLittleEndian()
{
    const std::uint16_t one = 1;
    std::uint8_t first = 0;
    std::memcpy(&first, &one, 1);
    return first == 1;
}

std::string lower(std::string s)
{
    return text::toLower(s);
}

/// One decoded block of samples in host byte order.
struct Block
{
    int width = 0;   ///< pixels per block row
    int height = 0;  ///< rows in the block
    int samples = 1; ///< samples per pixel stored in this block
    SampleType type = SampleType::Float32;
    bool empty = false; ///< sparse block: every pixel is NoData
    std::vector<std::uint8_t> data;

    double get(int x, int y, int s) const
    {
        if (empty)
            return kNaN;
        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)) *
                                  static_cast<std::size_t>(samples) +
                              static_cast<std::size_t>(s);
        const std::uint8_t* p = data.data();
        switch (type)
        {
        case SampleType::UInt8:
            return p[i];
        case SampleType::Int8:
            return static_cast<std::int8_t>(p[i]);
        case SampleType::UInt16:
        {
            std::uint16_t v;
            std::memcpy(&v, p + i * 2, 2);
            return v;
        }
        case SampleType::Int16:
        {
            std::int16_t v;
            std::memcpy(&v, p + i * 2, 2);
            return v;
        }
        case SampleType::UInt32:
        {
            std::uint32_t v;
            std::memcpy(&v, p + i * 4, 4);
            return v;
        }
        case SampleType::Int32:
        {
            std::int32_t v;
            std::memcpy(&v, p + i * 4, 4);
            return v;
        }
        case SampleType::Float32:
        {
            float v;
            std::memcpy(&v, p + i * 4, 4);
            return v;
        }
        case SampleType::Float64:
        {
            double v;
            std::memcpy(&v, p + i * 8, 8);
            return v;
        }
        }
        return kNaN;
    }
};

/// Geometry of one resolution level (the full image or an overview).
struct Level
{
    int width = 0, height = 0;
    int blockWidth = 0, blockHeight = 0;
    int across = 1, down = 1;
    bool planar = false; ///< one plane of blocks per band
};

/// A file format: describes the levels and decodes blocks.
class Source
{
public:
    virtual ~Source() = default;
    RasterInfo info;
    std::vector<Level> levels;
    std::vector<std::uint32_t> palette; ///< ARGB colours for palette images
    virtual std::shared_ptr<const Block> load(int level, int index, std::string* error) = 0;
};

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------
bool unpackBits(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out,
                std::size_t expected)
{
    std::size_t i = 0;
    while (i < n && out.size() < expected)
    {
        const auto c = static_cast<std::int8_t>(in[i++]);
        if (c >= 0)
        {
            const std::size_t count = static_cast<std::size_t>(c) + 1;
            if (i + count > n)
                return false;
            out.insert(out.end(), in + i, in + i + count);
            i += count;
        }
        else if (c != -128)
        {
            if (i >= n)
                return false;
            out.insert(out.end(), static_cast<std::size_t>(1 - c), in[i++]);
        }
    }
    return true;
}

/// TIFF LZW: codes of 9 to 12 bits, most significant bit first, code 256
/// clears the table, 257 ends the data, and the code width grows one code
/// early (TIFF 6.0, section 13).
bool lzwDecode(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out,
               std::size_t expected)
{
    constexpr int kClear = 256, kEnd = 257;
    std::vector<int> prefix(4096, -1);
    std::vector<std::uint8_t> suffix(4096, 0), firstByte(4096, 0);
    std::vector<int> length(4096, 0);
    for (int i = 0; i < 256; ++i)
    {
        suffix[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        firstByte[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        length[static_cast<std::size_t>(i)] = 1;
    }
    std::size_t bitPos = 0;
    const std::size_t totalBits = n * 8;
    int width = 9, next = 258, old = -1;
    auto readCode = [&]() -> int
    {
        if (bitPos + static_cast<std::size_t>(width) > totalBits)
            return kEnd;
        int code = 0;
        for (int b = 0; b < width; ++b, ++bitPos)
            code = (code << 1) | ((in[bitPos >> 3] >> (7 - (bitPos & 7))) & 1);
        return code;
    };
    auto emit = [&](int code)
    {
        out.resize(out.size() + static_cast<std::size_t>(length[static_cast<std::size_t>(code)]));
        std::size_t at = out.size();
        for (int c = code; c >= 0; c = prefix[static_cast<std::size_t>(c)])
            out[--at] = suffix[static_cast<std::size_t>(c)];
    };
    while (out.size() < expected)
    {
        int code = readCode();
        if (code == kEnd)
            break;
        if (code == kClear)
        {
            width = 9;
            next = 258;
            code = readCode();
            if (code == kEnd)
                break;
            if (code >= 256)
                return false;
            emit(code);
            old = code;
            continue;
        }
        if (old < 0)
        {
            if (code >= 256)
                return false;
            emit(code);
            old = code;
            continue;
        }
        if (code > next || code >= 4096)
            return false;
        const bool known = code < next;
        const std::uint8_t first = known ? firstByte[static_cast<std::size_t>(code)]
                                         : firstByte[static_cast<std::size_t>(old)];
        if (next < 4096)
        {
            const auto e = static_cast<std::size_t>(next);
            prefix[e] = old;
            suffix[e] = first;
            firstByte[e] = firstByte[static_cast<std::size_t>(old)];
            length[e] = length[static_cast<std::size_t>(old)] + 1;
            ++next;
        }
        emit(code);
        old = code;
        if (next >= (1 << width) - 1 && width < 12)
            ++width;
    }
    return true;
}

void swapBytes(std::vector<std::uint8_t>& data, int size)
{
    if (size <= 1)
        return;
    for (std::size_t i = 0; i + static_cast<std::size_t>(size) <= data.size();
         i += static_cast<std::size_t>(size))
        std::reverse(data.begin() + static_cast<std::ptrdiff_t>(i),
                     data.begin() + static_cast<std::ptrdiff_t>(i) + size);
}

template <typename T> void horizontalAccumulate(std::uint8_t* row, int width, int samples)
{
    for (int x = 1; x < width; ++x)
    {
        for (int s = 0; s < samples; ++s)
        {
            T prev, cur;
            std::memcpy(&prev, row + (static_cast<std::size_t>(x - 1) * samples + s) * sizeof(T),
                        sizeof(T));
            std::memcpy(&cur, row + (static_cast<std::size_t>(x) * samples + s) * sizeof(T),
                        sizeof(T));
            cur = static_cast<T>(cur + prev);
            std::memcpy(row + (static_cast<std::size_t>(x) * samples + s) * sizeof(T), &cur,
                        sizeof(T));
        }
    }
}

/// Floating-point predictor (Adobe TIFF Technical Note 3): bytes of each row
/// are differenced and stored as byte planes, most significant byte first.
void floatingPointAccumulate(std::uint8_t* row, int width, int samples, int bytes, bool littleHost)
{
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(samples);
    const std::size_t total = count * static_cast<std::size_t>(bytes);
    for (std::size_t i = static_cast<std::size_t>(samples); i < total; ++i)
        row[i] = static_cast<std::uint8_t>(row[i] + row[i - static_cast<std::size_t>(samples)]);
    std::vector<std::uint8_t> tmp(row, row + total);
    for (std::size_t c = 0; c < count; ++c)
    {
        for (int b = 0; b < bytes; ++b)
        {
            const int plane = littleHost ? bytes - b - 1 : b;
            row[c * static_cast<std::size_t>(bytes) + static_cast<std::size_t>(b)] =
                tmp[static_cast<std::size_t>(plane) * count + c];
        }
    }
}

// ---------------------------------------------------------------------------
// TIFF
// ---------------------------------------------------------------------------
enum Tag : std::uint16_t
{
    NewSubfileType = 254,
    ImageWidth = 256,
    ImageLength = 257,
    BitsPerSample = 258,
    Compression = 259,
    Photometric = 262,
    StripOffsets = 273,
    SamplesPerPixel = 277,
    RowsPerStrip = 278,
    StripByteCounts = 279,
    PlanarConfiguration = 284,
    Predictor = 317,
    ColorMap = 320,
    TileWidth = 322,
    TileLength = 323,
    TileOffsets = 324,
    TileByteCounts = 325,
    ExtraSamples = 338,
    SampleFormat = 339,
    ModelPixelScale = 33550,
    ModelTiepoint = 33922,
    ModelTransformation = 34264,
    GeoKeyDirectory = 34735,
    GeoDoubleParams = 34736,
    GeoAsciiParams = 34737,
    GdalNoData = 42113
};

struct Entry
{
    std::uint16_t type = 0;
    std::uint64_t count = 0;
    std::vector<std::uint8_t> bytes; ///< raw value bytes in file order
};

int typeSize(std::uint16_t type)
{
    switch (type)
    {
    case 1:
    case 2:
    case 6:
    case 7:
        return 1;
    case 3:
    case 8:
        return 2;
    case 4:
    case 9:
    case 11:
    case 13:
        return 4;
    case 5:
    case 10:
    case 12:
    case 16:
    case 17:
    case 18:
        return 8;
    default:
        return 0;
    }
}

struct TiffImage
{
    int width = 0, height = 0;
    int samples = 1;
    int bits = 8;
    int format = 1; ///< 1 unsigned, 2 signed, 3 float
    int compression = 1;
    int predictor = 1;
    int photometric = 1;
    bool planar = false;
    bool tiled = false;
    int blockWidth = 0, blockHeight = 0;
    std::uint32_t subfile = 0;
    std::vector<std::uint64_t> offsets, counts;
    std::map<std::uint16_t, Entry> tags;
};

class TiffSource : public Source
{
public:
    bool openFile(const fs::path& path, std::string* error);
    std::shared_ptr<const Block> load(int level, int index, std::string* error) override;

private:
    bool readBytes(std::uint64_t offset, std::size_t n, std::uint8_t* out);
    std::uint16_t u16(const std::uint8_t* p) const;
    std::uint32_t u32(const std::uint8_t* p) const;
    std::uint64_t u64(const std::uint8_t* p) const;
    bool readIfd(std::uint64_t offset, TiffImage& image, std::uint64_t& next, std::string* error);
    std::vector<double> numbers(const Entry& e) const;
    std::string ascii(const Entry& e) const;
    bool describe(TiffImage& image, std::string* error);
    void georeference(const TiffImage& image, const fs::path& path);

    std::ifstream file_;
    std::uint64_t fileSize_ = 0;
    bool little_ = true;
    bool big_ = false;
    std::vector<TiffImage> images_;
    SampleType type_ = SampleType::UInt8;
};

bool TiffSource::readBytes(std::uint64_t offset, std::size_t n, std::uint8_t* out)
{
    if (offset > fileSize_ || n > fileSize_ - offset)
        return false;
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(file_.gcount()) == n;
}

std::uint16_t TiffSource::u16(const std::uint8_t* p) const
{
    return little_ ? static_cast<std::uint16_t>(p[0] | (p[1] << 8))
                   : static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t TiffSource::u32(const std::uint8_t* p) const
{
    return little_ ? (std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
                      (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24))
                   : ((std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
                      (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]});
}

std::uint64_t TiffSource::u64(const std::uint8_t* p) const
{
    const std::uint64_t a = u32(p), b = u32(p + 4);
    return little_ ? (a | (b << 32)) : ((a << 32) | b);
}

std::vector<double> TiffSource::numbers(const Entry& e) const
{
    std::vector<double> v;
    const int size = typeSize(e.type);
    if (size == 0)
        return v;
    v.reserve(static_cast<std::size_t>(e.count));
    for (std::uint64_t i = 0; i < e.count; ++i)
    {
        const std::uint8_t* p = e.bytes.data() + i * static_cast<std::uint64_t>(size);
        switch (e.type)
        {
        case 1:
        case 7:
            v.push_back(p[0]);
            break;
        case 6:
            v.push_back(static_cast<std::int8_t>(p[0]));
            break;
        case 3:
            v.push_back(u16(p));
            break;
        case 8:
            v.push_back(static_cast<std::int16_t>(u16(p)));
            break;
        case 4:
        case 13:
            v.push_back(u32(p));
            break;
        case 9:
            v.push_back(static_cast<std::int32_t>(u32(p)));
            break;
        case 5:
            v.push_back(u32(p + 4) ? static_cast<double>(u32(p)) / u32(p + 4) : 0.0);
            break;
        case 10:
            v.push_back(u32(p + 4) ? static_cast<double>(static_cast<std::int32_t>(u32(p))) /
                                         static_cast<std::int32_t>(u32(p + 4))
                                   : 0.0);
            break;
        case 11:
        {
            const std::uint32_t bits = u32(p);
            float f;
            std::memcpy(&f, &bits, 4);
            v.push_back(f);
            break;
        }
        case 12:
        {
            const std::uint64_t bits = u64(p);
            double d;
            std::memcpy(&d, &bits, 8);
            v.push_back(d);
            break;
        }
        case 16:
        case 18:
            v.push_back(static_cast<double>(u64(p)));
            break;
        case 17:
            v.push_back(static_cast<double>(static_cast<std::int64_t>(u64(p))));
            break;
        default:
            break;
        }
    }
    return v;
}

std::string TiffSource::ascii(const Entry& e) const
{
    std::string s(e.bytes.begin(), e.bytes.end());
    while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
        s.pop_back();
    return s;
}

bool TiffSource::readIfd(std::uint64_t offset, TiffImage& image, std::uint64_t& next,
                         std::string* error)
{
    const int countSize = big_ ? 8 : 2;
    const int entrySize = big_ ? 20 : 12;
    const int inlineSize = big_ ? 8 : 4;
    std::uint8_t head[8];
    if (!readBytes(offset, static_cast<std::size_t>(countSize), head))
    {
        *error = "the image directory lies outside the file";
        return false;
    }
    const std::uint64_t n = big_ ? u64(head) : u16(head);
    if (n == 0 || n > 4096)
    {
        *error = "invalid image directory";
        return false;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(n) *
                                      static_cast<std::size_t>(entrySize) +
                                  static_cast<std::size_t>(big_ ? 8 : 4));
    if (!readBytes(offset + static_cast<std::uint64_t>(countSize), raw.size(), raw.data()))
    {
        *error = "truncated image directory";
        return false;
    }
    for (std::uint64_t i = 0; i < n; ++i)
    {
        const std::uint8_t* p = raw.data() + i * static_cast<std::uint64_t>(entrySize);
        Entry e;
        const std::uint16_t tag = u16(p);
        e.type = u16(p + 2);
        e.count = big_ ? u64(p + 4) : u32(p + 4);
        const int size = typeSize(e.type);
        if (size == 0)
            continue;
        const std::uint64_t bytes = e.count * static_cast<std::uint64_t>(size);
        if (e.count > (std::uint64_t{1} << 32) || bytes > fileSize_)
            continue;
        e.bytes.resize(static_cast<std::size_t>(bytes));
        const std::uint8_t* value = p + (big_ ? 12 : 8);
        if (bytes <= static_cast<std::uint64_t>(inlineSize))
        {
            std::memcpy(e.bytes.data(), value, static_cast<std::size_t>(bytes));
        }
        else
        {
            const std::uint64_t at = big_ ? u64(value) : u32(value);
            if (!readBytes(at, e.bytes.size(), e.bytes.data()))
                continue;
        }
        image.tags[tag] = std::move(e);
    }
    const std::uint8_t* tail = raw.data() + n * static_cast<std::uint64_t>(entrySize);
    next = big_ ? u64(tail) : u32(tail);
    return describe(image, error);
}

bool TiffSource::describe(TiffImage& im, std::string* error)
{
    auto first = [&](std::uint16_t tag, double fallback)
    {
        auto it = im.tags.find(tag);
        if (it == im.tags.end())
            return fallback;
        const auto v = numbers(it->second);
        return v.empty() ? fallback : v.front();
    };
    auto list = [&](std::uint16_t tag)
    {
        std::vector<std::uint64_t> out;
        auto it = im.tags.find(tag);
        if (it != im.tags.end())
        {
            for (double d : numbers(it->second))
                out.push_back(static_cast<std::uint64_t>(d));
        }
        return out;
    };
    im.width = static_cast<int>(first(ImageWidth, 0));
    im.height = static_cast<int>(first(ImageLength, 0));
    im.samples = static_cast<int>(first(SamplesPerPixel, 1));
    im.bits = static_cast<int>(first(BitsPerSample, 1));
    im.format = static_cast<int>(first(SampleFormat, 1));
    im.compression = static_cast<int>(first(Compression, 1));
    im.predictor = static_cast<int>(first(Predictor, 1));
    im.photometric = static_cast<int>(first(Photometric, 1));
    im.planar = first(PlanarConfiguration, 1) == 2;
    im.subfile = static_cast<std::uint32_t>(first(NewSubfileType, 0));
    if (im.width <= 0 || im.height <= 0 || im.samples <= 0 || im.samples > 64)
    {
        *error = "invalid image size";
        return false;
    }
    if (im.tags.count(TileWidth) && im.tags.count(TileOffsets))
    {
        im.tiled = true;
        im.blockWidth = static_cast<int>(first(TileWidth, 0));
        im.blockHeight = static_cast<int>(first(TileLength, 0));
        im.offsets = list(TileOffsets);
        im.counts = list(TileByteCounts);
    }
    else
    {
        im.blockWidth = im.width;
        const double rps = first(RowsPerStrip, im.height);
        im.blockHeight = static_cast<int>(std::clamp(rps, 1.0, static_cast<double>(im.height)));
        im.offsets = list(StripOffsets);
        im.counts = list(StripByteCounts);
    }
    if (im.blockWidth <= 0 || im.blockHeight <= 0)
    {
        *error = "invalid block size";
        return false;
    }
    const std::uint64_t across =
        (static_cast<std::uint64_t>(im.width) + static_cast<std::uint64_t>(im.blockWidth) - 1) /
        static_cast<std::uint64_t>(im.blockWidth);
    const std::uint64_t down =
        (static_cast<std::uint64_t>(im.height) + static_cast<std::uint64_t>(im.blockHeight) - 1) /
        static_cast<std::uint64_t>(im.blockHeight);
    const std::uint64_t blocks =
        across * down * static_cast<std::uint64_t>(im.planar ? im.samples : 1);
    if (im.offsets.size() < blocks)
    {
        *error = "the image has fewer data blocks than its size requires";
        return false;
    }
    if (im.counts.size() < blocks)
    {
        // Uncompressed files may omit the byte counts.
        const std::uint64_t bytesPerPixel = static_cast<std::uint64_t>(im.planar ? 1 : im.samples) *
                                            static_cast<std::uint64_t>(im.bits) / 8;
        im.counts.assign(blocks, static_cast<std::uint64_t>(im.blockWidth) *
                                     static_cast<std::uint64_t>(im.blockHeight) * bytesPerPixel);
    }
    return true;
}

bool TiffSource::openFile(const fs::path& path, std::string* error)
{
    file_.open(path, std::ios::binary);
    if (!file_)
    {
        *error = "cannot open the file";
        return false;
    }
    file_.seekg(0, std::ios::end);
    fileSize_ = static_cast<std::uint64_t>(file_.tellg());
    std::uint8_t head[16] = {};
    if (!readBytes(0, std::min<std::uint64_t>(16, fileSize_), head) || fileSize_ < 8)
    {
        *error = "the file is too short to be a TIFF image";
        return false;
    }
    if (head[0] == 'I' && head[1] == 'I')
        little_ = true;
    else if (head[0] == 'M' && head[1] == 'M')
        little_ = false;
    else
    {
        *error = "not a TIFF file";
        return false;
    }
    const std::uint16_t version = u16(head + 2);
    std::uint64_t offset = 0;
    if (version == 42)
    {
        offset = u32(head + 4);
    }
    else if (version == 43)
    {
        big_ = true;
        if (u16(head + 4) != 8 || fileSize_ < 16)
        {
            *error = "unsupported BigTIFF variant";
            return false;
        }
        offset = u64(head + 8);
    }
    else
    {
        *error = "not a TIFF file";
        return false;
    }
    std::vector<std::uint64_t> seen;
    while (offset != 0 && images_.size() < 64)
    {
        if (std::find(seen.begin(), seen.end(), offset) != seen.end())
            break; // loop in the directory chain
        seen.push_back(offset);
        TiffImage image;
        std::uint64_t next = 0;
        if (!readIfd(offset, image, next, error))
        {
            if (images_.empty())
                return false;
            break;
        }
        images_.push_back(std::move(image));
        offset = next;
    }
    if (images_.empty())
    {
        *error = "the file holds no image";
        return false;
    }
    const TiffImage& main = images_.front();
    switch (main.compression)
    {
    case 1:
        info.compression = "none";
        break;
    case 5:
        info.compression = "LZW";
        break;
    case 8:
    case 32946:
        info.compression = "Deflate";
        break;
    case 32773:
        info.compression = "PackBits";
        break;
    default:
    {
        static const std::map<int, const char*> names = {
            {2, "CCITT"},    {3, "CCITT G3"}, {4, "CCITT G4"}, {6, "old JPEG"}, {7, "JPEG"},
            {34887, "LERC"}, {34925, "LZMA"}, {50000, "ZSTD"}, {50001, "WebP"}, {50002, "JPEG XL"}};
        auto it = names.find(main.compression);
        *error = "the compression " +
                 (it != names.end() ? std::string(it->second)
                                    : "code " + std::to_string(main.compression)) +
                 " is not supported; save the file with no compression, LZW or Deflate";
        return false;
    }
    }
    if (main.predictor != 1 && main.predictor != 2 && main.predictor != 3)
    {
        *error = "unsupported TIFF predictor " + std::to_string(main.predictor);
        return false;
    }
    if (main.bits != 8 && main.bits != 16 && main.bits != 32 && main.bits != 64)
    {
        *error = std::to_string(main.bits) + "-bit samples are not supported";
        return false;
    }
    if (main.photometric == 6)
    {
        *error = "YCbCr images are not supported";
        return false;
    }
    if (main.format == 3)
        type_ = main.bits == 64 ? SampleType::Float64 : SampleType::Float32;
    else if (main.format == 2)
        type_ = main.bits == 8    ? SampleType::Int8
                : main.bits == 16 ? SampleType::Int16
                                  : SampleType::Int32;
    else
        type_ = main.bits == 8    ? SampleType::UInt8
                : main.bits == 16 ? SampleType::UInt16
                                  : SampleType::UInt32;
    if ((main.format == 3 && main.bits < 32) || (main.format != 3 && main.bits == 64))
    {
        *error = "unsupported sample format";
        return false;
    }

    info.format = "TIFF";
    info.width = main.width;
    info.height = main.height;
    info.bands = main.samples;
    info.type = type_;
    info.rgb = main.photometric == 2 && main.samples >= 3;
    info.palette = main.photometric == 3 && main.samples == 1;
    if (auto it = main.tags.find(ExtraSamples); it != main.tags.end())
    {
        const auto v = numbers(it->second);
        info.alpha = !v.empty() && (v.back() == 1 || v.back() == 2);
    }
    if (info.palette)
    {
        auto it = main.tags.find(ColorMap);
        const std::size_t n = std::size_t{1} << main.bits;
        const auto v = it != main.tags.end() ? numbers(it->second) : std::vector<double>{};
        if (main.bits <= 16 && v.size() >= 3 * n)
        {
            palette.resize(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto r = static_cast<std::uint32_t>(v[i]) >> 8;
                const auto g = static_cast<std::uint32_t>(v[n + i]) >> 8;
                const auto b = static_cast<std::uint32_t>(v[2 * n + i]) >> 8;
                palette[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
        else
        {
            info.palette = false;
        }
    }
    if (auto it = main.tags.find(GdalNoData); it != main.tags.end())
    {
        const std::string s = text::trim(ascii(it->second));
        if (lower(s) == "nan" || lower(s) == "-nan")
            info.noData = kNaN;
        else if (auto v = text::toDouble(s))
            info.noData = *v;
    }

    // Levels: the full image and its reduced-resolution copies.
    std::vector<std::size_t> used;
    for (std::size_t i = 0; i < images_.size(); ++i)
    {
        const TiffImage& im = images_[i];
        if (i > 0)
        {
            const bool reduced = (im.subfile & 1u) != 0 && (im.subfile & 4u) == 0;
            if (!reduced || im.samples != main.samples || im.bits != main.bits ||
                im.format != main.format || im.width >= main.width)
                continue;
            if (im.compression != 1 && im.compression != 5 && im.compression != 8 &&
                im.compression != 32946 && im.compression != 32773)
                continue;
        }
        Level lv;
        lv.width = im.width;
        lv.height = im.height;
        lv.blockWidth = im.blockWidth;
        lv.blockHeight = im.blockHeight;
        lv.across = (im.width + im.blockWidth - 1) / im.blockWidth;
        lv.down = (im.height + im.blockHeight - 1) / im.blockHeight;
        lv.planar = im.planar;
        levels.push_back(lv);
        used.push_back(i);
    }
    std::vector<TiffImage> kept;
    for (std::size_t i : used)
        kept.push_back(std::move(images_[i]));
    images_ = std::move(kept);
    std::sort(images_.begin() + 1, images_.end(),
              [](const TiffImage& a, const TiffImage& b) { return a.width > b.width; });
    std::sort(levels.begin() + 1, levels.end(),
              [](const Level& a, const Level& b) { return a.width > b.width; });
    info.overviews = static_cast<int>(levels.size()) - 1;
    georeference(images_.front(), path);
    return true;
}

// GeoKey identifiers (OGC GeoTIFF 1.1, annex).
enum GeoKey : int
{
    ModelTypeKey = 1024,
    RasterTypeKey = 1025,
    CitationKey = 1026,
    GeographicTypeKey = 2048,
    GeogCitationKey = 2049,
    GeogAngularUnitsKey = 2054,
    EllipsoidSemiMajorKey = 2057,
    EllipsoidInvFlatteningKey = 2059,
    ProjectedTypeKey = 3072,
    ProjCitationKey = 3073,
    ProjCoordTransKey = 3075,
    ProjLinearUnitsKey = 3076,
    ProjLinearUnitSizeKey = 3077,
    StdParallel1Key = 3078,
    StdParallel2Key = 3079,
    NatOriginLongKey = 3080,
    NatOriginLatKey = 3081,
    FalseEastingKey = 3082,
    FalseNorthingKey = 3083,
    FalseOriginLongKey = 3084,
    FalseOriginLatKey = 3085,
    FalseOriginEastingKey = 3086,
    FalseOriginNorthingKey = 3087,
    CenterLongKey = 3088,
    CenterLatKey = 3089,
    ScaleAtNatOriginKey = 3092,
    ScaleAtCenterKey = 3093,
    VerticalTypeKey = 4096,
    VerticalUnitsKey = 4099
};

double linearUnit(int code)
{
    switch (code)
    {
    case 9001:
        return 1.0;
    case 9002:
        return crs::kInternationalFoot;
    case 9003:
        return crs::kUsSurveyFoot;
    case 9036:
        return 1000.0;
    default:
        return 0.0;
    }
}

bool readWorldFile(const fs::path& file, GeoTransform& t)
{
    std::ifstream in(file);
    if (!in)
        return false;
    double v[6];
    std::string line;
    for (double& d : v)
    {
        if (!std::getline(in, line))
            return false;
        const auto parsed = text::toDouble(text::trim(line));
        if (!parsed)
            return false;
        d = *parsed;
    }
    if (v[0] == 0.0 || v[3] == 0.0)
        return false;
    t = GeoTransform::fromWorldFile(v);
    return true;
}

std::string readText(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

/// Looks for a .prj file next to @p path and parses it.
void readPrj(const fs::path& path, RasterInfo& info)
{
    if (info.crs)
        return;
    fs::path prj = path;
    prj.replace_extension(".prj");
    std::error_code ec;
    if (!fs::exists(prj, ec))
    {
        prj.replace_extension(".PRJ");
        if (!fs::exists(prj, ec))
            return;
    }
    const std::string wkt = readText(prj);
    if (auto c = crs::parse(text::trim(wkt)))
    {
        info.crs = *c;
        info.crsDescription = c->epsg ? "EPSG:" + std::to_string(c->epsg) : c->name;
    }
    else
    {
        info.notes.push_back("the coordinate system in " + prj.filename().u8string() +
                             " is not supported");
    }
}

void TiffSource::georeference(const TiffImage& im, const fs::path& path)
{
    auto doubles = [&](std::uint16_t tag)
    {
        auto it = im.tags.find(tag);
        return it == im.tags.end() ? std::vector<double>{} : numbers(it->second);
    };
    // GeoKeys
    std::map<int, double> keys;
    std::map<int, std::string> texts;
    const auto dir = doubles(GeoKeyDirectory);
    const auto params = doubles(GeoDoubleParams);
    std::string asciiParams;
    if (auto it = im.tags.find(GeoAsciiParams); it != im.tags.end())
        asciiParams.assign(it->second.bytes.begin(), it->second.bytes.end());
    if (dir.size() >= 4)
    {
        const std::size_t n = static_cast<std::size_t>(dir[3]);
        for (std::size_t k = 0; k < n && 4 + 4 * k + 3 < dir.size(); ++k)
        {
            const int id = static_cast<int>(dir[4 + 4 * k]);
            const int location = static_cast<int>(dir[5 + 4 * k]);
            const auto count = static_cast<std::size_t>(dir[6 + 4 * k]);
            const auto value = static_cast<std::size_t>(dir[7 + 4 * k]);
            if (location == 0)
                keys[id] = static_cast<double>(value);
            else if (location == GeoDoubleParams && value < params.size())
                keys[id] = params[value];
            else if (location == GeoAsciiParams && value < asciiParams.size())
            {
                std::string s = asciiParams.substr(value, count);
                while (!s.empty() && (s.back() == '|' || s.back() == '\0'))
                    s.pop_back();
                texts[id] = s;
            }
        }
    }
    auto key = [&](int id, double fallback = 0.0)
    {
        auto it = keys.find(id);
        return it == keys.end() ? fallback : it->second;
    };
    auto has = [&](int id) { return keys.count(id) > 0; };

    info.pixelIsPoint = key(RasterTypeKey, 1) == 2;
    const auto scale = doubles(ModelPixelScale);
    const auto tie = doubles(ModelTiepoint);
    const auto matrix = doubles(ModelTransformation);
    GeoTransform& t = info.transform;
    if (matrix.size() >= 16)
    {
        t.pixelWidth = matrix[0];
        t.rowRotation = matrix[1];
        t.originX = matrix[3];
        t.colRotation = matrix[4];
        t.pixelHeight = matrix[5];
        t.originY = matrix[7];
        info.georeferenced = true;
    }
    else if (scale.size() >= 2 && tie.size() >= 6 && scale[0] != 0.0 && scale[1] != 0.0)
    {
        t.pixelWidth = scale[0];
        t.pixelHeight = -scale[1];
        t.rowRotation = t.colRotation = 0.0;
        t.originX = tie[3] - tie[0] * scale[0];
        t.originY = tie[4] + tie[1] * scale[1];
        info.georeferenced = true;
        if (tie.size() > 6)
            info.notes.push_back("only the first tie point is used");
    }
    if (info.georeferenced && info.pixelIsPoint)
    {
        // The tie point refers to the pixel centre: move it to the corner.
        t.originX -= 0.5 * (t.pixelWidth + t.rowRotation);
        t.originY -= 0.5 * (t.colRotation + t.pixelHeight);
    }
    if (!info.georeferenced)
    {
        for (const char* ext : {".tfw", ".tifw", ".tiffw", ".wld", ".TFW", ".WLD"})
        {
            fs::path w = path;
            w.replace_extension(ext);
            if (readWorldFile(w, t))
            {
                info.georeferenced = true;
                break;
            }
        }
    }
    if (info.georeferenced)
        info.format = "GeoTIFF";

    // Coordinate system
    const int modelType = static_cast<int>(key(ModelTypeKey));
    const int projected = static_cast<int>(key(ProjectedTypeKey));
    const int geographic = static_cast<int>(key(GeographicTypeKey));
    std::string citation =
        texts.count(ProjCitationKey) ? texts[ProjCitationKey] : texts[CitationKey];
    if (projected > 0 && projected < 32767)
    {
        if (auto c = crs::fromEpsg(projected))
            info.crs = *c;
        else
            info.notes.push_back("EPSG:" + std::to_string(projected) +
                                 " is not among the supported systems");
        info.crsDescription = "EPSG:" + std::to_string(projected);
    }
    else if (projected == 32767 || (modelType == 1 && has(ProjCoordTransKey)))
    {
        crs::Crs c;
        if (auto base = crs::fromEpsg(geographic > 0 && geographic < 32767 ? geographic : 4326))
        {
            c.a = base->a;
            c.invf = base->invf;
            c.datum = base->datum;
        }
        if (has(EllipsoidSemiMajorKey))
            c.a = key(EllipsoidSemiMajorKey);
        if (has(EllipsoidInvFlatteningKey))
            c.invf = key(EllipsoidInvFlatteningKey);
        double unit = linearUnit(static_cast<int>(key(ProjLinearUnitsKey, 9001)));
        if (has(ProjLinearUnitSizeKey))
            unit = key(ProjLinearUnitSizeKey);
        if (unit <= 0)
            unit = 1.0;
        const double angle =
            key(GeogAngularUnitsKey, 9102) == 9101 ? 180.0 / 3.14159265358979323846 : 1.0;
        auto angleOf = [&](std::initializer_list<int> ids)
        {
            for (int id : ids)
            {
                if (has(id))
                    return key(id) * angle;
            }
            return 0.0;
        };
        auto lengthOf = [&](std::initializer_list<int> ids)
        {
            for (int id : ids)
            {
                if (has(id))
                    return key(id) * unit;
            }
            return 0.0;
        };
        c.unitToMeters = unit;
        c.falseEasting = lengthOf({FalseEastingKey, FalseOriginEastingKey});
        c.falseNorthing = lengthOf({FalseNorthingKey, FalseOriginNorthingKey});
        c.k0 = has(ScaleAtNatOriginKey) ? key(ScaleAtNatOriginKey) : key(ScaleAtCenterKey, 1.0);
        c.lon0 = angleOf({NatOriginLongKey, FalseOriginLongKey, CenterLongKey});
        c.lat0 = angleOf({NatOriginLatKey, FalseOriginLatKey, CenterLatKey});
        c.lat1 = angleOf({StdParallel1Key});
        c.lat2 = angleOf({StdParallel2Key});
        switch (static_cast<int>(key(ProjCoordTransKey)))
        {
        case 1:
            c.method = crs::Method::TransverseMercator;
            break;
        case 7:
            c.method = crs::Method::Mercator1SP;
            if (has(StdParallel1Key))
            {
                // Mercator (2SP): scale from the latitude of true scale.
                const double f = c.invf > 0 ? 1.0 / c.invf : 0.0;
                const double e2 = 2 * f - f * f;
                const double s = std::sin(c.lat1 * 3.14159265358979323846 / 180.0);
                c.k0 =
                    std::cos(c.lat1 * 3.14159265358979323846 / 180.0) / std::sqrt(1 - e2 * s * s);
            }
            break;
        case 8:
            c.method = crs::Method::LambertConformalConic2SP;
            c.lat0 = angleOf({FalseOriginLatKey, NatOriginLatKey});
            c.lon0 = angleOf({FalseOriginLongKey, NatOriginLongKey});
            break;
        case 9:
            c.method = crs::Method::LambertConformalConic1SP;
            break;
        case 11:
            c.method = crs::Method::AlbersEqualArea;
            c.lat0 = angleOf({FalseOriginLatKey, NatOriginLatKey, CenterLatKey});
            c.lon0 = angleOf({FalseOriginLongKey, NatOriginLongKey, CenterLongKey});
            break;
        default:
            break;
        }
        if (c.valid())
        {
            c.name = citation.empty() ? "User-defined projection" : citation;
            info.crs = c;
            info.crsDescription = c.name;
        }
        else if (auto p = crs::parse(citation))
        {
            info.crs = *p;
            info.crsDescription = citation;
        }
        else
        {
            info.notes.push_back("the user-defined projection of the file is not supported");
        }
    }
    else if (modelType == 2 || geographic > 0)
    {
        const int code = geographic > 0 && geographic < 32767 ? geographic : 4326;
        if (auto c = crs::fromEpsg(code))
            info.crs = *c;
        else if (auto w = crs::fromEpsg(4326))
            info.crs = *w; // any other geographic system is close enough for display
        info.crsDescription = "EPSG:" + std::to_string(code);
    }
    if (!info.crs)
        readPrj(path, info);

    // Vertical units: the VerticalUnitsGeoKey, else the units of a known vertical system.
    if (const int vu = static_cast<int>(key(VerticalUnitsKey)); vu > 0)
    {
        info.verticalUnitToMeters = linearUnit(vu);
    }
    else
    {
        switch (static_cast<int>(key(VerticalTypeKey)))
        {
        case 5703: // NAVD88 height
        case 5714: // MSL height
        case 3855: // EGM2008 height
        case 5773: // EGM96 height
            info.verticalUnitToMeters = 1.0;
            break;
        case 6360: // NAVD88 height (ftUS)
            info.verticalUnitToMeters = crs::kUsSurveyFoot;
            break;
        case 8228: // NAVD88 height (ft)
            info.verticalUnitToMeters = crs::kInternationalFoot;
            break;
        default:
            break;
        }
    }
}

std::shared_ptr<const Block> TiffSource::load(int level, int index, std::string* error)
{
    const TiffImage& im = images_[static_cast<std::size_t>(level)];
    const Level& lv = levels[static_cast<std::size_t>(level)];
    const int perPlane = lv.across * lv.down;
    const int inPlane = index % perPlane;
    const int by = inPlane / lv.across;
    auto block = std::make_shared<Block>();
    block->type = type_;
    block->samples = im.planar ? 1 : im.samples;
    block->width = lv.blockWidth;
    block->height =
        im.tiled ? lv.blockHeight : std::min(lv.blockHeight, lv.height - by * lv.blockHeight);
    const int bytes = sampleSize(type_);
    const std::size_t rowBytes = static_cast<std::size_t>(block->width) *
                                 static_cast<std::size_t>(block->samples) *
                                 static_cast<std::size_t>(bytes);
    const std::size_t expected = rowBytes * static_cast<std::size_t>(block->height);
    const std::uint64_t offset = im.offsets[static_cast<std::size_t>(index)];
    const std::uint64_t count = im.counts[static_cast<std::size_t>(index)];
    if (offset == 0 || count == 0)
    {
        block->empty = true; // sparse file: the block was never written
        return block;
    }
    if (count > (std::uint64_t{1} << 31))
    {
        *error = "data block too large";
        return nullptr;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(count));
    if (!readBytes(offset, raw.size(), raw.data()))
    {
        *error = "a data block lies outside the file";
        return nullptr;
    }
    bool ok = true;
    switch (im.compression)
    {
    case 1:
        block->data = std::move(raw);
        break;
    case 5:
        block->data.reserve(expected);
        ok = lzwDecode(raw.data(), raw.size(), block->data, expected);
        break;
    case 8:
    case 32946:
        ok = core::zlibDecompress(raw.data(), raw.size(), block->data, error, expected);
        if (!ok)
        {
            block->data.clear();
            ok = core::inflateRaw(raw.data(), raw.size(), block->data, error, expected);
        }
        break;
    case 32773:
        block->data.reserve(expected);
        ok = unpackBits(raw.data(), raw.size(), block->data, expected);
        break;
    default:
        ok = false;
        break;
    }
    if (!ok)
    {
        if (error && error->empty())
            *error = "corrupt " + info.compression + " data";
        return nullptr;
    }
    block->data.resize(expected, 0); // short blocks are padded
    const bool hostLittle = hostIsLittleEndian();
    if (im.predictor != 3 && little_ != hostLittle)
        swapBytes(block->data, bytes);
    if (im.predictor == 2)
    {
        for (int y = 0; y < block->height; ++y)
        {
            std::uint8_t* row = block->data.data() + static_cast<std::size_t>(y) * rowBytes;
            switch (bytes)
            {
            case 1:
                horizontalAccumulate<std::uint8_t>(row, block->width, block->samples);
                break;
            case 2:
                horizontalAccumulate<std::uint16_t>(row, block->width, block->samples);
                break;
            case 4:
                horizontalAccumulate<std::uint32_t>(row, block->width, block->samples);
                break;
            default:
                horizontalAccumulate<std::uint64_t>(row, block->width, block->samples);
                break;
            }
        }
    }
    else if (im.predictor == 3)
    {
        for (int y = 0; y < block->height; ++y)
            floatingPointAccumulate(block->data.data() + static_cast<std::size_t>(y) * rowBytes,
                                    block->width, block->samples, bytes, hostLittle);
    }
    return block;
}

// ---------------------------------------------------------------------------
// ESRI ASCII grid
// ---------------------------------------------------------------------------
class AsciiGridSource : public Source
{
public:
    bool openFile(const fs::path& path, std::string* error);
    std::shared_ptr<const Block> load(int, int, std::string*) override { return block_; }

private:
    std::shared_ptr<Block> block_;
};

bool AsciiGridSource::openFile(const fs::path& path, std::string* error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        *error = "cannot open the file";
        return false;
    }
    std::map<std::string, double> header;
    std::string word;
    std::streampos dataStart = in.tellg();
    for (int i = 0; i < 8; ++i)
    {
        dataStart = in.tellg();
        if (!(in >> word))
            break;
        if (text::toDouble(word) || word[0] == '-' || word[0] == '+')
        {
            break;
        }
        std::string value;
        if (!(in >> value))
            break;
        const auto v = text::toDouble(value);
        if (!v)
        {
            *error = "invalid header value for " + word;
            return false;
        }
        header[lower(word)] = *v;
        dataStart = in.tellg();
    }
    const int cols = static_cast<int>(header["ncols"]);
    const int rows = static_cast<int>(header["nrows"]);
    double dx = header.count("cellsize") ? header["cellsize"] : header["dx"];
    double dy = header.count("cellsize") ? header["cellsize"] : header["dy"];
    if (cols <= 0 || rows <= 0 || dx <= 0 || dy <= 0)
    {
        *error = "not an ESRI ASCII grid (ncols, nrows and cellsize are required)";
        return false;
    }
    double x0 = header.count("xllcorner") ? header["xllcorner"] : header["xllcenter"] - dx / 2;
    double y0 = header.count("yllcorner") ? header["yllcorner"] : header["yllcenter"] - dy / 2;
    if (header.count("nodata_value"))
        info.noData = header["nodata_value"];

    block_ = std::make_shared<Block>();
    block_->width = cols;
    block_->height = rows;
    block_->type = SampleType::Float32;
    block_->data.resize(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows) * 4);
    in.clear();
    in.seekg(dataStart);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const char* p = content.c_str();
    const char* end = p + content.size();
    std::size_t n = 0;
    const std::size_t total = static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);
    while (p < end && n < total)
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ','))
            ++p;
        const char* start = p;
        while (p < end && !(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ','))
            ++p;
        if (p == start)
            break;
        const auto v = text::toDouble(std::string_view(start, static_cast<std::size_t>(p - start)));
        if (!v)
        {
            *error = "invalid value in the grid";
            return false;
        }
        const float f = static_cast<float>(*v);
        std::memcpy(block_->data.data() + n * 4, &f, 4);
        ++n;
    }
    if (n < total)
    {
        *error = "the grid has fewer values than ncols x nrows";
        return false;
    }
    info.format = "ESRI ASCII grid";
    info.width = cols;
    info.height = rows;
    info.bands = 1;
    info.type = SampleType::Float32;
    info.compression = "none";
    info.georeferenced = true;
    info.transform.originX = x0;
    info.transform.originY = y0 + dy * rows;
    info.transform.pixelWidth = dx;
    info.transform.pixelHeight = -dy;
    Level lv;
    lv.width = cols;
    lv.height = rows;
    lv.blockWidth = cols;
    lv.blockHeight = rows;
    levels.push_back(lv);
    readPrj(path, info);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// GeoTransform
// ---------------------------------------------------------------------------
void GeoTransform::pixelToMap(double col, double row, double& x, double& y) const
{
    x = originX + col * pixelWidth + row * rowRotation;
    y = originY + col * colRotation + row * pixelHeight;
}

bool GeoTransform::mapToPixel(double x, double y, double& col, double& row) const
{
    const double det = pixelWidth * pixelHeight - rowRotation * colRotation;
    if (det == 0.0 || !std::isfinite(det))
        return false;
    const double dx = x - originX, dy = y - originY;
    col = (dx * pixelHeight - dy * rowRotation) / det;
    row = (dy * pixelWidth - dx * colRotation) / det;
    return true;
}

GeoTransform GeoTransform::fromWorldFile(const double v[6])
{
    GeoTransform t;
    t.pixelWidth = v[0];
    t.colRotation = v[1];
    t.rowRotation = v[2];
    t.pixelHeight = v[3];
    t.originX = v[4] - 0.5 * (v[0] + v[2]);
    t.originY = v[5] - 0.5 * (v[1] + v[3]);
    return t;
}

int sampleSize(SampleType type)
{
    switch (type)
    {
    case SampleType::UInt8:
    case SampleType::Int8:
        return 1;
    case SampleType::UInt16:
    case SampleType::Int16:
        return 2;
    case SampleType::UInt32:
    case SampleType::Int32:
    case SampleType::Float32:
        return 4;
    case SampleType::Float64:
        return 8;
    }
    return 4;
}

// ---------------------------------------------------------------------------
// Raster: block cache and sampling
// ---------------------------------------------------------------------------
struct Raster::Impl
{
    std::unique_ptr<Source> source;
    fs::path path;
    mutable std::mutex mutex;
    std::size_t limit = 64u * 1024u * 1024u;
    std::size_t used = 0;
    std::list<std::uint64_t> order; ///< most recently used first
    struct Cached
    {
        std::shared_ptr<const Block> block;
        std::list<std::uint64_t>::iterator position;
    };
    std::unordered_map<std::uint64_t, Cached> cache;
    std::uint64_t lastKey = ~std::uint64_t{0};
    const Block* lastBlock = nullptr;
    std::string lastError;
    bool failed = false;
    std::map<int, std::pair<double, double>> ranges;

    const RasterInfo& info() const { return source->info; }

    /// Returns the block or nullptr (caller holds the mutex).
    const Block* block(int level, int index)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(level) << 40) | static_cast<std::uint64_t>(index);
        if (key == lastKey)
            return lastBlock;
        auto it = cache.find(key);
        if (it != cache.end())
        {
            order.splice(order.begin(), order, it->second.position);
            lastKey = key;
            lastBlock = it->second.block.get();
            return lastBlock;
        }
        std::string error;
        std::shared_ptr<const Block> b = source->load(level, index, &error);
        if (!b)
        {
            failed = true;
            if (lastError.empty())
                lastError = error.empty() ? "cannot read a data block" : error;
            // Remember the failure as an empty block so it is not retried for every pixel.
            auto empty = std::make_shared<Block>();
            empty->empty = true;
            b = empty;
        }
        const std::size_t size = b->data.size() + 64;
        while (used + size > limit && !order.empty())
        {
            const std::uint64_t victim = order.back();
            order.pop_back();
            auto v = cache.find(victim);
            used -= v->second.block->data.size() + 64;
            cache.erase(v);
        }
        order.push_front(key);
        cache[key] = {b, order.begin()};
        used += size;
        lastKey = key;
        lastBlock = b.get();
        return lastBlock;
    }

    /// Raw value at a level (caller holds the mutex); NaN for NoData and outside.
    double pixel(int level, int col, int row, int band)
    {
        const Level& lv = source->levels[static_cast<std::size_t>(level)];
        if (col < 0 || row < 0 || col >= lv.width || row >= lv.height || band < 0 ||
            band >= info().bands)
            return kNaN;
        const int bx = col / lv.blockWidth, by = row / lv.blockHeight;
        int index = by * lv.across + bx;
        if (lv.planar)
            index += band * lv.across * lv.down;
        const Block* b = block(level, index);
        if (!b || b->empty)
            return kNaN;
        const int x = col - bx * lv.blockWidth, y = row - by * lv.blockHeight;
        if (y >= b->height || x >= b->width)
            return kNaN;
        const double v = b->get(x, y, lv.planar ? 0 : band);
        const auto& nd = info().noData;
        if (nd && (v == *nd || (std::isnan(*nd) && std::isnan(v))))
            return kNaN;
        return v;
    }

    int chooseLevel(double pixelsPerOutput) const
    {
        int best = 0;
        const double w0 = source->levels.front().width;
        for (std::size_t i = 1; i < source->levels.size(); ++i)
        {
            const double factor = w0 / source->levels[i].width;
            if (factor <= pixelsPerOutput * 1.01)
                best = static_cast<int>(i);
        }
        return best;
    }

    std::uint32_t rgba(int level, int col, int row)
    {
        const RasterInfo& in = info();
        auto byteOf = [&](double v)
        {
            if (in.type == SampleType::UInt16 || in.type == SampleType::Int16)
                v /= 256.0;
            return static_cast<std::uint32_t>(std::clamp(v, 0.0, 255.0));
        };
        const double v0 = pixel(level, col, row, 0);
        if (in.palette)
        {
            if (std::isnan(v0))
                return 0;
            const auto i = static_cast<std::size_t>(std::max(0.0, v0));
            return i < source->palette.size() ? source->palette[i] : 0;
        }
        if (in.bands >= 3)
        {
            const double v1 = pixel(level, col, row, 1), v2 = pixel(level, col, row, 2);
            if (std::isnan(v0) && std::isnan(v1) && std::isnan(v2))
                return 0;
            std::uint32_t a = 255;
            if (in.bands >= 4)
            {
                const double av = pixel(level, col, row, 3);
                a = std::isnan(av) ? 0 : byteOf(av);
            }
            auto c = [&](double v) { return std::isnan(v) ? 0u : byteOf(v); };
            return (a << 24) | (c(v0) << 16) | (c(v1) << 8) | c(v2);
        }
        if (std::isnan(v0))
            return 0;
        std::uint32_t a = 255;
        if (in.bands == 2)
        {
            const double av = pixel(level, col, row, 1);
            a = std::isnan(av) ? 0 : byteOf(av);
        }
        const std::uint32_t g = byteOf(v0);
        return (a << 24) | (g << 16) | (g << 8) | g;
    }
};

Raster::Raster(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Raster::~Raster() = default;

std::unique_ptr<Raster> Raster::open(const fs::path& path, std::string* error)
{
    std::string err;
    std::unique_ptr<Source> source;
    std::error_code ec;
    if (!fs::exists(path, ec))
    {
        err = "the file does not exist";
    }
    else
    {
        const std::string ext = lower(path.extension().u8string());
        bool isTiff = ext == ".tif" || ext == ".tiff" || ext == ".gtiff";
        if (!isTiff && ext != ".asc")
        {
            std::ifstream probe(path, std::ios::binary);
            char m[2] = {};
            probe.read(m, 2);
            isTiff = (m[0] == 'I' && m[1] == 'I') || (m[0] == 'M' && m[1] == 'M');
        }
        if (isTiff)
        {
            auto t = std::make_unique<TiffSource>();
            if (t->openFile(path, &err))
                source = std::move(t);
        }
        else
        {
            auto a = std::make_unique<AsciiGridSource>();
            if (a->openFile(path, &err))
                source = std::move(a);
        }
    }
    if (!source)
    {
        if (error)
            *error = path.filename().u8string() + ": " + err;
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->source = std::move(source);
    impl->path = path;
    return std::unique_ptr<Raster>(new Raster(std::move(impl)));
}

const RasterInfo& Raster::info() const
{
    return impl_->info();
}

const fs::path& Raster::path() const
{
    return impl_->path;
}

bool Raster::isImagery() const
{
    const RasterInfo& in = info();
    if (in.palette)
        return true;
    const bool byteLike = in.type == SampleType::UInt8 ||
                          ((in.rgb || in.bands >= 3) && in.type == SampleType::UInt16);
    return byteLike && (in.bands == 1 || in.bands == 2 || in.bands == 3 || in.bands == 4 || in.rgb);
}

std::string Raster::lastError() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->lastError;
}

void Raster::setCacheLimit(std::size_t bytes)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->limit = std::max<std::size_t>(bytes, 1u << 20);
}

double Raster::value(int col, int row, int band)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pixel(0, col, row, band);
}

std::optional<double> Raster::samplePixel(double col, double row, int band)
{
    const RasterInfo& in = info();
    if (!(col >= 0.0 && row >= 0.0 && col <= in.width && row <= in.height))
        return std::nullopt;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const int nc = std::min(static_cast<int>(col), in.width - 1);
    const int nr = std::min(static_cast<int>(row), in.height - 1);
    if (std::isnan(impl_->pixel(0, nc, nr, band)))
        return std::nullopt;
    const double u = col - 0.5, v = row - 0.5;
    const int i0 = static_cast<int>(std::floor(u)), j0 = static_cast<int>(std::floor(v));
    const double fx = u - i0, fy = v - j0;
    double sum = 0.0, weight = 0.0;
    for (int dj = 0; dj <= 1; ++dj)
    {
        for (int di = 0; di <= 1; ++di)
        {
            const double w = (di ? fx : 1.0 - fx) * (dj ? fy : 1.0 - fy);
            if (w <= 0.0)
                continue;
            const int c = std::clamp(i0 + di, 0, in.width - 1);
            const int r = std::clamp(j0 + dj, 0, in.height - 1);
            const double z = impl_->pixel(0, c, r, band);
            if (std::isnan(z))
                continue;
            sum += w * z;
            weight += w;
        }
    }
    if (weight <= 0.0)
        return impl_->pixel(0, nc, nr, band);
    return sum / weight;
}

std::optional<double> Raster::sample(double x, double y, int band)
{
    double col = 0, row = 0;
    if (!info().transform.mapToPixel(x, y, col, row))
        return std::nullopt;
    return samplePixel(col, row, band);
}

bool Raster::readWindow(int band, double col0, double row0, double cols, double rows, int outWidth,
                        int outHeight, float* out)
{
    if (outWidth <= 0 || outHeight <= 0)
        return true;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->failed = false;
    const int level = impl_->chooseLevel(std::min(cols / outWidth, rows / outHeight));
    const Level& lv = impl_->source->levels[static_cast<std::size_t>(level)];
    const double sx = static_cast<double>(lv.width) / info().width;
    const double sy = static_cast<double>(lv.height) / info().height;
    std::vector<int> columns(static_cast<std::size_t>(outWidth));
    for (int i = 0; i < outWidth; ++i)
        columns[static_cast<std::size_t>(i)] =
            static_cast<int>(std::floor((col0 + (i + 0.5) * cols / outWidth) * sx));
    for (int j = 0; j < outHeight; ++j)
    {
        const int r = static_cast<int>(std::floor((row0 + (j + 0.5) * rows / outHeight) * sy));
        float* line = out + static_cast<std::size_t>(j) * static_cast<std::size_t>(outWidth);
        for (int i = 0; i < outWidth; ++i)
            line[i] = static_cast<float>(
                impl_->pixel(level, columns[static_cast<std::size_t>(i)], r, band));
    }
    return !impl_->failed;
}

bool Raster::readWindowRgba(double col0, double row0, double cols, double rows, int outWidth,
                            int outHeight, std::uint32_t* out)
{
    if (outWidth <= 0 || outHeight <= 0)
        return true;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->failed = false;
    const int level = impl_->chooseLevel(std::min(cols / outWidth, rows / outHeight));
    const Level& lv = impl_->source->levels[static_cast<std::size_t>(level)];
    const double sx = static_cast<double>(lv.width) / info().width;
    const double sy = static_cast<double>(lv.height) / info().height;
    for (int j = 0; j < outHeight; ++j)
    {
        const int r = static_cast<int>(std::floor((row0 + (j + 0.5) * rows / outHeight) * sy));
        for (int i = 0; i < outWidth; ++i)
        {
            const int c = static_cast<int>(std::floor((col0 + (i + 0.5) * cols / outWidth) * sx));
            out[static_cast<std::size_t>(j) * static_cast<std::size_t>(outWidth) +
                static_cast<std::size_t>(i)] = impl_->rgba(level, c, r);
        }
    }
    return !impl_->failed;
}

bool Raster::range(int band, double& minimum, double& maximum)
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->ranges.find(band);
        if (it != impl_->ranges.end())
        {
            minimum = it->second.first;
            maximum = it->second.second;
            return minimum <= maximum;
        }
    }
    const RasterInfo& in = info();
    const int w = std::min(in.width, 512), h = std::min(in.height, 512);
    std::vector<float> values(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    readWindow(band, 0, 0, in.width, in.height, w, h, values.data());
    double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
    for (float v : values)
    {
        if (std::isnan(v))
            continue;
        lo = std::min(lo, static_cast<double>(v));
        hi = std::max(hi, static_cast<double>(v));
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ranges[band] = {lo, hi};
    minimum = lo;
    maximum = hi;
    return lo <= hi;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------
bool writeGeoTiff(const fs::path& path, int width, int height, const std::vector<float>& values,
                  const GeoTransform& transform, int epsg, std::optional<double> noData,
                  double verticalUnitToMeters, std::string* error)
{
    if (width <= 0 || height <= 0 ||
        values.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        if (error)
            *error = "invalid raster size";
        return false;
    }
    std::vector<std::uint8_t> out;
    auto put16 = [&](std::uint16_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    auto put32 = [&](std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    };
    auto putFloat = [&](float f)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        put32(bits);
    };
    // Header, then the strips, then the directory and its out-of-line values.
    out.insert(out.end(), {'I', 'I'});
    put16(42);
    put32(0); // directory offset, patched below
    const int rowsPerStrip = std::max(1, std::min(height, 65536 / (width * 4) + 1));
    const int strips = (height + rowsPerStrip - 1) / rowsPerStrip;
    std::vector<std::uint32_t> offsets, counts;
    for (int s = 0; s < strips; ++s)
    {
        offsets.push_back(static_cast<std::uint32_t>(out.size()));
        const int r0 = s * rowsPerStrip, r1 = std::min(height, r0 + rowsPerStrip);
        for (int r = r0; r < r1; ++r)
        {
            for (int c = 0; c < width; ++c)
                putFloat(values[static_cast<std::size_t>(r) * static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(c)]);
        }
        counts.push_back(static_cast<std::uint32_t>(out.size() - offsets.back()));
    }
    while (out.size() % 2)
        out.push_back(0);

    struct Field
    {
        std::uint16_t tag, type;
        std::uint32_t count;
        std::vector<std::uint8_t> data;
    };
    std::vector<Field> fields;
    auto shorts = [&](std::uint16_t tag, std::vector<std::uint16_t> v)
    {
        Field f{tag, 3, static_cast<std::uint32_t>(v.size()), {}};
        for (auto x : v)
        {
            f.data.push_back(static_cast<std::uint8_t>(x & 0xFF));
            f.data.push_back(static_cast<std::uint8_t>(x >> 8));
        }
        fields.push_back(f);
    };
    auto longs = [&](std::uint16_t tag, const std::vector<std::uint32_t>& v)
    {
        Field f{tag, 4, static_cast<std::uint32_t>(v.size()), {}};
        for (auto x : v)
        {
            for (int i = 0; i < 4; ++i)
                f.data.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
        }
        fields.push_back(f);
    };
    auto doubles = [&](std::uint16_t tag, const std::vector<double>& v)
    {
        Field f{tag, 12, static_cast<std::uint32_t>(v.size()), {}};
        for (double d : v)
        {
            std::uint64_t bits;
            std::memcpy(&bits, &d, 8);
            for (int i = 0; i < 8; ++i)
                f.data.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
        }
        fields.push_back(f);
    };
    longs(ImageWidth, {static_cast<std::uint32_t>(width)});
    longs(ImageLength, {static_cast<std::uint32_t>(height)});
    shorts(BitsPerSample, {32});
    shorts(Compression, {1});
    shorts(Photometric, {1});
    longs(StripOffsets, offsets);
    shorts(SamplesPerPixel, {1});
    longs(RowsPerStrip, {static_cast<std::uint32_t>(rowsPerStrip)});
    longs(StripByteCounts, counts);
    shorts(PlanarConfiguration, {1});
    shorts(SampleFormat, {3});
    const bool rotated = transform.rowRotation != 0.0 || transform.colRotation != 0.0;
    if (rotated)
    {
        doubles(ModelTransformation,
                {transform.pixelWidth, transform.rowRotation, 0, transform.originX,
                 transform.colRotation, transform.pixelHeight, 0, transform.originY, 0, 0, 0, 0, 0,
                 0, 0, 1});
    }
    else
    {
        doubles(ModelPixelScale, {transform.pixelWidth, -transform.pixelHeight, 0});
        doubles(ModelTiepoint, {0, 0, 0, transform.originX, transform.originY, 0});
    }
    std::vector<std::uint16_t> keys = {1, 1, 0, 0};
    auto addKey = [&](std::uint16_t id, std::uint16_t value)
    {
        keys.insert(keys.end(), {id, 0, 1, value});
        ++keys[3];
    };
    const auto c = epsg > 0 ? crs::fromEpsg(epsg) : std::nullopt;
    addKey(ModelTypeKey, c && c->isGeographic() ? 2 : 1);
    addKey(RasterTypeKey, 1);
    if (c)
        addKey(c->isGeographic() ? GeographicTypeKey : ProjectedTypeKey,
               static_cast<std::uint16_t>(epsg));
    if (verticalUnitToMeters > 0)
    {
        const std::uint16_t code =
            std::fabs(verticalUnitToMeters - crs::kUsSurveyFoot) < 1e-9        ? 9003
            : std::fabs(verticalUnitToMeters - crs::kInternationalFoot) < 1e-9 ? 9002
                                                                               : 9001;
        addKey(VerticalUnitsKey, code);
    }
    shorts(GeoKeyDirectory, keys);
    if (noData)
    {
        std::string s = std::isnan(*noData) ? "nan" : text::formatDouble(*noData, 17);
        Field f{GdalNoData, 2, static_cast<std::uint32_t>(s.size() + 1), {}};
        f.data.assign(s.begin(), s.end());
        f.data.push_back(0);
        fields.push_back(f);
    }
    std::sort(fields.begin(), fields.end(),
              [](const Field& a, const Field& b) { return a.tag < b.tag; });

    const std::uint32_t ifd = static_cast<std::uint32_t>(out.size());
    out[4] = static_cast<std::uint8_t>(ifd & 0xFF);
    out[5] = static_cast<std::uint8_t>((ifd >> 8) & 0xFF);
    out[6] = static_cast<std::uint8_t>((ifd >> 16) & 0xFF);
    out[7] = static_cast<std::uint8_t>(ifd >> 24);
    std::uint32_t extra = ifd + 2 + 12 * static_cast<std::uint32_t>(fields.size()) + 4;
    std::vector<std::uint8_t> tail;
    put16(static_cast<std::uint16_t>(fields.size()));
    for (const Field& f : fields)
    {
        put16(f.tag);
        put16(f.type);
        put32(f.count);
        if (f.data.size() <= 4)
        {
            std::vector<std::uint8_t> v = f.data;
            v.resize(4, 0);
            out.insert(out.end(), v.begin(), v.end());
        }
        else
        {
            put32(extra + static_cast<std::uint32_t>(tail.size()));
            tail.insert(tail.end(), f.data.begin(), f.data.end());
            if (tail.size() % 2)
                tail.push_back(0);
        }
    }
    put32(0);
    out.insert(out.end(), tail.begin(), tail.end());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        if (error)
            *error = "cannot write " + path.u8string();
        return false;
    }
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}

} // namespace mirab::raster
