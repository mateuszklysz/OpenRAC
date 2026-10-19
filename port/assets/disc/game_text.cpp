// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "assets/disc/game_text.h"

#include <array>
#include <cctype>
#include <format>
#include <set>
#include <utility>

#include "assets/disc/disc.h"
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

// The letters the fonts lack that the port draws (game/rac1/host/ui2d.c): the plain letter, then a
// mark byte the game's texts never use, for the mark the port adds over or under it.
struct Marked {
    std::string_view letter;
    char plain;
    u8 mark;
};

constexpr Marked kMarked[] = {
    {"ą", 'a', kMarkOgonek},
    {"ć", 'c', kMarkAcute},
    {"ę", 'e', kMarkOgonek},
    {"ł", 'l', kMarkStroke},
    {"ń", 'n', kMarkAcute},
    {"ś", 's', kMarkAcute},
    {"ź", 'z', kMarkAcute},
    {"ż", 'z', kMarkDot},
    {"Ą", 'A', kMarkOgonek},
    {"Ć", 'C', kMarkAcute},
    {"Ę", 'E', kMarkOgonek},
    {"Ł", 'L', kMarkStroke},
    {"Ń", 'N', kMarkAcute},
    {"Ś", 'S', kMarkAcute},
    {"Ź", 'Z', kMarkAcute},
    {"Ż", 'Z', kMarkDot},
};

const GameLanguage kLanguages[] = {
    {Language::English, "en", "English"},
    {Language::French, "fr", "Français"},
    {Language::German, "de", "Deutsch"},
    {Language::Spanish, "es", "Español"},
    {Language::Italian, "it", "Italiano"},
};

// The marked letter that OUT's last character and MARK make, if any.
const Marked* marked(const std::string& out, u8 mark) {
    for (const Marked& m : kMarked) {
        if (m.mark == mark && !out.empty() && out.back() == m.plain
            && (out.size() < 2 || out[out.size() - 2] != '[')) {
            return &m;
        }
    }
    return nullptr;
}

// The length of the UTF-8 character starting at `lead`.
std::size_t utf8_length(unsigned char lead) {
    return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
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
    for (const char ch : game) {
        const auto c = static_cast<u8>(ch);
        if (c == '[') {
            out += "[[";
        } else if (c >= 0x20 && c < 0x7f) {
            out += ch;
        } else if (c == kNewLine) {
            out += '\n';
        } else if (c >= kFirstAccent && c < kFirstAccent + kAccented.size()) {
            out += kAccented[c - kFirstAccent];
        } else if (const Marked* m = marked(out, c)) {
            out.back() = m->letter[0];
            out.append(m->letter.substr(1));
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
            if (m.letter == letter) {
                out += m.plain;
                out += static_cast<char>(m.mark);
                accented = true;
            }
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
    return pair_texts(blocks);
}

bool has_game_texts(const Disc& disc, Language language) {
    return !all_text_block(all_text_lump(disc), language).empty();
}

}  // namespace openrac::assets::disc
