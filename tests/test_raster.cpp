/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_raster.cpp
 * Purpose: Tests of the deflate decoder, the GeoTIFF and ASCII grid readers and DEM elevations.
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
#include "TestFramework.h"

#include "mirab/core/Crs.h"
#include "mirab/core/Elevation.h"
#include "mirab/core/Inflate.h"
#include "mirab/core/Model.h"
#include "mirab/core/ProjectSettings.h"
#include "mirab/core/Raster.h"
#include "mirab/core/Text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace mirab;
namespace fs = std::filesystem;
using raster::Raster;
using raster::SampleType;

namespace
{

// ---------------------------------------------------------------------------
// Encoders used to build test files (the reader must undo them).
// ---------------------------------------------------------------------------
class BitWriter
{
public:
    void put(std::uint32_t value, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            acc_ |= ((value >> i) & 1u) << bits_;
            if (++bits_ == 8)
                flush();
        }
    }
    /// Huffman codes are sent most significant bit first.
    void putCode(std::uint32_t code, int n)
    {
        for (int i = n - 1; i >= 0; --i)
            put((code >> i) & 1u, 1);
    }
    void align()
    {
        if (bits_)
            flush();
    }
    std::vector<std::uint8_t> bytes;

private:
    void flush()
    {
        bytes.push_back(static_cast<std::uint8_t>(acc_));
        acc_ = 0;
        bits_ = 0;
    }
    std::uint32_t acc_ = 0;
    int bits_ = 0;
};

void fixedLiteral(BitWriter& w, int sym)
{
    if (sym < 144)
        w.putCode(static_cast<std::uint32_t>(0x30 + sym), 8);
    else if (sym < 256)
        w.putCode(static_cast<std::uint32_t>(0x190 + sym - 144), 9);
    else if (sym < 280)
        w.putCode(static_cast<std::uint32_t>(sym - 256), 7);
    else
        w.putCode(static_cast<std::uint32_t>(0xC0 + sym - 280), 8);
}

/// Deflate with fixed Huffman codes and a simple greedy LZ77 search, or with
/// stored blocks when @p stored is true. Returns a zlib stream.
std::vector<std::uint8_t> deflate(const std::vector<std::uint8_t>& data, bool stored)
{
    static const int lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int lextra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int dbase[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                  33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    BitWriter w;
    w.bytes = {0x78, 0x9C};
    if (stored)
    {
        std::size_t at = 0;
        do
        {
            const std::size_t n = std::min<std::size_t>(data.size() - at, 1000);
            const bool last = at + n == data.size();
            w.put(last ? 1 : 0, 1);
            w.put(0, 2);
            w.align();
            w.put(static_cast<std::uint32_t>(n), 16);
            w.put(static_cast<std::uint32_t>(n ^ 0xFFFF), 16);
            for (std::size_t i = 0; i < n; ++i)
                w.put(data[at + i], 8);
            at += n;
        } while (at < data.size());
    }
    else
    {
        w.put(1, 1);
        w.put(1, 2);
        std::size_t i = 0;
        while (i < data.size())
        {
            int bestLen = 0, bestDist = 0;
            for (int d = 1; d <= 4096 && static_cast<std::size_t>(d) <= i; ++d)
            {
                int len = 0;
                while (len < 258 && i + static_cast<std::size_t>(len) < data.size() &&
                       data[i + static_cast<std::size_t>(len)] ==
                           data[i + static_cast<std::size_t>(len) - static_cast<std::size_t>(d)])
                    ++len;
                if (len > bestLen)
                {
                    bestLen = len;
                    bestDist = d;
                }
            }
            if (bestLen >= 3)
            {
                int code = 28;
                while (lbase[code] > bestLen)
                    --code;
                fixedLiteral(w, 257 + code);
                w.put(static_cast<std::uint32_t>(bestLen - lbase[code]), lextra[code]);
                int dc = 29;
                while (dbase[dc] > bestDist)
                    --dc;
                w.putCode(static_cast<std::uint32_t>(dc), 5);
                w.put(static_cast<std::uint32_t>(bestDist - dbase[dc]), dextra[dc]);
                i += static_cast<std::size_t>(bestLen);
            }
            else
            {
                fixedLiteral(w, data[i++]);
            }
        }
        fixedLiteral(w, 256);
    }
    w.align();
    const std::uint32_t a = core::adler32(data.data(), data.size());
    for (int s = 24; s >= 0; s -= 8)
        w.bytes.push_back(static_cast<std::uint8_t>((a >> s) & 0xFF));
    return w.bytes;
}

std::vector<std::uint8_t> packBits(const std::vector<std::uint8_t>& in)
{
    std::vector<std::uint8_t> out;
    std::size_t i = 0;
    while (i < in.size())
    {
        std::size_t run = 1;
        while (i + run < in.size() && run < 128 && in[i + run] == in[i])
            ++run;
        if (run >= 3)
        {
            out.push_back(static_cast<std::uint8_t>(257 - run));
            out.push_back(in[i]);
            i += run;
            continue;
        }
        std::size_t n = 0;
        while (i + n < in.size() && n < 128 &&
               !(i + n + 2 < in.size() && in[i + n] == in[i + n + 1] && in[i + n] == in[i + n + 2]))
            ++n;
        out.push_back(static_cast<std::uint8_t>(n - 1));
        out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(i),
                   in.begin() + static_cast<std::ptrdiff_t>(i + n));
        i += n;
    }
    return out;
}

/// TIFF LZW as written by libtiff: 9 to 12-bit codes, MSB first, clear code
/// at the start and when the table fills.
std::vector<std::uint8_t> lzw(const std::vector<std::uint8_t>& in)
{
    std::vector<std::uint8_t> out;
    std::uint32_t acc = 0;
    int bits = 0;
    int width = 9;
    auto emit = [&](int code)
    {
        acc = (acc << width) | static_cast<std::uint32_t>(code);
        bits += width;
        while (bits >= 8)
        {
            out.push_back(static_cast<std::uint8_t>((acc >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    };
    std::map<std::pair<int, int>, int> table;
    int next = 258;
    emit(256);
    int prefix = -1;
    for (std::uint8_t c : in)
    {
        if (prefix < 0)
        {
            prefix = c;
            continue;
        }
        auto it = table.find({prefix, c});
        if (it != table.end())
        {
            prefix = it->second;
            continue;
        }
        emit(prefix);
        table[{prefix, c}] = next++;
        if (next == 4094)
        {
            emit(256);
            table.clear();
            next = 258;
            width = 9;
        }
        else if (next > (1 << width) - 1 && width < 12)
        {
            ++width;
        }
        prefix = c;
    }
    if (prefix >= 0)
    {
        emit(prefix);
        ++next;
        if (next > (1 << width) - 1 && width < 12)
            ++width;
    }
    emit(257);
    if (bits > 0)
        out.push_back(static_cast<std::uint8_t>((acc << (8 - bits)) & 0xFF));
    return out;
}

// ---------------------------------------------------------------------------
// TIFF builder
// ---------------------------------------------------------------------------
struct Image
{
    int width = 0, height = 0, samples = 1;
    SampleType type = SampleType::Float32;
    std::vector<double> values; ///< row-major, samples interleaved
    int compression = 1;        ///< 1, 5, 8, 32773
    int predictor = 1;
    bool planar = false;
    bool tiled = false;
    int blockWidth = 16, blockHeight = 4; ///< tile size, or rows per strip (blockHeight)
    int photometric = 1;
    std::uint32_t subfile = 0;
    std::vector<std::uint16_t> extraSamples;
    std::vector<std::uint16_t> colorMap;
};

struct Tiff
{
    bool bigEndian = false;
    bool bigTiff = false;
    std::vector<Image> images;
    std::vector<double> scale, tiepoint, matrix;
    std::vector<std::uint16_t> geoKeys; ///< full GeoKeyDirectory
    std::vector<double> geoDoubles;
    std::string geoAscii;
    std::string noData;
};

struct Field
{
    std::uint16_t tag, type;
    std::uint64_t count;
    std::vector<std::uint8_t> data;
};

class TiffBuilder
{
public:
    explicit TiffBuilder(const Tiff& t) : t_(t) {}

    std::vector<std::uint8_t> build()
    {
        out_.clear();
        if (t_.bigEndian)
            out_ = {'M', 'M'};
        else
            out_ = {'I', 'I'};
        put(t_.bigTiff ? 43 : 42, 2);
        if (t_.bigTiff)
        {
            put(8, 2);
            put(0, 2);
        }
        std::size_t link = out_.size();
        put(0, t_.bigTiff ? 8 : 4);
        for (std::size_t i = 0; i < t_.images.size(); ++i)
        {
            std::vector<Field> fields = imageFields(t_.images[i]);
            if (i == 0)
                geoFields(fields);
            std::sort(fields.begin(), fields.end(),
                      [](const Field& a, const Field& b) { return a.tag < b.tag; });
            while (out_.size() % 2)
                out_.push_back(0);
            patch(link, out_.size(), t_.bigTiff ? 8 : 4);
            link = writeIfd(fields);
        }
        return out_;
    }

private:
    void put(std::uint64_t v, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const int shift = t_.bigEndian ? 8 * (n - 1 - i) : 8 * i;
            out_.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
        }
    }
    void patch(std::size_t at, std::uint64_t v, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const int shift = t_.bigEndian ? 8 * (n - 1 - i) : 8 * i;
            out_[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> shift) & 0xFF);
        }
    }
    std::vector<std::uint8_t> encodeValue(const Image& im, double v) const
    {
        std::vector<std::uint8_t> b;
        std::uint64_t bits = 0;
        int n = raster::sampleSize(im.type);
        switch (im.type)
        {
        case SampleType::UInt8:
        case SampleType::UInt16:
        case SampleType::UInt32:
            bits = static_cast<std::uint64_t>(v);
            break;
        case SampleType::Int8:
        case SampleType::Int16:
        case SampleType::Int32:
            bits = static_cast<std::uint64_t>(static_cast<std::int64_t>(v));
            break;
        case SampleType::Float32:
        {
            const float f = static_cast<float>(v);
            std::uint32_t u;
            std::memcpy(&u, &f, 4);
            bits = u;
            break;
        }
        case SampleType::Float64:
            std::memcpy(&bits, &v, 8);
            break;
        }
        for (int i = 0; i < n; ++i)
        {
            const int shift = t_.bigEndian ? 8 * (n - 1 - i) : 8 * i;
            b.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFF));
        }
        return b;
    }
    /// Bytes of one block before compression, with the predictor applied.
    std::vector<std::uint8_t> blockBytes(const Image& im, int x0, int y0, int bw, int bh,
                                         int band) const
    {
        const int spp = im.planar ? 1 : im.samples;
        const int size = raster::sampleSize(im.type);
        std::vector<std::uint8_t> out;
        for (int y = y0; y < y0 + bh; ++y)
        {
            std::vector<double> row;
            for (int x = x0; x < x0 + bw; ++x)
            {
                for (int s = 0; s < spp; ++s)
                {
                    const int sample = im.planar ? band : s;
                    double v = 0;
                    if (x < im.width && y < im.height)
                        v = im.values[(static_cast<std::size_t>(y) * im.width + x) * im.samples +
                                      sample];
                    row.push_back(v);
                }
            }
            if (im.predictor == 2)
            {
                std::vector<double> d(row.size());
                for (std::size_t i = 0; i < row.size(); ++i)
                {
                    const double prev = i >= static_cast<std::size_t>(spp) ? row[i - spp] : 0.0;
                    double diff = row[i] - prev;
                    const double range = std::pow(2.0, 8.0 * size);
                    if (diff < 0 &&
                        (im.type == SampleType::UInt8 || im.type == SampleType::UInt16 ||
                         im.type == SampleType::UInt32))
                        diff += range;
                    d[i] = diff;
                }
                for (double v : d)
                {
                    auto b = encodeValue(im, v);
                    out.insert(out.end(), b.begin(), b.end());
                }
            }
            else if (im.predictor == 3)
            {
                // Byte planes, most significant byte first, then byte differences.
                const std::size_t count = row.size();
                std::vector<std::uint8_t> planes(count * static_cast<std::size_t>(size));
                for (std::size_t c = 0; c < count; ++c)
                {
                    Tiff be = t_;
                    be.bigEndian = true;
                    auto b = TiffBuilder(be).encodeValue(im, row[c]);
                    for (int p = 0; p < size; ++p)
                        planes[static_cast<std::size_t>(p) * count + c] =
                            b[static_cast<std::size_t>(p)];
                }
                for (std::size_t i = planes.size() - 1; i >= static_cast<std::size_t>(spp); --i)
                    planes[i] = static_cast<std::uint8_t>(planes[i] - planes[i - spp]);
                out.insert(out.end(), planes.begin(), planes.end());
            }
            else
            {
                for (double v : row)
                {
                    auto b = encodeValue(im, v);
                    out.insert(out.end(), b.begin(), b.end());
                }
            }
        }
        return out;
    }
    std::vector<std::uint8_t> compress(const Image& im, const std::vector<std::uint8_t>& raw) const
    {
        switch (im.compression)
        {
        case 5:
            return lzw(raw);
        case 8:
            return deflate(raw, false);
        case 32773:
            return packBits(raw);
        default:
            return raw;
        }
    }
    Field numbers(std::uint16_t tag, std::uint16_t type, const std::vector<double>& v) const
    {
        Field f{tag, type, v.size(), {}};
        Image dummy;
        for (double x : v)
        {
            if (type == 12)
            {
                dummy.type = SampleType::Float64;
                auto b = encodeValue(dummy, x);
                f.data.insert(f.data.end(), b.begin(), b.end());
                continue;
            }
            const int n = type == 3 ? 2 : type == 16 ? 8 : 4;
            for (int i = 0; i < n; ++i)
            {
                const int shift = t_.bigEndian ? 8 * (n - 1 - i) : 8 * i;
                f.data.push_back(
                    static_cast<std::uint8_t>((static_cast<std::uint64_t>(x) >> shift) & 0xFF));
            }
        }
        return f;
    }
    std::vector<Field> imageFields(const Image& im)
    {
        const int bw = im.tiled ? im.blockWidth : im.width;
        const int bh = std::min(im.blockHeight, im.height);
        const int across = (im.width + bw - 1) / bw;
        const int down = (im.height + bh - 1) / bh;
        std::vector<double> offsets, counts;
        for (int band = 0; band < (im.planar ? im.samples : 1); ++band)
        {
            for (int by = 0; by < down; ++by)
            {
                for (int bx = 0; bx < across; ++bx)
                {
                    const int rows = im.tiled ? bh : std::min(bh, im.height - by * bh);
                    const auto data =
                        compress(im, blockBytes(im, bx * bw, by * bh, bw, rows, band));
                    offsets.push_back(static_cast<double>(out_.size()));
                    counts.push_back(static_cast<double>(data.size()));
                    out_.insert(out_.end(), data.begin(), data.end());
                }
            }
        }
        const std::uint16_t offType = t_.bigTiff ? 16 : 4;
        std::vector<Field> f;
        const int bits = raster::sampleSize(im.type) * 8;
        const int format = im.type == SampleType::Float32 || im.type == SampleType::Float64 ? 3
                           : im.type == SampleType::Int8 || im.type == SampleType::Int16 ||
                                   im.type == SampleType::Int32
                               ? 2
                               : 1;
        if (im.subfile)
            f.push_back(numbers(254, 4, {static_cast<double>(im.subfile)}));
        f.push_back(numbers(256, 4, {static_cast<double>(im.width)}));
        f.push_back(numbers(257, 4, {static_cast<double>(im.height)}));
        f.push_back(
            numbers(258, 3, std::vector<double>(static_cast<std::size_t>(im.samples), bits)));
        f.push_back(numbers(259, 3, {static_cast<double>(im.compression)}));
        f.push_back(numbers(262, 3, {static_cast<double>(im.photometric)}));
        f.push_back(numbers(277, 3, {static_cast<double>(im.samples)}));
        f.push_back(numbers(284, 3, {im.planar ? 2.0 : 1.0}));
        if (im.predictor != 1)
            f.push_back(numbers(317, 3, {static_cast<double>(im.predictor)}));
        f.push_back(
            numbers(339, 3, std::vector<double>(static_cast<std::size_t>(im.samples), format)));
        if (!im.extraSamples.empty())
            f.push_back(numbers(
                338, 3, std::vector<double>(im.extraSamples.begin(), im.extraSamples.end())));
        if (!im.colorMap.empty())
            f.push_back(
                numbers(320, 3, std::vector<double>(im.colorMap.begin(), im.colorMap.end())));
        if (im.tiled)
        {
            f.push_back(numbers(322, 4, {static_cast<double>(bw)}));
            f.push_back(numbers(323, 4, {static_cast<double>(bh)}));
            f.push_back(numbers(324, offType, offsets));
            f.push_back(numbers(325, 4, counts));
        }
        else
        {
            f.push_back(numbers(273, offType, offsets));
            f.push_back(numbers(278, 4, {static_cast<double>(bh)}));
            f.push_back(numbers(279, 4, counts));
        }
        return f;
    }
    void geoFields(std::vector<Field>& f) const
    {
        if (!t_.scale.empty())
            f.push_back(numbers(33550, 12, t_.scale));
        if (!t_.tiepoint.empty())
            f.push_back(numbers(33922, 12, t_.tiepoint));
        if (!t_.matrix.empty())
            f.push_back(numbers(34264, 12, t_.matrix));
        if (!t_.geoKeys.empty())
            f.push_back(
                numbers(34735, 3, std::vector<double>(t_.geoKeys.begin(), t_.geoKeys.end())));
        if (!t_.geoDoubles.empty())
            f.push_back(numbers(34736, 12, t_.geoDoubles));
        if (!t_.geoAscii.empty())
        {
            Field a{34737, 2, t_.geoAscii.size() + 1, {}};
            a.data.assign(t_.geoAscii.begin(), t_.geoAscii.end());
            a.data.push_back(0);
            f.push_back(a);
        }
        if (!t_.noData.empty())
        {
            Field a{42113, 2, t_.noData.size() + 1, {}};
            a.data.assign(t_.noData.begin(), t_.noData.end());
            a.data.push_back(0);
            f.push_back(a);
        }
    }
    /// Writes a directory and returns the position of its next-directory link.
    std::size_t writeIfd(const std::vector<Field>& fields)
    {
        const bool big = t_.bigTiff;
        const int inlineSize = big ? 8 : 4;
        const std::size_t start = out_.size();
        const std::size_t size = (big ? 8 : 2) + fields.size() * (big ? 20 : 12) + (big ? 8 : 4);
        std::size_t extra = start + size;
        std::vector<std::uint8_t> tail;
        put(fields.size(), big ? 8 : 2);
        for (const Field& f : fields)
        {
            put(f.tag, 2);
            put(f.type, 2);
            put(f.count, big ? 8 : 4);
            if (f.data.size() <= static_cast<std::size_t>(inlineSize))
            {
                auto d = f.data;
                d.resize(static_cast<std::size_t>(inlineSize), 0);
                out_.insert(out_.end(), d.begin(), d.end());
            }
            else
            {
                put(extra + tail.size(), inlineSize);
                tail.insert(tail.end(), f.data.begin(), f.data.end());
                if (tail.size() % 2)
                    tail.push_back(0);
            }
        }
        const std::size_t link = out_.size();
        put(0, big ? 8 : 4);
        out_.insert(out_.end(), tail.begin(), tail.end());
        return link;
    }

    const Tiff& t_;
    std::vector<std::uint8_t> out_;
};

fs::path tempDir()
{
    const fs::path dir = fs::temp_directory_path() / "mirab-raster-tests";
    fs::create_directories(dir);
    return dir;
}

fs::path writeFile(const std::string& name, const std::vector<std::uint8_t>& bytes)
{
    const fs::path p = tempDir() / name;
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return p;
}

fs::path writeText(const std::string& name, const std::string& text)
{
    return writeFile(name, std::vector<std::uint8_t>(text.begin(), text.end()));
}

/// GeoKeyDirectory with the given (key, value) pairs stored inline.
std::vector<std::uint16_t> keys(std::initializer_list<std::pair<int, int>> list)
{
    std::vector<std::uint16_t> k = {1, 1, 0, static_cast<std::uint16_t>(list.size())};
    for (const auto& [id, value] : list)
        k.insert(k.end(),
                 {static_cast<std::uint16_t>(id), 0, 1, static_cast<std::uint16_t>(value)});
    return k;
}

/// Test pattern with values that fit every sample type.
double pattern(int x, int y, int s)
{
    return (x * 7 + y * 13 + s * 31) % 120;
}

Image patternImage(int w, int h, int samples, SampleType type)
{
    Image im;
    im.width = w;
    im.height = h;
    im.samples = samples;
    im.type = type;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int s = 0; s < samples; ++s)
            {
                double v = pattern(x, y, s);
                if (type == SampleType::Int8 || type == SampleType::Int16 ||
                    type == SampleType::Int32)
                    v -= 60;
                if (type == SampleType::Float32 || type == SampleType::Float64)
                    v = v * 0.25 - 3.5;
                im.values.push_back(v);
            }
    return im;
}

void checkPattern(Raster& r, const Image& im)
{
    for (int y = 0; y < im.height; ++y)
        for (int x = 0; x < im.width; ++x)
            for (int s = 0; s < im.samples; ++s)
                CHECK_NEAR(r.value(x, y, s),
                           im.values[(static_cast<std::size_t>(y) * im.width + x) * im.samples + s],
                           1e-6);
}

std::unique_ptr<Raster> openTiff(const std::string& name, const Tiff& t)
{
    const fs::path p = writeFile(name, TiffBuilder(t).build());
    std::string err;
    auto r = Raster::open(p, &err);
    if (!r)
        throw mirab::test::Failure("open failed: " + err);
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Deflate
// ---------------------------------------------------------------------------
TEST_CASE("raster: inflate decodes stored, fixed and dynamic blocks")
{
    std::vector<std::uint8_t> data;
    for (int i = 0; i < 5000; ++i)
        data.push_back(static_cast<std::uint8_t>((i * i + i / 7) % 251));
    for (bool stored : {true, false})
    {
        const auto z = deflate(data, stored);
        std::vector<std::uint8_t> out;
        std::string err;
        CHECK(core::zlibDecompress(z.data(), z.size(), out, &err));
        CHECK(out == data);
    }
    // Repetitive text exercises long matches and overlapping copies.
    std::string text;
    for (int i = 0; i < 300; ++i)
        text += "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa water main " + std::to_string(i % 9) + "\n";
    const std::vector<std::uint8_t> t(text.begin(), text.end());
    const auto zt = deflate(t, false);
    CHECK(zt.size() < t.size() / 4);
    std::vector<std::uint8_t> out;
    CHECK(core::zlibDecompress(zt.data(), zt.size(), out));
    CHECK(out == t);

    // A stream with dynamic Huffman tables, written by zlib at level 9.
    const char* hex = "78da8dd54b6ec3300c04d07d4fa12358fce8d33be4100ee045d022010ca3b97e8142c39da6da"
                      "0f44fa91926f8f73bfa7"
                      "ed33bdf7eb38d3f3b8deaff32b1ddfc7cf7e3d5ecfb47ddcfe22791ec92322f3888d88ce237d"
                      "448c142a23e3f38cf8c8"
                      "947946714e251da39f36cf147c569f671a743241566488b2a05866ce1521225d0521621d1962"
                      "adf16d04bbc44944bb63"
                      "6a99706b3011ef8a8515e29dd1931070473921e01dab2404dc303a21e03dae1111b7e88988f7"
                      "2847c43d9c8878438680"
                      "178c4e09b8602f958037945302eef1941070c17c958037dc7025e0055351f69e040101cf7112"
                      "03c73a291347c888b801"
                      "d388b8c2c988788e938878b4c4361ccb6b04bcc1d208788d9f0501af711201afd1535f08f9b6"
                      "50cef342e32e0b04ae0b"
                      "986eff4fc57d61bc5e1616c5ebc2ca795b585eeff41afc026c1bef04";
    std::vector<std::uint8_t> z;
    for (const char* p = hex; p[0] && p[1]; p += 2)
        z.push_back(static_cast<std::uint8_t>(std::stoi(std::string(p, 2), nullptr, 16)));
    std::string expected;
    for (int i = 0; i < 60; ++i)
        expected += "Mirab " + std::to_string(i) + ": water network elevation " +
                    std::to_string(i * i % 97) + "\n";
    out.clear();
    std::string err;
    CHECK(core::zlibDecompress(z.data(), z.size(), out, &err));
    CHECK_EQ(std::string(out.begin(), out.end()), expected);
    CHECK_EQ(core::adler32(out.data(), out.size()), 0x6c1bef04u);

    // Corrupt checksum and truncated data are reported.
    auto bad = z;
    bad.back() ^= 0x55;
    out.clear();
    CHECK(!core::zlibDecompress(bad.data(), bad.size(), out, &err));
    out.clear();
    CHECK(!core::zlibDecompress(z.data(), z.size() / 2, out, &err));
    const std::uint8_t junk[] = {0x78, 0x9C, 0xFF, 0xFF, 0xFF};
    out.clear();
    CHECK(!core::zlibDecompress(junk, sizeof junk, out, &err));
}

// ---------------------------------------------------------------------------
// GeoTIFF
// ---------------------------------------------------------------------------
TEST_CASE("raster: every sample type, byte order and compression reads back")
{
    const SampleType types[] = {SampleType::UInt8,   SampleType::Int8,   SampleType::UInt16,
                                SampleType::Int16,   SampleType::UInt32, SampleType::Int32,
                                SampleType::Float32, SampleType::Float64};
    int n = 0;
    for (SampleType type : types)
    {
        for (int compression : {1, 5, 8, 32773})
        {
            for (bool bigEndian : {false, true})
            {
                const bool isFloat = type == SampleType::Float32 || type == SampleType::Float64;
                for (int predictor : {1, isFloat ? 3 : 2})
                {
                    Tiff t;
                    t.bigEndian = bigEndian;
                    t.bigTiff = (n % 3) == 1;
                    Image im = patternImage(37, 23, (n % 4) == 0 ? 3 : 1, type);
                    im.compression = compression;
                    im.predictor = predictor;
                    im.tiled = (n % 2) == 0;
                    im.planar = im.samples > 1 && (n % 8) == 0;
                    im.blockWidth = 16;
                    im.blockHeight = im.tiled ? 16 : 5;
                    t.images.push_back(im);
                    auto r = openTiff("types.tif", t);
                    CHECK_EQ(r->info().width, 37);
                    CHECK_EQ(r->info().height, 23);
                    CHECK_EQ(r->info().bands, im.samples);
                    CHECK(r->info().type == type);
                    checkPattern(*r, im);
                    CHECK(std::isnan(r->value(37, 0)));
                    CHECK(std::isnan(r->value(-1, 0)));
                    ++n;
                }
            }
        }
    }
    CHECK_EQ(n, 128);
}

TEST_CASE("raster: GeoKeys, tie points, NoData and bilinear sampling")
{
    // A plane z = 100 + 0.5 * x_px - 0.25 * y_px on a 4 m grid in UTM zone 11N.
    Tiff t;
    Image im;
    im.width = 50;
    im.height = 40;
    im.type = SampleType::Float32;
    for (int y = 0; y < im.height; ++y)
        for (int x = 0; x < im.width; ++x)
            im.values.push_back(100.0 + 0.5 * x - 0.25 * y);
    im.values[static_cast<std::size_t>(10 * 50 + 10)] = -9999;
    im.compression = 8;
    im.predictor = 3;
    im.tiled = true;
    t.images.push_back(im);
    t.scale = {4, 4, 0};
    t.tiepoint = {0, 0, 0, 390000, 3740000, 0};
    t.geoKeys = keys({{1024, 1}, {1025, 1}, {3072, 32611}, {4099, 9002}});
    t.noData = "-9999";
    auto r = openTiff("plane.tif", t);
    const auto& info = r->info();
    CHECK(info.georeferenced);
    CHECK_EQ(info.format, std::string("GeoTIFF"));
    CHECK(info.crs.has_value());
    CHECK_EQ(info.crs->epsg, 32611);
    CHECK_NEAR(info.verticalUnitToMeters, 0.3048, 1e-12);
    CHECK(info.noData.has_value());
    CHECK_EQ(info.compression, std::string("Deflate"));
    CHECK_NEAR(info.transform.originX, 390000, 1e-9);
    CHECK_NEAR(info.transform.pixelHeight, -4, 1e-12);

    // Centre of pixel (3, 5)
    auto v = r->sample(390000 + 3.5 * 4, 3740000 - 5.5 * 4);
    CHECK(v.has_value());
    CHECK_NEAR(*v, 100 + 1.5 - 1.25, 1e-5);
    // Between pixel centres the plane is reproduced exactly.
    v = r->sample(390000 + 20.25 * 4, 3740000 - 30.75 * 4);
    CHECK(v.has_value());
    CHECK_NEAR(*v, 100 + 0.5 * 19.75 - 0.25 * 30.25, 1e-4);
    // Outside
    CHECK(!r->sample(389990, 3739000).has_value());
    // NoData pixel itself, and a neighbour that leaves it out of the weights
    CHECK(std::isnan(r->value(10, 10)));
    CHECK(!r->samplePixel(10.5, 10.5).has_value());
    v = r->samplePixel(11.2, 10.7);
    CHECK(v.has_value());
    CHECK(*v > 90 && *v < 110);
    double lo = 0, hi = 0;
    CHECK(r->range(0, lo, hi));
    CHECK_NEAR(lo, 100 - 0.25 * 39, 1e-4);
    CHECK_NEAR(hi, 100 + 0.5 * 49, 1e-4);
}

TEST_CASE("raster: PixelIsPoint, model transformation, user-defined projection")
{
    Image im = patternImage(8, 8, 1, SampleType::Int16);
    {
        Tiff t;
        t.images.push_back(im);
        t.scale = {10, 10, 0};
        t.tiepoint = {0, 0, 0, 1000, 2000, 0};
        t.geoKeys = keys({{1024, 2}, {1025, 2}, {2048, 4326}});
        auto r = openTiff("point.tif", t);
        CHECK(r->info().pixelIsPoint);
        CHECK_NEAR(r->info().transform.originX, 995, 1e-9);
        CHECK_NEAR(r->info().transform.originY, 2005, 1e-9);
        CHECK(r->info().crs && r->info().crs->isGeographic());
    }
    {
        Tiff t;
        t.images.push_back(im);
        t.matrix = {2, 0, 0, 500, 0, -3, 0, 800, 0, 0, 0, 0, 0, 0, 0, 1};
        auto r = openTiff("matrix.tif", t);
        double x = 0, y = 0;
        r->info().transform.pixelToMap(4, 2, x, y);
        CHECK_NEAR(x, 508, 1e-9);
        CHECK_NEAR(y, 794, 1e-9);
        CHECK_NEAR(*r->sample(501, 799), pattern(0, 0, 0) - 60, 1e-9);
        CHECK(!r->info().crs);
    }
    {
        // UTM zone 11N spelled out as a user-defined Transverse Mercator.
        Tiff t;
        t.images.push_back(im);
        t.scale = {30, 30, 0};
        t.tiepoint = {0, 0, 0, 400000, 3700000, 0};
        std::vector<std::uint16_t> k = {1, 1, 0, 0};
        auto add = [&](int id, int location, int value)
        {
            k.insert(k.end(), {static_cast<std::uint16_t>(id), static_cast<std::uint16_t>(location),
                               1, static_cast<std::uint16_t>(value)});
            ++k[3];
        };
        add(1024, 0, 1);
        add(2048, 0, 4326);
        add(3072, 0, 32767);
        add(3075, 0, 1);
        add(3076, 0, 9001);
        add(3080, 34736, 0);
        add(3081, 34736, 1);
        add(3082, 34736, 2);
        add(3083, 34736, 3);
        add(3092, 34736, 4);
        t.geoKeys = k;
        t.geoDoubles = {-117, 0, 500000, 0, 0.9996};
        auto r = openTiff("userdefined.tif", t);
        CHECK(r->info().crs.has_value());
        const auto utm = crs::fromEpsg(32611);
        double lon1 = 0, lat1 = 0, lon2 = 0, lat2 = 0;
        CHECK(crs::toLonLat(*r->info().crs, 412345, 3712345, lon1, lat1));
        CHECK(crs::toLonLat(*utm, 412345, 3712345, lon2, lat2));
        CHECK_NEAR(lon1, lon2, 1e-9);
        CHECK_NEAR(lat1, lat2, 1e-9);
    }
}

TEST_CASE("raster: world file and .prj sidecars, ASCII grid")
{
    Tiff t;
    t.images.push_back(patternImage(6, 5, 1, SampleType::UInt8));
    const fs::path p = writeFile("sidecar.tif", TiffBuilder(t).build());
    writeText("sidecar.tfw", "2.0\n0.0\n0.0\n-2.0\n101.0\n209.0\n");
    std::ofstream(tempDir() / "sidecar.prj")
        << "PROJCS[\"WGS 84 / UTM zone 11N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS "
           "84\",6378137,"
           "298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]],"
           "PROJECTION["
           "\"Transverse_Mercator\"],PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_"
           "meridian\",-117],"
           "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],PARAMETER["
           "\"false_northing\",0],"
           "UNIT[\"metre\",1]]";
    std::string err;
    auto r = Raster::open(p, &err);
    CHECK(r != nullptr);
    CHECK(r->info().georeferenced);
    CHECK_NEAR(r->info().transform.originX, 100, 1e-9);
    CHECK_NEAR(r->info().transform.originY, 210, 1e-9);
    CHECK(r->info().crs.has_value());
    CHECK_EQ(r->info().crs->epsg, 32611);
    CHECK(r->isImagery());

    const fs::path asc =
        writeText("grid.asc", "ncols 4\nnrows 3\nxllcenter 1000.5\nyllcenter 2000.5\ncellsize 1\n"
                              "NODATA_value -9999\n1 2 3 4\n5 6 7 8\n9 10 -9999 12\n");
    auto g = Raster::open(asc, &err);
    CHECK(g != nullptr);
    CHECK_EQ(g->info().format, std::string("ESRI ASCII grid"));
    CHECK_NEAR(g->info().transform.originX, 1000, 1e-9);
    CHECK_NEAR(g->info().transform.originY, 2003, 1e-9);
    CHECK_NEAR(g->value(0, 0), 1, 1e-9);
    CHECK_NEAR(g->value(3, 2), 12, 1e-9);
    CHECK(std::isnan(g->value(2, 2)));
    CHECK_NEAR(*g->sample(1001.5, 2002.5), 2, 1e-6);
    CHECK_NEAR(*g->sample(1001.0, 2002.0), (1 + 2 + 5 + 6) / 4.0, 1e-6);
    CHECK(!g->isImagery());

    CHECK(!Raster::open(writeText("bad.asc", "hello world\n"), &err));
    CHECK(!err.empty());
}

TEST_CASE("raster: imagery, palettes, overviews and unsupported files")
{
    // RGBA, 8-bit, alpha in the fourth band
    Tiff t;
    Image im = patternImage(9, 7, 4, SampleType::UInt8);
    im.photometric = 2;
    im.extraSamples = {2};
    im.compression = 5;
    t.images.push_back(im);
    auto r = openTiff("rgba.tif", t);
    CHECK(r->isImagery());
    CHECK(r->info().rgb);
    CHECK(r->info().alpha);
    std::vector<std::uint32_t> px(9 * 7);
    CHECK(r->readWindowRgba(0, 0, 9, 7, 9, 7, px.data()));
    const auto expect = [](int x, int y)
    {
        auto b = [&](int s) { return static_cast<std::uint32_t>(pattern(x, y, s)); };
        return (b(3) << 24) | (b(0) << 16) | (b(1) << 8) | b(2);
    };
    CHECK_EQ(px[0], expect(0, 0));
    CHECK_EQ(px[static_cast<std::size_t>(6 * 9 + 8)], expect(8, 6));

    // Palette
    Tiff pt;
    Image pal = patternImage(4, 4, 1, SampleType::UInt8);
    pal.photometric = 3;
    pal.colorMap.assign(3 * 256, 0);
    for (int i = 0; i < 256; ++i)
    {
        pal.colorMap[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(i * 257);
        pal.colorMap[static_cast<std::size_t>(512 + i)] = 65535;
    }
    pt.images.push_back(pal);
    auto rp = openTiff("palette.tif", pt);
    CHECK(rp->info().palette);
    std::uint32_t one = 0;
    rp->readWindowRgba(1, 0, 1, 1, 1, 1, &one);
    const auto idx = static_cast<std::uint32_t>(pattern(1, 0, 0));
    CHECK_EQ(one, 0xFF000000u | (idx << 16) | 0xFFu);

    // Overview: the main image is all 1, the half-size overview all 2.
    Tiff ot;
    Image full;
    full.width = 64;
    full.height = 64;
    full.values.assign(64 * 64, 1.0);
    full.compression = 8;
    Image half = full;
    half.width = 32;
    half.height = 32;
    half.values.assign(32 * 32, 2.0);
    half.subfile = 1;
    ot.images = {full, half};
    ot.scale = {1, 1, 0};
    ot.tiepoint = {0, 0, 0, 0, 64, 0};
    auto ro = openTiff("overview.tif", ot);
    CHECK_EQ(ro->info().overviews, 1);
    std::vector<float> w(16 * 16);
    ro->readWindow(0, 0, 0, 64, 64, 16, 16, w.data());
    CHECK_NEAR(w[0], 2.0, 1e-9);
    std::vector<float> full64(64 * 64);
    ro->readWindow(0, 0, 0, 64, 64, 64, 64, full64.data());
    CHECK_NEAR(full64[100], 1.0, 1e-9);
    CHECK_NEAR(*ro->sample(10, 10), 1.0, 1e-9);

    // Unsupported compression is reported when opening.
    Tiff jt;
    Image j = patternImage(4, 4, 1, SampleType::UInt8);
    j.compression = 7;
    jt.images.push_back(j);
    std::string err;
    CHECK(!Raster::open(writeFile("jpeg.tif", TiffBuilder(jt).build()), &err));
    CHECK(err.find("JPEG") != std::string::npos);
    CHECK(!Raster::open(writeText("notatiff.tif", "II*garbage"), &err));
}

TEST_CASE("raster: large tiled file read lazily with a small cache")
{
    Tiff t;
    Image im;
    im.width = 600;
    im.height = 500;
    im.type = SampleType::Int16;
    im.values.resize(static_cast<std::size_t>(600 * 500));
    for (int y = 0; y < 500; ++y)
        for (int x = 0; x < 600; ++x)
            im.values[static_cast<std::size_t>(y * 600 + x)] = (x + 3 * y) % 1000;
    im.tiled = true;
    im.blockWidth = 64;
    im.blockHeight = 64;
    im.compression = 8;
    im.predictor = 2;
    t.images.push_back(im);
    t.scale = {1, 1, 0};
    t.tiepoint = {0, 0, 0, 0, 500, 0};
    auto r = openTiff("large.tif", t);
    r->setCacheLimit(1);
    for (int i = 0; i < 2000; ++i)
    {
        const int x = (i * 7919) % 600, y = (i * 104729) % 500;
        CHECK_NEAR(r->value(x, y), (x + 3 * y) % 1000, 1e-9);
    }
    std::vector<float> win(50 * 40);
    CHECK(r->readWindow(0, 100, 200, 100, 80, 50, 40, win.data()));
    CHECK_NEAR(win[0], (101 + 3 * 201) % 1000, 1e-9);
    CHECK(r->lastError().empty());
}

TEST_CASE("raster: writer round trip")
{
    std::vector<float> v(30 * 20);
    for (int i = 0; i < 600; ++i)
        v[static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.5f;
    v[7] = -32768.0f;
    raster::GeoTransform g;
    g.originX = 5000;
    g.originY = 9000;
    g.pixelWidth = 2;
    g.pixelHeight = -2;
    const fs::path p = tempDir() / "written.tif";
    std::string err;
    CHECK(raster::writeGeoTiff(p, 30, 20, v, g, 2229, -32768.0, crs::kUsSurveyFoot, &err));
    auto r = Raster::open(p, &err);
    CHECK(r != nullptr);
    CHECK_EQ(r->info().crs->epsg, 2229);
    CHECK_NEAR(r->info().verticalUnitToMeters, crs::kUsSurveyFoot, 1e-12);
    CHECK(std::isnan(r->value(7, 0)));
    CHECK_NEAR(r->value(29, 19), 599 * 0.5, 1e-6);
    CHECK_NEAR(r->info().transform.originY, 9000, 1e-9);
}

// ---------------------------------------------------------------------------
// Elevations from a DEM
// ---------------------------------------------------------------------------
namespace
{

/// Plane DEM in UTM 11N: z = 10 + 0.01 (x - 390000) + 0.02 (y - 3740000) metres.
std::shared_ptr<Raster> planeDem(std::optional<double> noDataAt = std::nullopt)
{
    const int w = 200, h = 200;
    std::vector<float> v(static_cast<std::size_t>(w * h));
    raster::GeoTransform g;
    g.originX = 390000;
    g.originY = 3740000 + 10.0 * h;
    g.pixelWidth = 10;
    g.pixelHeight = -10;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
        {
            const double x = g.originX + (c + 0.5) * 10, y = g.originY - (r + 0.5) * 10;
            v[static_cast<std::size_t>(r * w + c)] =
                static_cast<float>(10 + 0.01 * (x - 390000) + 0.02 * (y - 3740000));
        }
    if (noDataAt)
        v[static_cast<std::size_t>(*noDataAt)] = -9999.0f;
    const fs::path p = tempDir() / "plane-dem.tif";
    raster::writeGeoTiff(p, w, h, v, g, 32611, -9999.0, 1.0);
    std::string err;
    return std::shared_ptr<Raster>(Raster::open(p, &err).release());
}

double plane(double x, double y)
{
    return 10 + 0.01 * (x - 390000) + 0.02 * (y - 3740000);
}

const char* kEpanet = R"([TITLE]
DEM test

[JUNCTIONS]
;ID   Elev   Demand
 J1   0      1       ; keep this comment
 J2   55.5   1
 J3   0      1
 J4   0      1

[RESERVOIRS]
 R1   80

[TANKS]
 T1   0   2   0   5   10   0

[PIPES]
 P1  R1  J1  100  200  100  0  Open
 P2  J1  J2  100  200  100  0  Open
 P3  J2  J3  100  200  100  0  Open
 P4  J3  T1  100  200  100  0  Open

[OPTIONS]
 Units  LPS

[COORDINATES]
 J1   390500   3740500
 J2   391000   3741000
 J3   389000   3741000
 J4   390000.0001   3740000.0001
 R1   390100   3740100
 T1   391500   3741500

[END]
)";

} // namespace

TEST_CASE("elevation: EPANET junctions, tanks and reservoirs from a DEM")
{
    auto dem = planeDem(4 * 200 + 0); // NoData at row 4, column 0... not under any node
    auto model = core::Model::fromText(kEpanet);
    core::TerrainSampler terrain(dem, "EPSG:32611", model.units());
    CHECK(!terrain.reprojects());
    CHECK_NEAR(terrain.verticalFactor(), 1.0, 1e-12);

    core::ElevationOptions o;
    auto plan = core::planElevations(model, terrain, o);
    std::map<std::string, core::ElevationChange> by;
    for (const auto& c : plan)
        by[c.id] = c;
    CHECK(!by.count("R1")); // reservoirs are off by default
    CHECK(by["J1"].status == core::ElevationChange::Status::Update);
    CHECK_NEAR(*by["J1"].newValue, plane(390500, 3740500), 0.006);
    CHECK(by["J3"].status == core::ElevationChange::Status::Outside);
    CHECK(by["T1"].status == core::ElevationChange::Status::Update);
    CHECK(by["J4"].status == core::ElevationChange::Status::Update);

    o.scope = core::ElevationScope::Missing;
    o.reservoirs = true;
    plan = core::planElevations(model, terrain, o);
    by.clear();
    for (const auto& c : plan)
        by[c.id] = c;
    CHECK(!by.count("J2")); // has an elevation already
    CHECK(!by.count("R1")); // has a head already
    CHECK(by.count("J1") && by.count("J4") && by.count("T1"));

    o.scope = core::ElevationScope::Selected;
    o.selected = {"J2"};
    plan = core::planElevations(model, terrain, o);
    CHECK_EQ(plan.size(), std::size_t{1});
    CHECK_EQ(plan[0].id, std::string("J2"));

    o.scope = core::ElevationScope::All;
    plan = core::planElevations(model, terrain, o);
    for (const auto& c : plan)
    {
        if (c.id == "R1")
            CHECK_EQ(c.field, std::string("Head"));
    }
    const int n = core::applyElevations(model, plan);
    CHECK_EQ(n, 5); // J1 J2 J4 T1 R1
    const std::string text = model.toText();
    CHECK(text.find("keep this comment") != std::string::npos);
    const auto rows = model.rowsFor(core::Category::Node, "J1");
    CHECK_NEAR(*text::toDouble(rows[0].tokens[1]), plane(390500, 3740500), 0.006);
    CHECK_EQ(text::trim(model.rowsFor(core::Category::Node, "J3")[0].tokens[1]), std::string("0"));
}

TEST_CASE("elevation: reprojection and vertical units")
{
    auto dem = planeDem();
    // The same nodes in California zone 5 (US survey feet), a US-units model.
    std::string text = kEpanet;
    text = text.substr(0, text.find("[OPTIONS]")) + "[OPTIONS]\n Units GPM\n\n[COORDINATES]\n";
    const auto utm = crs::fromEpsg(32611);
    const auto ca5 = crs::fromEpsg(2229);
    CHECK(utm && ca5);
    const std::pair<const char*, std::pair<double, double>> nodes[] = {
        {"J1", {390500, 3740500}}, {"J2", {391000, 3741000}}, {"T1", {391500, 3741500}}};
    for (const auto& [id, xy] : nodes)
    {
        double x = 0, y = 0;
        CHECK(crs::transform(*utm, *ca5, xy.first, xy.second, x, y));
        text += std::string(" ") + id + " " + text::formatDouble(x, 15) + " " +
                text::formatDouble(y, 15) + "\n";
    }
    text += "\n[END]\n";
    auto model = core::Model::fromText(text);
    core::TerrainSampler terrain(dem, "EPSG:2229", model.units());
    CHECK(terrain.reprojects());
    CHECK_NEAR(terrain.verticalFactor(), 1.0 / 0.3048, 1e-9);
    double z = 0;
    const auto* j2 = model.findNode("J2");
    CHECK(terrain.sample(j2->pos.x, j2->pos.y, z) == core::TerrainSampler::Status::Ok);
    CHECK_NEAR(z, plane(391000, 3741000) / 0.3048, 0.01);

    // A DEM in feet: values are already in project units.
    core::TerrainSampler feet(dem, "EPSG:2229", model.units(), 0.3048);
    CHECK(feet.sample(j2->pos.x, j2->pos.y, z) == core::TerrainSampler::Status::Ok);
    CHECK_NEAR(z, plane(391000, 3741000), 0.01);

    // NoData under a node
    auto holed = planeDem(100 * 200 + 50); // pixel containing (390505, 3740995)
    core::TerrainSampler h(holed, "EPSG:32611", model.units());
    CHECK(h.sample(390505, 3740995, z) == core::TerrainSampler::Status::NoData);
    CHECK(h.sample(380000, 3740995, z) == core::TerrainSampler::Status::Outside);
}

TEST_CASE("elevation: SWMM invert or maximum depth from the rim")
{
    auto dem = planeDem();
    const char* swmm = R"([OPTIONS]
FLOW_UNITS CMS

[JUNCTIONS]
;Name Elev MaxDepth InitDepth SurDepth Aponded
 N1   5    2        0         0        0
 N2   8    0        0         0        0
 N3   40   3        0         0        0

[OUTFALLS]
 OUT1 1 FREE

[CONDUITS]
 C1 N1 N2 100 0.01 0 0
 C2 N2 N3 100 0.01 0 0
 C3 N3 OUT1 100 0.01 0 0

[COORDINATES]
 N1   390500   3740500
 N2   391000   3741000
 N3   390200   3740200
 OUT1 391200   3741200
)";
    auto model = core::Model::fromText(swmm);
    CHECK(model.dialect() == core::Dialect::Swmm);
    core::TerrainSampler terrain(dem, "EPSG:32611", model.units());
    core::ElevationOptions o;
    o.swmmMode = core::SwmmElevationMode::Invert;
    auto plan = core::planElevations(model, terrain, o);
    std::map<std::string, core::ElevationChange> by;
    for (const auto& c : plan)
        by[c.id] = c;
    CHECK_NEAR(*by["N1"].newValue, plane(390500, 3740500) - 2, 0.006);
    CHECK(by["OUT1"].status == core::ElevationChange::Status::Skipped);
    CHECK(!by["N2"].note.empty());

    o.swmmMode = core::SwmmElevationMode::MaxDepth;
    plan = core::planElevations(model, terrain, o);
    by.clear();
    for (const auto& c : plan)
        by[c.id] = c;
    CHECK_EQ(by["N1"].field, std::string("MaxDepth"));
    CHECK_NEAR(*by["N1"].newValue, plane(390500, 3740500) - 5, 0.006);
    CHECK_NEAR(*by["N3"].newValue, 0.0, 1e-12); // ground below the invert
    CHECK(core::applyElevations(model, plan) >= 2);
    CHECK_NEAR(*text::toDouble(model.rowsFor(core::Category::Node, "N1")[0].tokens[2]),
               plane(390500, 3740500) - 5, 0.006);
}

TEST_CASE("raster: files written by libtiff")
{
    const fs::path dir = fs::path(MIRAB_SOURCE_DIR) / "tests" / "data";
    std::string err;
    auto lzw = Raster::open(dir / "libtiff-int16-lzw-predictor2.tif", &err);
    CHECK(lzw != nullptr);
    CHECK_EQ(lzw->info().compression, std::string("LZW"));
    auto fp = Raster::open(dir / "libtiff-float32-deflate-predictor3.tif", &err);
    CHECK(fp != nullptr);
    CHECK(fp->info().type == SampleType::Float32);
    auto rgb = Raster::open(dir / "libtiff-rgb-packbits.tif", &err);
    CHECK(rgb != nullptr);
    CHECK(rgb->isImagery());
    for (int y = 0; y < 30; ++y)
    {
        for (int x = 0; x < 40; ++x)
        {
            CHECK_NEAR(lzw->value(x, y), (x * 37 + y * 101) % 2000 - 1000, 0);
            CHECK_NEAR(fp->value(x, y), 50 + x * 0.25 - y * 0.125, 0);
            CHECK_NEAR(rgb->value(x, y, 0), (x * 5) % 256, 0);
            CHECK_NEAR(rgb->value(x, y, 1), (y * 7) % 256, 0);
            CHECK_NEAR(rgb->value(x, y, 2), (x + y) % 256, 0);
        }
    }
}

TEST_CASE("settings: raster layers and map service in the sidecar")
{
    const fs::path inp = tempDir() / "layers-model.inp";
    core::ProjectSettings s;
    s.crs = "EPSG:2229";
    core::RasterLayerSettings dem;
    dem.path = "terrain/dem.tif";
    dem.style = "hillshade-color";
    dem.opacity = 0.4;
    dem.exaggeration = 3;
    dem.verticalUnit = crs::kUsSurveyFoot;
    core::RasterLayerSettings photo;
    photo.path = "photo.tif";
    photo.style = "image";
    photo.visible = false;
    s.rasters = {dem, photo};
    s.basemap = "custom:County imagery";
    s.mapService.id = s.basemap;
    s.mapService.name = "County imagery";
    s.mapService.type = "wms";
    s.mapService.url = "https://maps.example.org/wms";
    s.mapService.layer = "ortho";
    s.mapService.crs = "EPSG:3857";
    s.saveFor(inp);
    const auto back = core::ProjectSettings::loadFor(inp);
    CHECK_EQ(back.rasters.size(), std::size_t{2});
    CHECK_EQ(back.rasters[0].path, dem.path);
    CHECK_EQ(back.rasters[0].style, dem.style);
    CHECK_NEAR(back.rasters[0].opacity, 0.4, 1e-12);
    CHECK_NEAR(back.rasters[0].exaggeration, 3, 1e-12);
    CHECK_NEAR(back.rasters[0].verticalUnit, crs::kUsSurveyFoot, 1e-15);
    CHECK(back.rasters[0].visible);
    CHECK(!back.rasters[1].visible);
    CHECK_EQ(back.mapService.layer, std::string("ortho"));
    CHECK_EQ(back.mapService.type, std::string("wms"));
    CHECK(back.mapService.valid());

    // The service definition is kept only while it is the basemap.
    s.basemap = "osm";
    s.saveFor(inp);
    CHECK(!core::ProjectSettings::loadFor(inp).mapService.valid());
}
