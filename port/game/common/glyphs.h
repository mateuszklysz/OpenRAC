// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The letters of a language the game's fonts cannot draw even with the port's marks (Greek,
// Hangul, Chinese: assets/disc/game_text.h, kForeign), drawn from the language's font file
// (port/lang/fonts/<code>.ttf, built into the program with the translations). Each of the
// language's letters is drawn once, white with a dark edge, into one texture the renderer's pool
// holds at a key of the port's; the game's printers put a stand-in in each such letter's place
// and the port draws the letter there from that texture (game/rac1/host/ui2d.c).

#pragma once

#include <string>
#include <vector>

#include "openrac/game_host.h"

namespace openrac::frontend::glyphs {

// The letters of language CODE that are drawn from its font file, in the order the game's bytes
// number them (assets::disc::foreign_letters()); none when it has no font file.
void use(const std::string& code, const std::vector<char32_t>& letters);

// Whether CODE has a font file built in.
bool has_font(const std::string& code);

// UTF8 (a translation as its file writes it) drawn into a texture of the port's in CODE's font
// file, or the port's Latin one: on one line, or two when it is long, fitted into MAX_WIDTH x
// MAX_HEIGHT of the game's pixels. False when it cannot be drawn (no window yet).
bool picture(const std::string& utf8, const std::string& code, float max_width, float max_height,
             openrac_game_picture& out);

// The letter numbered INDEX: false when there is none.
bool glyph(int index, openrac_game_glyph& out);

}  // namespace openrac::frontend::glyphs
