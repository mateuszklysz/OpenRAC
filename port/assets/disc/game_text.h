// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// RAC1's texts as files a translator edits (port/lang/README.md): every text of the game, the
// global `all_text` lump's (menus, the HUD, goodies) and each level's (help boxes, banners,
// subtitles: messages.h), read from the player's disc in each of its languages and written as
// readable UTF-8, and turned back into the game's bytes for the game to draw.
//
// The game's bytes, after ReRAC's study (docs/plan/hud_text.md section 5; ISC License, Copyright
// (c) 2026 ReRAC contributors): ASCII 0x20..0x7e; 0x01 a new line; 0x08..0x0f colour codes;
// 0x10..0x19 pad buttons; 0x80..0xa7 the accented vowels A a E e I i O o U u, four of each
// (acute, then grave, circumflex and diaeresis: the order of the last three is ReRAC's inference
// from the French and Spanish strings), drawn as the letter with an accent over it; 0xa8.. a few
// more letters whose order is not known. In the files a colour or a button is a word in brackets
// ([orange], [cross]), a byte without a name [xNN], and a '[' of the text "[[". The game's fonts
// have no other letters. The Latin ones of the languages the port adds are the plain letter and
// its mark (kMark*: ś is 's', 0x02; č is 'c', 0x06, 0x02), which the port draws over or under it
// from the fonts' own glyphs (game/rac1/host/ui2d.c) and the game skips; Greek, Hangul and Chinese
// letters are a byte about as wide and the letter's number (kForeign), drawn from a font file;
// any other letter the fonts lack is written as its plain one (ß ss).
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/bytes.h"
#include "assets/disc/messages.h"

namespace openrac::assets::disc {

class Disc;

// The mark bytes after a Polish letter's plain one: bytes the game's texts never hold.
inline constexpr u8 kMarkAcute = 0x02;   // ć ń ś ź
inline constexpr u8 kMarkDot = 0x03;     // ż
inline constexpr u8 kMarkOgonek = 0x04;  // ą ę
inline constexpr u8 kMarkStroke = 0x05;  // ł đ
// The other marks: kMarkMore, then one of these.
inline constexpr u8 kMarkMore = 0x06;
inline constexpr u8 kMarkCaron = 0x02;         // č ď ě ň ř š ť ž
inline constexpr u8 kMarkRing = 0x03;          // ů
inline constexpr u8 kMarkDoubleAcute = 0x04;   // ő ű
inline constexpr u8 kMarkBreve = 0x05;         // ă ğ
inline constexpr u8 kMarkBelow = 0x06;         // ș ț, and as a cedilla ç ş
inline constexpr u8 kMarkDotless = 0x07;       // ı: í (0x94) without its accent

// A letter no mark makes (Greek, Hangul, Chinese): a byte the fonts draw about as wide, whose quad
// the port replaces with the letter from a font file (game/common/glyphs.h), then kForeign and
// the letter's number among the language's (set_foreign_letters) in foreign_digits() digits of
// base 6, each a byte 0x02..0x07, the highest first. A lone kMarkMore after a letter is a place
// a line may break (between Chinese characters), which the printers skip.
inline constexpr u8 kForeign = 0x07;

// The letters of the language in use that are drawn from its font file, sorted; empty for none.
// A language's texts are turned into the game's bytes after its letters are set.
void set_foreign_letters(std::vector<char32_t> letters);
const std::vector<char32_t>& foreign_letters();
int foreign_digits();

// The letters of UTF8 that neither the fonts nor the port's marks draw (the ones a font file has to).
std::vector<char32_t> letters_without_marks(std::string_view utf8);

// The game's bytes as a translator reads them.
std::string game_text_to_utf8(std::string_view game);

// A translator's text as the game's bytes (game_text_to_utf8 undone; any other letter as the
// closest the fonts draw, '?' for none).
std::string game_text_from_utf8(std::string_view utf8);

// A text's key in a translation built into the port (port/lang/game/): "<id>/<crc>", the CRC-32
// (zlib's) of UTF8_ENGLISH, its English as game_text_to_utf8 writes it, in eight lower-case hex
// digits. Such a file says which of the disc's texts it translates without holding the English.
std::string game_text_key(s32 id, std::string_view utf8_english);

// The languages of the disc's text blocks, by slot (messages.h): their codes and names.
struct GameLanguage {
    Language slot;
    const char* code;  // "en", "fr", ...
    const char* name;  // in its own words
};

std::span<const GameLanguage> game_languages();

// One text of the game in one language.
// The id the scenes' subtitles have among the game's texts: they have none of their own, so their
// keys are this id and the CRC of their English.
inline constexpr s32 kSubtitleId = -1;

struct GameText {
    s32 id;
    std::string where;    // "all_text", or "level 05"
    std::string english;  // the game's bytes
    std::string text;     // the game's bytes, in the language asked for; empty when it has none
};

// The texts of a set of blocks (the global one and each level's), English beside LANGUAGE's,
// paired by id, in the blocks' order. A text found again with the same id and English (most
// levels repeat the common ones) is kept once.
struct TextBlocks {
    std::string where;
    std::vector<Message> english;
    std::vector<Message> translated;
};

std::vector<GameText> pair_texts(const std::vector<TextBlocks>& blocks);

// Every text of the disc in LANGUAGE (the PAL gameplay files where the disc has them). Throws
// AssetError when the disc is not what its layout says. NOTES, when given, gets a line a level on
// what was read there, for the log.
std::vector<GameText> read_game_texts(
    const Disc& disc, Language language, std::vector<std::string>* notes = nullptr
);

// Whether the disc has any text in LANGUAGE (an NTSC-U disc is English only).
bool has_game_texts(const Disc& disc, Language language);

}  // namespace openrac::assets::disc
