// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "glyphs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/i18n_files.h"
#include "common/log.h"
#include "frontend.h"
#include "renderer/texture.h"

// stb_truetype, as Dear ImGui ships it, for this file only.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace openrac::frontend::glyphs {

namespace {

// Past the sky's and the particles' keys (frontend.cpp).
constexpr std::uint32_t kKey = 0x40000;
// The size the letters are drawn at, in texels to the em, and the room around each: their dark
// edge and what linear filtering reads next to them.
constexpr float kEm = 36.0f;
constexpr int kEdge = 2;
constexpr int kPad = kEdge + 2;
constexpr int kWidth = 2048;

struct State {
    std::string code;
    std::vector<openrac_game_glyph> glyphs;
    renderer::Rgba8Image image;
    bool placed = false;
};
State s;

std::string_view font_file(const std::string& code) {
    const std::string name = "fonts/" + code + ".ttf";
    for (const i18n::files::File& f : i18n::files::all()) {
        if (name == f.name) {
            return f.text;
        }
    }
    return {};
}

// Hangul and Han characters, and the punctuation of their texts: as wide as high.
bool square(char32_t c) {
    return (c >= 0x1100 && c < 0x1200) || (c >= 0x2E80 && c < 0xA000) || (c >= 0xAC00 && c < 0xD7B0)
           || (c >= 0xF900 && c < 0xFB00) || (c >= 0xFF00 && c < 0xFF61);
}

}  // namespace

bool has_font(const std::string& code) {
    return !font_file(code).empty();
}

void use(const std::string& code, const std::vector<char32_t>& letters) {
    s = State{};
    s.code = code;
    const std::string_view file = font_file(code);
    if (file.empty() || letters.empty()) {
        return;
    }
    const auto* data = reinterpret_cast<const unsigned char*>(file.data());
    stbtt_fontinfo font;
    if (stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0)) == 0) {
        log::warn("fonts/{}.ttf: not a font the port reads", code);
        return;
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, kEm);
    // Each letter's bitmap, then their places in rows of the texture.
    struct Drawn {
        std::vector<unsigned char> alpha;
        int w = 0, h = 0, x0 = 0, y0 = 0;
        float advance = 0.0f;
    };
    std::vector<Drawn> drawn(letters.size());
    int missing = 0;
    for (std::size_t i = 0; i < letters.size(); ++i) {
        Drawn& d = drawn[i];
        const int index = stbtt_FindGlyphIndex(&font, static_cast<int>(letters[i]));
        missing += index == 0;
        int advance = 0, bearing = 0;
        stbtt_GetGlyphHMetrics(&font, index, &advance, &bearing);
        d.advance = static_cast<float>(advance) * scale / kEm;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&font, index, scale, scale, &x0, &y0, &x1, &y1);
        d.w = std::max(0, x1 - x0);
        d.h = std::max(0, y1 - y0);
        d.x0 = x0;
        d.y0 = y0;
        d.alpha.assign(static_cast<std::size_t>(d.w * d.h), 0);
        if (d.w > 0 && d.h > 0) {
            stbtt_MakeGlyphBitmap(&font, d.alpha.data(), d.w, d.h, d.w, scale, scale, index);
        }
    }
    if (missing > 0) {
        log::warn("fonts/{}.ttf lacks {} of the language's {} letters", code, missing, letters.size());
    }
    int x = 0, y = 0, row = 0;
    std::vector<std::pair<int, int>> at(letters.size());
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        const int w = drawn[i].w + 2 * kPad, h = drawn[i].h + 2 * kPad;
        if (x + w > kWidth) {
            x = 0;
            y += row;
            row = 0;
        }
        at[i] = {x, y};
        x += w;
        row = std::max(row, h);
    }
    int height = 1;
    while (height < y + row) {
        height *= 2;
    }
    s.image = renderer::Rgba8Image(kWidth, height);
    // White (the chip's 0x80) where the letter is, dark around it, as the game's fonts are drawn.
    s.glyphs.resize(letters.size());
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        const Drawn& d = drawn[i];
        const int w = d.w + 2 * kPad, h = d.h + 2 * kPad;
        const auto fill = [&](int px, int py) -> float {
            px -= kPad, py -= kPad;
            return px < 0 || py < 0 || px >= d.w || py >= d.h
                       ? 0.0f
                       : static_cast<float>(d.alpha[static_cast<std::size_t>(py * d.w + px)]) / 255.0f;
        };
        for (int py = 0; py < h; ++py) {
            for (int px = 0; px < w; ++px) {
                const float f = fill(px, py);
                float edge = 0.0f;
                for (int dy = -kEdge; dy <= kEdge; ++dy) {
                    for (int dx = -kEdge; dx <= kEdge; ++dx) {
                        if (dx * dx + dy * dy <= kEdge * kEdge + 1) {
                            edge = std::max(edge, fill(px + dx, py + dy));
                        }
                    }
                }
                const float a = std::max(f, edge * 0.85f);
                const float white = a > 0.0f ? 128.0f * f / a : 0.0f;
                const auto c = static_cast<std::uint32_t>(std::lround(white));
                s.image.set_texel(at[i].first + px, at[i].second + py,
                                  c | c << 8 | c << 16 | static_cast<std::uint32_t>(std::lround(a * 255.0f)) << 24);
            }
        }
        openrac_game_glyph& g = s.glyphs[i];
        g.key = kKey;
        g.log2_width = static_cast<std::uint32_t>(std::log2(kWidth));
        g.log2_height = static_cast<std::uint32_t>(std::log2(height));
        g.u0 = static_cast<float>(at[i].first);
        g.v0 = static_cast<float>(at[i].second);
        g.u1 = static_cast<float>(at[i].first + w);
        g.v1 = static_cast<float>(at[i].second + h);
        g.x0 = static_cast<float>(d.x0 - kPad) / kEm;
        g.y0 = static_cast<float>(d.y0 - kPad) / kEm;
        g.x1 = static_cast<float>(d.x0 - kPad + w) / kEm;
        g.y1 = static_cast<float>(d.y0 - kPad + h) / kEm;
        g.advance = d.advance;
        g.chinese = square(letters[i]) ? 1 : 0;
    }
    log::info("{} letters drawn from fonts/{}.ttf into a {} x {} texture", letters.size(), code, kWidth, height);
}

bool glyph(int index, openrac_game_glyph& out) {
    if (index < 0 || static_cast<std::size_t>(index) >= s.glyphs.size()) {
        return false;
    }
    if (!s.placed) {
        s.placed = place_texture(kKey, s.image, "font " + s.code);
        if (!s.placed) {
            return false;
        }
    }
    out = s.glyphs[static_cast<std::size_t>(index)];
    return true;
}

}  // namespace openrac::frontend::glyphs
