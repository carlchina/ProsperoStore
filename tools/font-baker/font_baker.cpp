// ProsperoStore - Bakes a TrueType or OpenType font into a .huifont signed-distance atlas.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The layout of the file is in src/gfx/font_format.hpp. The store's own faces were baked the same
// way: every glyph a distance field with `range` pixels of margin, 128 on the glyph's edge.
//
//   font-baker FONT OUT --size 56 --range 8 --chars TEXT_FILE [--keep OLD.huifont]
//
// The glyphs are ASCII, every character in TEXT_FILE (UTF-8) and, with --keep, every glyph OLD has
// (so a face for one language also draws what the catalog writes in Latin letters). A character the
// font does not have is left out.
//
// Build: clang++ -std=c++17 -O2 -I src tools/font-baker/font_baker.cpp -o font-baker
//
// The Japanese faces (assets/fonts/noto-sans-jp-*.huifont) are Noto Sans CJK JP, from
// https://github.com/notofonts/noto-cjk (Sans/OTF/Japanese: Regular, Bold and Medium, SIL OFL 1.1),
// baked from the characters of the Japanese column of src/core/i18n_strings.cpp:
//
//   font-baker NotoSansCJKjp-Regular.otf assets/fonts/noto-sans-jp-regular.huifont --size 56 \
//       --range 8 --chars japanese.txt --keep assets/fonts/inter-regular.huifont
//
// (Bold for the semibold face, keeping inter-semibold; Medium for the title face, keeping
// montserrat-medium.) Bake them again when a Japanese sentence uses a character they lack.
//
// The Simplified Chinese faces (assets/fonts/noto-sans-sc-*.huifont) are Noto Sans CJK SC, from
// the same repository (Sans/OTF/SimplifiedChinese), baked from the characters of the Simplified
// Chinese column of src/core/i18n_strings.cpp with the same --keep faces:
//
//   font-baker NotoSansCJKsc-Regular.otf assets/fonts/noto-sans-sc-regular.huifont --size 56 \
//       --range 8 --chars chinese.txt --keep assets/fonts/inter-regular.huifont
//
// Bake them again when a Chinese sentence uses a character they lack.

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

#include "gfx/font_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace ff = hui::gfx::font_format;

namespace
{

std::string read_all(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::exit(2);
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void add_utf8(const std::string &text, std::set<std::uint32_t> &into)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        int length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        std::uint32_t value = length == 1 ? lead : lead & (0xFFu >> (length + 1));
        for (int k = 1; k < length && i + static_cast<std::size_t>(k) < text.size(); ++k)
            value = (value << 6) | (static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]) & 0x3Fu);
        i += static_cast<std::size_t>(length);
        if (value >= 0x20 && value != 0x7F)
            into.insert(value);
    }
}


// The distance field of a glyph, made by drawing it four times larger and measuring the exact
// distance from each pixel to the nearest pixel on the other side of the edge (Felzenszwalb and
// Huttenlocher's transform). stb_truetype's own routine does not cope with the cubic curves of
// CFF fonts such as Noto Sans CJK, so it is not used.
constexpr int kOver = 4;
constexpr float kFar = 1e20f;

// Squared distance from every cell to the nearest cell that is 0 in `f` (one dimension).
void squared_distance_1d(const float *f, int n, float *d, int *v, float *z)
{
    int k = 0;
    v[0] = 0;
    z[0] = -kFar;
    z[1] = kFar;
    for (int q = 1; q < n; ++q)
    {
        float s;
        while (true)
        {
            const int p = v[k];
            s = ((f[q] + static_cast<float>(q) * static_cast<float>(q)) -
                 (f[p] + static_cast<float>(p) * static_cast<float>(p))) /
                (2.0f * static_cast<float>(q) - 2.0f * static_cast<float>(p));
            if (s > z[k] || k == 0)
                break;
            --k;
        }
        if (s <= z[k])
        {
            v[k] = q;
            z[k] = -kFar;
            z[k + 1] = kFar;
            continue;
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = kFar;
    }
    k = 0;
    for (int q = 0; q < n; ++q)
    {
        while (z[k + 1] < static_cast<float>(q))
            ++k;
        const float diff = static_cast<float>(q - v[k]);
        d[q] = diff * diff + f[v[k]];
    }
}

// Squared distance of each cell to the nearest `seed` cell; the grid is w x h.
std::vector<float> squared_distance(const std::vector<unsigned char> &seed, int w, int h)
{
    std::vector<float> grid(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (std::size_t i = 0; i < grid.size(); ++i)
        grid[i] = seed[i] ? 0.0f : kFar;
    const int longest = std::max(w, h);
    std::vector<float> column(static_cast<std::size_t>(longest)), out(static_cast<std::size_t>(longest)),
        z(static_cast<std::size_t>(longest) + 1);
    std::vector<int> v(static_cast<std::size_t>(longest));
    for (int x = 0; x < w; ++x)
    {
        for (int y = 0; y < h; ++y)
            column[static_cast<std::size_t>(y)] = grid[static_cast<std::size_t>(y) * w + x];
        squared_distance_1d(column.data(), h, out.data(), v.data(), z.data());
        for (int y = 0; y < h; ++y)
            grid[static_cast<std::size_t>(y) * w + x] = out[static_cast<std::size_t>(y)];
    }
    for (int y = 0; y < h; ++y)
    {
        squared_distance_1d(&grid[static_cast<std::size_t>(y) * w], w, out.data(), v.data(), z.data());
        for (int x = 0; x < w; ++x)
            grid[static_cast<std::size_t>(y) * w + x] = out[static_cast<std::size_t>(x)];
    }
    return grid;
}

// The glyph as w x h distance bytes, 128 on the edge and falling `128 / range` per pixel outward,
// with `range` pixels of margin. xoff and yoff place the top left corner from the pen on the
// baseline (y grows down). Empty (a space): no bitmap.
std::vector<unsigned char> glyph_field(const stbtt_fontinfo &info, int glyph, float scale, int range,
                                       int &w, int &h, int &xoff, int &yoff)
{
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&info, glyph, scale, scale, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0)
    {
        w = h = xoff = yoff = 0;
        return {};
    }
    xoff = x0 - range;
    yoff = y0 - range;
    w = (x1 - x0) + 2 * range;
    h = (y1 - y0) + 2 * range;
    const int W = w * kOver, H = h * kOver;
    std::vector<unsigned char> cover(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0);
    int hx0, hy0, hx1, hy1;
    const float big = scale * static_cast<float>(kOver);
    stbtt_GetGlyphBitmapBox(&info, glyph, big, big, &hx0, &hy0, &hx1, &hy1);
    const int gw = hx1 - hx0, gh = hy1 - hy0;
    std::vector<unsigned char> drawn(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), 0);
    stbtt_MakeGlyphBitmap(&info, drawn.data(), gw, gh, gw, big, big, glyph);
    for (int row = 0; row < gh; ++row)
        for (int col = 0; col < gw; ++col)
        {
            const int cx = hx0 - xoff * kOver + col, cy = hy0 - yoff * kOver + row;
            if (cx >= 0 && cx < W && cy >= 0 && cy < H)
                cover[static_cast<std::size_t>(cy) * W + cx] = drawn[static_cast<std::size_t>(row) * gw + col];
        }
    std::vector<unsigned char> inside(cover.size()), outside(cover.size());
    for (std::size_t i = 0; i < cover.size(); ++i)
    {
        inside[i] = cover[i] >= 128;
        outside[i] = !inside[i];
    }
    const std::vector<float> to_inside = squared_distance(inside, W, H);
    const std::vector<float> to_outside = squared_distance(outside, W, H);
    // Signed distance in big pixels: positive outside, negative inside; half a pixel is the
    // distance from a cell's centre to the edge between two cells.
    std::vector<float> signed_distance(cover.size());
    for (std::size_t i = 0; i < cover.size(); ++i)
        signed_distance[i] = inside[i] ? -(std::sqrt(to_outside[i]) - 0.5f) : (std::sqrt(to_inside[i]) - 0.5f);
    const float per_pixel = 128.0f / static_cast<float>(range);
    std::vector<unsigned char> field(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
        {
            // The final pixel's centre, in big-pixel cells; bilinear between the four around it.
            const float fx = (static_cast<float>(i) + 0.5f) * kOver - 0.5f;
            const float fy = (static_cast<float>(j) + 0.5f) * kOver - 0.5f;
            const int ix = std::clamp(static_cast<int>(std::floor(fx)), 0, W - 2);
            const int iy = std::clamp(static_cast<int>(std::floor(fy)), 0, H - 2);
            const float tx = std::clamp(fx - static_cast<float>(ix), 0.0f, 1.0f);
            const float ty = std::clamp(fy - static_cast<float>(iy), 0.0f, 1.0f);
            const auto at = [&](int x, int y) { return signed_distance[static_cast<std::size_t>(y) * W + x]; };
            const float d = (at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty) +
                            (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty;
            const float value = 128.0f - (d / static_cast<float>(kOver)) * per_pixel;
            field[static_cast<std::size_t>(j) * w + i] =
                static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
        }
    return field;
}

struct Baked
{
    ff::Glyph glyph{};
    std::vector<unsigned char> bitmap; // glyph.w * glyph.h
};

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: font-baker FONT OUT --size N --range N --chars FILE [--keep OLD]\n");
        return 2;
    }
    const std::string font_path = argv[1], out_path = argv[2];
    float size = 56.0f;
    int range = 8;
    std::string chars_path, keep_path;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string flag = argv[i];
        if (flag == "--size")
            size = static_cast<float>(std::atof(argv[i + 1]));
        else if (flag == "--range")
            range = std::atoi(argv[i + 1]);
        else if (flag == "--chars")
            chars_path = argv[i + 1];
        else if (flag == "--keep")
            keep_path = argv[i + 1];
    }

    std::set<std::uint32_t> wanted;
    for (std::uint32_t c = 0x20; c < 0x7F; ++c)
        wanted.insert(c);
    if (!chars_path.empty())
        add_utf8(read_all(chars_path), wanted);
    if (!keep_path.empty())
    {
        const std::string old = read_all(keep_path);
        ff::Header header{};
        std::memcpy(&header, old.data(), sizeof(header));
        for (std::uint32_t i = 0; i < header.glyph_count; ++i)
        {
            ff::Glyph glyph{};
            std::memcpy(&glyph, old.data() + sizeof(header) + i * sizeof(glyph), sizeof(glyph));
            wanted.insert(glyph.codepoint);
        }
    }

    const std::string data = read_all(font_path);
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, reinterpret_cast<const unsigned char *>(data.data()),
                        stbtt_GetFontOffsetForIndex(
                            reinterpret_cast<const unsigned char *>(data.data()), 0)))
    {
        std::fprintf(stderr, "not a font stb_truetype can read: %s\n", font_path.c_str());
        return 2;
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, size);

    std::vector<Baked> baked;
    std::size_t missing = 0;
    for (const std::uint32_t codepoint : wanted)
    {
        const int index = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
        if (index == 0)
        {
            ++missing;
            continue;
        }
        Baked one;
        one.glyph.codepoint = codepoint;
        int advance = 0, bearing = 0;
        stbtt_GetGlyphHMetrics(&info, index, &advance, &bearing);
        one.glyph.advance = static_cast<float>(advance) * scale;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        std::vector<unsigned char> field = glyph_field(info, index, scale, range, w, h, xoff, yoff);
        if (!field.empty())
        {
            one.glyph.w = static_cast<std::uint16_t>(w);
            one.glyph.h = static_cast<std::uint16_t>(h);
            one.glyph.offset_x = static_cast<float>(xoff);
            one.glyph.offset_y = static_cast<float>(yoff);
            one.bitmap = std::move(field);
        }
        baked.push_back(std::move(one));
    }

    // Shelves, tallest first, in an atlas 2048 wide; a gap keeps one glyph's edge out of the next.
    constexpr int kWidth = 2048, kGap = 2;
    std::vector<std::size_t> order(baked.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return baked[a].glyph.h > baked[b].glyph.h; });
    int x = 0, y = 0, shelf = 0;
    for (const std::size_t at : order)
    {
        ff::Glyph &glyph = baked[at].glyph;
        if (glyph.w == 0)
            continue;
        if (x + glyph.w > kWidth)
        {
            x = 0;
            y += shelf + kGap;
            shelf = 0;
        }
        glyph.x = static_cast<std::uint16_t>(x);
        glyph.y = static_cast<std::uint16_t>(y);
        x += glyph.w + kGap;
        shelf = std::max<int>(shelf, glyph.h);
    }
    const int height = y + shelf + 1;
    std::vector<unsigned char> atlas(static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(height), 0);
    for (const Baked &one : baked)
        for (int row = 0; row < one.glyph.h; ++row)
            std::memcpy(&atlas[static_cast<std::size_t>(one.glyph.y + row) * kWidth + one.glyph.x],
                        &one.bitmap[static_cast<std::size_t>(row) * one.glyph.w], one.glyph.w);

    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    ff::Header header{};
    header.magic = ff::kMagic;
    header.version = ff::kVersion;
    header.atlas_width = kWidth;
    header.atlas_height = static_cast<std::uint16_t>(height);
    header.pixel_size = size;
    header.sdf_range = static_cast<float>(range);
    header.ascent = static_cast<float>(ascent) * scale;
    header.descent = static_cast<float>(descent) * scale;
    header.line_gap = static_cast<float>(line_gap) * scale;
    header.glyph_count = static_cast<std::uint32_t>(baked.size());
    header.kern_count = 0;

    std::sort(baked.begin(), baked.end(),
              [](const Baked &a, const Baked &b) { return a.glyph.codepoint < b.glyph.codepoint; });
    std::ofstream out(out_path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    for (const Baked &one : baked)
        out.write(reinterpret_cast<const char *>(&one.glyph), sizeof(one.glyph));
    out.write(reinterpret_cast<const char *>(atlas.data()), static_cast<std::streamsize>(atlas.size()));
    std::fprintf(stderr, "%s: %zu glyphs (%zu not in the font), atlas %dx%d, %.1f MB\n", out_path.c_str(),
                 baked.size(), missing, kWidth, height, static_cast<double>(atlas.size()) / 1048576.0);
    return out ? 0 : 1;
}
