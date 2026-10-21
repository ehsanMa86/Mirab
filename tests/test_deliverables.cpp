/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    tests/test_deliverables.cpp
 * Purpose: Tests for the GIF encoder, print layout geometry and report summaries.
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

#include "mirab/core/Gif.h"
#include "mirab/core/Model.h"
#include "mirab/core/PrintLayout.h"
#include "mirab/core/Results.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace mirab;
using namespace mirab::core;
namespace fs = std::filesystem;

namespace
{
std::uint32_t rgb(int r, int g, int b)
{
    return 0xFF000000u | static_cast<std::uint32_t>(r) << 16 | static_cast<std::uint32_t>(g) << 8 |
           static_cast<std::uint32_t>(b);
}

gif::PixelView view(const std::vector<std::uint32_t>& px, int w, int h)
{
    return gif::PixelView{px.data(), w, h, w};
}

/// A small "map": flat background, a few flat colour patches and a gradient band.
std::vector<std::uint32_t> testImage(int w, int h, int shift)
{
    std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, rgb(246, 248, 251));
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if (y > h / 2 && y < h / 2 + 6)
                px[static_cast<std::size_t>(y) * w + x] = rgb(x * 255 / w, 128, 255 - x * 255 / w);
            if (x >= 4 + shift && x < 10 + shift && y >= 3 && y < 9)
                px[static_cast<std::size_t>(y) * w + x] = rgb(224, 64, 48);
            if (x >= 20 && x < 26 && y >= 3 && y < 9)
                px[static_cast<std::size_t>(y) * w + x] =
                    shift % 2 ? rgb(46, 178, 96) : rgb(43, 103, 199);
        }
    }
    return px;
}
} // namespace

TEST_CASE("gif: LZW round trip, including table resets")
{
    std::mt19937 rng(7);
    for (int minCode : {2, 4, 8})
    {
        for (std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{17}, std::size_t{5000},
                              std::size_t{70000}})
        {
            std::vector<std::uint8_t> data(n);
            const int symbols = 1 << minCode;
            for (std::size_t i = 0; i < n; ++i)
            {
                // Runs and noise: long runs fill the table with long strings, noise with many short
                // ones.
                data[i] = static_cast<std::uint8_t>((i / 37) % 3 == 0 ? (i / 11) % symbols
                                                                      : rng() % symbols);
            }
            const auto code = gif::lzwEncode(data.data(), data.size(), minCode);
            std::vector<std::uint8_t> back;
            CHECK(gif::lzwDecode(code.data(), code.size(), minCode, back));
            CHECK_EQ(back.size(), data.size());
            CHECK(back == data);
        }
    }
    // A flat image compresses well.
    std::vector<std::uint8_t> flat(100000, 3);
    CHECK(gif::lzwEncode(flat.data(), flat.size(), 2).size() < 1000);
}

TEST_CASE("gif: LZW matches a hand-checked code stream")
{
    // Indices 0 0 0 0 with 2-bit symbols: clear(4) 0 6 0 in 3 bits; the decoder's
    // table then reaches 8 entries, so the end code (5) takes 4 bits.
    const std::vector<std::uint8_t> data = {0, 0, 0, 0};
    const auto code = gif::lzwEncode(data.data(), data.size(), 2);
    // 4 | 0 << 3 | 6 << 6 | 0 << 9 | 5 << 12 = 0x5184, least significant byte first.
    CHECK_EQ(code.size(), std::size_t{2});
    CHECK_EQ(static_cast<int>(code[0]), 0x84);
    CHECK_EQ(static_cast<int>(code[1]), 0x51);
}

TEST_CASE("gif: median cut keeps few colours exactly and approximates many")
{
    std::vector<std::uint32_t> px;
    const std::uint32_t colors[] = {rgb(10, 20, 30), rgb(200, 10, 10), rgb(10, 200, 10),
                                    rgb(255, 255, 255)};
    for (int i = 0; i < 400; ++i)
        px.push_back(colors[i % 4]);
    gif::Histogram h;
    h.add(view(px, 20, 20));
    CHECK_EQ(h.cells(), std::size_t{4});
    const auto pal = gif::medianCut(h, 256);
    CHECK_EQ(pal.size(), std::size_t{4});
    for (const auto c : colors)
    {
        const gif::Color want{static_cast<std::uint8_t>(c >> 16), static_cast<std::uint8_t>(c >> 8),
                              static_cast<std::uint8_t>(c)};
        CHECK(std::find(pal.begin(), pal.end(), want) != pal.end());
    }

    // A smooth RGB gradient with thousands of colours: 64 colours stay close.
    std::vector<std::uint32_t> grad;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 256; ++x)
            grad.push_back(rgb(x, y * 4, 255 - x));
    gif::Histogram g;
    g.add(view(grad, 256, 64));
    const auto pal64 = gif::medianCut(g, 64);
    CHECK(pal64.size() <= 64);
    CHECK(pal64.size() >= 48);
    gif::Quantizer q(pal64, gif::Dither::None);
    std::vector<std::uint8_t> idx;
    q.map(view(grad, 256, 64), idx);
    double err = 0;
    for (std::size_t i = 0; i < grad.size(); ++i)
    {
        const auto& c = pal64[idx[i]];
        const int dr = static_cast<int>((grad[i] >> 16) & 0xFF) - c.r,
                  dg = static_cast<int>((grad[i] >> 8) & 0xFF) - c.g,
                  db = static_cast<int>(grad[i] & 0xFF) - c.b;
        err += std::sqrt(static_cast<double>(dr * dr + dg * dg + db * db));
    }
    CHECK(err / grad.size() < 20.0);
}

TEST_CASE("gif: ordered dithering leaves exact palette colours alone")
{
    const gif::Palette pal = {{0, 0, 0}, {255, 255, 255}, {224, 64, 48}};
    gif::Quantizer q(pal, gif::Dither::Ordered);
    std::vector<std::uint32_t> px(64, rgb(224, 64, 48));
    for (int i = 32; i < 64; ++i)
        px[static_cast<std::size_t>(i)] =
            rgb(128, 128, 128); // mid grey: dithered between black and white
    std::vector<std::uint8_t> idx;
    q.map(view(px, 8, 8), idx);
    for (int i = 0; i < 32; ++i)
        CHECK_EQ(static_cast<int>(idx[static_cast<std::size_t>(i)]), 2);
    int black = 0, white = 0;
    for (int i = 32; i < 64; ++i)
    {
        black += idx[static_cast<std::size_t>(i)] == 0;
        white += idx[static_cast<std::size_t>(i)] == 1;
    }
    CHECK(black > 0);
    CHECK(white > 0);
}

TEST_CASE("gif: still image decodes to the same pixels")
{
    const int w = 37, h = 23;
    const auto px = testImage(w, h, 0);
    gif::Histogram hist;
    hist.add(view(px, w, h));
    const auto pal = gif::medianCut(hist, 256);
    gif::Quantizer q(pal, gif::Dither::None);
    gif::Frame f;
    f.width = w;
    f.height = h;
    q.map(view(px, w, h), f.indices);
    std::ostringstream out;
    gif::Writer writer(out, w, h, pal, -1);
    writer.addFrame(f);
    CHECK(writer.finish());
    const std::string bytes = out.str();
    CHECK_EQ(bytes.substr(0, 6), std::string("GIF89a"));
    CHECK_EQ(static_cast<int>(static_cast<unsigned char>(bytes.back())), 0x3B);

    gif::Decoded d;
    std::string error;
    CHECK(gif::decode(bytes, d, &error));
    CHECK_EQ(d.width, w);
    CHECK_EQ(d.height, h);
    CHECK_EQ(d.loopCount, -1);
    CHECK_EQ(d.images.size(), std::size_t{1});
    CHECK(d.images[0].frame.indices == f.indices);
    // The test image has fewer than 256 colours, so the picture is exact.
    for (std::size_t i = 0; i < px.size(); ++i)
        CHECK_EQ(d.images[0].canvas[i], px[i]);
}

TEST_CASE("gif: animation stores changed rectangles and decodes frame by frame")
{
    const int w = 40, h = 24;
    std::vector<std::vector<std::uint32_t>> frames;
    for (int i = 0; i < 6; ++i)
        frames.push_back(testImage(w, h, std::min(i, 3))); // frames 3, 4 and 5 are equal
    frames.push_back(frames.back());                       // and so is 6

    std::vector<gif::PixelView> samples;
    for (const auto& f : frames)
        samples.push_back(view(f, w, h));
    const auto pal = gif::animationPalette(samples, {{224, 64, 48}, {46, 178, 96}}, 255);
    CHECK(pal.size() <= 255);
    CHECK(pal[0] == (gif::Color{224, 64, 48}));

    std::ostringstream out;
    gif::AnimationWriter::Options opt;
    opt.delayCs = 20;
    opt.dither = gif::Dither::Ordered;
    gif::AnimationWriter anim(out, w, h, pal, opt);
    for (const auto& f : frames)
        anim.addFrame(view(f, w, h));
    CHECK(anim.finish());
    CHECK_EQ(anim.framesWritten(), 4);

    gif::Decoded d;
    CHECK(gif::decode(out.str(), d));
    CHECK_EQ(d.loopCount, 0);
    CHECK_EQ(d.images.size(), std::size_t{4});
    CHECK_EQ(d.images[0].frame.width, w);
    CHECK_EQ(d.images[3].frame.delayCs, 80); // four equal frames merged
    for (std::size_t i = 1; i < d.images.size(); ++i)
    {
        const auto& f = d.images[i].frame;
        CHECK(f.width < w); // only the moving patch
        CHECK(f.transparent >= 0);
        CHECK_EQ(f.disposal, 1);
    }
    // Each composed frame equals the source wherever the palette is exact.
    for (std::size_t i = 0; i < d.images.size(); ++i)
    {
        const auto& src = frames[i];
        const auto& canvas = d.images[i].canvas;
        int same = 0;
        for (std::size_t k = 0; k < src.size(); ++k)
            same += canvas[k] == src[k];
        CHECK(same > static_cast<int>(src.size()) * 9 / 10);
        CHECK_EQ(canvas[static_cast<std::size_t>(5) * w + 5 + static_cast<int>(i)],
                 rgb(224, 64, 48));
    }
}

TEST_CASE("gif: decoder rejects malformed files")
{
    gif::Decoded d;
    std::string error;
    CHECK(!gif::decode("GIF89", d, &error));
    CHECK(!gif::decode("PNG89a1234567890", d, &error));
    std::ostringstream out;
    gif::Writer writer(out, 2, 2, {{0, 0, 0}, {255, 255, 255}});
    gif::Frame f;
    f.width = 2;
    f.height = 2;
    f.indices = {0, 1, 1, 0};
    writer.addFrame(f);
    writer.finish();
    std::string bytes = out.str();
    CHECK(gif::decode(bytes, d, &error));
    CHECK(!gif::decode(bytes.substr(0, bytes.size() - 8), d, &error));
    CHECK(!error.empty());
}

TEST_CASE("layout: page frames for both templates")
{
    layout::PageOptions o;
    o.paper = "A4";
    o.landscape = true;
    auto f = layout::computeFrames(o);
    CHECK_NEAR(f.widthMm, 297.0, 1e-9);
    CHECK_NEAR(f.heightMm, 210.0, 1e-9);
    CHECK(!f.side.empty());
    CHECK(f.chart.empty());
    CHECK_NEAR(f.map.x, 10.0, 1e-9);
    CHECK(f.map.right() < f.side.x);
    CHECK_NEAR(f.side.right(), 287.0, 1e-9);
    CHECK(f.header.bottom() < f.map.y);
    CHECK(f.map.bottom() < f.footer.y);
    CHECK_NEAR(f.footer.bottom(), 200.0, 1e-9);
    CHECK_NEAR(f.map.w / (f.map.w + f.side.w), 0.75, 0.01);

    o.layout = layout::Template::ChartBelow;
    o.landscape = false;
    o.paper = "letter";
    o.split = 2.0; // clamped
    f = layout::computeFrames(o);
    CHECK_NEAR(f.widthMm, 215.9, 1e-9);
    CHECK(f.side.empty());
    CHECK(!f.chart.empty());
    CHECK_NEAR(f.map.w, f.chart.w, 1e-9);
    CHECK_NEAR(f.map.h / (f.map.h + f.chart.h), 0.9, 0.01);
    CHECK(f.chart.bottom() < f.footer.y);

    CHECK(layout::findPaper("tabloid") != nullptr);
    CHECK(layout::findPaper("B5") == nullptr);
    o.paper = "B5"; // unknown paper falls back to A4
    CHECK_NEAR(layout::computeFrames(o).widthMm, 210.0, 1e-9);
}

TEST_CASE("layout: scale helpers")
{
    CHECK_NEAR(layout::niceLength(740.0), 500.0, 1e-9);
    CHECK_NEAR(layout::niceLength(199.0), 100.0, 1e-9);
    CHECK_NEAR(layout::niceLength(2.0), 2.0, 1e-12);
    CHECK_NEAR(layout::niceLength(0.031), 0.02, 1e-12);
    CHECK_NEAR(layout::niceLength(-1.0), 0.0, 1e-12);
    CHECK_NEAR(layout::roundScale(12345.0), 12000.0, 1e-9);
    CHECK_NEAR(layout::roundScale(987.0), 990.0, 1e-9);
    CHECK_EQ(layout::groupDigits(1234567.0), std::string("1,234,567"));
    CHECK_EQ(layout::groupDigits(950.0), std::string("950"));
}

TEST_CASE("summary: project and run facts")
{
    const auto model =
        Model::fromFile(fs::path(MIRAB_SOURCE_DIR) / "examples" / "epanet" / "mirab-demo.inp");
    ProjectSettings settings;
    settings.crs = "EPSG:32614";
    const auto s = summarizeProject(model, settings);
    CHECK(s.dialect == Dialect::Epanet);
    int nodes = 0, links = 0;
    for (const auto& [kind, n] : s.nodeKinds)
        nodes += n;
    for (const auto& [kind, n] : s.linkKinds)
        links += n;
    CHECK_EQ(nodes, static_cast<int>(model.nodes().size()));
    CHECK_EQ(links, static_cast<int>(model.links().size()));
    CHECK_EQ(s.nodeKinds.front().first, model.nodes().front().kind);
    CHECK_EQ(s.crs, std::string("EPSG:32614"));
    CHECK(s.durationSeconds > 0);

    ResultSet r;
    r.engineName = "EPA SWMM 5.2";
    r.addPeriod(0);
    r.addPeriod(900);
    r.addPeriod(1800);
    r.metadata["qualityContinuityErrorPct"] = "0.5";
    r.metadata["flowContinuityErrorPct"] = "-0.25";
    r.metadata["runoffContinuityErrorPct"] = "1.5";
    r.metadata["startDate"] = "45000";
    r.messages.push_back("2 warning(s)");
    const auto run = summarizeRun(r);
    CHECK_EQ(run.periods, 3);
    CHECK_NEAR(run.lastSeconds, 1800.0, 1e-9);
    CHECK_NEAR(run.reportStepSeconds, 900.0, 1e-9);
    CHECK_EQ(run.continuity.size(), std::size_t{3});
    CHECK_EQ(run.continuity[0].first, std::string("runoff"));
    CHECK_EQ(run.continuity[1].first, std::string("flow"));
    CHECK_NEAR(run.continuity[1].second, -0.25, 1e-12);
    CHECK_EQ(run.messages.size(), std::size_t{1});
}
