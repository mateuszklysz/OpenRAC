// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "options_menu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <vector>

#include "common/i18n.h"
#include "game_text.h"
#include "common/log.h"
#include "openrac/game_host.h"
#include "openrac/guest.h"
#include "pause_options.h"
#include "platform/window.h"
#include "renderer/framebuffer.h"
#include "renderer/texture_pool.h"
#include "settings.h"

namespace openrac::frontend::options_menu {

namespace {

// ---- The rows: one per setting, whatever game shows them ----

enum Row {
    kDisplayMode,
    kMonitor,
    kResolution,
    kAspect,
    kHud,
    kRenderScale,
    kMsaa,
    kAnisotropy,
    kVSync,
    kFrameRate,
    kFrameCap,
    kShowFps,
    kMuteUnfocused,
    kSkipCutscenes,
    kStrafe,
    kRows
};
using i18n::N_;
constexpr const char* kLabels[kRows] = {
    N_("Display mode"),   N_("Monitor"),    N_("Resolution"),    N_("Picture"),
    N_("HUD"),            N_("Render scale"), N_("Anti-aliasing"), N_("Texture filter"),
    N_("VSync"),          N_("Frame rate"), N_("Frame cap"),     N_("Frame counter"),
    N_("Mute in background"), N_("Skip cutscenes"), N_("Strafe (experimental)"),
};

// The value after `current` in a list (before it, for by = -1), around the ends; the first when
// `current` is not one of them.
template <typename T, std::size_t N>
T stepped(const T (&list)[N], T current, int by, std::size_t count = N) {
    count = std::min(count, N);
    for (std::size_t i = 0; i < count; ++i) {
        if (list[i] == current) {
            return list[(i + count + static_cast<std::size_t>(by + static_cast<int>(count))) % count];
        }
    }
    return list[0];
}

template <typename E>
E stepped_enum(E current, int by) {
    const int count = static_cast<int>(E::Count);
    return static_cast<E>((static_cast<int>(current) + by + count) % count);
}

// How many of a list's values the graphics card offers (those up to `most`).
template <std::size_t N>
std::size_t offered(const int (&list)[N], int most) {
    std::size_t count = 1;
    while (count < N && list[count] <= most) {
        ++count;
    }
    return count;
}

std::string value_of(int row) {
    const settings::Settings& s = settings::get();
    switch (row) {
        case kDisplayMode:
            return settings::name(s.display_mode);
        case kMonitor: {
            if (s.monitor == 0) {
                return i18n::tr("Window's own");
            }
            const auto all = platform::displays();
            if (static_cast<std::size_t>(s.monitor) > all.size()) {
                return std::vformat(i18n::tr("{} (not connected)"), std::make_format_args(s.monitor));
            }
            const platform::DisplayInfo& d = all[static_cast<std::size_t>(s.monitor - 1)];
            return std::format("{}: {}x{} {:.0f} Hz", s.monitor, d.width, d.height, d.refresh_rate);
        }
        case kResolution:
            // Only the fullscreen display mode changes the display's own.
            return s.fullscreen_width > 0 && s.fullscreen_height > 0
                       ? std::format("{}x{}", s.fullscreen_width, s.fullscreen_height)
                       : std::string(i18n::tr("Desktop"));
        case kAspect:
            return s.aspect == settings::Aspect::Custom
                       ? std::vformat(i18n::tr("Custom {}:{}"), std::make_format_args(s.aspect_custom_x, s.aspect_custom_y))
                       : std::string(settings::name(s.aspect));
        case kHud:
            return settings::name(s.hud);
        case kRenderScale:
            // As a factor: the game's font draws a pad button for the percent sign.
            return s.render_scale % 100 == 0 ? std::format("{}x", s.render_scale / 100)
                                             : std::format("{:.2f}x", static_cast<float>(s.render_scale) / 100.0f);
        case kMsaa:
            return s.msaa > 1 ? std::format("{}x", s.msaa) : std::string(i18n::tr("Off"));
        case kAnisotropy:
            return s.anisotropy > 1 ? std::vformat(i18n::tr("Anisotropic {}x"), std::make_format_args(s.anisotropy))
                                  : std::string(i18n::tr("Trilinear"));
        case kVSync:
            return settings::name(s.vsync);
        case kFrameRate:
            return settings::frame_rate_name(s);
        case kFrameCap:
            return s.frame_rate == settings::FrameRate::Capped ? std::to_string(s.frame_cap) : std::string(i18n::tr("Off"));
        case kShowFps:
            return s.show_fps ? i18n::tr("On") : i18n::tr("Off");
        case kMuteUnfocused:
            return s.mute_unfocused ? i18n::tr("On") : i18n::tr("Off");
        case kSkipCutscenes:
            return s.skip_cutscenes ? i18n::tr("Start") : i18n::tr("Off");
        case kStrafe:
            return settings::name(s.strafe);
        default:
            return {};
    }
}

// A row moved to its next value (by = 1: Cross or Right) or its last one (by = -1: Left).
void step_value(int row, int by) {
    settings::Settings& s = settings::get();
    switch (row) {
        case kDisplayMode:
            s.display_mode = stepped_enum(s.display_mode, by);
            break;
        case kMonitor: {
            // 0 (the display the window is on), then each display connected.
            const int count = static_cast<int>(platform::displays().size()) + 1;
            s.monitor = ((s.monitor < count ? s.monitor : 0) + by + count) % count;
            // Another display has other modes: back to its desktop's.
            s.fullscreen_width = s.fullscreen_height = 0;
            break;
        }
        case kResolution: {
            // The desktop's own mode, then the display's sizes, largest first.
            std::vector<platform::DisplaySize> sizes = platform::fullscreen_sizes(s.monitor);
            sizes.insert(sizes.begin(), platform::DisplaySize{0, 0});
            const platform::DisplaySize now{s.fullscreen_width, s.fullscreen_height};
            const auto at = std::find(sizes.begin(), sizes.end(), now);
            const std::size_t index = at == sizes.end() ? 0 : static_cast<std::size_t>(at - sizes.begin());
            const platform::DisplaySize next =
                sizes[(index + sizes.size() + static_cast<std::size_t>(by + static_cast<int>(sizes.size())))
                      % sizes.size()];
            s.fullscreen_width = next.width;
            s.fullscreen_height = next.height;
            break;
        }
        case kAspect:
            s.aspect = stepped_enum(s.aspect, by);
            break;
        case kHud:
            s.hud = stepped_enum(s.hud, by);
            break;
        case kRenderScale:
            s.render_scale = stepped(settings::kRenderScales, s.render_scale, by);
            break;
        case kMsaa:
            s.msaa = stepped(settings::kSampleCounts, s.msaa, by,
                             offered(settings::kSampleCounts, renderer::FrameBuffer::max_samples()));
            break;
        case kAnisotropy:
            s.anisotropy = stepped(settings::kAnisotropies, s.anisotropy, by,
                                   offered(settings::kAnisotropies,
                                           static_cast<int>(renderer::TexturePool::max_anisotropy())));
            break;
        case kVSync:
            s.vsync = stepped_enum(s.vsync, by);
            break;
        case kFrameRate: {
            // Original, Display, the caps, Unlimited: one list, the cap's number its place in it.
            constexpr int kCaps = static_cast<int>(std::size(settings::kFrameCaps));
            const int count = kCaps + 3;
            int at = 1;
            if (s.frame_rate == settings::FrameRate::Original) {
                at = 0;
            } else if (s.frame_rate == settings::FrameRate::Unlimited) {
                at = count - 1;
            } else if (s.frame_rate == settings::FrameRate::Capped) {
                // A cap of the player's own between two of the list's: from the one below it.
                at = 2;
                while (at - 2 + 1 < kCaps && settings::kFrameCaps[at - 2 + 1] <= s.frame_cap) {
                    ++at;
                }
                if (by < 0 && settings::kFrameCaps[at - 2] != s.frame_cap && s.frame_cap > settings::kFrameCaps[0]) {
                    ++at;  // going down from between two: to the one below, not past it
                }
            }
            at = (at + by + count) % count;
            if (at == 0) {
                s.frame_rate = settings::FrameRate::Original;
            } else if (at == 1) {
                s.frame_rate = settings::FrameRate::Display;
            } else if (at == count - 1) {
                s.frame_rate = settings::FrameRate::Unlimited;
            } else {
                s.frame_rate = settings::FrameRate::Capped;
                s.frame_cap = settings::kFrameCaps[at - 2];
            }
            break;
        }
        case kFrameCap:
            // A cap of the player's own, five frames a second at a time (Cross: one).
            if (s.frame_rate != settings::FrameRate::Capped) {
                s.frame_rate = settings::FrameRate::Capped;
            } else {
                s.frame_cap = std::clamp(s.frame_cap + by * 5, settings::kFrameCapMin, settings::kFrameCapMax);
            }
            break;
        case kShowFps:
            s.show_fps = !s.show_fps;
            break;
        case kMuteUnfocused:
            s.mute_unfocused = !s.mute_unfocused;
            break;
        case kSkipCutscenes:
            s.skip_cutscenes = !s.skip_cutscenes;
            break;
        case kStrafe:
            s.strafe = stepped_enum(s.strafe, by);
            break;
        default:
            break;
    }
}

/*
 * Ratchet & Clank: the front end's Video options page.
 *
 * A page is data in the boot program (debug_menu.cpp describes it: seq[14], the frame objects'
 * animations; parent; kind; focus; widget[14]). The Video page's list is the game's row list
 * (update func_00221E60, draw func_00222070): rows of 0x14 bytes {label text, a byte's address,
 * the text shown while the byte is set, the text shown while it is clear, flags}, ended by a row
 * without a label. Cross on a row flips its byte (a row with flag 1 is the game's own video mode,
 * which it handles itself).
 *
 * The port's rows take that list's place, with texts of the port's own (ids the game's table
 * does not have: the text lookup is wrapped, host/options.c) and a byte each, which update() reads
 * as "Cross was pressed here": the setting moves to its next value and the byte is cleared. Both
 * of a row's value texts are the same id, whose string is the setting's value now. Left and Right
 * are read from the pad as the game's menus read it, and move the row under the cursor back and on.
 *
 * The game's own rows are not among them. On the PAL disc it was "Video mode", 50 Hz or 60 Hz: the
 * game's choice of the television's field rate (it sets D_0016044C, which the title loop
 * func_001EBB48 makes the video mode, sceGsResetGraph's 3 or 2, and the next level's time base,
 * func_002157C0). The port's window shows the game at the display's own rate whatever the game's
 * tick is, so the choice means nothing here, and the port keeps the game in its 60 Hz mode
 * (update(); main.cpp's tick rate is 60 for every version).
 *
 * The addresses are the disc's own, from the game's program (openrac_front_end).
 *
 * The page's frame is too low for all the rows, so the list the game is given is a window of
 * kShown rows onto them, which moves with the cursor: at the window's last row with more below, the
 * rows move up one and the cursor stays on the row it reached, and the same at the top.
 *
 * The page's frame for one row is too low for kShown, so the page borrows the Sound options page's
 * frame animations for its list and its hint (seq 0xD2 and 0xD7: three sliders' worth) and that
 * page's hint widget ("Toggle", "Exit").
 */
namespace page {

constexpr gaddr kRowBytes = 0x14;
// The pad's buttons as the game's menus test them (the pad's two bytes the other way round).
constexpr std::uint32_t kPadLeft = 0x8000;
constexpr std::uint32_t kPadRight = 0x2000;

// The port's text ids: past the game's last (its table's ids are under 0x7000).
constexpr int kTextFirst = 0x7E00;
constexpr int kShown = 4;  // the rows the page's frame holds
constexpr gaddr kTextBytes = 256;  // room for letters drawn from a font file (seven bytes each)

constexpr gaddr kItemBytes = 12;  // an Options list item: label, action, page, sublabel, timer
constexpr int kMaxItems = 8;

struct Native {
    gaddr items = 0;   // the Options list's items with the Video page's added, when the game's lack it
    gaddr items_for = 0;  // the game's items those were made from (each program has its own copy)
    gaddr rows = 0;    // the kShown rows the game is given, and the end
    gaddr flags = 0;   // a byte per row shown
    gaddr labels = 0;  // kShown strings of kTextBytes
    gaddr values = 0;  // kShown strings of kTextBytes
    int top = 0;       // the first row shown
};
Native n;

// In the game's font, Polish letters drawn with their marks (game_text.h).
void put_text(gaddr at, const std::string& text) {
    char* p = static_cast<char*>(G(at));
    std::snprintf(p, kTextBytes, "%s", game_text::for_font(text).c_str());
}

void build() {
    if (n.rows != 0) {
        return;
    }
    n.rows = openrac_guest_static(kRowBytes * (kShown + 1));
    n.flags = openrac_guest_static(8);
    n.labels = openrac_guest_static(kTextBytes * kShown);
    n.values = openrac_guest_static(kTextBytes * kShown);
    std::memset(G(n.rows), 0, kRowBytes * (kShown + 1));
    std::memset(G(n.flags), 0, 8);
    for (int i = 0; i < kShown; ++i) {
        const gaddr row = n.rows + kRowBytes * static_cast<gaddr>(i);
        GREF(std::int32_t, row + 0x00) = kTextFirst + i;
        GREF(std::uint32_t, row + 0x04) = n.flags + static_cast<gaddr>(i);
        GREF(std::int32_t, row + 0x08) = kTextFirst + kShown + i;
        GREF(std::int32_t, row + 0x0C) = kTextFirst + kShown + i;
    }
}

// The Options list's items: the game's, and the Video page's item where the game's list has none
// (the US disc's list is Language and Sound; its Video page, the 50/60 Hz row's, is still in its
// data). Put in the list each time the game gives it its own items, as it does on entering it.
void list_video_page(const openrac_front_end& fe) {
    if (fe.video_item == 0 || GREF(std::uint32_t, fe.options_list + 0x34) != fe.options_items) {
        return;
    }
    if (n.items != 0 && n.items_for != fe.options_items) {
        n.items = 0;  // another program's list: the boot program's, or another level's
    }
    if (n.items == 0) {
        n.items_for = fe.options_items;
        int count = 0;
        bool has_video = false;
        while (count < kMaxItems && GREF(std::int16_t, fe.options_items + kItemBytes * static_cast<gaddr>(count)) != 0) {
            has_video |= GREF(std::uint32_t, fe.options_items + kItemBytes * static_cast<gaddr>(count) + 4) == fe.video_page;
            ++count;
        }
        if (has_video) {
            n.items = fe.options_items;
            return;
        }
        // Language, Video, Sound: the Video page's item second, where PAL's list has it.
        n.items = openrac_guest_static(kItemBytes * static_cast<gaddr>(count + 2));
        const gaddr own = fe.options_items;
        const int before = count > 0 ? 1 : 0;
        std::memcpy(G(n.items), G(own), kItemBytes * static_cast<gaddr>(before));
        std::memcpy(G(n.items + kItemBytes * static_cast<gaddr>(before)), G(fe.video_item), kItemBytes);
        std::memcpy(G(n.items + kItemBytes * static_cast<gaddr>(before + 1)), G(own + kItemBytes * static_cast<gaddr>(before)),
                    kItemBytes * static_cast<gaddr>(count - before));
    }
    GREF(std::uint32_t, fe.options_list + 0x34) = n.items;
}

bool update(const openrac_front_end& fe) {
    build();
    list_video_page(fe);
    if (GREF(std::uint32_t, fe.video_list + 0x34) == fe.video_rows) {
        GREF(std::uint32_t, fe.video_list + 0x34) = n.rows;
        GREF(std::int32_t, fe.video_list + 0x38) = 0;
        n.top = 0;
        GREF(std::int32_t, fe.video_page + 4 * 3) = fe.sound_list_seq;
        GREF(std::int32_t, fe.video_page + 4 * 4) = fe.sound_hint_seq;
        GREF(std::uint32_t, fe.video_page + 0x44 + 4 * 4) = fe.sound_hint;
    }
    // The cursor, as the game moved it this tick.
    std::int32_t& cursor = GREF(std::int32_t, fe.video_list + 0x38);
    cursor = std::clamp(cursor, 0, kShown - 1);
    bool changed = false;
    for (int i = 0; i < kShown; ++i) {
        std::uint8_t& pressed = GREF(std::uint8_t, n.flags + static_cast<gaddr>(i));
        if (pressed != 0) {
            pressed = 0;
            step_value(n.top + i, 1);
            changed = true;
        }
    }
    // Left and Right, while the page is open with its list in hand.
    const gaddr page = GREF(std::uint32_t, fe.page_menu + 4);
    if (page == fe.video_page && GREF(std::uint32_t, page + 0x40) == fe.video_list
        && GREF(std::int32_t, fe.video_list + 0x3C) == 0) {
        const std::uint32_t pad = GREF(std::uint32_t, fe.pad + 0x1C4);
        if ((pad & (kPadLeft | kPadRight)) != 0) {
            step_value(n.top + cursor, (pad & kPadRight) != 0 ? 1 : -1);
            changed = true;
        }
    }
    // The window follows the cursor: a row short of each end while there are more that way.
    if (cursor == kShown - 1 && n.top + kShown < kRows) {
        ++n.top;
        --cursor;
    } else if (cursor == 0 && n.top > 0) {
        --n.top;
        ++cursor;
    }
    for (int i = 0; i < kShown; ++i) {
        put_text(n.labels + kTextBytes * static_cast<gaddr>(i), i18n::tr(kLabels[n.top + i]));
        put_text(n.values + kTextBytes * static_cast<gaddr>(i), value_of(n.top + i));
    }
    if (changed) {
        settings::save();
    }
    return changed;
}

// The pause menu's Video item, its page's title and its hint (pause_page below).
constexpr int kTextVideo = kTextFirst + 2 * kShown;
constexpr int kPauseTexts = 3;
gaddr pause_texts = 0;

// The pause menu's Video item, its title and its hint, in the language now: put again each frame
// the Options page or the Video page is shown, since the Language row changes it.
void put_pause_texts() {
    if (pause_texts == 0) {
        pause_texts = openrac_guest_static(kTextBytes * kPauseTexts);
    }
    put_text(pause_texts, i18n::tr("Video"));
    put_text(pause_texts + kTextBytes, i18n::tr("Video Options"));
    put_text(pause_texts + 2 * kTextBytes, i18n::tr("Change the display, picture and frame rate"));
}
// The pause menu's Video page's rows: kPauseRows labels, then as many values.
constexpr int kPauseRows = 5;
constexpr int kPauseRowText = kTextVideo + kPauseTexts;
gaddr pause_rows_text = 0;

std::uint32_t text(int id) {
    if (id >= kTextVideo && id < kTextVideo + kPauseTexts && pause_texts != 0) {
        return pause_texts + kTextBytes * static_cast<gaddr>(id - kTextVideo);
    }
    if (id >= kPauseRowText && id < kPauseRowText + 2 * kPauseRows && pause_rows_text != 0) {
        return pause_rows_text + kTextBytes * static_cast<gaddr>(id - kPauseRowText);
    }
    if (n.rows == 0 || id < kTextFirst || id >= kTextFirst + 2 * kShown) {
        return 0;
    }
    const int i = id - kTextFirst;
    return i < kShown ? n.labels + kTextBytes * static_cast<gaddr>(i)
                      : n.values + kTextBytes * static_cast<gaddr>(i - kShown);
}

}  // namespace page

/* The game's Language page (title, Options): the disc's languages, each an item whose action (9)
 * puts its slot in the game's language global, the global's item marked (flag 0x800: the item whose
 * value is the global). The languages the port translates into and the disc lacks (game_text.h)
 * are added after them, in their own words, with an action the list ignores (0x7F): Cross on one is
 * taken here, the port's language becomes it and the game's English, which its translation is of.
 * While it is chosen the mark is moved to it (English's value goes to it, and English's item takes
 * the ignored action, Cross on it taken here too), and the global is kept at English. A disc's
 * language chosen in the list becomes the port's language as well. */
namespace language_page {

constexpr std::int16_t kOwnAction = 0x7F;
constexpr std::int16_t kSetLanguage = 9;
constexpr std::int32_t kEnglish = 0;  // the global's English (InitOnce)
constexpr std::int32_t kNone = -1;    // a value the global never has: no mark
constexpr std::uint32_t kPadCross = 0x40;
constexpr int kMaxExtra = 16;
constexpr int kTextFirst = page::kPauseRowText + 2 * page::kPauseRows;  // the added items' labels

// The port's languages as each language names them, as the game's list names its own ("Englisch"
// on the German page): a row a language, a column a language of the page (kPages). A language
// missing here is named in its own words.
constexpr const char* kPages[] = {"en", "fr", "de", "es", "it", "pl", "cs", "el",
                                  "hu", "ro", "hr", "tr", "ko", "zh-Hans", "zh-Hant"};
constexpr std::size_t kPageCount = std::size(kPages);
struct Names {
    const char* code;
    const char* in[kPageCount];
};
constexpr Names kNames[] = {
    {"pl", {"Polish", "Polonais", "Polnisch", "Polaco", "Polacco", "Polski", "Polština", "Πολωνικά", "Lengyel",
            "Poloneză", "Poljski", "Lehçe", "폴란드어", "波兰语", "波蘭語"}},
    {"cs", {"Czech", "Tchèque", "Tschechisch", "Checo", "Ceco", "Czeski", "Čeština", "Τσεχικά", "Cseh", "Cehă",
            "Češki", "Çekçe", "체코어", "捷克语", "捷克語"}},
    {"el", {"Greek", "Grec", "Griechisch", "Griego", "Greco", "Grecki", "Řečtina", "Ελληνικά", "Görög", "Greacă",
            "Grčki", "Yunanca", "그리스어", "希腊语", "希臘語"}},
    {"hu", {"Hungarian", "Hongrois", "Ungarisch", "Húngaro", "Ungherese", "Węgierski", "Maďarština", "Ουγγρικά",
            "Magyar", "Maghiară", "Mađarski", "Macarca", "헝가리어", "匈牙利语", "匈牙利語"}},
    {"ro", {"Romanian", "Roumain", "Rumänisch", "Rumano", "Rumeno", "Rumuński", "Rumunština", "Ρουμανικά", "Román",
            "Română", "Rumunjski", "Rumence", "루마니아어", "罗马尼亚语", "羅馬尼亞語"}},
    {"hr", {"Croatian", "Croate", "Kroatisch", "Croata", "Croato", "Chorwacki", "Chorvatština", "Κροατικά", "Horvát",
            "Croată", "Hrvatski", "Hırvatça", "크로아티아어", "克罗地亚语", "克羅埃西亞語"}},
    {"tr", {"Turkish", "Turc", "Türkisch", "Turco", "Turco", "Turecki", "Turečtina", "Τουρκικά", "Török", "Turcă",
            "Turski", "Türkçe", "터키어", "土耳其语", "土耳其語"}},
    {"ko", {"Korean", "Coréen", "Koreanisch", "Coreano", "Coreano", "Koreański", "Korejština", "Κορεατικά", "Koreai",
            "Coreeană", "Korejski", "Korece", "한국어", "韩语", "韓語"}},
    {"zh-Hans", {"Chinese (Simplified)", "Chinois simplifié", "Chinesisch (vereinf.)", "Chino simplificado",
                 "Cinese semplificato", "Chiński uproszczony", "Čínština (zjedn.)", "Κινεζικά (απλοπ.)",
                 "Kínai (egyszerűsített)", "Chineză simplificată", "Kineski (pojednost.)", "Basitleştirilmiş Çince",
                 "중국어(간체)", "简体中文", "簡體中文"}},
    {"zh-Hant", {"Chinese (Traditional)", "Chinois traditionnel", "Chinesisch (tradit.)", "Chino tradicional",
                 "Cinese tradizionale", "Chiński tradycyjny", "Čínština (trad.)", "Κινεζικά (παραδ.)",
                 "Kínai (hagyományos)", "Chineză tradițională", "Kineski (tradicionalni)", "Geleneksel Çince",
                 "중국어(번체)", "繁体中文", "繁體中文"}},
};

std::string name_in(const std::string& code, const std::string& in, const std::string& own) {
    for (const Names& n : kNames) {
        for (std::size_t k = 0; k < kPageCount && code == n.code; ++k) {
            if (in == kPages[k]) {
                return n.in[k];
            }
        }
    }
    return own;
}

// The names the page shows in language IN, for the letters a font file draws.
std::vector<std::string> names_in(const std::string& in) {
    std::vector<std::string> out;
    for (const Names& n : kNames) {
        out.push_back(name_in(n.code, in, n.code));
    }
    return out;
}

struct Made {
    gaddr game_items = 0;  // the list's own items
    int game_count = 0;
    gaddr items = 0;  // those, then the added ones, and the end
    gaddr labels = 0;
    std::vector<std::string> codes;  // the added languages
    std::vector<std::string> names;  // their names in their own words
    std::string built_for;           // the language they were marked for
    std::int32_t seen = kNone - 1;   // the global as last seen
};
Made m;

std::string current() {
    return game_text::code();
}

bool is_added(const std::string& code) {
    return std::find(m.codes.begin(), m.codes.end(), code) != m.codes.end();
}

void put_item(gaddr at, std::int16_t label, std::int16_t action, std::int32_t value) {
    GREF(std::int16_t, at) = label;
    GREF(std::int16_t, at + 2) = action;
    GREF(std::int32_t, at + 4) = value;
    GREF(std::int16_t, at + 8) = 0;
    GREF(std::int16_t, at + 10) = 0;
}

// The items, marked for the language now.
void build(const openrac_front_end& fe) {
    const gaddr own = GREF(std::uint32_t, fe.language_list + 0x34);
    if (m.items == 0) {
        if (own == 0) {
            return;
        }
        m.game_items = own;
        while (m.game_count < page::kMaxItems
               && GREF(std::int16_t, own + page::kItemBytes * static_cast<gaddr>(m.game_count)) != 0) {
            ++m.game_count;
        }
        for (const i18n::LanguageInfo& l : game_text::languages()) {
            if (!game_text::on_disc(l.code) && m.codes.size() < kMaxExtra) {
                m.codes.push_back(l.code);
                m.names.push_back(l.name);
            }
        }
        m.items = openrac_guest_static(page::kItemBytes * static_cast<gaddr>(m.game_count + kMaxExtra + 1));
        m.labels = openrac_guest_static(page::kTextBytes * kMaxExtra);
    }
    const std::string now = current();
    if (m.built_for == now && GREF(std::int16_t, m.items) != 0) {
        return;
    }
    m.built_for = now;
    const bool added = is_added(now);
    for (std::size_t k = 0; k < m.codes.size(); ++k) {
        page::put_text(m.labels + page::kTextBytes * static_cast<gaddr>(k), name_in(m.codes[k], now, m.names[k]));
    }
    std::memcpy(G(m.items), G(m.game_items), page::kItemBytes * static_cast<gaddr>(m.game_count));
    for (int i = 0; i < m.game_count; ++i) {
        const gaddr it = m.items + page::kItemBytes * static_cast<gaddr>(i);
        if (added && GREF(std::int16_t, it + 2) == kSetLanguage && GREF(std::int32_t, it + 4) == kEnglish) {
            GREF(std::int16_t, it + 2) = kOwnAction;
            GREF(std::int32_t, it + 4) = kNone;
        }
    }
    for (std::size_t k = 0; k < m.codes.size(); ++k) {
        put_item(m.items + page::kItemBytes * static_cast<gaddr>(m.game_count + static_cast<int>(k)),
                 static_cast<std::int16_t>(kTextFirst + static_cast<int>(k)), kOwnAction,
                 m.codes[k] == now ? kEnglish : kNone);
    }
    put_item(m.items + page::kItemBytes * static_cast<gaddr>(m.game_count + static_cast<int>(m.codes.size())), 0, 0, 0);
}

void choose(const std::string& code) {
    settings::get().language = code;
    settings::save();
    apply_language();
}

// FRONT: the boot program is loaded (its menus' data are where fe says; a level's program is
// elsewhere and over them).
void update(const openrac_front_end& fe, bool front) {
    if (fe.language == 0) {
        return;
    }
    std::int32_t& global = GREF(std::int32_t, fe.language);
    if (front && fe.language_list != 0) {
        build(fe);
        const gaddr shown = GREF(std::uint32_t, fe.language_list + 0x34);
        if (m.items != 0 && shown == m.game_items) {
            GREF(std::uint32_t, fe.language_list + 0x34) = m.items;
            // More languages than the frame holds: the list scrolls by whole rows (0x8000), as the
            // debug menu's list of planets does.
            GREF(std::uint32_t, fe.language_list + 0x30) |= 0x8000u;
        }
        // Cross on an item of the port's, on the Language page.
        const std::uint32_t pressed = GREF(std::uint32_t, fe.pad + 0x1C4);
        if (m.items != 0 && (pressed & kPadCross) != 0 && GREF(std::uint32_t, fe.page_menu + 4) == fe.language_page
            && GREF(std::uint32_t, fe.language_list + 0x34) == m.items) {
            const int at = GREF(std::int32_t, fe.language_list + 0x40);
            const int count = m.game_count + static_cast<int>(m.codes.size());
            const gaddr it = m.items + page::kItemBytes * static_cast<gaddr>(at);
            if (at >= 0 && at < count && GREF(std::int16_t, it + 2) == kOwnAction) {
                choose(at >= m.game_count ? m.codes[static_cast<std::size_t>(at - m.game_count)] : "en");
                global = kEnglish;
                m.seen = global;  // the port's change, not the game's
                if (fe.menu_sound != nullptr) {
                    fe.menu_sound(0x11, GREF(std::uint32_t, fe.language_list + 0x14));
                }
            }
        }
    }
    // The global changed by the game (its list, a save): its language becomes the port's. While
    // one of the port's languages is chosen, it stays English.
    if (m.seen != kNone - 1 && global != m.seen) {
        const std::string code = game_text::disc_language(global);
        choose(code.empty() ? "en" : code);
    } else if (is_added(current())) {
        global = kEnglish;
    }
    m.seen = global;
}

std::uint32_t text(int id) {
    const int k = id - kTextFirst;
    return k >= 0 && k < static_cast<int>(m.codes.size()) ? m.labels + page::kTextBytes * static_cast<gaddr>(k) : 0;
}

}  // namespace language_page

/* In a level, the pause menu's Options page (HelpDesk, Save, Load, Sound, Camera, Subtitles, Quit
 * Game) is level code with no Video page, so one is made: a copy of the Camera page (with the Sound
 * page's frames, which are taller), whose list
 * widget shows rows of a label and one of up to four value names chosen by an int (each row 0x18:
 * label, the int's address, the names, 0). The port's rows are given to it kShownHere at a time,
 * each with its value as all three names and its int at 1: Right makes it 2 and Left 0 (whether the
 * widget wraps or stops), which steps the setting, and the int goes back to 1. "Video" goes into a
 * copy of the Options list's items after Sound, put in the list each time the game gives it its own,
 * as on the title's Options page, and the hint box's texts and the picture box's entries get one for
 * Video too (both are read by the list's index). Made again for each level program (each has its own
 * menu data). */
namespace pause_page {

constexpr std::int16_t kSoundText = 20258;   // the Options items' "Sound" and "Camera"
constexpr std::int16_t kCameraText = 20259;
constexpr gaddr kPageBytes = 0x88;           // MenuPage
constexpr gaddr kScreenBytes = 0x80;         // what of the Camera list widget is copied
constexpr gaddr kChoiceBytes = 0x18;
constexpr gaddr kTitleBytes = 0x58;          // the Camera page's title box: +0x34 its text
constexpr int kShownHere = page::kPauseRows;  // the rows shown at once

struct Made {
    gaddr items_for = 0;  // the game's Options items these were made from
    gaddr items = 0, page = 0, list = 0, rows = 0, values = 0;
    gaddr hints_for = 0, hints = 0;  // the Options hint box's texts by item, with Video's
    gaddr pictures_for = 0, pictures = 0;  // the Options picture stream's entries by item, with Video's
    int top = 0;
};
Made m;

int count_items(gaddr items, gaddr* camera, gaddr* sound) {
    int count = 0;
    while (count < 16 && GREF(std::int16_t, items + page::kItemBytes * static_cast<gaddr>(count)) != 0) {
        const gaddr it = items + page::kItemBytes * static_cast<gaddr>(count);
        if (GREF(std::int16_t, it) == kCameraText) {
            *camera = it;
        }
        if (GREF(std::int16_t, it) == kSoundText) {
            *sound = it;
        }
        ++count;
    }
    return count;
}

// The hint box's texts: a screen of the Options page whose +0x34 is a list of `count` text ids
// ended by 0, one per item. 0 when there is none.
gaddr hint_screen(gaddr options_page, int count) {
    for (gaddr at = 0x44; at < 0x7C; at += 4) {
        const gaddr screen = GREF(std::uint32_t, options_page + at);
        if (screen == 0) {
            continue;
        }
        const gaddr list = GREF(std::uint32_t, screen + 0x34);
        if (list < 0x100000u || list >= 0x2000000u || (list & 3) != 0) {
            continue;
        }
        int n = 0;
        while (n <= count && GREF(std::int32_t, list + 4 * static_cast<gaddr>(n)) > 0
               && GREF(std::int32_t, list + 4 * static_cast<gaddr>(n)) < 0x7000) {
            ++n;
        }
        if (n == count && GREF(std::int32_t, list + 4 * static_cast<gaddr>(n)) == 0) {
            return screen;
        }
    }
    return 0;
}

void make(gaddr items) {
    gaddr camera = 0, sound = 0;
    const int count = count_items(items, &camera, &sound);
    const gaddr camera_page = GREF(std::uint32_t, camera + 4);
    const gaddr camera_list = GREF(std::uint32_t, camera_page + 0x40);
    m = Made{};
    m.items_for = items;
    m.page = openrac_guest_static(kPageBytes);
    m.list = openrac_guest_static(kScreenBytes);
    m.rows = openrac_guest_static(kChoiceBytes * (kShownHere + 1));
    m.values = openrac_guest_static(4 * kShownHere);
    std::memcpy(G(m.page), G(camera_page), kPageBytes);
    std::memcpy(G(m.list), G(camera_list), kScreenBytes);
    if (sound != 0) {
        // The Sound page's frames for the list and the boxes around it: tall enough for five rows
        // (the Camera page's hold three). The title's stays the Camera page's.
        const gaddr sound_page = GREF(std::uint32_t, sound + 4);
        std::memcpy(G(m.page + 4), G(sound_page + 4), 4 * 4);
    }
    // The title box, the screen after the page in the level's data, with the port's title.
    const gaddr camera_title = camera_page + kPageBytes;
    const gaddr title = openrac_guest_static(kTitleBytes);
    std::memcpy(G(title), G(camera_title), kTitleBytes);
    GREF(std::int32_t, title + 0x34) = page::kTextVideo + 1;
    for (gaddr at = 0x40; at < 0x7C; at += 4) {
        if (GREF(std::uint32_t, m.page + at) == camera_title) {
            GREF(std::uint32_t, m.page + at) = title;
        }
    }
    if (page::pause_rows_text == 0) {
        page::pause_rows_text = openrac_guest_static(page::kTextBytes * 2 * kShownHere);
    }
    page::put_pause_texts();
    for (gaddr at = 0x40; at < 0x7C; at += 4) {  // the focus and the screens: our list for Camera's
        if (GREF(std::uint32_t, m.page + at) == camera_list) {
            GREF(std::uint32_t, m.page + at) = m.list;
        }
    }
    std::memset(G(m.rows), 0, kChoiceBytes * (kShownHere + 1));
    for (int i = 0; i < kShownHere; ++i) {
        const gaddr row = m.rows + kChoiceBytes * static_cast<gaddr>(i);
        GREF(std::int32_t, row + 0x00) = page::kPauseRowText + i;
        GREF(std::uint32_t, row + 0x04) = m.values + 4 * static_cast<gaddr>(i);
        for (gaddr k = 0; k < 3; ++k) {
            GREF(std::int32_t, row + 0x08 + 4 * k) = page::kPauseRowText + kShownHere + i;
        }
        GREF(std::int32_t, m.values + 4 * static_cast<gaddr>(i)) = 1;
    }
    GREF(std::uint32_t, m.list + 0x34) = m.rows;
    GREF(std::int32_t, m.list + 0x38) = 0;
    // The items: the game's, and Video after Sound (before Camera when there is no Sound).
    const gaddr after = sound != 0 ? sound : camera - page::kItemBytes;
    const int before = static_cast<int>((after - items) / page::kItemBytes) + 1;
    m.items = openrac_guest_static(page::kItemBytes * static_cast<gaddr>(count + 2));
    std::memcpy(G(m.items), G(items), page::kItemBytes * static_cast<gaddr>(before));
    const gaddr video = m.items + page::kItemBytes * static_cast<gaddr>(before);
    std::memcpy(G(video), G(camera), page::kItemBytes);
    GREF(std::int16_t, video) = page::kTextVideo;
    GREF(std::uint32_t, video + 4) = m.page;
    std::memcpy(G(video + page::kItemBytes), G(items + page::kItemBytes * static_cast<gaddr>(before)),
                page::kItemBytes * static_cast<gaddr>(count - before + 1));  // with the end item
    m.top = 0;
    (void)count;
}

// The hint box's list with Video's hint at Video's place, in place of the game's while ours is shown.
void hints(gaddr options_page, gaddr game_items) {
    gaddr camera = 0, sound = 0;
    const int count = count_items(game_items, &camera, &sound);
    const gaddr screen = hint_screen(options_page, count);
    if (screen == 0) {
        return;
    }
    const gaddr list = GREF(std::uint32_t, screen + 0x34);
    if (m.hints_for != list) {
        const gaddr after = sound != 0 ? sound : camera - page::kItemBytes;
        const int before = static_cast<int>((after - game_items) / page::kItemBytes) + 1;
        m.hints_for = list;
        m.hints = openrac_guest_static(4 * static_cast<gaddr>(count + 2));
        std::memcpy(G(m.hints), G(list), 4 * static_cast<gaddr>(before));
        GREF(std::int32_t, m.hints + 4 * static_cast<gaddr>(before)) = page::kTextVideo + 2;
        std::memcpy(G(m.hints + 4 * static_cast<gaddr>(before + 1)), G(list + 4 * static_cast<gaddr>(before)),
                    4 * static_cast<gaddr>(count - before + 1));
    }
    GREF(std::uint32_t, screen + 0x34) = m.hints;
}

// The picture box's entries with an empty one at Video's place, in place of the game's while ours
// is shown: the stream reads entries[selected] of the list (pause_options.h, options_stream).
void pictures(gaddr options_page, gaddr list, gaddr game_items) {
    gaddr camera = 0, sound = 0;
    const int count = count_items(game_items, &camera, &sound);
    const gaddr screen = pause_options::options_stream([](gaddr at) { return GREF(std::uint32_t, at); }, options_page,
                                                       list, count);
    if (screen == 0) {
        return;
    }
    const gaddr entries = GREF(std::uint32_t, screen + pause_options::kStreamEntries);
    if (m.pictures_for != entries) {
        const gaddr after = sound != 0 ? sound : camera - page::kItemBytes;
        const int before = static_cast<int>((after - game_items) / page::kItemBytes) + 1;
        m.pictures_for = entries;
        m.pictures = openrac_guest_static(pause_options::kStreamEntryBytes * static_cast<gaddr>(count + 1));
        pause_options::entries_with_video(static_cast<const std::uint32_t*>(G(entries)), count, before,
                                          static_cast<std::uint32_t*>(G(m.pictures)));
    }
    GREF(std::uint32_t, screen + pause_options::kStreamEntries) = m.pictures;
}

bool update(gaddr page_menu, gaddr pad) {
    const gaddr shown = GREF(std::uint32_t, page_menu + 4);
    if (shown == 0) {
        return false;
    }
    if (page::pause_texts != 0) {
        page::put_pause_texts();
    }
    if (shown != m.page || m.page == 0) {
        // The Options page (pause_options.h): its list's items are the game's with Camera in them.
        // Another page's focus may be no list at all: the item grids keep floats where a list keeps
        // its items.
        const gaddr list = pause_options::options_list([](gaddr at) { return GREF(std::uint32_t, at); }, shown);
        if (list == 0) {
            return false;
        }
        const gaddr items = GREF(std::uint32_t, list + 0x34);
        gaddr camera = 0, sound = 0;
        if (items != 0 && items != m.items && count_items(items, &camera, &sound) > 0 && camera != 0) {
            if (m.items_for != items) {
                make(items);
                m.hints_for = 0;
            }
            hints(shown, items);
            pictures(shown, list, items);
            GREF(std::uint32_t, list + 0x34) = m.items;
        }
        return false;
    }
    // The port's Video page.
    page::build();
    bool changed = false;
    for (int i = 0; i < kShownHere; ++i) {
        std::int32_t& v = GREF(std::int32_t, m.values + 4 * static_cast<gaddr>(i));
        if (v != 1) {
            step_value(m.top + i, v > 1 ? 1 : -1);
            v = 1;
            changed = true;
        }
    }
    std::int32_t& cursor = GREF(std::int32_t, m.list + 0x38);
    cursor = std::clamp(cursor, 0, kShownHere - 1);
    // Left and Right step the setting too (the widget itself steps on Cross).
    const std::uint32_t pressed = GREF(std::uint32_t, pad + 0x1C4);
    if ((pressed & (page::kPadLeft | page::kPadRight)) != 0) {
        step_value(m.top + cursor, (pressed & page::kPadRight) != 0 ? 1 : -1);
        changed = true;
    }
    if (cursor == kShownHere - 1 && m.top + kShownHere < kRows) {
        ++m.top;
        --cursor;
    } else if (cursor == 0 && m.top > 0) {
        --m.top;
        ++cursor;
    }
    for (int i = 0; i < kShownHere; ++i) {
        page::put_text(page::pause_rows_text + page::kTextBytes * static_cast<gaddr>(i), i18n::tr(kLabels[m.top + i]));
        page::put_text(page::pause_rows_text + page::kTextBytes * static_cast<gaddr>(kShownHere + i), value_of(m.top + i));
    }
    if (changed) {
        settings::save();
    }
    return changed;
}

}  // namespace pause_page

// The game's front end, when its program describes one with a Video options page.
const openrac_front_end* front_end() {
    const openrac_front_end* fe = openrac_game_front_end;
    return fe != nullptr && fe->video_list != 0 ? fe : nullptr;
}

}  // namespace

std::vector<i18n::LanguageInfo> all_languages() {
    std::vector<i18n::LanguageInfo> all = i18n::languages();
    for (const i18n::LanguageInfo& l : game_text::languages()) {
        if (std::none_of(all.begin(), all.end(), [&](const auto& a) { return a.code == l.code; })) {
            all.push_back(l);
        }
    }
    return all;
}

void apply_language() {
    const std::string& chosen = settings::get().language;
    const std::string code =
        chosen == "auto" ? i18n::from_locales(platform::preferred_languages(), all_languages()) : chosen;
    i18n::set_language(code);
    game_text::use(code, language_page::names_in(code));
}

bool update(int loaded_overlay) {
    const openrac_front_end* fe = front_end();
    if (fe == nullptr) {
        return false;
    }
    if (fe->video_mode != 0) {
        // The port runs every version at 60 Hz: the game is kept in its own 60 Hz mode (the byte
        // 0; nonzero is PAL's 50 Hz), which its title loop makes the video mode and the next
        // level's time base, as choosing 60 Hz in its menu would.
        GREF(std::uint8_t, fe->video_mode) = 0;
    }
    language_page::update(*fe, loaded_overlay == OPENRAC_OVERLAY_EXE);
    if (loaded_overlay != OPENRAC_OVERLAY_EXE) {
        // In a level: the pause menu's own Options page, on the level program's menu data.
        return loaded_overlay >= 0 && pause_page::update(OPENRAC_DATA(fe->page_menu), fe->pad);
    }
    return page::update(*fe);
}

std::uint32_t text(int id) {
    if (front_end() == nullptr) {
        return 0;
    }
    const std::uint32_t own = language_page::text(id);
    return own != 0 ? own : page::text(id);
}

std::uint32_t options_items() {
    const openrac_front_end* fe = front_end();
    return fe != nullptr && page::n.items != fe->options_items ? page::n.items : 0;
}

}  // namespace openrac::frontend::options_menu
