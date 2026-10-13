// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "picture.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "renderer/gl.h"

namespace openrac::frontend {

namespace {

// 5 x 7 glyphs, one string of 7 rows of 5 columns per character ('#' lit).
struct Glyph {
    char c;
    const char* rows;
};

constexpr Glyph kFont[] = {
    {'A', " ### #   ##   #######   ##   ##   #"}, {'B', "#### #   ##   ##### #   ##   ##### "},
    {'C', " ### #   ##    #    #    #   # ### "}, {'D', "#### #   ##   ##   ##   ##   ##### "},
    {'E', "######    #    #### #    #    #####"}, {'F', "######    #    #### #    #    #    "},
    {'G', " ### #   ##    # ####   ##   # ####"}, {'H', "#   ##   ##   #######   ##   ##   #"},
    {'I', " ###   #    #    #    #    #   ### "}, {'J', "  ###   #    #    #    # #  #  ##  "},
    {'K', "#   ##  # # #  ##   # #  #  # #   #"}, {'L', "#    #    #    #    #    #    #####"},
    {'M', "#   ### ### # ##   ##   ##   ##   #"}, {'N', "#   ###  ## # ##  ###   ##   ##   #"},
    {'O', " ### #   ##   ##   ##   ##   # ### "}, {'P', "#### #   ##   ##### #    #    #    "},
    {'Q', " ### #   ##   ##   ## # ##  #  ## #"}, {'R', "#### #   ##   ##### # #  #  # #   #"},
    {'S', " #####    #     ###     #    ##### "}, {'T', "#####  #    #    #    #    #    #  "},
    {'U', "#   ##   ##   ##   ##   ##   # ### "}, {'V', "#   ##   ##   ##   ##   # # #   #  "},
    {'W', "#   ##   ##   ## # ## # ## # # # # "}, {'X', "#   ##   # # #   #   # # #   ##   #"},
    {'Y', "#   ##   # # #   #    #    #    #  "}, {'Z', "#####    #   #   #   #   #    #####"},
    {'0', " ### #   ##  ### # ###  ##   # ### "}, {'1', "  #   ##    #    #    #    #   ### "},
    {'2', " ### #   #    #   #   #   #   #####"}, {'3', "#####   #   #     #     ##   # ### "},
    {'4', "   #   ##  # # #  # #####   #    # "}, {'5', "######    ####     #    ##   # ### "},
    {'6', "  ##  #   #    #### #   ##   # ### "}, {'7', "#####    #   #   #   #    #    #   "},
    {'8', " ### #   ##   # ### #   ##   # ### "}, {'9', " ### #   ##   # ####    #   #  ##  "},
    {'(', "   #   #   #    #    #     #     # "}, {')', " #     #     #    #    #   #   #   "},
    {'-', "                ###                "}, {':', "      ##   ##        ##   ##       "},
    {'.', "                          ##   ##  "}, {'\'', "  #    #   #                       "},
    {'/', "    #    #   #   #   #   #    #    "}, {'>', " #     #     #     #   #   #   #   "},
    {',', "                     ##    #   #   "}, {'+', "       #    #  #####  #    #       "},
    {'%', "##   ##  #   #   #   #   #  ##   ##"}, {'=', "          #####     #####          "},
    {'|', "  #    #    #    #    #    #    #  "},
};

}  // namespace

const char* glyph(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const Glyph& g : kFont) {
        if (g.c == c && std::strlen(g.rows) >= 35) {
            return g.rows;
        }
    }
    return nullptr;
}

Picture::Picture(int w, int h) : width(w), height(h), rgba(static_cast<std::size_t>(w) * h * 4) {}

void Picture::fill(int x0, int y0, int x1, int y1, std::uint32_t colour) {
    for (int y = std::max(0, y0); y < std::min(height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(width, x1); ++x) {
            std::memcpy(&rgba[(static_cast<std::size_t>(y) * width + x) * 4], &colour, 4);
        }
    }
}

int Picture::text(int x, int y, std::string_view t, std::uint32_t colour) {
    for (char c : t) {
        if (const char* rows = glyph(c)) {
            for (int r = 0; r < 7; ++r) {
                for (int col = 0; col < 5; ++col) {
                    if (rows[r * 5 + col] == '#') {
                        fill(x + col, y + r, x + col + 1, y + r + 1, colour);
                    }
                }
            }
        }
        x += 6;
    }
    return x;
}

int Picture::panel(std::string_view title) {
    fill(0, 0, width, height, kMenuFrame);
    fill(2, 2, width - 2, height - 2, kMenuNavy);
    text(10, 9, title, kMenuTitle);
    fill(8, 20, width - 8, 21, kMenuFrame);
    return 27;
}

void PictureBlitter::upload(const Picture& p) {
    using namespace openrac::gl;
    if (m_texture == 0) {
        glGenTextures(1, &m_texture);
        glGenFramebuffers(1, &m_framebuffer);
    }
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), p.width, p.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, p.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
    m_width = p.width;
    m_height = p.height;
}

void PictureBlitter::blit(unsigned framebuffer, int target_height, int x0, int y0, int w, int h) {
    using namespace openrac::gl;
    if (m_width <= 0 || m_height <= 0) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glDisable(GL_SCISSOR_TEST);
    // The picture's first row is its top; GL's framebuffer counts rows from the bottom.
    const int top = target_height - y0;
    glBlitFramebuffer(0, 0, m_width, m_height, x0, top, x0 + w, top - h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void PictureBlitter::release() {
    using namespace openrac::gl;
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        glDeleteFramebuffers(1, &m_framebuffer);
        m_texture = 0;
        m_framebuffer = 0;
        m_width = 0;
        m_height = 0;
    }
}

}  // namespace openrac::frontend
