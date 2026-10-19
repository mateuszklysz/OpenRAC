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

// The characters of a translation as its file writes it, as they are shown: a button or colour
// in brackets left out, [xAD] (the game's ellipsis) as one, "[[" as '['.
std::u32string shown_letters(const std::string& utf8) {
    std::u32string out;
    for (std::size_t i = 0; i < utf8.size();) {
        if (utf8[i] == '[') {
            if (utf8.compare(i, 2, "[[") == 0) {
                out += U'[';
                i += 2;
                continue;
            }
            const std::size_t close = utf8.find(']', i);
            if (close != std::string::npos) {
                if (utf8.compare(i, close - i + 1, "[xAD]") == 0) {
                    out += U'…';
                }
                i = close + 1;
                continue;
            }
        }
        const auto lead = static_cast<unsigned char>(utf8[i]);
        const std::size_t n = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        char32_t c = n == 1 ? lead : lead & (0xFF >> (n + 1));
        for (std::size_t k = 1; k < n && i + k < utf8.size(); ++k) {
            c = c << 6 | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        out += c;
        i += n;
    }
    return out;
}

constexpr std::uint32_t kPictureKey = 0x41000;
std::uint32_t g_next_picture = 0;

}  // namespace

bool picture(const std::string& utf8, const std::string& code, float max_width, float max_height,
             openrac_game_picture& out) {
    std::string_view file = font_file(code);
    if (file.empty()) {
        file = font_file("latin");
    }
    if (file.empty()) {
        return false;
    }
    const auto* data = reinterpret_cast<const unsigned char*>(file.data());
    stbtt_fontinfo font;
    if (stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0)) == 0) {
        return false;
    }
    constexpr float em = 32.0f;
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, em);
    const std::u32string text = shown_letters(utf8);
    const auto width_of = [&](std::size_t from, std::size_t to) {
        float w = 0.0f;
        for (std::size_t i = from; i < to; ++i) {
            int advance = 0, bearing = 0;
            stbtt_GetCodepointHMetrics(&font, static_cast<int>(text[i]), &advance, &bearing);
            w += static_cast<float>(advance) * scale;
        }
        return w;
    };
    // One line, or two split at the space (or newline) nearest the middle when one line would be
    // wider than the slide.
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    std::size_t split = std::u32string::npos;
    const float whole = width_of(0, text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == U'\n' || (whole * 18.0f / em > max_width - 24.0f && text[i] == U' '
                                  && (split == std::u32string::npos
                                      || std::abs(2.0f * width_of(0, i) - whole)
                                             < std::abs(2.0f * width_of(0, split) - whole)))) {
            split = i;
            if (text[i] == U'\n') {
                break;
            }
        }
    }
    if (split == std::u32string::npos) {
        lines.push_back({0, text.size()});
    } else {
        lines.push_back({0, split});
        lines.push_back({split + 1, text.size()});
    }
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
    const float line_height = static_cast<float>(ascent - descent) * scale;
    float widest = 0.0f;
    for (const auto& [from, to] : lines) {
        widest = std::max(widest, width_of(from, to));
    }
    // The letters as high as the slides' own (18 pixels to the em on one line, 15 on two),
    // smaller when that is too wide or too high for the slide less a margin.
    const float text_w = widest, text_h = line_height * static_cast<float>(lines.size());
    float k = (lines.size() == 1 ? 18.0f : 15.0f) / em;
    k = std::min({k, (max_width - 24.0f) / text_w, (max_height - 4.0f) / text_h});
    // The whole slide, black, with the letters left clear: drawn over the game's band, as its
    // slides are, they show the band through them.
    const int w = static_cast<int>(std::ceil(max_width / k)), h = static_cast<int>(std::ceil(max_height / k));
    int tw = 1, th = 1;
    while (tw < w) {
        tw *= 2;
    }
    while (th < h) {
        th *= 2;
    }
    renderer::Rgba8Image image(tw, th);
    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            image.set_texel(x, y, 0xFF000000u);
        }
    }
    const float left = (static_cast<float>(w) - text_w) / 2.0f, top = (static_cast<float>(h) - text_h) / 2.0f;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const float line_width = width_of(lines[l].first, lines[l].second);
        float x = left + (widest - line_width) / 2.0f;
        const float baseline = top + line_height * static_cast<float>(l) + static_cast<float>(ascent) * scale;
        for (std::size_t i = lines[l].first; i < lines[l].second; ++i) {
            const int c = static_cast<int>(text[i]);
            int advance = 0, bearing = 0;
            stbtt_GetCodepointHMetrics(&font, c, &advance, &bearing);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            const float shift = x - std::floor(x);
            stbtt_GetCodepointBitmapBoxSubpixel(&font, c, scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> coverage(static_cast<std::size_t>(gw * gh));
                stbtt_MakeCodepointBitmapSubpixel(&font, coverage.data(), gw, gh, gw, scale, scale, shift, 0.0f, c);
                const int ox = static_cast<int>(std::floor(x)) + x0;
                const int oy = static_cast<int>(std::lround(baseline)) + y0;
                for (int py = 0; py < gh; ++py) {
                    for (int px = 0; px < gw; ++px) {
                        const int ix = ox + px, iy = oy + py;
                        if (ix < 0 || iy < 0 || ix >= tw || iy >= th) {
                            continue;
                        }
                        const std::uint32_t clear = coverage[static_cast<std::size_t>(py * gw + px)];
                        const std::uint32_t was = image.texel(ix, iy) >> 24;
                        image.set_texel(ix, iy, std::min(was, 255u - clear) << 24);
                    }
                }
            }
            x += static_cast<float>(advance) * scale;
        }
    }
    const std::uint32_t key = kPictureKey + (g_next_picture++ & 0xFFF);
    if (!place_texture(key, image, "text picture")) {
        return false;
    }
    out.key = key;
    out.log2_width = static_cast<std::uint32_t>(std::log2(tw));
    out.log2_height = static_cast<std::uint32_t>(std::log2(th));
    out.u1 = static_cast<float>(w);
    out.v1 = static_cast<float>(h);
    out.width = max_width;
    out.height = max_height;
    return true;
}

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
