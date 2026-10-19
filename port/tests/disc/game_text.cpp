// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The game's texts as translators edit them (assets/disc/game_text.h): every byte the game's
// texts hold comes back the same from its UTF-8, a translator's letters become the ones the
// fonts draw, and the global and level blocks pair English with a language by id, once each.

#include "assets/disc/game_text.h"

#include <string>

#include "assets/disc/disc.h"
#include "tests/check.h"
#include "tests/disc/synthetic_disc.h"

using namespace openrac::assets;
using namespace openrac::assets::disc;

namespace {

// A text block (messages.h) of (id, text) pairs.
std::vector<u8> block(const std::vector<std::pair<s32, std::string>>& texts) {
    std::vector<u8> out(8 + 16 * texts.size());
    std::string strings;
    const std::size_t base = out.size();
    for (std::size_t k = 0; k < texts.size(); ++k) {
        openrac::test::put32(out, 8 + 16 * k, static_cast<u32>(base + strings.size()));
        openrac::test::put32(out, 8 + 16 * k + 4, static_cast<u32>(texts[k].first));
        openrac::test::put32(out, 8 + 16 * k + 8, 0xFFFFFFFFu);
        strings += texts[k].second;
        strings.push_back('\0');
    }
    openrac::test::put32(out, 0, static_cast<u32>(texts.size()));
    openrac::test::put32(out, 4, static_cast<u32>(base + strings.size()));
    out.insert(out.end(), strings.begin(), strings.end());
    return out;
}

// The synthetic disc with an all_text lump (English and French) at sector 1534 and level 1's PAL
// gameplay file (English and French, not compressed) at sector 1519.
void reads_a_disc() {
    std::vector<u8> img =
        openrac::test::rac1_disc("SCES_509.16", openrac::test::fake_elf("Ratchet & Clank"));
    const std::size_t toc = 1500 * openrac::test::kSs;
    auto put_at = [&](std::size_t at, const std::vector<u8>& bytes) {
        std::copy(bytes.begin(), bytes.end(), img.begin() + static_cast<std::ptrdiff_t>(at));
    };
    const std::vector<u8> en = block({{20000, "Options"}, {20001, "Caf\x8c"}});
    const std::vector<u8> fr = block({{20001, "Caf\x8c!"}, {20000, "Options FR"}});
    const std::size_t all = 1534 * openrac::test::kSs;
    std::fill(
        img.begin() + static_cast<std::ptrdiff_t>(all),
        img.begin() + static_cast<std::ptrdiff_t>(all + 0x800),
        u8{0}
    );
    openrac::test::put32(img, all + 0, 0x40);                                // English
    openrac::test::put32(img, all + 4 * 2, static_cast<u32>(0x40 + 0x100));  // French
    put_at(all + 0x40, en);
    put_at(all + 0x140, fr);
    openrac::test::put32(img, toc + 0x1528, 1534);
    openrac::test::put32(img, toc + 0x152c, 1);
    const std::size_t g = 1519 * openrac::test::kSs;
    std::fill(
        img.begin() + static_cast<std::ptrdiff_t>(g),
        img.begin() + static_cast<std::ptrdiff_t>(g + 0x800),
        u8{0}
    );
    openrac::test::put32(img, g + 0x10, 0x40);
    openrac::test::put32(img, g + 0x10 + 4 * 2, 0x200);
    put_at(g + 0x40, block({{1000, "Help"}, {20000, "Options"}}));
    put_at(g + 0x200, block({{1000, "Aide"}}));

    const Disc disc(IsoImage(image_in_memory(img)), rac1_layout());
    CHECK(has_game_texts(disc, Language::English));
    CHECK(has_game_texts(disc, Language::French));
    CHECK(!has_game_texts(disc, Language::German));
    const std::vector<GameText> t = read_game_texts(disc, Language::French);
    CHECK(t.size() == 3);  // level 1's "Options" is all_text's
    if (t.size() == 3) {
        CHECK(t[0].id == 20000 && t[0].text == "Options FR" && t[0].where == "all_text");
        CHECK(t[1].id == 20001 && game_text_to_utf8(t[1].text) == "Café!");
        CHECK(t[2].id == 1000 && t[2].text == "Aide" && t[2].where == "level 01");
    }
}

}  // namespace

int main() {
    reads_a_disc();

    // Names and letters.
    CHECK(
        game_text_to_utf8("Press \x10 to \x0cjump\x08\x01now")
        == "Press [cross] to [orange]jump[default]\nnow"
    );
    CHECK(game_text_to_utf8("caf\x8c [x]") == "café [[x]");
    CHECK(game_text_to_utf8("\x18\x19") == "[rstick][lstick]");  // the camera's, then walking's
    CHECK(game_text_to_utf8("\x8d\x9c\xa7\x7f\xb0") == "èóü[x7F][xB0]");

    // Every byte but NUL comes back from its UTF-8.
    std::string all;
    for (int c = 1; c < 256; ++c) {
        all += static_cast<char>(c);
    }
    CHECK(game_text_from_utf8(game_text_to_utf8(all)) == all);

    // A translator's text: Polish letters as the plain one and a mark the port draws.
    CHECK(
        game_text_from_utf8("Wciśnij [cross], żeby skoczyć.\nGotowe?")
        == "Wcis\x02nij \x10, z\x03"
           "eby skoczyc\x02.\x01Gotowe?"
    );
    CHECK(
        game_text_from_utf8("Łódź ąę")
        == "L\x05\x9c"
           "dz\x02 a\x04"
           "e\x04"
    );  // ó is the fonts' o acute
    CHECK(
        game_text_to_utf8(game_text_from_utf8("Zażółć gęślą jaźń ŻÓŁW")) == "Zażółć gęślą jaźń ŻÓŁW"
    );
    CHECK(game_text_to_utf8("\x02x\x02") == "[x02]x[x02]");  // a mark after no letter of its own
    CHECK(game_text_from_utf8("[[cross] [nope] [x4] [xZZ]") == "[cross] [nope] [x4] [xZZ]");
    CHECK(game_text_from_utf8("→ ß") == "? ss");

    // Pairing by id, a repeated text once.
    const std::vector<TextBlocks> blocks = {
        {"all_text", {{1, "Yes", -1}, {2, "No", -1}}, {{2, "Non", -1}, {1, "Oui", -1}}},
        {"level 01", {{1, "Yes", -1}, {1000, "Help", 4}}, {{1000, "Aide", 4}}},
        {"level 02", {{1000, "Other help", 4}}, {}},
    };
    const std::vector<GameText> t = pair_texts(blocks);
    CHECK(t.size() == 4);
    CHECK(t[0].id == 1 && t[0].english == "Yes" && t[0].text == "Oui" && t[0].where == "all_text");
    CHECK(t[1].id == 2 && t[1].text == "Non");
    CHECK(t[2].id == 1000 && t[2].text == "Aide" && t[2].where == "level 01");
    CHECK(t[3].id == 1000 && t[3].english == "Other help" && t[3].text.empty());

    // The keys of the built-in translations: zlib's CRC-32, as port/tools/game_text_po.py computes
    // it.
    CHECK(game_text_key(1000, "Café [cross]") == "1000/2ec6618e");
    CHECK(game_text_key(-3, "") == "-3/00000000");

    CHECK(game_languages().size() == 5);
    return openrac::test::result();
}
