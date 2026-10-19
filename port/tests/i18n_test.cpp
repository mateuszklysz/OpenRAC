// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The port's translations (common/i18n.h): a .po file read as gettext writes one, the languages
// built in from port/lang, the system's languages matched to them, a folder's file in place of a
// built-in one, and texts for the game's fonts. Every built-in translation of a text the Video
// options page shows must fit its 48-byte slot once its letters are plain.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "common/i18n.h"
#include "common/i18n_files.h"

namespace {

namespace i18n = openrac::i18n;

constexpr const char* kPo = R"po(# A comment
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\n"
"Language: xx_YY\n"
"X-Language-Name: Testish\n"

#. A note
#: somewhere.cpp
msgid "Off"
msgstr "Wył."

msgid ""
"Two "
"lines"
msgstr "Dwie \"linie\"\n"

msgctxt "passed over"
msgid "Untranslated"
msgstr ""
)po";

}  // namespace

int main() {
    // A .po file.
    std::string why;
    const auto c = i18n::parse_po(kPo, why);
    CHECK(c.has_value());
    if (c) {
        CHECK(c->code == "xx_YY");
        CHECK(c->name == "Testish");
        CHECK(c->texts.size() == 2);  // the empty translation is left out
        CHECK(c->texts.at("Off") == "Wył.");
        CHECK(c->texts.at("Two lines") == "Dwie \"linie\"\n");
    }
    CHECK(!i18n::parse_po("msgid \"a\"\nmsgstr \"b\"\n", why));  // no header
    CHECK(!i18n::parse_po("msgstr \"b\"\n", why));               // no msgid

    // The built-in languages: English first, Polish from port/lang/pl.po.
    const std::vector<i18n::LanguageInfo> all = i18n::languages();
    CHECK(!all.empty() && all[0].code == "en");
    bool polish = false;
    for (const auto& l : all) {
        polish |= l.code == "pl" && l.name == "Polski";
    }
    CHECK(polish);

    // The system's languages.
    CHECK(i18n::from_locales(std::vector<std::string>{"de-DE", "pl-PL", "en-US"}) == "pl");
    CHECK(i18n::from_locales(std::vector<std::string>{"en_GB", "pl"}) == "en");
    CHECK(i18n::from_locales(std::vector<std::string>{"PL"}) == "pl");
    CHECK(i18n::from_locales(std::vector<std::string>{"fr"}) == "en");
    CHECK(i18n::from_locales(std::vector<std::string>{}) == "en");

    // Texts in the language chosen.
    CHECK(std::string(i18n::tr("Off")) == "Off");
    i18n::set_language("pl");
    CHECK(i18n::language() == "pl");
    CHECK(std::string(i18n::tr("Off")) == "Wył.");
    CHECK(std::string(i18n::tr("Not a text of the port")) == "Not a text of the port");
    i18n::set_language("klingon");
    CHECK(i18n::language() == "en");
    CHECK(std::string(i18n::tr("Off")) == "Off");

    // The game's fonts.
    CHECK(i18n::game_font("Rozdzielczość") == "Rozdzielczosc");
    CHECK(i18n::game_font("ŻÓŁĆ ąęńśź") == "ZOLC aensz");
    CHECK(i18n::game_font("Straße, crème") == "Strasse, creme");
    CHECK(i18n::game_font("4:3 → 16:9") == "4:3 ? 16:9");
    CHECK(i18n::game_font("plain") == "plain");

    // A folder's translation takes the place of the built-in one.
    const auto dir = std::filesystem::temp_directory_path() / "openrac_i18n_test";
    std::filesystem::create_directories(dir);
    std::ofstream(
        dir / "pl.po"
    ) << "msgid \"\"\nmsgstr \"Language: pl\\nX-Language-Name: Polski\\n\"\n\n"
         "msgid \"Off\"\nmsgstr \"Wyłączone\"\n";
    std::ofstream(dir / "broken.po") << "msgstr \"no id\"\n";
    std::ofstream(dir / "notes.txt") << "not a translation\n";
    i18n::set_language("pl");
    CHECK(i18n::load_folder(dir) == 1);
    CHECK(std::string(i18n::tr("Off")) == "Wyłączone");
    std::filesystem::remove_all(dir);

    // Every built-in translation of a short text (the Video options page's) fits the page's slot.
    for (const auto& f : i18n::files::all()) {
        const auto po = i18n::parse_po(f.text, why);
        CHECK(po.has_value());
        if (!po) {
            continue;
        }
        for (const auto& [english, translated] : po->texts) {
            if (i18n::game_font(english).size() < 48 && i18n::game_font(translated).size() >= 48) {
                std::fprintf(
                    stderr,
                    "%s: \"%s\" is too long for the Video options page\n",
                    f.name,
                    translated.c_str()
                );
                CHECK(false);
            }
        }
    }

    return openrac::test::result();
}
