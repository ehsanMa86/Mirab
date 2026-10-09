/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/core/src/Hash.cpp
 * Purpose: Implementation of SHA-256 (FIPS 180-4).
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
#include "anahita/core/Hash.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace anahita::core
{

namespace
{
constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotr(std::uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

class Sha256
{
public:
    void update(const unsigned char* data, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            block_[used_++] = data[i];
            if (used_ == 64)
            {
                compress();
                used_ = 0;
            }
        }
        bits_ += static_cast<std::uint64_t>(n) * 8;
    }

    std::string finish()
    {
        const std::uint64_t bits = bits_;
        const unsigned char one = 0x80;
        const unsigned char zero = 0x00;
        update(&one, 1);
        while (used_ != 56)
            update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i)
            len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        update(len, 8);
        static const char* hex = "0123456789abcdef";
        std::string out;
        for (std::uint32_t v : h_)
        {
            for (int s = 28; s >= 0; s -= 4)
                out += hex[(v >> s) & 0xF];
        }
        return out;
    }

private:
    void compress()
    {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(block_[4 * i]) << 24) | (std::uint32_t(block_[4 * i + 1]) << 16) |
                   (std::uint32_t(block_[4 * i + 2]) << 8) | std::uint32_t(block_[4 * i + 3]);
        for (int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + K[static_cast<std::size_t>(i)] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

    std::array<std::uint32_t, 8> h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char block_[64] = {};
    std::size_t used_ = 0;
    std::uint64_t bits_ = 0;
};
} // namespace

std::string sha256Hex(std::string_view data)
{
    Sha256 s;
    s.update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return s.finish();
}

std::string sha256File(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.u8string());
    Sha256 s;
    std::vector<char> buf(1 << 16);
    while (in)
    {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = in.gcount();
        if (n > 0)
            s.update(reinterpret_cast<const unsigned char*>(buf.data()), static_cast<std::size_t>(n));
    }
    return s.finish();
}

} // namespace anahita::core
