// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "assets/disc/game_text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <set>
#include <utility>

#include "assets/disc/disc.h"
#include "assets/disc/scene.h"
#include "assets/disc/wad.h"
#include "common/i18n.h"

namespace openrac::assets::disc {

namespace {

// The names of the control bytes, as the files write them.
constexpr std::pair<u8, std::string_view> kNamed[] = {
    {0x08, "default"},
    {0x09, "blue"},
    {0x0a, "green"},
    {0x0b, "purple"},
    {0x0c, "orange"},
    {0x10, "cross"},
    {0x11, "circle"},
    {0x12, "triangle"},
    {0x13, "square"},
    {0x14, "L1"},
    {0x15, "R1"},
    {0x16, "L2"},
    {0x17, "R2"},
    // The help texts name 0x18 for the camera and 0x19 for walking and steering: the right stick,
    // then the left (ReRAC's notes have them the other way round).
    {0x18, "rstick"},
    {0x19, "lstick"},
};

constexpr u8 kNewLine = 0x01;
constexpr u8 kFirstAccent = 0x80;
// The accented letters 0x80..0xa7 in UTF-8: four of each of A a E e I i O o U u (acute, grave,
// circumflex, diaeresis).
constexpr std::array<std::string_view, 40> kAccented = {
    "Á", "À", "Â", "Ä", "á", "à", "â", "ä", "É", "È", "Ê", "Ë", "é", "è",
    "ê", "ë", "Í", "Ì", "Î", "Ï", "í", "ì", "î", "ï", "Ó", "Ò", "Ô", "Ö",
    "ó", "ò", "ô", "ö", "Ú", "Ù", "Û", "Ü", "ú", "ù", "û", "ü",
};

// The letters the fonts lack that the port draws (game/rac1/host/ui2d.c): the plain letter, then
// mark bytes the game's texts never use, for the mark the port adds over or under it.
struct Marked {
    std::string_view letter;
    char plain;
    std::string_view mark;
};

#define MORE "\x06"
constexpr Marked kMarked[] = {
    // Polish
    {"ą", 'a', "\x04"},
    {"ć", 'c', "\x02"},
    {"ę", 'e', "\x04"},
    {"ł", 'l', "\x05"},
    {"ń", 'n', "\x02"},
    {"ś", 's', "\x02"},
    {"ź", 'z', "\x02"},
    {"ż", 'z', "\x03"},
    {"Ą", 'A', "\x04"},
    {"Ć", 'C', "\x02"},
    {"Ę", 'E', "\x04"},
    {"Ł", 'L', "\x05"},
    {"Ń", 'N', "\x02"},
    {"Ś", 'S', "\x02"},
    {"Ź", 'Z', "\x02"},
    {"Ż", 'Z', "\x03"},
    // Czech, Croatian, Hungarian, Romanian, Turkish
    {"ý", 'y', "\x02"},
    {"Ý", 'Y', "\x02"},
    {"İ", 'I', "\x03"},
    {"đ", 'd', "\x05"},
    {"Đ", 'D', "\x05"},
    {"č", 'c', MORE "\x02"},
    {"ď", 'd', MORE "\x02"},
    {"ě", 'e', MORE "\x02"},
    {"ň", 'n', MORE "\x02"},
    {"ř", 'r', MORE "\x02"},
    {"š", 's', MORE "\x02"},
    {"ť", 't', MORE "\x02"},
    {"ž", 'z', MORE "\x02"},
    {"Č", 'C', MORE "\x02"},
    {"Ď", 'D', MORE "\x02"},
    {"Ě", 'E', MORE "\x02"},
    {"Ň", 'N', MORE "\x02"},
    {"Ř", 'R', MORE "\x02"},
    {"Š", 'S', MORE "\x02"},
    {"Ť", 'T', MORE "\x02"},
    {"Ž", 'Z', MORE "\x02"},
    {"ů", 'u', MORE "\x03"},
    {"Ů", 'U', MORE "\x03"},
    {"ő", 'o', MORE "\x04"},
    {"ű", 'u', MORE "\x04"},
    {"Ő", 'O', MORE "\x04"},
    {"Ű", 'U', MORE "\x04"},
    {"ă", 'a', MORE "\x05"},
    {"ğ", 'g', MORE "\x05"},
    {"Ă", 'A', MORE "\x05"},
    {"Ğ", 'G', MORE "\x05"},
    {"ș", 's', MORE "\x06"},
    {"ț", 't', MORE "\x06"},
    {"ç", 'c', MORE "\x06"},
    {"ş", 's', MORE "\x06"},
    {"Ș", 'S', MORE "\x06"},
    {"Ț", 'T', MORE "\x06"},
    {"Ç", 'C', MORE "\x06"},
    {"Ş", 'S', MORE "\x06"},
    {"ţ", 't', MORE "\x06"},  // the cedilla forms Romanian once used
    {"Ţ", 'T', MORE "\x06"},
    {"ı", '\x94', MORE "\x07"},
};
#undef MORE

const GameLanguage kLanguages[] = {
    {Language::English, "en", "English"},
    {Language::French, "fr", "Français"},
    {Language::German, "de", "Deutsch"},
    {Language::Spanish, "es", "Español"},
    {Language::Italian, "it", "Italiano"},
};

// The marked letter GAME's bytes from AT make (its plain letter and the longest mark after it).
const Marked* marked(std::string_view game, std::size_t at) {
    const Marked* found = nullptr;
    for (const Marked& m : kMarked) {
        if (game[at] == m.plain && game.substr(at + 1).starts_with(m.mark)
            && (found == nullptr || m.mark.size() > found->mark.size())) {
            found = &m;
        }
    }
    return found;
}

// The length of the UTF-8 character starting at `lead`.
std::size_t utf8_length(unsigned char lead) {
    return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

// The character a UTF-8 sequence holds (0xFFFD when it is not one).
char32_t utf8_code(std::string_view s) {
    const auto b = [&](std::size_t k) { return static_cast<char32_t>(static_cast<unsigned char>(s[k])); };
    switch (s.size()) {
        case 1:
            return b(0) < 0x80 ? b(0) : 0xFFFD;
        case 2:
            return (b(0) & 0x1F) << 6 | (b(1) & 0x3F);
        case 3:
            return (b(0) & 0x0F) << 12 | (b(1) & 0x3F) << 6 | (b(2) & 0x3F);
        case 4:
            return (b(0) & 0x07) << 18 | (b(1) & 0x3F) << 12 | (b(2) & 0x3F) << 6 | (b(3) & 0x3F);
        default:
            return 0xFFFD;
    }
}

std::string utf8_of(char32_t c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | c >> 6);
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | c >> 12);
        out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | c >> 18);
        out += static_cast<char>(0x80 | (c >> 12 & 0x3F));
        out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

std::vector<char32_t> g_foreign;
int g_digits = 1;

// Han characters and the punctuation of Chinese text: a line may break between them.
bool chinese(char32_t c) {
    return (c >= 0x3000 && c < 0x3040) || (c >= 0x3400 && c < 0xA000) || (c >= 0xF900 && c < 0xFB00)
           || (c >= 0xFF00 && c < 0xFFF0);
}

// What a line may not begin with, and what it may not end with.
bool no_break_before(char32_t c) {
    return std::u32string_view(U"，。、；：！？）」』】》〉…—～·%,.;:!?)]") .find(c) != std::u32string_view::npos;
}

bool no_break_after(char32_t c) {
    return std::u32string_view(U"（「『【《〈([") .find(c) != std::u32string_view::npos;
}

// The byte the game draws in a foreign letter's place: about as wide. Greek letters take the
// Latin letter they look like (or one as wide); Hangul and Chinese characters, as wide as high,
// a W.
char placeholder(char32_t c) {
    // Α..Ω (0x3A2 is no letter), then α..ω.
    constexpr std::string_view upper = "ABFAEZHOIKAMNEOHP?ETYOXWO";
    constexpr std::string_view lower = "abydeznoikkuveonpsotuoxww";
    constexpr std::u32string_view accented = U"ΆΈΉΊΌΎΏάέήίόύώϊϋΐΰ";
    constexpr std::string_view accented_as = "AEHIOYOaeniouwiuiu";
    if (c >= 0x391 && c <= 0x3A9) {
        return upper[c - 0x391];
    }
    if (c >= 0x3B1 && c <= 0x3C9) {
        return lower[c - 0x3B1];
    }
    if (const auto k = accented.find(c); k != std::u32string_view::npos) {
        return accented_as[k];
    }
    if (c >= 0x370 && c < 0x400) {
        return 'o';
    }
    if (c == 0x2026) {  // …: three dots' width
        return 'm';
    }
    return 'W';
}

// A text block of `all_text`: eight offsets, one a language slot.
std::vector<Message> all_text_block(ByteView lump, Language language) {
    const u32 off = lump.u32_at(4 * static_cast<std::size_t>(language));
    if (off == 0 || off >= lump.size()) {
        return {};
    }
    return parse_text_block(lump.tail(off, "all_text block"));
}

std::vector<u8> all_text_lump(const Disc& disc) {
    for (const StreamLump& l : disc.global_lumps()) {
        if (l.name == "all_text") {
            return disc.read_lump(l);
        }
    }
    fail("the disc has no all_text lump");
}

// Level ID's gameplay file, decompressed: PAL's where the disc has one, else NTSC's.
std::optional<std::vector<u8>> gameplay(const Disc& disc, u32 id) {
    std::optional<DiscFile> found;
    for (const DiscFile& f : disc.level_group_files(id)) {
        if (f.path.ends_with("/gameplay_pal.bin")
            || (!found && f.path.ends_with("/gameplay_ntsc.bin"))) {
            found = f;
        }
    }
    if (!found) {
        return std::nullopt;
    }
    const std::vector<u8> raw = disc.iso().read_bytes(found->offset, found->bytes);
    return is_wad(raw) ? wad_decompress(raw) : raw;
}

}  // namespace

std::string game_text_to_utf8(std::string_view game) {
    std::string out;
    for (std::size_t i = 0; i < game.size(); ++i) {
        const auto c = static_cast<u8>(game[i]);
        // A foreign letter: its byte, kForeign and the digits of its number.
        const auto digit_count = static_cast<std::size_t>(g_digits);
        if (!g_foreign.empty() && c >= 0x20 && i + 1 + digit_count < game.size()
            && static_cast<u8>(game[i + 1]) == kForeign) {
            std::size_t n = 0;
            bool digits = true;
            for (int k = 0; digits && k < g_digits; ++k) {
                const auto d = static_cast<u8>(game[i + 2 + static_cast<std::size_t>(k)]);
                digits = d >= 0x02 && d <= 0x07;
                n = n * 6 + (d - 0x02u);
            }
            if (digits && n < g_foreign.size()) {
                out += utf8_of(g_foreign[n]);
                i += 1 + static_cast<std::size_t>(g_digits);
                // The place a line may break after it.
                if (i + 1 < game.size() && static_cast<u8>(game[i + 1]) == kMarkMore
                    && (i + 2 >= game.size() || static_cast<u8>(game[i + 2]) - 0x02u >= 6u)) {
                    ++i;
                }
                continue;
            }
        }
        if (const Marked* m = marked(game, i)) {
            out += m->letter;
            i += m->mark.size();
        } else if (c == '[') {
            out += "[[";
        } else if (c >= 0x20 && c < 0x7f) {
            out += static_cast<char>(c);
        } else if (c == kNewLine) {
            out += '\n';
        } else if (c >= kFirstAccent && c < kFirstAccent + kAccented.size()) {
            out += kAccented[c - kFirstAccent];
        } else {
            std::string name = std::format("x{:02X}", c);
            for (const auto& [byte, word] : kNamed) {
                if (byte == c) {
                    name = word;
                }
            }
            out += "[" + name + "]";
        }
    }
    return out;
}

void set_foreign_letters(std::vector<char32_t> letters) {
    std::sort(letters.begin(), letters.end());
    letters.erase(std::unique(letters.begin(), letters.end()), letters.end());
    g_foreign = std::move(letters);
    g_digits = 1;
    for (std::size_t room = 6; room < g_foreign.size(); room *= 6) {
        ++g_digits;
    }
}

const std::vector<char32_t>& foreign_letters() {
    return g_foreign;
}

int foreign_digits() {
    return g_digits;
}

std::vector<char32_t> letters_without_marks(std::string_view utf8) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < utf8.size();) {
        const std::size_t length = std::min(utf8_length(static_cast<unsigned char>(utf8[i])), utf8.size() - i);
        const std::string_view letter = utf8.substr(i, length);
        i += length;
        if (length == 1 || std::find(kAccented.begin(), kAccented.end(), letter) != kAccented.end()
            || std::any_of(std::begin(kMarked), std::end(kMarked), [&](const Marked& m) { return m.letter == letter; })) {
            continue;
        }
        out.push_back(utf8_code(letter));
    }
    return out;
}

std::string game_text_key(s32 id, std::string_view utf8_english) {
    static const std::array<u32, 256> table = [] {
        std::array<u32, 256> t{};
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    u32 crc = 0xFFFFFFFFu;
    for (const char ch : utf8_english) {
        crc = table[(crc ^ static_cast<u8>(ch)) & 0xFF] ^ (crc >> 8);
    }
    return std::format("{}/{:08x}", id, crc ^ 0xFFFFFFFFu);
}

std::string game_text_from_utf8(std::string_view utf8) {
    std::string out;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        if (c == '[') {
            if (utf8.substr(i).starts_with("[[")) {
                out += '[';
                i += 2;
                continue;
            }
            const auto close = utf8.find(']', i);
            if (close != std::string_view::npos) {
                const std::string_view name = utf8.substr(i + 1, close - i - 1);
                std::optional<u8> byte;
                for (const auto& [b, word] : kNamed) {
                    if (word == name) {
                        byte = b;
                    }
                }
                if (!byte && name.size() == 3 && name[0] == 'x'
                    && std::isxdigit(static_cast<unsigned char>(name[1]))
                    && std::isxdigit(static_cast<unsigned char>(name[2]))) {
                    byte = static_cast<u8>(std::stoi(std::string(name.substr(1)), nullptr, 16));
                }
                if (byte && *byte != 0) {
                    out += static_cast<char>(*byte);
                    i = close + 1;
                    continue;
                }
            }
            out += '[';  // not a name: the bracket itself
            ++i;
            continue;
        }
        if (c == '\n') {
            out += static_cast<char>(kNewLine);
            ++i;
            continue;
        }
        if (c < 0x80) {
            out += utf8[i++];
            continue;
        }
        const std::size_t length = std::min(utf8_length(c), utf8.size() - i);
        const std::string_view letter = utf8.substr(i, length);
        i += length;
        bool accented = false;
        for (std::size_t k = 0; k < kAccented.size(); ++k) {
            if (kAccented[k] == letter) {
                out += static_cast<char>(kFirstAccent + k);
                accented = true;
            }
        }
        for (const Marked& m : kMarked) {
            if (!accented && m.letter == letter) {
                out += m.plain;
                out += m.mark;
                accented = true;
            }
        }
        const char32_t code = utf8_code(letter);
        if (const auto at = std::lower_bound(g_foreign.begin(), g_foreign.end(), code);
            !accented && at != g_foreign.end() && *at == code) {
            auto n = static_cast<std::size_t>(at - g_foreign.begin());
            out += placeholder(code);
            out += static_cast<char>(kForeign);
            std::string digits(static_cast<std::size_t>(g_digits), '\x02');
            for (std::size_t k = digits.size(); k-- > 0; n /= 6) {
                digits[k] = static_cast<char>(0x02 + n % 6);
            }
            out += digits;
            // Chinese breaks between its characters, not before a closing mark or after an
            // opening one.
            if (i < utf8.size() && chinese(code) && !no_break_after(code)) {
                const std::size_t next_length = std::min(utf8_length(static_cast<unsigned char>(utf8[i])), utf8.size() - i);
                const char32_t next = utf8_code(utf8.substr(i, next_length));
                if (next > 0x20 && !no_break_before(next)) {
                    out += static_cast<char>(kMarkMore);
                }
            }
            accented = true;
        }
        if (!accented) {
            out += i18n::game_font(letter);  // the plain letter, or '?'
        }
    }
    return out;
}

std::span<const GameLanguage> game_languages() {
    return kLanguages;
}

std::vector<GameText> pair_texts(const std::vector<TextBlocks>& blocks) {
    std::vector<GameText> out;
    std::set<std::pair<s32, std::string>> seen;
    for (const TextBlocks& b : blocks) {
        for (const Message& m : b.english) {
            if (!seen.emplace(m.id, m.text).second) {
                continue;
            }
            const auto at = find_message(b.translated, m.id);
            out.push_back({m.id, b.where, m.text, at ? b.translated[*at].text : std::string()});
        }
    }
    return out;
}

std::vector<GameText> read_game_texts(
    const Disc& disc, Language language, std::vector<std::string>* notes
) {
    std::vector<TextBlocks> blocks;
    const std::vector<u8> all = all_text_lump(disc);
    blocks.push_back(
        {"all_text", all_text_block(all, Language::English), all_text_block(all, language)}
    );
    for (const u32 id : disc.level_ids()) {
        const auto g = gameplay(disc, id);
        if (!g) {
            if (notes) {
                notes->push_back(std::format("level {:02}: no gameplay file", id));
            }
            continue;
        }
        const ByteView view(*g);
        // A slot whose offset is 0 has no block (NTSC-U's other languages).
        auto block = [&](Language l) {
            const u32 off = view.u32_at(0x10 + 4 * static_cast<std::size_t>(l));
            return off == 0 ? std::vector<Message>{} : parse_messages(view, static_cast<u32>(l));
        };
        blocks.push_back({std::format("level {:02}", id), block(Language::English), block(language)}
        );
        if (notes) {
            notes->push_back(std::format(
                "level {:02}: gameplay {} bytes, English at {:#x} ({} texts), language at {:#x}",
                id,
                g->size(),
                view.u32_at(0x10),
                blocks.back().english.size(),
                view.u32_at(0x10 + 4 * static_cast<std::size_t>(language))
            ));
        }
    }
    std::vector<GameText> out = pair_texts(blocks);
    // The scenes' subtitles: each line in every language slot of its own (English first, then the
    // languages from French), under kSubtitleId, once for each English.
    const std::size_t slot =
        language == Language::English ? 0 : static_cast<std::size_t>(language) - 1;
    std::set<std::string> seen;
    for (const u32 id : disc.level_ids()) {
        const LevelHeader& h = disc.level_header(id);
        std::size_t lines = 0;
        for (std::size_t k = 0; k < h.scenes.size(); ++k) {
            for (const bool pal : {false, true}) {
                std::optional<std::vector<u8>> file;
                try {
                    file = disc.scene_region(id, k, pal);
                    if (!file) {
                        continue;
                    }
                    const Scene scene = Scene::load(
                        h, ByteView(*file), k, pal ? SceneRegion::Pal : SceneRegion::Ntsc
                    );
                    for (const SceneChunk& c : scene.chunks) {
                        for (const Subtitle& s : c.subtitles) {
                            if (s.text[0].empty() || !seen.insert(s.text[0]).second) {
                                continue;
                            }
                            out.push_back(
                                {kSubtitleId,
                                 std::format("level {:02} scene {}", id, k),
                                 s.text[0],
                                 slot < kSubtitleLanguages ? s.text[slot] : std::string()}
                            );
                            ++lines;
                        }
                    }
                } catch (const std::exception& e) {
                    if (notes) {
                        notes->push_back(std::format("level {:02} scene {}: {}", id, k, e.what()));
                    }
                }
                break;  // one region's chunks: the other's subtitles are the same lines
            }
        }
        if (notes && lines > 0) {
            notes->push_back(std::format("level {:02}: {} subtitle lines", id, lines));
        }
    }
    return out;
}

bool has_game_texts(const Disc& disc, Language language) {
    return !all_text_block(all_text_lump(disc), language).empty();
}

}  // namespace openrac::assets::disc
