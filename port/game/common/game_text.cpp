// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "game_text.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <system_error>
#include <unordered_map>

#include "common/i18n_files.h"
#include "common/log.h"
#include "openrac/game_host.h"
#include "openrac/guest.h"

#ifdef OPENRAC_HAS_DISC
#include "assets/disc/disc.h"
#include "assets/disc/game_text.h"
#include "assets/disc/toc.h"
#endif

namespace openrac::frontend::game_text {

namespace {

namespace fs = std::filesystem;

constexpr const char* kTemplate = "game.pot";

// One text of the file: the game's English, and the translation, both as the game's bytes.
struct Translated {
    std::string english;
    std::string text;
};

// A text of a translation built into the port: its key (game_text_key) and its words, as the
// game's bytes.
struct Keyed {
    std::string key;
    std::string text;
};

fs::path g_folder;
std::string g_code;
std::unordered_map<int, std::vector<Translated>> g_texts;  // the folder's, by the English
std::unordered_map<int, std::vector<Keyed>> g_keyed;       // the port's, by key

// The translations the game is given live in a ring of game memory: a string stays where it is
// until the ring has gone round, far longer than the game keeps one it was given (it asks again
// each frame it draws one). Going round forgets where each was.
constexpr std::uint32_t kRingBytes = 256 * 1024;
gaddr g_ring = 0;
std::uint32_t g_ring_next = 0;
std::unordered_map<const std::string*, gaddr> g_where;

// What translate answered for an id and a game address, while the text there is the same: the
// game asks for the same texts every frame, and a key costs a conversion and a CRC.
struct Answer {
    std::string shown;
    gaddr translated;
};

std::unordered_map<std::uint64_t, Answer> g_answers;

gaddr place(const std::string& text) {
    if (const auto it = g_where.find(&text); it != g_where.end()) {
        return it->second;
    }
    const auto size =
        static_cast<std::uint32_t>(std::min<std::size_t>(text.size() + 1, kRingBytes / 4));
    if (g_ring == 0) {
        g_ring = openrac_guest_static(kRingBytes);
    }
    if (g_ring_next + size > kRingBytes) {
        g_ring_next = 0;
        g_where.clear();
        g_answers.clear();  // they point into the ring
    }
    const gaddr at = g_ring + g_ring_next;
    char* dst = static_cast<char*>(G(at));
    std::memcpy(dst, text.data(), size - 1);
    dst[size - 1] = '\0';
    g_where.emplace(&text, at);
    g_ring_next = (g_ring_next + size + 3) & ~3u;
    return at;
}

// The language of a .po file's header, from its first lines.
std::optional<i18n::LanguageInfo> header_of(std::string_view text) {
    std::string why;
    const auto po = i18n::read_po(text.substr(0, text.find("\n\n")), why);
    if (!po || po->code.empty()) {
        return std::nullopt;
    }
    return i18n::LanguageInfo{po->code, po->name.empty() ? po->code : po->name};
}

// The built-in translation of the game's texts in CODE (port/lang/game/<code>.po), empty for none.
[[maybe_unused]] std::string_view built_in(const std::string& code) {
    const std::string name = "game/" + code + ".po";
    for (const i18n::files::File& f : i18n::files::all()) {
        if (name == f.name) {
            return f.text;
        }
    }
    return {};
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool write_file(const fs::path& path, const std::string& text) {
    const fs::path partial = path.string() + ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            return false;
        }
    }
    std::error_code error;
    fs::rename(partial, path, error);
    return !error;
}

}  // namespace

bool export_from_disc(const fs::path& image, const fs::path& folder, bool force, std::string& why) {
#ifdef OPENRAC_HAS_DISC
    namespace disc = assets::disc;
    if (!force && fs::exists(folder / kTemplate)) {
        return true;
    }
    try {
        const disc::Disc d = disc::Disc::open(image, disc::rac1_layout());
        std::error_code made;
        fs::create_directories(folder, made);
        const std::string note = "The game's texts, from your disc: keep this file on your "
                                 "computer (it is the disc's).\n"
                                 "port/lang/README.md in OpenRAC says how to translate it.";
        std::vector<disc::GameText> english;
        for (const disc::GameLanguage& l : disc::game_languages()) {
            if (!disc::has_game_texts(d, l.slot)) {
                continue;
            }
            std::vector<std::string> notes;
            const std::vector<disc::GameText> texts = disc::read_game_texts(d, l.slot, &notes);
            if (l.slot == disc::Language::English) {
                for (const std::string& n : notes) {
                    log::info("the game's texts: {}", n);
                }
            }
            i18n::PoFile po{l.code, l.name, {}};
            for (const disc::GameText& t : texts) {
                po.entries.push_back(
                    {std::to_string(t.id),
                     disc::game_text_to_utf8(t.english),
                     disc::game_text_to_utf8(t.text),
                     t.where}
                );
            }
            if (!write_file(folder / (std::string(l.code) + ".po"), i18n::write_po(po, note))) {
                why = std::format("cannot write {}", (folder / l.code).string());
                return false;
            }
            log::info(
                "the game's texts in {}: {} written to {}", l.name, texts.size(), folder.string()
            );
            if (l.slot == disc::Language::English) {
                english = texts;
            }
        }
        // The template: the English of every text, for a language the disc does not have.
        i18n::PoFile pot;
        for (const disc::GameText& t : english) {
            pot.entries.push_back(
                {std::to_string(t.id), disc::game_text_to_utf8(t.english), {}, t.where}
            );
        }
        if (!write_file(folder / kTemplate, i18n::write_po(pot, note))) {
            why = std::format("cannot write {}", (folder / kTemplate).string());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        why = e.what();
        return false;
    }
#else
    (void)image;
    (void)folder;
    (void)force;
    why = "this program reads no disc";
    return false;
#endif
}

void set_folder(const fs::path& folder) {
    g_folder = folder;
}

std::vector<i18n::LanguageInfo> languages() {
    std::vector<i18n::LanguageInfo> list;
    const auto add = [&](std::optional<i18n::LanguageInfo> l) {
        if (l && std::none_of(list.begin(), list.end(), [&](const auto& k) {
                return k.code == l->code;
            })) {
            list.push_back(std::move(*l));
        }
    };
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(g_folder, error)) {
        if (entry.path().extension() != ".po") {
            continue;
        }
        // The header is enough: the first lines.
        std::ifstream in(entry.path(), std::ios::binary);
        std::string head(4096, '\0');
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(in.gcount()));
        add(header_of(head));
    }
    for (const i18n::files::File& f : i18n::files::all()) {
        if (std::string_view(f.name).starts_with("game/")) {
            add(header_of(f.text));
        }
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.code < b.code;
    });
    return list;
}

bool use(const std::string& code) {
    if (code == g_code) {
        return !g_texts.empty() || !g_keyed.empty();
    }
    g_code = code;
    g_texts.clear();
    g_keyed.clear();
    g_where.clear();
    g_answers.clear();
    g_ring_next = 0;  // what the ring held was the last language's
#ifdef OPENRAC_HAS_DISC
    namespace disc = assets::disc;
    // The folder's file, by the English: a translator's own, or the disc's language edited.
    const fs::path path = g_folder / (code + ".po");
    if (!g_folder.empty() && fs::exists(path)) {
        std::string why;
        if (const auto po = i18n::read_po(read_file(path), why)) {
            std::size_t count = 0;
            for (const i18n::PoEntry& e : po->entries) {
                char* end = nullptr;
                const long id = std::strtol(e.context.c_str(), &end, 10);
                if (e.text.empty() || e.context.empty() || *end != '\0') {
                    continue;
                }
                g_texts[static_cast<int>(id)].push_back(
                    {disc::game_text_from_utf8(e.id), disc::game_text_from_utf8(e.text)}
                );
                ++count;
            }
            log::info("the game's texts from {}: {} translated", path.string(), count);
        } else {
            log::warn("{}: not a translation the port reads: {}", path.string(), why);
        }
    }
    // The port's own, by key: what the folder's leaves out.
    if (const std::string_view text = built_in(code); !text.empty()) {
        std::string why;
        if (const auto po = i18n::read_po(text, why)) {
            std::size_t count = 0;
            for (const i18n::PoEntry& e : po->entries) {
                char* end = nullptr;
                const long id = std::strtol(e.id.c_str(), &end, 10);
                if (e.text.empty() || end == e.id.c_str() || *end != '/') {
                    continue;
                }
                g_keyed[static_cast<int>(id)].push_back({e.id, disc::game_text_from_utf8(e.text)});
                ++count;
            }
            log::info("the game's texts from port/lang/game/{}.po: {} translated", code, count);
        } else {
            log::warn("port/lang/game/{}.po: {}", code, why);
        }
    }
    return !g_texts.empty() || !g_keyed.empty();
#else
    return false;
#endif
}

const std::string& code() {
    return g_code;
}

std::string disc_language(int slot) {
#ifdef OPENRAC_HAS_DISC
    for (const assets::disc::GameLanguage& l : assets::disc::game_languages()) {
        if (static_cast<int>(l.slot) == slot) {
            return l.code;
        }
    }
#endif
    (void)slot;
    return {};
}

bool on_disc(const std::string& code) {
#ifdef OPENRAC_HAS_DISC
    for (const assets::disc::GameLanguage& l : assets::disc::game_languages()) {
        if (code == l.code) {
            return true;
        }
    }
#endif
    (void)code;
    return false;
}

std::string for_font(const std::string& utf8) {
#ifdef OPENRAC_HAS_DISC
    // Only RAC1's FontPrint draws the marks (game/rac1/host/ui2d.c).
    if (std::string_view(openrac_game.game) != "rac1") {
        return i18n::game_font(utf8);
    }
    std::string escaped;  // a '[' of the text is not a button
    for (const char c : utf8) {
        escaped += c == '[' ? std::string("[[") : std::string(1, c);
    }
    return assets::disc::game_text_from_utf8(escaped);
#else
    return i18n::game_font(utf8);
#endif
}

namespace {

gaddr look_up(int id, const char* shown) {
    if (const auto it = g_texts.find(id); it != g_texts.end()) {
        for (const Translated& t : it->second) {
            if (std::strcmp(shown, t.english.c_str()) == 0) {
                return place(t.text);
            }
        }
    }
#ifdef OPENRAC_HAS_DISC
    if (const auto it = g_keyed.find(id); it != g_keyed.end()) {
        const std::string key =
            assets::disc::game_text_key(id, assets::disc::game_text_to_utf8(shown));
        for (const Keyed& k : it->second) {
            if (k.key == key) {
                return place(k.text);
            }
        }
    }
#endif
    return 0;
}

}  // namespace

std::uint32_t translate(int id, std::uint32_t original) {
    if (original == 0 || (g_texts.empty() && g_keyed.empty())) {
        return 0;
    }
    const char* shown = static_cast<const char*>(G(original));
    const std::uint64_t at =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(id)) << 32 | original;
    if (const auto it = g_answers.find(at); it != g_answers.end() && it->second.shown == shown) {
        return it->second.translated;
    }
    if (g_answers.size() >= 0x4000) {
        g_answers.clear();
    }
    const gaddr translated = look_up(id, shown);
#ifdef OPENRAC_HAS_DISC
    if (translated == 0 && id == assets::disc::kSubtitleId) {
        // A subtitle the language has no line for: its key, to find why (a changed English, an
        // export without the scenes).
        static std::set<std::string> told;
        const std::string key = assets::disc::game_text_key(id, assets::disc::game_text_to_utf8(shown));
        if (told.insert(key).second) {
            log::info("subtitle without a {} translation: {}", g_code, key);
        }
    }
#endif
    g_answers[at] = {shown, translated};
    return translated;
}

std::uint32_t translate_id(int id) {
    if (const auto it = g_texts.find(id); it != g_texts.end()) {
        for (const Translated& t : it->second) {
            if (t.text != t.english) {
                return place(t.text);
            }
        }
        return 0;
    }
    if (const auto it = g_keyed.find(id); it != g_keyed.end() && !it->second.empty()) {
        return place(it->second.front().text);
    }
    return 0;
}

}  // namespace openrac::frontend::game_text
