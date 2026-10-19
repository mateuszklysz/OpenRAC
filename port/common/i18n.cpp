// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>

#include "common/i18n_files.h"
#include "common/log.h"

namespace openrac::i18n {

namespace {

// The catalogues by code: port/lang's (i18n_files.h), then a folder's in their place.
std::map<std::string, Catalogue>& catalogues() {
    static std::map<std::string, Catalogue> all = [] {
        std::map<std::string, Catalogue> built;
        for (const files::File& f : files::all()) {
            if (std::string_view(f.name).starts_with("game/")) {
                continue;  // the game's texts: game/common/game_text.h
            }
            std::string why;
            if (auto c = parse_po(f.text, why)) {
                built.insert_or_assign(c->code, std::move(*c));
            } else {
                log::warn("port/lang/{}: {}", f.name, why);
            }
        }
        return built;
    }();
    return all;
}

std::string g_language(kEnglish);
const Catalogue* g_catalogue = nullptr;

std::string lowered(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// A tag's form for comparing: lower case, '_' for '-' ("pt-BR" and "pt_br" are one).
std::string normal(std::string_view tag) {
    std::string out = lowered(tag);
    std::replace(out.begin(), out.end(), '-', '_');
    return out;
}

// ---- .po files ----

// The text of a quoted string at `line` (after its keyword), its escapes undone. False when it is
// not one.
bool unquote(std::string_view line, std::string& out) {
    const auto open = line.find('"');
    const auto close = line.rfind('"');
    if (open == std::string_view::npos || close == open) {
        return false;
    }
    for (std::size_t i = open + 1; i < close; ++i) {
        char c = line[i];
        if (c == '\\' && i + 1 < close) {
            c = line[++i];
            switch (c) {
                case 'n':
                    c = '\n';
                    break;
                case 't':
                    c = '\t';
                    break;
                default:
                    break;  // \" and \\ are themselves
            }
        }
        out += c;
    }
    return true;
}

// A header field's value ("Language: pl\n" ...), or empty.
std::string header_field(const std::string& header, std::string_view field) {
    std::istringstream in(header);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() > field.size() && line.starts_with(field) && line[field.size()] == ':') {
            const auto first = line.find_first_not_of(' ', field.size() + 1);
            return first == std::string::npos ? std::string() : line.substr(first);
        }
    }
    return {};
}

// TEXT as a .po string's inside.
std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
        }
    }
    return out;
}

// `keyword "text"`, a text with line breaks continued on a line for each, as gettext's tools write it.
std::string quoted(std::string_view keyword, std::string_view text) {
    const auto nl = text.find('\n');
    if (nl == std::string_view::npos || nl + 1 == text.size()) {
        return std::format("{} \"{}\"\n", keyword, escaped(text));
    }
    std::string out = std::format("{} \"\"\n", keyword);
    std::size_t at = 0;
    while (at < text.size()) {
        const auto cut = text.find('\n', at);
        const std::size_t end = cut == std::string_view::npos ? text.size() : cut + 1;
        out += "\"" + escaped(text.substr(at, end - at)) + "\"\n";
        at = end;
    }
    return out;
}

// The Latin letters with accents (U+00C0..U+017F) as the plain letters the game's fonts have.
constexpr const char* kPlain[] = {
    "A", "A", "A",  "A",  "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I",  "I",   // U+00C0
    "D", "N", "O",  "O",  "O", "O", "O",  "x", "O", "U", "U", "U", "U", "Y", "TH", "ss",  // U+00D0
    "a", "a", "a",  "a",  "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i",  "i",   // U+00E0
    "d", "n", "o",  "o",  "o", "o", "o",  "/", "o", "u", "u", "u", "u", "y", "th", "y",   // U+00F0
    "A", "a", "A",  "a",  "A", "a", "C",  "c", "C", "c", "C", "c", "C", "c", "D",  "d",   // U+0100
    "D", "d", "E",  "e",  "E", "e", "E",  "e", "E", "e", "E", "e", "G", "g", "G",  "g",   // U+0110
    "G", "g", "G",  "g",  "H", "h", "H",  "h", "I", "i", "I", "i", "I", "i", "I",  "i",   // U+0120
    "I", "i", "IJ", "ij", "J", "j", "K",  "k", "k", "L", "l", "L", "l", "L", "l",  "L",   // U+0130
    "l", "L", "l",  "N",  "n", "N", "n",  "N", "n", "n", "N", "n", "O", "o", "O",  "o",   // U+0140
    "O", "o", "OE", "oe", "R", "r", "R",  "r", "R", "r", "S", "s", "S", "s", "S",  "s",   // U+0150
    "S", "s", "T",  "t",  "T", "t", "Ŧ",  "ŧ", "U", "u", "U", "u", "U", "u", "U",  "u",   // U+0160
    "U", "u", "U",  "u",  "W", "w", "Y",  "y", "Y", "Z", "z", "Z", "z", "Z", "z",  "s",   // U+0170
};

}  // namespace

std::optional<PoFile> read_po(std::string_view text, std::string& why) {
    // Entries of msgctxt, msgid and msgstr, each string continued by the quoted lines after it.
    // Comments and plural forms (the port has none) are passed over.
    PoFile po;
    std::optional<std::string> header;
    PoEntry entry;
    std::string* into = nullptr;
    bool has_id = false;
    auto finish = [&] {
        if (has_id) {
            if (entry.id.empty() && entry.context.empty()) {
                header = entry.text;
            } else {
                po.entries.push_back(std::move(entry));
            }
        }
        entry = PoEntry{};
        has_id = false;
        into = nullptr;
    };
    std::size_t number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find('\n', start), text.size());
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++number;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')
        ) {
            line.remove_suffix(1);
        }
        const auto first = line.find_first_not_of(" \t");
        line = first == std::string_view::npos ? std::string_view() : line.substr(first);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        if (line.starts_with("msgctxt ")) {
            finish();
            into = &entry.context;
        } else if (line.starts_with("msgid ")) {
            if (has_id) {
                finish();
            }
            has_id = true;
            into = &entry.id;
        } else if (line.starts_with("msgstr ")) {
            if (!has_id) {
                why = std::format("line {}: msgstr without a msgid", number);
                return std::nullopt;
            }
            into = &entry.text;
        } else if (line.starts_with('"')) {
            if (into == nullptr) {
                continue;  // a continuation of something passed over (a plural)
            }
        } else {
            into = nullptr;  // msgid_plural, msgstr[n]: not the port's
            continue;
        }
        if (!unquote(line, *into)) {
            why = std::format("line {}: no quoted text", number);
            return std::nullopt;
        }
    }
    finish();
    if (!header) {
        why = "no header (the msgid \"\" entry)";
        return std::nullopt;
    }
    po.code = header_field(*header, "Language");
    po.name = header_field(*header, "X-Language-Name");
    return po;
}

std::optional<Catalogue> parse_po(std::string_view text, std::string& why) {
    std::optional<PoFile> po = read_po(text, why);
    if (!po) {
        return std::nullopt;
    }
    if (po->code.empty()) {
        why = "the header has no Language:";
        return std::nullopt;
    }
    Catalogue c;
    c.code = po->code;
    c.name = po->name.empty() ? po->code : po->name;
    for (PoEntry& e : po->entries) {
        if (!e.text.empty()) {
            c.texts.insert_or_assign(std::move(e.id), std::move(e.text));
        }
    }
    return c;
}

std::string write_po(const PoFile& po, std::string_view comment) {
    std::string out;
    std::istringstream lines{std::string(comment)};
    for (std::string line; std::getline(lines, line);) {
        out += line.empty() ? "#\n" : "# " + line + "\n";
    }
    out += "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n";
    if (!po.code.empty()) {
        out += "\"Language: " + po.code + "\\n\"\n";
    }
    if (!po.name.empty()) {
        out += "\"X-Language-Name: " + po.name + "\\n\"\n";
    }
    for (const PoEntry& e : po.entries) {
        out += "\n";
        if (!e.reference.empty()) {
            out += "#: " + e.reference + "\n";
        }
        if (!e.context.empty()) {
            out += quoted("msgctxt", e.context);
        }
        out += quoted("msgid", e.id);
        out += quoted("msgstr", e.text);
    }
    return out;
}

std::vector<LanguageInfo> languages() {
    std::vector<LanguageInfo> list{{std::string(kEnglish), "English"}};
    for (const auto& [code, c] : catalogues()) {
        if (code != kEnglish) {
            list.push_back({code, c.name});
        }
    }
    return list;
}

int load_folder(const std::filesystem::path& folder) {
    std::error_code error;
    int read = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.path().extension() != ".po") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string text(
            (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()
        );
        std::string why;
        if (auto c = parse_po(text, why)) {
            log::info("translation {} ({}) from {}", c->code, c->name, entry.path().string());
            const std::string code = c->code;
            catalogues().insert_or_assign(code, std::move(*c));
            ++read;
        } else {
            log::warn("{}: not a translation the port reads: {}", entry.path().string(), why);
        }
    }
    set_language(g_language);  // the catalogue in use may have been replaced
    return read;
}

std::string language() {
    return g_language;
}

void set_language(std::string_view code) {
    const auto it = catalogues().find(std::string(code));
    g_catalogue = it != catalogues().end() ? &it->second : nullptr;
    g_language = g_catalogue != nullptr ? std::string(code) : std::string(kEnglish);
}

std::string from_locales(std::span<const std::string> tags) {
    return from_locales(tags, languages());
}

std::string from_locales(
    std::span<const std::string> tags, const std::vector<LanguageInfo>& known
) {
    auto find = [&](std::string_view tag) -> const LanguageInfo* {
        for (const LanguageInfo& l : known) {
            if (normal(l.code) == normal(tag)) {
                return &l;
            }
        }
        return nullptr;
    };
    for (const std::string& tag : tags) {
        if (const LanguageInfo* whole = find(tag)) {
            return whole->code;
        }
        if (const LanguageInfo* base = find(tag.substr(0, tag.find_first_of("-_")))) {
            return base->code;
        }
    }
    return std::string(kEnglish);
}

const char* tr(const char* english) {
    if (g_catalogue != nullptr) {
        if (const auto it = g_catalogue->texts.find(english); it != g_catalogue->texts.end()) {
            return it->second.c_str();
        }
    }
    return english;
}

std::string game_font(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto byte = static_cast<unsigned char>(utf8[i]);
        if (byte < 0x80) {
            out += utf8[i++];
            continue;
        }
        // The lead byte says how many follow.
        const std::size_t length = byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
        if (length == 2 && i + 1 < utf8.size()) {
            const unsigned code =
                ((byte & 0x1Fu) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
            if (code >= 0xC0 && code < 0xC0 + std::size(kPlain)) {
                out += kPlain[code - 0xC0];
                i += 2;
                continue;
            }
        }
        out += '?';
        i += length;
    }
    return out;
}

}  // namespace openrac::i18n
