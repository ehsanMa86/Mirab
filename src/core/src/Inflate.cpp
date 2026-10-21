/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/core/src/Inflate.cpp
 * Purpose: Implementation of a deflate decoder (RFC 1951) with the zlib wrapper (RFC 1950).
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
#include "mirab/core/Inflate.h"

#include <array>

namespace mirab::core
{

namespace
{

constexpr int kMaxBits = 15;
constexpr int kFastBits = 9;

/// Least-significant-bit-first reader over the compressed bytes. Reading past
/// the end yields zero bits and marks the stream as truncated once consumed.
class BitReader
{
public:
    BitReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    void refill()
    {
        while (bits_ <= 56)
        {
            if (pos_ < size_)
            {
                buffer_ |= static_cast<std::uint64_t>(data_[pos_++]) << bits_;
            }
            else
            {
                virtual_ += 8;
            }
            bits_ += 8;
        }
    }
    std::uint32_t peek(int n)
    {
        if (bits_ < n)
            refill();
        return static_cast<std::uint32_t>(buffer_ & ((std::uint64_t{1} << n) - 1));
    }
    void consume(int n)
    {
        buffer_ >>= n;
        bits_ -= n;
        if (bits_ < virtual_)
            truncated_ = true;
    }
    std::uint32_t read(int n)
    {
        if (n == 0)
            return 0;
        const std::uint32_t v = peek(n);
        consume(n);
        return v;
    }
    void alignToByte() { consume(bits_ % 8); }
    bool truncated() const { return truncated_; }
    /// Bytes of input not yet consumed (after alignToByte()).
    std::size_t position() const { return pos_ - static_cast<std::size_t>((bits_ - virtual_) / 8); }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::uint64_t buffer_ = 0;
    int bits_ = 0;
    int virtual_ = 0;
    bool truncated_ = false;
};

/// Canonical Huffman code: counts per length and symbols ordered by code,
/// plus a lookup table for codes of up to kFastBits bits.
struct Huffman
{
    std::array<std::uint16_t, kMaxBits + 1> count{};
    std::array<std::uint16_t, 320> symbol{};
    std::array<std::uint16_t, 1 << kFastBits> fast{}; ///< (symbol << 4) | length, 0 when longer

    bool build(const std::uint8_t* lengths, int n)
    {
        count.fill(0);
        fast.fill(0);
        for (int s = 0; s < n; ++s)
            ++count[lengths[s]];
        if (count[0] == n)
            return true; // no codes: only an error if a symbol is decoded
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            left = (left << 1) - count[len];
            if (left < 0)
                return false; // over-subscribed
        }
        std::array<std::uint16_t, kMaxBits + 2> offs{};
        for (int len = 1; len < kMaxBits; ++len)
            offs[len + 1] = static_cast<std::uint16_t>(offs[len] + count[len]);
        for (int s = 0; s < n; ++s)
        {
            if (lengths[s])
                symbol[offs[lengths[s]]++] = static_cast<std::uint16_t>(s);
        }
        // Canonical codes, bit-reversed for the LSB-first table.
        std::array<int, kMaxBits + 2> next{};
        int code = 0;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            code = (code + (len > 1 ? count[len - 1] : 0)) << 1;
            next[len] = code;
        }
        for (int s = 0; s < n; ++s)
        {
            const int len = lengths[s];
            if (len == 0)
                continue;
            const int c = next[len]++;
            if (len > kFastBits)
                continue;
            int rev = 0;
            for (int i = 0; i < len; ++i)
                rev |= ((c >> i) & 1) << (len - 1 - i);
            for (int k = rev; k < (1 << kFastBits); k += 1 << len)
                fast[static_cast<std::size_t>(k)] = static_cast<std::uint16_t>((s << 4) | len);
        }
        return true;
    }
};

/// Returns the next symbol, or -1 when the bits match no code.
int decode(BitReader& in, const Huffman& h)
{
    const std::uint32_t bits = in.peek(kMaxBits);
    const std::uint16_t entry = h.fast[bits & ((1u << kFastBits) - 1)];
    if (entry)
    {
        in.consume(entry & 15);
        return entry >> 4;
    }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        code |= static_cast<int>((bits >> (len - 1)) & 1);
        const int count = h.count[static_cast<std::size_t>(len)];
        if (code - count < first)
        {
            in.consume(len);
            return h.symbol[static_cast<std::size_t>(index + (code - first))];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                           15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                           67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool fail(std::string* error, const char* message)
{
    if (error)
        *error = message;
    return false;
}

bool inflateBlock(BitReader& in, const Huffman& lit, const Huffman& dist,
                  std::vector<std::uint8_t>& out, std::string* error)
{
    for (;;)
    {
        int sym = decode(in, lit);
        if (sym < 0 || in.truncated())
            return fail(error, in.truncated() ? "truncated deflate data" : "invalid deflate code");
        if (sym < 256)
        {
            out.push_back(static_cast<std::uint8_t>(sym));
            continue;
        }
        if (sym == 256)
            return true;
        sym -= 257;
        if (sym >= 29)
            return fail(error, "invalid deflate length code");
        const std::size_t length = kLengthBase[sym] + in.read(kLengthExtra[sym]);
        const int dsym = decode(in, dist);
        if (dsym < 0 || dsym >= 30)
            return fail(error, "invalid deflate distance code");
        const std::size_t distance = kDistBase[dsym] + in.read(kDistExtra[dsym]);
        if (in.truncated())
            return fail(error, "truncated deflate data");
        if (distance > out.size())
            return fail(error, "deflate distance too far back");
        std::size_t from = out.size() - distance;
        for (std::size_t i = 0; i < length; ++i)
            out.push_back(out[from++]);
    }
}

const Huffman& fixedLiteral()
{
    static const Huffman h = []
    {
        std::array<std::uint8_t, 288> len{};
        for (int i = 0; i < 288; ++i)
            len[static_cast<std::size_t>(i)] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        Huffman t;
        t.build(len.data(), 288);
        return t;
    }();
    return h;
}

const Huffman& fixedDistance()
{
    static const Huffman h = []
    {
        std::array<std::uint8_t, 30> len{};
        len.fill(5);
        Huffman t;
        t.build(len.data(), 30);
        return t;
    }();
    return h;
}

bool dynamicTables(BitReader& in, Huffman& lit, Huffman& dist, std::string* error)
{
    static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                11, 4,  12, 3, 13, 2, 14, 1, 15};
    const int nlen = static_cast<int>(in.read(5)) + 257;
    const int ndist = static_cast<int>(in.read(5)) + 1;
    const int ncode = static_cast<int>(in.read(4)) + 4;
    if (nlen > 286 || ndist > 30)
        return fail(error, "bad deflate table sizes");
    std::array<std::uint8_t, 320> lengths{};
    for (int i = 0; i < ncode; ++i)
        lengths[kOrder[i]] = static_cast<std::uint8_t>(in.read(3));
    Huffman codes;
    if (!codes.build(lengths.data(), 19))
        return fail(error, "bad deflate code lengths");
    lengths.fill(0);
    int index = 0;
    while (index < nlen + ndist)
    {
        const int sym = decode(in, codes);
        if (sym < 0 || in.truncated())
            return fail(error, "bad deflate code lengths");
        if (sym < 16)
        {
            lengths[static_cast<std::size_t>(index++)] = static_cast<std::uint8_t>(sym);
            continue;
        }
        std::uint8_t value = 0;
        int repeat = 0;
        if (sym == 16)
        {
            if (index == 0)
                return fail(error, "deflate repeat without a previous length");
            value = lengths[static_cast<std::size_t>(index - 1)];
            repeat = 3 + static_cast<int>(in.read(2));
        }
        else if (sym == 17)
        {
            repeat = 3 + static_cast<int>(in.read(3));
        }
        else
        {
            repeat = 11 + static_cast<int>(in.read(7));
        }
        if (index + repeat > nlen + ndist)
            return fail(error, "too many deflate code lengths");
        while (repeat--)
            lengths[static_cast<std::size_t>(index++)] = value;
    }
    if (lengths[256] == 0)
        return fail(error, "deflate block without an end code");
    if (!lit.build(lengths.data(), nlen) || !dist.build(lengths.data() + nlen, ndist))
        return fail(error, "bad deflate code lengths");
    return true;
}

bool inflateStream(BitReader& in, std::vector<std::uint8_t>& out, std::string* error)
{
    bool last = false;
    while (!last)
    {
        last = in.read(1) != 0;
        const std::uint32_t type = in.read(2);
        if (type == 0)
        {
            in.alignToByte();
            const std::uint32_t len = in.read(16);
            const std::uint32_t nlen = in.read(16);
            if (in.truncated() || (len ^ 0xFFFFu) != nlen)
                return fail(error, "bad stored deflate block");
            for (std::uint32_t i = 0; i < len; ++i)
                out.push_back(static_cast<std::uint8_t>(in.read(8)));
            if (in.truncated())
                return fail(error, "truncated deflate data");
        }
        else if (type == 1)
        {
            if (!inflateBlock(in, fixedLiteral(), fixedDistance(), out, error))
                return false;
        }
        else if (type == 2)
        {
            Huffman lit, dist;
            if (!dynamicTables(in, lit, dist, error) || !inflateBlock(in, lit, dist, out, error))
                return false;
        }
        else
        {
            return fail(error, "invalid deflate block type");
        }
    }
    return true;
}

} // namespace

std::uint32_t adler32(const std::uint8_t* data, std::size_t size, std::uint32_t adler)
{
    std::uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (size > 0)
    {
        const std::size_t n = size < 5552 ? size : 5552; // keeps the sums below 2^32
        for (std::size_t i = 0; i < n; ++i)
        {
            a += data[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        data += n;
        size -= n;
    }
    return (b << 16) | a;
}

bool inflateRaw(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                std::string* error, std::size_t sizeHint)
{
    if (sizeHint)
        out.reserve(out.size() + sizeHint);
    BitReader in(data, size);
    return inflateStream(in, out, error);
}

bool zlibDecompress(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                    std::string* error, std::size_t sizeHint)
{
    if (size < 2 || (data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0)
        return fail(error, "not a zlib stream");
    if (data[1] & 0x20)
        return fail(error, "zlib preset dictionaries are not supported");
    if (sizeHint)
        out.reserve(out.size() + sizeHint);
    const std::size_t start = out.size();
    BitReader in(data + 2, size - 2);
    if (!inflateStream(in, out, error))
        return false;
    in.alignToByte();
    const std::size_t at = 2 + in.position();
    if (at + 4 > size)
        return true; // some writers omit the checksum
    const std::uint32_t expected = (static_cast<std::uint32_t>(data[at]) << 24) |
                                   (static_cast<std::uint32_t>(data[at + 1]) << 16) |
                                   (static_cast<std::uint32_t>(data[at + 2]) << 8) | data[at + 3];
    if (adler32(out.data() + start, out.size() - start) != expected)
        return fail(error, "zlib checksum mismatch");
    return true;
}

} // namespace mirab::core
