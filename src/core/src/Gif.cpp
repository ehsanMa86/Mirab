/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Gif.cpp
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
#include "mirab/core/Gif.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace mirab::core::gif
{

namespace
{
constexpr int kMaxCodes = 4096;

std::uint32_t packRgb(int r, int g, int b)
{
    return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) |
           static_cast<std::uint32_t>(b);
}

int cellOf(std::uint32_t rgb)
{
    return static_cast<int>(((rgb >> 19) & 31) << 10 | ((rgb >> 11) & 31) << 5 | ((rgb >> 3) & 31));
}

/// Bits of a colour table that holds @p colors entries (1 to 8).
int tableBits(std::size_t colors)
{
    int bits = 1;
    while ((std::size_t{1} << bits) < colors && bits < 8)
        ++bits;
    return bits;
}

void put16(std::ostream& out, int v)
{
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
}

void putTable(std::ostream& out, const Palette& palette, int bits)
{
    const std::size_t size = std::size_t{1} << bits;
    for (std::size_t i = 0; i < size; ++i)
    {
        const Color c = i < palette.size() ? palette[i] : Color{};
        out.put(static_cast<char>(c.r));
        out.put(static_cast<char>(c.g));
        out.put(static_cast<char>(c.b));
    }
}

class BitWriter
{
public:
    explicit BitWriter(std::vector<std::uint8_t>& out) : out_(out) {}
    void put(int code, int width)
    {
        acc_ |= static_cast<std::uint32_t>(code) << bits_;
        bits_ += width;
        while (bits_ >= 8)
        {
            out_.push_back(static_cast<std::uint8_t>(acc_ & 0xFF));
            acc_ >>= 8;
            bits_ -= 8;
        }
    }
    void flush()
    {
        if (bits_ > 0)
            out_.push_back(static_cast<std::uint8_t>(acc_ & 0xFF));
        acc_ = 0;
        bits_ = 0;
    }

private:
    std::vector<std::uint8_t>& out_;
    std::uint32_t acc_ = 0;
    int bits_ = 0;
};

/// Prefix-plus-symbol dictionary of the encoder (open addressing).
class CodeTable
{
public:
    CodeTable() : keys_(kSize), codes_(kSize) { clear(); }
    void clear() { std::fill(keys_.begin(), keys_.end(), -1); }
    int find(int key) const
    {
        for (std::size_t i = slot(key);; i = (i + 1) & (kSize - 1))
        {
            if (keys_[i] == key)
                return codes_[i];
            if (keys_[i] < 0)
                return -1;
        }
    }
    void insert(int key, int code)
    {
        std::size_t i = slot(key);
        while (keys_[i] >= 0)
            i = (i + 1) & (kSize - 1);
        keys_[i] = key;
        codes_[i] = static_cast<std::int16_t>(code);
    }

private:
    static constexpr std::size_t kSize = 16384;
    static std::size_t slot(int key)
    {
        return (static_cast<std::uint32_t>(key) * 2654435761u) >> (32 - 14);
    }
    std::vector<std::int32_t> keys_;
    std::vector<std::int16_t> codes_;
};

void writeSubBlocks(std::ostream& out, const std::vector<std::uint8_t>& data)
{
    for (std::size_t pos = 0; pos < data.size(); pos += 255)
    {
        const std::size_t n = std::min<std::size_t>(255, data.size() - pos);
        out.put(static_cast<char>(n));
        out.write(reinterpret_cast<const char*>(data.data() + pos),
                  static_cast<std::streamsize>(n));
    }
    out.put(0);
}

Palette trimmed(Palette palette, bool optimize)
{
    if (palette.empty())
        palette.push_back(Color{});
    const std::size_t limit = optimize ? 255 : 256;
    if (palette.size() > limit)
        palette.resize(limit);
    return palette;
}

Palette withTransparentSlot(const Palette& palette, bool optimize)
{
    Palette out = palette;
    if (optimize && out.size() < 256)
        out.push_back(Color{});
    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
Histogram::Histogram() : cells_(32768) {}

void Histogram::add(const PixelView& image, int step)
{
    step = std::max(1, step);
    std::uint32_t last = 0xFFFFFFFF;
    for (int y = 0; y < image.height; y += step)
    {
        for (int x = 0; x < image.width; x += step)
        {
            const std::uint32_t p = image.at(x, y);
            if (!overflow_ && (p & 0xFFFFFF) != last)
            {
                last = p & 0xFFFFFF;
                addExact(last);
            }
            Cell& c = cells_[static_cast<std::size_t>(cellOf(p))];
            ++c.count;
            c.r += (p >> 16) & 0xFF;
            c.g += (p >> 8) & 0xFF;
            c.b += p & 0xFF;
        }
    }
}

void Histogram::addExact(std::uint32_t rgb)
{
    const auto it = std::lower_bound(exact_.begin(), exact_.end(), rgb);
    if (it != exact_.end() && *it == rgb)
        return;
    if (exact_.size() >= 256)
    {
        overflow_ = true;
        exact_.clear();
        return;
    }
    exact_.insert(it, rgb);
}

void Histogram::addColor(Color color, std::uint64_t weight)
{
    if (!overflow_)
        addExact(packRgb(color.r, color.g, color.b));
    Cell& c = cells_[static_cast<std::size_t>(cellOf(packRgb(color.r, color.g, color.b)))];
    c.count += weight;
    c.r += weight * color.r;
    c.g += weight * color.g;
    c.b += weight * color.b;
}

std::size_t Histogram::cells() const
{
    return static_cast<std::size_t>(
        std::count_if(cells_.begin(), cells_.end(), [](const Cell& c) { return c.count > 0; }));
}

Palette medianCut(const Histogram& histogram, int maxColors)
{
    maxColors = std::clamp(maxColors, 1, 256);
    if (!histogram.exactOverflow() && !histogram.exactColors().empty() &&
        static_cast<int>(histogram.exactColors().size()) <= maxColors)
    {
        Palette exact;
        for (const std::uint32_t c : histogram.exactColors())
            exact.push_back(Color{static_cast<std::uint8_t>(c >> 16),
                                  static_cast<std::uint8_t>(c >> 8), static_cast<std::uint8_t>(c)});
        return exact;
    }
    struct Entry
    {
        double v[3];
        const Histogram::Cell* cell;
    };
    std::vector<Entry> entries;
    for (const auto& c : histogram.data())
    {
        if (c.count == 0)
            continue;
        const double n = static_cast<double>(c.count);
        entries.push_back({{c.r / n, c.g / n, c.b / n}, &c});
    }
    if (entries.empty())
        return {Color{}};

    auto average = [&](std::size_t begin, std::size_t end)
    {
        double n = 0, r = 0, g = 0, b = 0;
        for (std::size_t i = begin; i < end; ++i)
        {
            const auto* c = entries[i].cell;
            n += static_cast<double>(c->count);
            r += static_cast<double>(c->r);
            g += static_cast<double>(c->g);
            b += static_cast<double>(c->b);
        }
        auto ch = [n](double s)
        { return static_cast<std::uint8_t>(std::clamp(std::lround(s / n), 0L, 255L)); };
        return Color{ch(r), ch(g), ch(b)};
    };

    struct Box
    {
        std::size_t begin, end;
        double count = 0;
        int channel = 0;
        double range = 0;
    };
    // Green differences are the most visible, blue the least.
    const double weight[3] = {1.0, 1.2, 0.8};
    auto measure = [&](Box& box)
    {
        double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
        box.count = 0;
        for (std::size_t i = box.begin; i < box.end; ++i)
        {
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], entries[i].v[k]);
                hi[k] = std::max(hi[k], entries[i].v[k]);
            }
            box.count += static_cast<double>(entries[i].cell->count);
        }
        box.range = 0;
        for (int k = 0; k < 3; ++k)
        {
            const double r = (hi[k] - lo[k]) * weight[k];
            if (r > box.range)
            {
                box.range = r;
                box.channel = k;
            }
        }
    };

    std::vector<Box> boxes(1, Box{0, entries.size()});
    measure(boxes[0]);
    while (static_cast<int>(boxes.size()) < maxColors)
    {
        int best = -1;
        double bestScore = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            const Box& b = boxes[i];
            if (b.end - b.begin < 2 || b.range <= 0)
                continue;
            const double score = b.range * std::sqrt(b.count);
            if (score > bestScore)
            {
                bestScore = score;
                best = static_cast<int>(i);
            }
        }
        if (best < 0)
            break;
        Box box = boxes[static_cast<std::size_t>(best)];
        const int ch = box.channel;
        std::sort(entries.begin() + static_cast<std::ptrdiff_t>(box.begin),
                  entries.begin() + static_cast<std::ptrdiff_t>(box.end),
                  [ch](const Entry& a, const Entry& b) { return a.v[ch] < b.v[ch]; });
        double half = box.count / 2.0, sum = 0;
        std::size_t split = box.begin + 1;
        for (std::size_t i = box.begin; i < box.end - 1; ++i)
        {
            sum += static_cast<double>(entries[i].cell->count);
            split = i + 1;
            if (sum >= half)
                break;
        }
        Box a{box.begin, split}, b{split, box.end};
        measure(a);
        measure(b);
        boxes[static_cast<std::size_t>(best)] = a;
        boxes.push_back(b);
    }
    Palette palette;
    palette.reserve(boxes.size());
    for (const Box& b : boxes)
    {
        const Color c = average(b.begin, b.end);
        if (std::find(palette.begin(), palette.end(), c) == palette.end())
            palette.push_back(c);
    }
    return palette;
}

Palette animationPalette(const std::vector<PixelView>& samples, const std::vector<Color>& keyColors,
                         int maxColors)
{
    Palette keys;
    for (const Color& c : keyColors)
    {
        if (std::find(keys.begin(), keys.end(), c) == keys.end() &&
            static_cast<int>(keys.size()) < maxColors / 2)
            keys.push_back(c);
    }
    Histogram h;
    for (const PixelView& v : samples)
    {
        const double pixels = static_cast<double>(v.width) * v.height;
        h.add(v, std::max(1, static_cast<int>(std::sqrt(pixels / 250000.0))));
    }
    Palette palette = keys;
    for (const Color& c : medianCut(h, maxColors - static_cast<int>(keys.size())))
    {
        if (std::find(palette.begin(), palette.end(), c) == palette.end())
            palette.push_back(c);
    }
    return palette;
}

// ---------------------------------------------------------------------------
// Quantizer
// ---------------------------------------------------------------------------
Quantizer::Quantizer(Palette palette, Dither dither)
    : palette_(std::move(palette)), dither_(dither), cache_(std::size_t{1} << 18, -1),
      exact_((std::size_t{1} << 24) / 64, 0)
{
    if (palette_.empty())
        palette_.push_back(Color{});
    for (const Color& c : palette_)
    {
        const std::uint32_t rgb = packRgb(c.r, c.g, c.b);
        exact_[rgb >> 6] |= std::uint64_t{1} << (rgb & 63);
    }
}

int Quantizer::nearest(int r, int g, int b) const
{
    int best = 0;
    long bestD = -1;
    for (std::size_t i = 0; i < palette_.size(); ++i)
    {
        const long dr = r - palette_[i].r, dg = g - palette_[i].g, db = b - palette_[i].b;
        const long d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (bestD < 0 || d < bestD)
        {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int Quantizer::exactIndex(std::uint32_t rgb) const
{
    for (std::size_t i = 0; i < palette_.size(); ++i)
    {
        if (packRgb(palette_[i].r, palette_[i].g, palette_[i].b) == rgb)
            return static_cast<int>(i);
    }
    return -1;
}

void Quantizer::map(const PixelView& image, std::vector<std::uint8_t>& indices)
{
    static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const double strength =
        0.6 * 255.0 / std::cbrt(static_cast<double>(std::max<std::size_t>(2, palette_.size())));
    indices.resize(static_cast<std::size_t>(image.width) * image.height);
    std::unordered_map<std::uint32_t, std::uint8_t> exactMap;
    auto cached = [this](int r, int g, int b)
    {
        const int key = (r >> 2) << 12 | (g >> 2) << 6 | (b >> 2);
        std::int16_t& slot = cache_[static_cast<std::size_t>(key)];
        if (slot < 0)
            slot = static_cast<std::int16_t>(nearest((r & ~3) + 2, (g & ~3) + 2, (b & ~3) + 2));
        return static_cast<std::uint8_t>(slot);
    };
    std::uint32_t memoRgb = 0xFFFFFFFF;
    std::uint8_t memoIdx = 0;
    for (int y = 0; y < image.height; ++y)
    {
        std::uint8_t* row = indices.data() + static_cast<std::size_t>(y) * image.width;
        for (int x = 0; x < image.width; ++x)
        {
            const std::uint32_t rgb = image.at(x, y) & 0xFFFFFF;
            if (rgb == memoRgb)
            {
                row[x] = memoIdx;
                continue;
            }
            const int r = static_cast<int>(rgb >> 16), g = static_cast<int>((rgb >> 8) & 0xFF),
                      b = static_cast<int>(rgb & 0xFF);
            if (exact_[rgb >> 6] & (std::uint64_t{1} << (rgb & 63)))
            {
                auto it = exactMap.find(rgb);
                if (it == exactMap.end())
                    it = exactMap.emplace(rgb, static_cast<std::uint8_t>(exactIndex(rgb))).first;
                row[x] = memoIdx = it->second;
                memoRgb = rgb;
            }
            else if (dither_ == Dither::None)
            {
                row[x] = memoIdx = cached(r, g, b);
                memoRgb = rgb;
            }
            else
            {
                const int off = static_cast<int>(
                    std::lround(((bayer[y & 3][x & 3] + 0.5) / 16.0 - 0.5) * strength));
                row[x] = cached(std::clamp(r + off, 0, 255), std::clamp(g + off, 0, 255),
                                std::clamp(b + off, 0, 255));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// LZW
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> lzwEncode(const std::uint8_t* indices, std::size_t count, int minCodeSize)
{
    minCodeSize = std::clamp(minCodeSize, 2, 8);
    std::vector<std::uint8_t> out;
    out.reserve(count / 2 + 16);
    BitWriter bits(out);
    const int clear = 1 << minCodeSize, eoi = clear + 1;
    int width = minCodeSize + 1;
    int next = eoi + 1;
    CodeTable table;
    bits.put(clear, width);
    if (count == 0)
    {
        bits.put(eoi, width);
        bits.flush();
        return out;
    }
    const int mask = clear - 1;
    int prefix = indices[0] & mask;
    for (std::size_t i = 1; i < count; ++i)
    {
        const int k = indices[i] & mask;
        const int key = prefix << 8 | k;
        const int code = table.find(key);
        if (code >= 0)
        {
            prefix = code;
            continue;
        }
        bits.put(prefix, width);
        table.insert(key, next++);
        if (next > (1 << width) && width < 12)
            ++width;
        if (next >= kMaxCodes)
        {
            bits.put(clear, width);
            table.clear();
            width = minCodeSize + 1;
            next = eoi + 1;
        }
        prefix = k;
    }
    bits.put(prefix, width);
    // The decoder adds one more entry after the last code before it reads the end code.
    if (++next > (1 << width) && width < 12)
        ++width;
    bits.put(eoi, width);
    bits.flush();
    return out;
}

bool lzwDecode(const std::uint8_t* data, std::size_t size, int minCodeSize,
               std::vector<std::uint8_t>& out)
{
    if (minCodeSize < 2 || minCodeSize > 8)
        return false;
    const int clear = 1 << minCodeSize, eoi = clear + 1;
    std::vector<std::int16_t> prefix(kMaxCodes, -1);
    std::vector<std::uint8_t> suffix(kMaxCodes, 0);
    for (int i = 0; i < clear; ++i)
        suffix[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    std::vector<std::uint8_t> stack;
    int width = minCodeSize + 1;
    int next = eoi + 1;
    int old = -1;
    int first = 0;
    std::uint32_t acc = 0;
    int have = 0;
    std::size_t pos = 0;
    for (;;)
    {
        while (have < width && pos < size)
        {
            acc |= static_cast<std::uint32_t>(data[pos++]) << have;
            have += 8;
        }
        if (have < width)
            return true; // no end code; accept what was decoded
        int code = static_cast<int>(acc & ((1u << width) - 1));
        acc >>= width;
        have -= width;
        if (code == clear)
        {
            width = minCodeSize + 1;
            next = eoi + 1;
            old = -1;
            continue;
        }
        if (code == eoi)
            return true;
        if (old < 0)
        {
            if (code > clear)
                return false;
            out.push_back(static_cast<std::uint8_t>(code));
            old = first = code;
            continue;
        }
        const int in = code;
        stack.clear();
        if (code == next)
        {
            stack.push_back(static_cast<std::uint8_t>(first));
            code = old;
        }
        else if (code > next)
        {
            return false;
        }
        while (code >= clear)
        {
            if (code >= kMaxCodes || prefix[static_cast<std::size_t>(code)] < 0 ||
                stack.size() > kMaxCodes)
                return false;
            stack.push_back(suffix[static_cast<std::size_t>(code)]);
            code = prefix[static_cast<std::size_t>(code)];
        }
        first = code;
        stack.push_back(static_cast<std::uint8_t>(code));
        out.insert(out.end(), stack.rbegin(), stack.rend());
        if (next < kMaxCodes)
        {
            prefix[static_cast<std::size_t>(next)] = static_cast<std::int16_t>(old);
            suffix[static_cast<std::size_t>(next)] = static_cast<std::uint8_t>(first);
            ++next;
            if (next == (1 << width) && width < 12)
                ++width;
        }
        old = in;
    }
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------
Writer::Writer(std::ostream& out, int width, int height, const Palette& global, int loopCount)
    : out_(out), width_(width), height_(height)
{
    globalBits_ = tableBits(std::max<std::size_t>(2, global.size()));
    out_.write("GIF89a", 6);
    put16(out_, width_);
    put16(out_, height_);
    out_.put(static_cast<char>(0x80 | ((globalBits_ - 1) << 4) | (globalBits_ - 1)));
    out_.put(0); // background colour index
    out_.put(0); // pixel aspect ratio
    putTable(out_, global, globalBits_);
    if (loopCount >= 0)
    {
        out_.put(0x21);
        out_.put(static_cast<char>(0xFF));
        out_.put(11);
        out_.write("NETSCAPE2.0", 11);
        out_.put(3);
        out_.put(1);
        put16(out_, std::min(loopCount, 65535));
        out_.put(0);
    }
}

void Writer::addFrame(const Frame& f)
{
    out_.put(0x21);
    out_.put(static_cast<char>(0xF9));
    out_.put(4);
    out_.put(static_cast<char>(((f.disposal & 7) << 2) | (f.transparent >= 0 ? 1 : 0)));
    put16(out_, std::clamp(f.delayCs, 0, 65535));
    out_.put(static_cast<char>(f.transparent >= 0 ? f.transparent : 0));
    out_.put(0);

    out_.put(0x2C);
    put16(out_, f.x);
    put16(out_, f.y);
    put16(out_, f.width);
    put16(out_, f.height);
    int bits = globalBits_;
    if (!f.local.empty())
    {
        bits = tableBits(std::max<std::size_t>(2, f.local.size()));
        out_.put(static_cast<char>(0x80 | (bits - 1)));
        putTable(out_, f.local, bits);
    }
    else
    {
        out_.put(0);
    }
    const int minCode = std::max(2, bits);
    out_.put(static_cast<char>(minCode));
    writeSubBlocks(out_, lzwEncode(f.indices.data(), f.indices.size(), minCode));
}

bool Writer::finish()
{
    if (!finished_)
    {
        out_.put(0x3B);
        out_.flush();
        finished_ = true;
    }
    return static_cast<bool>(out_);
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------
AnimationWriter::AnimationWriter(std::ostream& out, int width, int height, Palette palette,
                                 const Options& options)
    : options_(options), width_(width), height_(height),
      quantizer_(trimmed(std::move(palette), options.optimize), options.dither),
      writer_(out, width, height, withTransparentSlot(quantizer_.palette(), options.optimize),
              options.loopCount)
{
    if (options_.optimize)
        transparent_ = static_cast<int>(quantizer_.palette().size());
}

void AnimationWriter::addFrame(const PixelView& image)
{
    addFrame(image, options_.delayCs);
}

void AnimationWriter::addFrame(const PixelView& image, int delayCs)
{
    PixelView view = image;
    view.width = std::min(view.width, width_);
    view.height = std::min(view.height, height_);
    quantizer_.map(view, current_);
    if (view.width != width_ || view.height != height_)
    {
        // A smaller picture is padded with the first palette colour.
        std::vector<std::uint8_t> full(static_cast<std::size_t>(width_) * height_, 0);
        for (int y = 0; y < view.height; ++y)
            std::copy_n(current_.begin() + static_cast<std::ptrdiff_t>(y) * view.width, view.width,
                        full.begin() + static_cast<std::ptrdiff_t>(y) * width_);
        current_.swap(full);
    }
    if (previous_.empty())
    {
        pending_ = Frame{0, 0, width_, height_, current_, delayCs, -1, 1, {}};
        havePending_ = true;
        previous_ = current_;
        return;
    }
    int x0 = width_, y0 = height_, x1 = -1, y1 = -1;
    for (int y = 0; y < height_; ++y)
    {
        const std::uint8_t* a = current_.data() + static_cast<std::size_t>(y) * width_;
        const std::uint8_t* b = previous_.data() + static_cast<std::size_t>(y) * width_;
        int first = -1, last = -1;
        for (int x = 0; x < width_; ++x)
        {
            if (a[x] != b[x])
            {
                if (first < 0)
                    first = x;
                last = x;
            }
        }
        if (first >= 0)
        {
            x0 = std::min(x0, first);
            x1 = std::max(x1, last);
            y0 = std::min(y0, y);
            y1 = y;
        }
    }
    if (x1 < 0)
    {
        pending_.delayCs += delayCs;
        return;
    }
    flush();
    Frame f;
    f.delayCs = delayCs;
    f.disposal = 1;
    if (options_.optimize)
    {
        f.x = x0;
        f.y = y0;
        f.width = x1 - x0 + 1;
        f.height = y1 - y0 + 1;
        f.transparent = transparent_;
        f.indices.resize(static_cast<std::size_t>(f.width) * f.height);
        for (int y = 0; y < f.height; ++y)
        {
            for (int x = 0; x < f.width; ++x)
            {
                const std::size_t src = static_cast<std::size_t>(y + y0) * width_ + (x + x0);
                f.indices[static_cast<std::size_t>(y) * f.width + x] =
                    current_[src] == previous_[src] ? static_cast<std::uint8_t>(transparent_)
                                                    : current_[src];
            }
        }
    }
    else
    {
        f.width = width_;
        f.height = height_;
        f.indices = current_;
    }
    pending_ = std::move(f);
    havePending_ = true;
    previous_.swap(current_);
}

void AnimationWriter::flush()
{
    if (!havePending_)
        return;
    writer_.addFrame(pending_);
    ++written_;
    havePending_ = false;
}

bool AnimationWriter::finish()
{
    flush();
    return writer_.finish();
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------
bool decode(std::string_view bytes, Decoded& out, std::string* error)
{
    auto fail = [error](const char* message)
    {
        if (error)
            *error = message;
        return false;
    };
    std::size_t pos = 0;
    auto avail = [&](std::size_t n) { return pos + n <= bytes.size(); };
    auto u8 = [&]() { return static_cast<std::uint8_t>(bytes[pos++]); };
    auto u16 = [&]()
    {
        const int lo = u8();
        const int hi = u8();
        return lo | hi << 8;
    };
    auto table = [&](int bits, Palette& p)
    {
        const std::size_t n = std::size_t{1} << bits;
        if (!avail(3 * n))
            return false;
        p.resize(n);
        for (auto& c : p)
        {
            c.r = u8();
            c.g = u8();
            c.b = u8();
        }
        return true;
    };
    auto skipBlocks = [&]()
    {
        while (avail(1))
        {
            const std::size_t n = u8();
            if (n == 0)
                return true;
            if (!avail(n))
                return false;
            pos += n;
        }
        return false;
    };

    out = Decoded();
    if (!avail(13) || (bytes.substr(0, 6) != "GIF89a" && bytes.substr(0, 6) != "GIF87a"))
        return fail("not a GIF file");
    pos = 6;
    out.width = u16();
    out.height = u16();
    const int flags = u8();
    pos += 2;
    if ((flags & 0x80) && !table((flags & 7) + 1, out.global))
        return fail("truncated colour table");
    const std::size_t canvasSize = static_cast<std::size_t>(out.width) * out.height;
    std::vector<std::uint32_t> canvas(canvasSize, 0);
    Frame control;
    for (;;)
    {
        if (!avail(1))
            return fail("missing trailer");
        const int block = u8();
        if (block == 0x3B)
            return true;
        if (block == 0x21)
        {
            if (!avail(1))
                return fail("truncated extension");
            const int label = u8();
            if (label == 0xF9)
            {
                if (!avail(6) || u8() != 4)
                    return fail("bad graphic control extension");
                const int packed = u8();
                control.delayCs = u16();
                const int t = u8();
                control.transparent = (packed & 1) ? t : -1;
                control.disposal = (packed >> 2) & 7;
                if (!skipBlocks())
                    return fail("bad graphic control extension");
            }
            else if (label == 0xFF && avail(12) && bytes[pos] == 11 &&
                     bytes.substr(pos + 1, 11) == "NETSCAPE2.0")
            {
                pos += 12;
                if (avail(5) && bytes[pos] == 3 && bytes[pos + 1] == 1)
                {
                    pos += 2;
                    out.loopCount = u16();
                }
                if (!skipBlocks())
                    return fail("bad application extension");
            }
            else if (!skipBlocks())
            {
                return fail("truncated extension");
            }
            continue;
        }
        if (block != 0x2C)
            return fail("unknown block");
        if (!avail(9))
            return fail("truncated image descriptor");
        Decoded::Image image;
        Frame& f = image.frame;
        f = control;
        f.x = u16();
        f.y = u16();
        f.width = u16();
        f.height = u16();
        const int packed = u8();
        if ((packed & 0x80) && !table((packed & 7) + 1, f.local))
            return fail("truncated local colour table");
        if (!avail(1))
            return fail("truncated image data");
        const int minCode = u8();
        std::vector<std::uint8_t> data;
        while (true)
        {
            if (!avail(1))
                return fail("truncated image data");
            const std::size_t n = u8();
            if (n == 0)
                break;
            if (!avail(n))
                return fail("truncated image data");
            data.insert(data.end(), bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                        bytes.begin() + static_cast<std::ptrdiff_t>(pos + n));
            pos += n;
        }
        const std::size_t pixels = static_cast<std::size_t>(f.width) * f.height;
        if (!lzwDecode(data.data(), data.size(), minCode, f.indices))
            return fail("corrupt LZW data");
        if (f.indices.size() < pixels)
            return fail("image data too short");
        f.indices.resize(pixels);
        if (packed & 0x40)
        {
            // Interlaced rows: every 8th from 0, every 8th from 4, every 4th from 2, every 2nd
            // from 1.
            std::vector<std::uint8_t> rows(pixels);
            int src = 0;
            for (const auto& [start, step] :
                 {std::pair{0, 8}, std::pair{4, 8}, std::pair{2, 4}, std::pair{1, 2}})
            {
                for (int y = start; y < f.height; y += step, ++src)
                    std::copy_n(f.indices.begin() + static_cast<std::ptrdiff_t>(src) * f.width,
                                f.width, rows.begin() + static_cast<std::ptrdiff_t>(y) * f.width);
            }
            f.indices.swap(rows);
        }
        const Palette& pal = f.local.empty() ? out.global : f.local;
        const std::vector<std::uint32_t> before = canvas;
        for (int y = 0; y < f.height; ++y)
        {
            for (int x = 0; x < f.width; ++x)
            {
                const int cx = f.x + x, cy = f.y + y;
                const int idx = f.indices[static_cast<std::size_t>(y) * f.width + x];
                if (cx >= out.width || cy >= out.height || idx == f.transparent)
                    continue;
                const Color c = idx < static_cast<int>(pal.size())
                                    ? pal[static_cast<std::size_t>(idx)]
                                    : Color{};
                canvas[static_cast<std::size_t>(cy) * out.width + cx] =
                    0xFF000000u | packRgb(c.r, c.g, c.b);
            }
        }
        image.canvas = canvas;
        if (f.disposal == 2)
        {
            for (int y = f.y; y < std::min(out.height, f.y + f.height); ++y)
                for (int x = f.x; x < std::min(out.width, f.x + f.width); ++x)
                    canvas[static_cast<std::size_t>(y) * out.width + x] = 0;
        }
        else if (f.disposal == 3)
        {
            canvas = before;
        }
        out.images.push_back(std::move(image));
        control = Frame();
    }
}

} // namespace mirab::core::gif
