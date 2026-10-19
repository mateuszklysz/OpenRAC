// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The game's texts from files a translator edits (port/lang/README.md). At the first start the
// port writes every text of the player's disc to `lang/game/` beside its settings: `game.pot`, the
// English to translate, and one `<code>.po` for each language the disc has (en, fr, de, es, it),
// each text's msgctxt its id. Those files stay on the player's computer: they are the disc's.
// The game's lookup (msg_string, host/options.c) then answers a text in the language chosen (the
// Language row; settings.h) from, first, a `<code>.po` there, by the text's id and English (a
// translator's own, or an edited fr.po), and else the port's own translation, port/lang/game/
// <code>.po, built in, which holds no English: each text's msgid is its key, its id and the
// CRC-32 of its English (assets/disc/game_text.h, game_text_key), so it is the same file for
// every disc. The game itself stays in English underneath (its own Language page set to another
// language shows that language's texts as the disc has them).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "common/i18n.h"

namespace openrac::frontend::game_text {

// Writes the disc's texts to FOLDER when it has no game.pot yet (or always, with FORCE). False,
// with WHY, when the disc cannot be read; the game then plays with its own texts.
bool export_from_disc(
    const std::filesystem::path& image,
    const std::filesystem::path& folder,
    bool force,
    std::string& why
);

// Where the files are, and the languages FOLDER or the port has a .po for.
void set_folder(const std::filesystem::path& folder);
std::vector<i18n::LanguageInfo> languages();

// Reads FOLDER/<code>.po and the built-in game/<code>.po for the game's texts; English (none)
// when neither is there. True when one was read.
bool use(const std::string& code);

// UTF8 as the game's font draws it: Polish letters as their letter and mark (the port draws the
// mark), other letters it lacks plain.
std::string for_font(const std::string& utf8);

// The game's lookup asked for ID, which gave ORIGINAL (a game address): the address of the
// translation, or 0 to keep the game's.
std::uint32_t translate(int id, std::uint32_t original);

// The translation of ID whatever the game holds now (rac1: the title's picture of "Press Start",
// drawn before any text table is loaded): its address, or 0 when the language has none other than
// the game's own words.
std::uint32_t translate_id(int id);

// The game's text table at TABLE (COUNT entries: +0 the text, +4 its id) pointed at the texts in
// the language used, for the game's code that reads the table without its lookup. Each frame:
// entries already given this language's text are left as they are.
void translate_table(std::uint32_t table, int count);

}  // namespace openrac::frontend::game_text
