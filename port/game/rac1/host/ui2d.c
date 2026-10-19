/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 the OpenRAC contributors */

/*
 * Ratchet & Clank: the 2D layer's draw routines, wrapped (libraries.tsv, port = wrap) so the
 * HUD and menus are drawn natively instead of from their GS packets (OPENRAC_UI_REPLAY replays the
 * packets, for comparing). Each routine's quad goes to the window as a record
 * (openrac_game_ui_quad) with the corners and texel coordinates its packet would have held, and the
 * display list gets a marker in its place, so the window draws it where it came.
 * OPENRAC_UI_LOG: each distinct caller and texture of DrawTexturedQuad, once, in the log.
 */
#include "rac1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The screen offset the game adds to every 2D position (D_0013E600 +0x10/+0x14), and the HUD
 * arena (D_0019A4E8): +0xC the depth its sprites are drawn at, +0x20/+0x24 its texture tables. */
#define SCREEN_OFX GREF(int, RAC1_ADDR(0x0013E610u))
#define SCREEN_OFY GREF(int, RAC1_ADDR(0x0013E614u))
#define HUD_ARENA OPENRAC_DATA(RAC1_ADDR(0x0019A4E8u))

static void log_caller(const char* what, unsigned long long tex, int w, int h) {
    static const char* seen[256];
    static int count = 0;
    /* Who called the routine that called this, as the host names it (guest.h: any host). */
    char caller[160];
    openrac_host_caller(1, caller, sizeof(caller));
    char key[256];
    snprintf(key, sizeof(key), "%s%s tbp %#x %dx%d", what, caller, (unsigned)tex & 0x3FFF, w, h);
    for (int i = 0; i < count; ++i) {
        if (strcmp(seen[i], key) == 0) {
            return;
        }
    }
    if (count < 256) {
        seen[count++] = strdup(key);
        fprintf(stderr, "ui2d: %s\n", key);
    }
}

/* The quad to the window and its marker into the display list (DMA cnt and VIF DIRECT of two
 * quadwords, a PACKED GIF tag of one A+D register, the A+D write to NOP carrying the index); false
 * when the 2D layer is replayed, and the caller sends the game's own packet. */
/* OPENRAC_UI_SKIP=NAME,...: those routines send their own packets (bisecting). */
static int skipped(const char* name) {
    const char* list = getenv("OPENRAC_UI_SKIP");
    return list != NULL && strstr(list, name) != NULL;
}

static int emit(openrac_game_ui_rect* q) {
    static int log = -1;
    if (log < 0) {
        log = getenv("OPENRAC_UI_LOG") != NULL;
    }
    if (log) {
        log_caller("emit ", q->tex0, (int)(q->x[3] - q->x[0]) / 16, (int)(q->y[3] - q->y[0]) / 16);
    }
    const int index = openrac_game_ui_quad(q);
    if (index < 0) {
        return 0;
    }
    const gaddr at = GREF(gaddr, OPENRAC_DATA(RAC1_ADDR(0x00161000u)));
    GREF(uint32_t, at + 0) = 0x10000002u;
    GREF(uint32_t, at + 4) = 0;
    GREF(uint32_t, at + 8) = 0;
    GREF(uint32_t, at + 12) = 0x50000002u;
    GREF(uint64_t, at + 16) = 1ull | (1ull << 15) | (1ull << 60);
    GREF(uint64_t, at + 24) = 0xEull;
    GREF(uint64_t, at + 32) = (0x4E55ull << 48) | (uint32_t)index;
    GREF(uint64_t, at + 40) = 0x7Full;
    GREF(gaddr, OPENRAC_DATA(RAC1_ADDR(0x00161000u))) = at + 48;
    return 1;
}

/* An axis-aligned quad, (x0, y0) to (x1, y1) on the screen (12.4, offset as the game sends them)
 * and (u0, v0) to (u1, v1) in the texture (12.4), as a strip. */
static void rect(openrac_game_ui_rect* q, int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1) {
    q->x[0] = (uint32_t)x0, q->y[0] = (uint32_t)y0, q->u[0] = (uint32_t)u0, q->v[0] = (uint32_t)v0;
    q->x[1] = (uint32_t)x1, q->y[1] = (uint32_t)y0, q->u[1] = (uint32_t)u1, q->v[1] = (uint32_t)v0;
    q->x[2] = (uint32_t)x0, q->y[2] = (uint32_t)y1, q->u[2] = (uint32_t)u0, q->v[2] = (uint32_t)v1;
    q->x[3] = (uint32_t)x1, q->y[3] = (uint32_t)y1, q->u[3] = (uint32_t)u1, q->v[3] = (uint32_t)v1;
}

/* A HUD texture's size: 1 << its entry's +6/+7 log2 sizes (the entry found through the arena's
 * +0x20 index table into its +0x24 texture table). */
static void hud_size(int tex, int* tw, int* th) {
    const gaddr arena = HUD_ARENA;
    const gaddr index = GREF(gaddr, arena + 0x20);
    const gaddr e = GREF(gaddr, arena + 0x24) + (gaddr)GREF(int16_t, index + (gaddr)tex * 4 + 2) * 8;
    *tw = 1 << GREF(uint8_t, e + 6);
    *th = 1 << GREF(uint8_t, e + 7);
}

static void hud_common(openrac_game_ui_rect* q, unsigned long long tex0, int alpha) {
    memset(q, 0, sizeof(*q));
    q->flags = 2; /* the HUD's routines send sprites (the turned and rotated ones strips) */
    q->z = (uint32_t)GREF(int, HUD_ARENA + 0xC);
    q->rgba = ((uint32_t)alpha << 24) | 0x7F7F7Fu;
    q->tex0 = tex0;
}

/* Inside FontPrint: the quads drawn are glyphs of text. */
static int g_glyphs = 0;

/* The glyphs a string printer drew, while it is followed for Polish letters: where each quad went. */
typedef struct {
    float x, y;
    unsigned long long rgba;
} drawn_glyph;

static drawn_glyph g_drawn[512];
static int g_drawn_count = -1; /* -1: not followed */

static void drawn(float x, float y, unsigned long long rgba) {
    if (g_drawn_count >= 0) {
        if (g_drawn_count < (int)(sizeof(g_drawn) / sizeof(g_drawn[0]))) {
            g_drawn[g_drawn_count].x = x, g_drawn[g_drawn_count].y = y, g_drawn[g_drawn_count].rgba = rgba;
        }
        ++g_drawn_count;
    }
}

/* A glyph cell of a font's table (assets/world/font.h): its texel, how far below the line it is
 * drawn, and the pen's advance (0: the byte draws nothing). */
typedef struct {
    int u, v, top, adv;
} glyph_cell;

static glyph_cell cell(gaddr glyphs, unsigned byte) {
    const gaddr at = glyphs + byte * 4;
    glyph_cell c = {GREF(uint8_t, at), GREF(uint8_t, at + 1), GREF(int8_t, at + 2), GREF(int8_t, at + 3)};
    return c;
}

/* The marks after a Polish letter (assets/disc/game_text.h, kMark*): the fonts have no such
 * letters, so the port draws the plain one, then the mark from the fonts' own glyphs: the acute of
 * e (or E) for ć ń ś ź, lower over the stem for ł; a full stop raised for ż; a comma turned round at
 * the foot for ą ę. OPENRAC_PL_MARKS="ACUTE_DX STROKE_DX STROKE_DY DOT_DY DOT_DY_CAPITAL OGONEK_DX
 * OGONEK_DY" moves them, in pixels of the unscaled font, to tune them by eye. */
enum { MARK_ACUTE = 2, MARK_DOT = 3, MARK_OGONEK = 4, MARK_STROKE = 5 };

static int is_mark(unsigned byte) {
    return byte >= MARK_ACUTE && byte <= MARK_STROKE;
}

static const int* mark_offsets(void) {
    static int offsets[7] = {0, -1, 5, -9, -12, -2, 1};
    static int read = 0;
    if (!read) {
        read = 1;
        const char* tune = getenv("OPENRAC_PL_MARKS");
        if (tune != NULL) {
            sscanf(tune, "%d %d %d %d %d %d %d", &offsets[0], &offsets[1], &offsets[2], &offsets[3],
                   &offsets[4], &offsets[5], &offsets[6]);
        }
    }
    return offsets;
}

/* A 16 x 16 cell of the font at (x, y), times scale: through DrawTexturedQuad for FontPrint (scale
 * 0), through the fractional quad for Font_DrawString. */
static void mark_quad(float x, float y, float scale, int u, int v, int uw, unsigned long long rgba, long long texture) {
    if (scale == 0.0f) {
        func_001F5800((int)x, (int)y, 16, 16, u, v, uw, 16, rgba, texture);
    } else {
        func_001F5BB8(x, y, scale * 16.0f, scale * 16.0f, u, v, uw, 16, rgba, texture);
    }
}

static void draw_mark(unsigned base, unsigned mark, const drawn_glyph* at, long long texture, gaddr glyphs,
                      float scale) {
    const int* o = mark_offsets();
    const float k = scale == 0.0f ? 1.0f : scale;
    const int capital = base >= 'A' && base <= 'Z';
    const glyph_cell letter = cell(glyphs, base);
    const float line = at->y - (float)letter.top * k;
    if (mark == MARK_ACUTE || mark == MARK_STROKE) {
        /* The acute FontPrint adds to é (0x8C) or É (0x88): table entry + 0x40, at the pen plus its
         * advance and top, moved by half the width the letters differ by. */
        const unsigned e = capital ? 'E' : 'e';
        const glyph_cell accent = cell(glyphs, (capital ? 0x88u : 0x8Cu) + 0x40u);
        int dx = accent.adv + (letter.adv - cell(glyphs, e).adv) / 2 + o[0];
        int dy = accent.top;
        if (mark == MARK_STROKE) {
            dx += o[1] - (capital ? letter.adv / 4 : 0);
            dy += o[2] + (capital ? 2 : 0);
        }
        mark_quad(at->x + (float)dx * k, line + (float)dy * k, scale, accent.u, accent.v, 16, at->rgba, texture);
    } else if (mark == MARK_DOT) {
        const glyph_cell stop = cell(glyphs, '.');
        mark_quad(at->x + (float)((letter.adv - stop.adv) / 2) * k,
                  line + (float)(stop.top + (capital ? o[4] : o[3])) * k, scale, stop.u, stop.v, 16, at->rgba,
                  texture);
    } else if (mark == MARK_OGONEK) {
        /* The comma mirrored: its tail then turns right, at the letter's right foot. */
        const glyph_cell comma = cell(glyphs, ',');
        mark_quad(at->x + (float)(letter.adv - 16 + o[5]) * k, line + (float)(comma.top + o[6]) * k, scale,
                  comma.u + 16, comma.v, -16, at->rgba, texture);
    }
}

/* A string with Polish letters, for a printer: its bytes without the marks, in game memory, and
 * the mark each kept byte has after it. */
enum { MARKED_CAPACITY = 4096 };
typedef struct {
    gaddr plain;
    int kept, kept_in_limit;
    int16_t mark_after[MARKED_CAPACITY];
} marked_text;

/* The marks of TEXT counted up to LIMIT (negative: the whole string). */
static int count_marks(gaddr text, int limit) {
    const uint8_t* src = (const uint8_t*)G(text);
    int marks = 0;
    for (int i = 0; src[i] != 0 && (limit < 0 || i < limit); ++i) {
        marks += is_mark(src[i]);
    }
    return marks;
}

static void strip_marks(marked_text* m, gaddr text, int limit) {
    if (m->plain == 0) {
        m->plain = openrac_guest_static(MARKED_CAPACITY);
    }
    const uint8_t* src = (const uint8_t*)G(text);
    uint8_t* dst = (uint8_t*)G(m->plain);
    m->kept = 0, m->kept_in_limit = 0;
    for (int i = 0; src[i] != 0 && m->kept < MARKED_CAPACITY - 1; ++i) {
        if (is_mark(src[i])) {
            if (m->kept > 0) {
                m->mark_after[m->kept - 1] = src[i];
            }
            continue;
        }
        m->mark_after[m->kept] = 0;
        dst[m->kept++] = src[i];
        if (limit < 0 || i < limit) {
            m->kept_in_limit = m->kept;
        }
    }
    dst[m->kept] = 0;
}

/* The marks drawn over the letters the printer drew (DRAWN quads, g_drawn): which quad each kept
 * byte drew, as both printers draw them: colour codes none, a byte of no advance none, an accented
 * letter its accent first, a space none. Nothing when the count disagrees. */
static void draw_marks(const marked_text* m, int limit, int drawn, long long texture, gaddr glyphs, float scale) {
    const uint8_t* dst = (const uint8_t*)G(m->plain);
    const int end = limit < 0 ? m->kept : m->kept_in_limit;
    static int at_byte[MARKED_CAPACITY];
    int quad = 0;
    for (int i = 0; i < end; ++i) {
        const unsigned b = dst[i];
        at_byte[i] = -1;
        if (b - 8u < 8u || cell(glyphs, b).adv == 0) {
            continue;
        }
        quad += b - 0x80u < 0x28u;
        if (b != 0x20) {
            at_byte[i] = quad++;
        }
    }
    if (quad != drawn || drawn > (int)(sizeof(g_drawn) / sizeof(g_drawn[0]))) {
        return;
    }
    for (int i = 0; i < end; ++i) {
        if (m->mark_after[i] != 0 && at_byte[i] >= 0) {
            draw_mark(dst[i], (unsigned)m->mark_after[i], &g_drawn[at_byte[i]], texture, glyphs, scale);
        }
    }
}

/* FontPrint: a string at (x, y) in colour, glyph by glyph through DrawTexturedQuad. A string with
 * Polish letters is drawn without their marks, then each mark over its letter, where the letter's
 * quad went. */
void func_001F6668(int x, int y, unsigned long long colour, gaddr text, int limit, long long texture,
                   gaddr glyphs) {
    ++g_glyphs;
    if (limit == 0 || count_marks(text, limit) == 0) {
        func_001F6668__game(x, y, colour, text, limit, texture, glyphs);
        --g_glyphs;
        return;
    }
    static marked_text m;
    strip_marks(&m, text, limit);
    g_drawn_count = 0;
    func_001F6668__game(x, y, colour, m.plain, limit < 0 ? limit : m.kept_in_limit, texture, glyphs);
    const int drawn = g_drawn_count;
    g_drawn_count = -1;
    draw_marks(&m, limit, drawn, texture, glyphs, 0.0f);
    --g_glyphs;
}

/* Font_DrawString: a string at (x, y) scaled, its glyphs through the fractional quad; the help
 * window's justified lines. Polish letters as FontPrint draws them. */
#if RAC1_DRAW_STRING_FLOATS_LAST
void func_001F69F0(unsigned long long colour, gaddr text, int limit, long long texture, gaddr glyphs, float x,
                   float y, float scale)
#define RAC1_DRAW_STRING(t, n) func_001F69F0__game(colour, t, n, texture, glyphs, x, y, scale)
#else
void func_001F69F0(unsigned long long colour, gaddr text, float x, int limit, long long texture, gaddr glyphs,
                   float y, float scale)
#define RAC1_DRAW_STRING(t, n) func_001F69F0__game(colour, t, x, n, texture, glyphs, y, scale)
#endif
{
    if (limit == 0 || count_marks(text, limit) == 0) {
        RAC1_DRAW_STRING(text, limit);
        return;
    }
    static marked_text m;
    strip_marks(&m, text, limit);
    g_drawn_count = 0;
    RAC1_DRAW_STRING(m.plain, limit < 0 ? limit : m.kept_in_limit);
    const int drawn = g_drawn_count;
    g_drawn_count = -1;
    draw_marks(&m, limit, drawn, texture, glyphs, scale);
}
#undef RAC1_DRAW_STRING

/* The textured quad at a fractional place: only followed, for Font_DrawString's marks. */
void func_001F5BB8(float x, float y, float w, float h, int u, int v, int uw, int vh, unsigned long long rgba,
                   unsigned long long tex) {
    drawn(x, y, rgba);
    func_001F5BB8__game(x, y, w, h, u, v, uw, vh, rgba, tex);
}

/* The title's "PRESS START", a picture per language the disc has (effect texture 4 + the
 * language, drawn 192 x 96 at (160, screen height - 80) by the title's draw, func_001EB7C0). In a
 * language the port translates into, its text "Press Start" (21026) is printed there instead, in
 * capitals with a shadow, centred on the picture's place. True when it was. */
static int press_start(int x, int y, int w, int h, unsigned long long rgba, unsigned long long tex) {
    if (x != 0xA0 || w != 0xC0 || h != 0x60) {
        return 0;
    }
    int n = GREF(int, OPENRAC_DATA(RAC1_ADDR(0x0015EE88u))) - 1;
    n = n < 0 ? 0 : n;
    if (tex != (unsigned long long)func_001F4868(n + 4)) {
        return 0;
    }
    /* By id: the title draws this before any text table is loaded. */
    const gaddr translated = openrac_game_text_by_id(21026);
    if (translated == 0) {
        return 0; /* the game's own words: its picture says them */
    }
    static gaddr text = 0;
    enum { SIZE = 128 };
    if (text == 0) {
        text = openrac_guest_static(SIZE);
    }
    const uint8_t* src = (const uint8_t*)G(translated);
    uint8_t* dst = (uint8_t*)G(text);
    const gaddr glyphs = OPENRAC_DATA(RAC1_ADDR(0x001DF3D0u));
    int i = 0, width = 0;
    for (; src[i] != 0 && i < SIZE - 1; ++i) {
        const unsigned c = src[i];
        dst[i] = (uint8_t)(c >= 'a' && c <= 'z' ? c - 0x20 : c);
        if (!is_mark(dst[i]) && dst[i] - 8u >= 8u) {
            width += cell(glyphs, dst[i]).adv;
        }
    }
    dst[i] = 0;
    const long long font = (long long)func_001F4868(1);
    const int tx = x + w / 2 - width / 2, ty = y + h / 2 - 8;
    const unsigned long long alpha = rgba & 0xFF000000ull;
    func_001F6668(tx + 1, ty + 1, alpha, text, -1, font, glyphs);
    func_001F6668(tx, ty, alpha | 0x80F0F0ull, text, -1, font, glyphs);
    return 1;
}

/* DrawTexturedQuad: a w x h screen rectangle at (x, y) textured from (u, v) .. (u + uw, v + vh) of
 * the texture tex (TEX0) in colour rgba. */
void func_001F5800(int x, int y, int w, int h, int u, int v, int uw, int vh,
                   RAC1_TYPE_func_001F5800_u64 rgba_, RAC1_TYPE_func_001F5800_u64 tex_) {
    const unsigned long long rgba = (unsigned long long)rgba_, tex = (unsigned long long)tex_;
    drawn((float)x, (float)y, rgba);
    if (g_drawn_count < 0 && press_start(x, y, w, h, rgba, tex)) {
        return;
    }
    if (skipped(RAC1_NAME(func_001F5800))) {
        func_001F5800__game(x, y, w, h, u, v, uw, vh, rgba, tex);
        return;
    }
    static int log = -1;
    if (log < 0) {
        log = getenv("OPENRAC_UI_LOG") != NULL;
    }
    if (log) {
        log_caller("quad ", tex, w, h);
    }
    openrac_game_ui_rect q;
    memset(&q, 0, sizeof(q));
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    rect(&q, x * 16 + ofx - 8, y * 16 + ofy - 8, (x + w) * 16 + ofx - 8, (y + h) * 16 + ofy - 8, u * 16, v * 16,
         (u + uw) * 16, (v + vh) * 16);
    q.z = 0xFFFFF0u;
    q.rgba = (uint32_t)rgba;
    q.tex0 = tex;
    q.flags = g_glyphs > 0 ? 1u : 0u;
    if (!emit(&q)) {
        func_001F5800__game(x, y, w, h, u, v, uw, vh, rgba, tex);
    }
}

/* HUD texture tex as a sprite at (x, y), w x h pixels, its whole texture, with alpha. */
void func_00200468(int tex, int x, int y, int w, int h, int alpha) {
    if (skipped(RAC1_NAME(func_00200468))) {
        func_00200468__game(tex, x, y, w, h, alpha);
        return;
    }
    int tw, th;
    openrac_game_ui_rect q;
    hud_size(tex, &tw, &th);
    hud_common(&q, func_00200248(tex), alpha);
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    rect(&q, x * 16 + ofx - 8, y * 16 + ofy - 8, (x + w) * 16 + ofx - 8, (y + h) * 16 + ofy - 8, 0, 0, tw * 16,
         th * 16);
    if (!emit(&q)) {
        func_00200468__game(tex, x, y, w, h, alpha);
    }
}

/* func_00200468 with the texture turned a quarter: corners (x, y+h), (x, y), (x+w, y+h), (x+w, y)
 * take texels (tw, 0), (tw, th), (0, 0), (0, th). */
void func_00200650(int tex, int x, int y, int w, int h, int alpha) {
    if (skipped(RAC1_NAME(func_00200650))) {
        func_00200650__game(tex, x, y, w, h, alpha);
        return;
    }
    int tw, th;
    openrac_game_ui_rect q;
    hud_size(tex, &tw, &th);
    hud_common(&q, func_00200248(tex), alpha);
    q.flags = 0;
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    const int xl = x * 16 + ofx - 8, xr = (x + w) * 16 + ofx - 8;
    const int yt = y * 16 + ofy - 8, yb = (y + h) * 16 + ofy - 8;
    const int xs[4] = {xl, xl, xr, xr}, ys[4] = {yb, yt, yb, yt};
    const int us[4] = {tw * 16, tw * 16, 0, 0}, vs[4] = {0, th * 16, 0, th * 16};
    for (int i = 0; i < 4; ++i) {
        q.x[i] = (uint32_t)xs[i], q.y[i] = (uint32_t)ys[i], q.u[i] = (uint32_t)us[i], q.v[i] = (uint32_t)vs[i];
    }
    if (!emit(&q)) {
        func_00200650__game(tex, x, y, w, h, alpha);
    }
}

/* func_00200468 with the position and size in 16ths of a pixel. */
void func_002008B8(int tex, int x, int y, int w, int h, int alpha) {
    if (skipped(RAC1_NAME(func_002008B8))) {
        func_002008B8__game(tex, x, y, w, h, alpha);
        return;
    }
    int tw, th;
    openrac_game_ui_rect q;
    hud_size(tex, &tw, &th);
    hud_common(&q, func_00200248(tex), alpha);
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    rect(&q, x + ofx - 8, y + ofy - 8, x + w + ofx - 8, y + h + ofy - 8, 0, 0, tw * 16, th * 16);
    if (!emit(&q)) {
        func_002008B8__game(tex, x, y, w, h, alpha);
    }
}

/* func_002008B8 with a texture window: texels from (u, v) on by the texture's size (12.4). */
void func_00200A90(int tex, int x, int y, int w, int h, int u, int v, int alpha) {
    if (skipped(RAC1_NAME(func_00200A90))) {
        func_00200A90__game(tex, x, y, w, h, u, v, alpha);
        return;
    }
    int tw, th;
    openrac_game_ui_rect q;
    hud_size(tex, &tw, &th);
    hud_common(&q, func_00200248(tex), alpha);
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    rect(&q, x + ofx - 8, y + ofy - 8, x + w + ofx - 8, y + h + ofy - 8, u, v, u + tw * 16, v + th * 16);
    if (!emit(&q)) {
        func_00200A90__game(tex, x, y, w, h, u, v, alpha);
    }
}

/* A sprite of the texture tex0 at (x, y), w x h in 16ths of a pixel, texels from (u, v) on by
 * (1 << (lw + 4), 1 << (lh + 4)), with alpha. */
void func_00200CA0(long long tex0, int x, int y, int lw, int lh, int w, int h, int u, int v, int alpha) {
    if (skipped(RAC1_NAME(func_00200CA0))) {
        func_00200CA0__game(tex0, x, y, lw, lh, w, h, u, v, alpha);
        return;
    }
    openrac_game_ui_rect q;
    hud_common(&q, (unsigned long long)tex0, alpha);
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    rect(&q, x + ofx - 8, y + ofy - 8, x + w + ofx - 8, y + h + ofy - 8, u, v, u + (1 << (lw + 4)),
         v + (1 << (lh + 4)));
    if (!emit(&q)) {
        func_00200CA0__game(tex0, x, y, lw, lh, w, h, u, v, alpha);
    }
}

/* A w x h sprite of tex at (x, y) (16ths of a pixel) turned by rot: corners pos +- a +- b with
 * a = h (sin, cos), b = w (-cos, sin), texels from (0, 0) to (u, v) texels. */
void func_00200E38(int u, int v, long long tex, float x, float y, float w, float h, float rot) {
    if (skipped(RAC1_NAME(func_00200E38))) {
        func_00200E38__game(u, v, tex, x, y, w, h, rot);
        return;
    }
    openrac_game_ui_rect q;
    hud_common(&q, (unsigned long long)tex, 0x80);
    q.flags = 0;
    q.rgba = 0x807F7F7Fu;
    const float s = func_001F9FA8(rot), c = func_001F9F90(rot);
    const float ax = h * s, ay = h * c, bx = -w * c, by = w * s;
    const float cx[4] = {x + ax - bx, x + ax + bx, x - ax - bx, x - ax + bx};
    const float cy[4] = {y + ay - by, y + ay + by, y - ay - by, y - ay + by};
    const int us[4] = {u * 16, u * 16, 0, 0}, vs[4] = {0, v * 16, 0, v * 16};
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    for (int i = 0; i < 4; ++i) {
        q.x[i] = (uint32_t)((int)cx[i] + ofx - 8);
        q.y[i] = (uint32_t)((int)cy[i] + ofy - 8);
        q.u[i] = (uint32_t)us[i];
        q.v[i] = (uint32_t)vs[i];
    }
    if (!emit(&q)) {
        func_00200E38__game(u, v, tex, x, y, w, h, rot);
    }
}

/* HUD texture tex as a sprite from (x0, y0) to (x1, y1) (16ths of a pixel), texels (u0, v0) to
 * (u1, v1) (12.4), with alpha. */
void func_00201190(int tex, int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, int alpha) {
    if (skipped(RAC1_NAME(func_00201190))) {
        func_00201190__game(tex, x0, y0, x1, y1, u0, v0, u1, v1, alpha);
        return;
    }
    openrac_game_ui_rect q;
    hud_common(&q, func_00200248(tex), alpha);
    const int ofx = SCREEN_OFX, ofy = SCREEN_OFY;
    rect(&q, x0 + ofx - 8, y0 + ofy - 8, x1 + ofx - 8, y1 + ofy - 8, u0, v0, u1, v1);
    if (!emit(&q)) {
        func_00201190__game(tex, x0, y0, x1, y1, u0, v0, u1, v1, alpha);
    }
}
