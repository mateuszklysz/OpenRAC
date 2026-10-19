// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The language of the port's own words: the rows of the Video options page and the message boxes
// a player may see. The game's texts are the game's (its Language page picks among the disc's);
// these are only what the port adds. The log stays in English, for whoever reads a report.
//
// The translations are gettext files, one a language, in port/lang/ (its README says how to make
// one): `<code>.po`, built into the program, so a translator never edits code. A folder of .po
// files read at start (load_folder) adds to them or takes their place, to try a translation
// without building. The English text in the code is the key: tr("Off") is "Off" in English and
// the file's msgstr in another language; a text a file lacks shows in English. In the code a text
// to translate is written tr("...") where it is shown, or N_("...") in a table of them, so
// port/tools/i18n_template.py finds it for port/lang/openrac.pot.
#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openrac::i18n {

// The English of the port's words: the key, and what is shown without a translation.
inline constexpr std::string_view kEnglish = "en";

// One language's translations, from its .po file.
struct Catalogue {
    std::string code;  // the header's Language: ("pl", "pt_BR")
    std::string name;  // the header's X-Language-Name: the language in its own words ("Polski")
    std::unordered_map<std::string, std::string> texts;  // msgid -> msgstr, the empty ones left out
};

// Reads a .po file's text. Nothing, with WHY, for one that is not one or has no Language:.
std::optional<Catalogue> parse_po(std::string_view text, std::string& why);

// The languages there are: English first, then each catalogue's, by code.
struct LanguageInfo {
    std::string code;
    std::string name;
};

std::vector<LanguageInfo> languages();

// Reads every .po file in FOLDER; one for a language already known takes its place. Returns how
// many were read (a file that is not one is skipped, with a warning in the log).
int load_folder(const std::filesystem::path& folder);

// The language the port speaks now, by code ("en" until set_language). An unknown code is English.
std::string language();
void set_language(std::string_view code);

// The first language there is among BCP 47 tags in order of preference ("pl-PL", "pt_BR", "de"),
// as the system lists them: the whole tag's, else its language's; "en" when none.
std::string from_locales(std::span<const std::string> tags);

// ENGLISH in the language now; ENGLISH itself where the catalogue has nothing for it.
const char* tr(const char* english);

// Marks a text in a table for the template; it is translated with tr() where it is shown.
constexpr const char* N_(const char* english) {
    return english;
}

// A text for the game's bitmap fonts, whose 232 glyphs are ASCII and the accented letters of the
// disc's own languages (assets/world/font.h), in an order of the game's: a Latin letter with an
// accent becomes its plain one (ą a, Ł L, é e, ß ss), and any other character outside ASCII a '?',
// so no UTF-8 byte is drawn as a stray glyph.
std::string game_font(std::string_view utf8);

}  // namespace openrac::i18n
