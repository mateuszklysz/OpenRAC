// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "game_text.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_map>

#include "common/log.h"
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

fs::path g_folder;
std::string g_code;
std::unordered_map<int, std::vector<Translated>> g_texts;

// The translations the game is given live in a ring of game memory: a string stays where it is
// until the ring has gone round, far longer than the game keeps one it was given (it asks again
// each frame it draws one). Going round forgets where each was.
constexpr std::uint32_t kRingBytes = 256 * 1024;
gaddr g_ring = 0;
std::uint32_t g_ring_next = 0;
std::unordered_map<const std::string*, gaddr> g_where;

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
    }
    const gaddr at = g_ring + g_ring_next;
    char* dst = static_cast<char*>(G(at));
    std::memcpy(dst, text.data(), size - 1);
    dst[size - 1] = '\0';
    g_where.emplace(&text, at);
    g_ring_next = (g_ring_next + size + 3) & ~3u;
    return at;
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
            const std::vector<disc::GameText> texts = disc::read_game_texts(d, l.slot);
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
        const auto cut = head.find("\n\n");
        std::string why;
        if (const auto po = i18n::read_po(head.substr(0, cut), why); po && !po->code.empty()) {
            list.push_back({po->code, po->name.empty() ? po->code : po->name});
        }
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.code < b.code;
    });
    return list;
}

bool use(const std::string& code) {
    if (code == g_code) {
        return !g_texts.empty();
    }
    g_code = code;
    g_texts.clear();
    g_where.clear();
    const fs::path path = g_folder / (code + ".po");
    if (g_folder.empty() || !fs::exists(path)) {
        return false;
    }
    std::string why;
    const auto po = i18n::read_po(read_file(path), why);
    if (!po) {
        log::warn("{}: not a translation the port reads: {}", path.string(), why);
        return false;
    }
#ifdef OPENRAC_HAS_DISC
    std::size_t count = 0;
    for (const i18n::PoEntry& e : po->entries) {
        char* end = nullptr;
        const long id = std::strtol(e.context.c_str(), &end, 10);
        if (e.text.empty() || e.context.empty() || *end != '\0') {
            continue;
        }
        g_texts[static_cast<int>(id)].push_back(
            {assets::disc::game_text_from_utf8(e.id), assets::disc::game_text_from_utf8(e.text)}
        );
        ++count;
    }
    log::info("the game's texts from {}: {} translated", path.string(), count);
    return true;
#else
    return false;
#endif
}

std::uint32_t translate(int id, std::uint32_t original) {
    if (g_texts.empty() || original == 0) {
        return 0;
    }
    const auto it = g_texts.find(id);
    if (it == g_texts.end()) {
        return 0;
    }
    const char* shown = static_cast<const char*>(G(original));
    for (const Translated& t : it->second) {
        if (std::strcmp(shown, t.english.c_str()) == 0) {
            return place(t.text);
        }
    }
    return 0;
}

}  // namespace openrac::frontend::game_text
