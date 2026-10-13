// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Pictures the port draws over the game's frame itself (the debug menu, the performance overlay):
// made on the CPU, pixel by pixel, in a 5 x 7 font, then blitted over a framebuffer. Nothing here
// needs a shader; a picture is opaque.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace openrac::frontend {

constexpr std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return 0xFF000000u | (std::uint32_t{b} << 16) | (std::uint32_t{g} << 8) | r;
}

// The colours of Ratchet & Clank's own menus: the panels' navy, their khaki frame, light blue text,
// yellow for what is picked or counted, a lighter navy behind a picked row.
inline constexpr std::uint32_t kMenuNavy = rgba(14, 14, 30);
inline constexpr std::uint32_t kMenuFrame = rgba(150, 146, 104);
inline constexpr std::uint32_t kMenuText = rgba(136, 168, 255);
inline constexpr std::uint32_t kMenuYellow = rgba(255, 255, 64);
inline constexpr std::uint32_t kMenuTitle = rgba(230, 230, 230);
inline constexpr std::uint32_t kMenuHint = rgba(120, 120, 140);
inline constexpr std::uint32_t kMenuHighlight = rgba(36, 36, 70);

// An RGBA8 picture whose first row is its top.
struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    Picture() = default;
    Picture(int w, int h);

    void fill(int x0, int y0, int x1, int y1, std::uint32_t colour);

    // Text in 6-pixel cells, 7 pixels high (letters drawn as capitals); returns the x after it.
    int text(int x, int y, std::string_view t, std::uint32_t colour);

    // The whole picture as one of the game's menu panels: the khaki frame, navy inside, `title` at
    // the top over a khaki rule. Returns the y the panel's content starts at.
    int panel(std::string_view title);
};

// The 5 x 7 glyph of a character (35 characters, '#' lit, row by row), or null for one the font
// does not have (drawn as a space).
const char* glyph(char c);

// A picture blitted over a framebuffer through a texture of its own.
class PictureBlitter {
public:
    // Copies `picture` into the texture: only when it changed, blit() draws the last one copied.
    void upload(const Picture& picture);

    // Draws the picture last uploaded at (x0, y0) (from the target's top left), scaled to w x h.
    void blit(unsigned framebuffer, int target_height, int x0, int y0, int w, int h);

    // GL objects, before the context goes.
    void release();

private:
    unsigned m_texture = 0;
    unsigned m_framebuffer = 0;
    int m_width = 0;
    int m_height = 0;
};

}  // namespace openrac::frontend
