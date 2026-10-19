// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The game's texts from files a translator edits (port/lang/README.md). At the first start the
// port writes every text of the player's disc to `lang/game/` beside its settings: `game.pot`, the
// English to translate, and one `<code>.po` for each language the disc has (en, fr, de, es, it),
// each text's msgctxt its id. Those files stay on the player's computer: they are the disc's.
// A `<code>.po` there for the language chosen (the Language row; settings.h) then gives the game's
// texts: the game's lookup (msg_string, host/options.c) answers a text whose id and English are in
// the file with the file's words instead, so a translation such as pl.po needs no code, and an
// edited fr.po changes the French. The game itself stays in English underneath (its own Language
// page set to another language shows that language's texts as the disc has them).
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

// Where the files are, and the languages FOLDER has a .po for.
void set_folder(const std::filesystem::path& folder);
std::vector<i18n::LanguageInfo> languages();

// Reads FOLDER/<code>.po for the game's texts; English (none) when it is missing. True when one
// was read.
bool use(const std::string& code);

// The game's lookup asked for ID, which gave ORIGINAL (a game address): the address of the
// translation, or 0 to keep the game's.
std::uint32_t translate(int id, std::uint32_t original);

}  // namespace openrac::frontend::game_text
