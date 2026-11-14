/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 the OpenRAC contributors */

/*
 * Imported moby classes: a class of another level's program placed in this level by OpenRAC
 * Editor (editor/foreign_classes.py packs it). Its data is in this level's core like the level's
 * own classes; its update is its own level's code, which reads that level's addresses, so it runs
 * in its own program for the call: that level's data is swapped in over this one's, and its code
 * is read from a copy (openrac_guest_foreign_enter, openrac/foreign.h). Every level's data ends
 * below every level's program end, so a class of any level runs in any other.
 *
 * The pack lists the classes in <mods>/level_NN/classes.tsv, one "class<TAB>level" a line.
 * assign_moby_class_data (moby_class.c) binds such a class to a stand-in address, which the moby
 * update loop calls as any update: the stand-in enters the class's program, runs its update and
 * leaves. A level with no such file runs exactly as before.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openrac/foreign.h"
#include "openrac/guest.h"
#include "rac1.h"

#define FOREIGN_MAX 16
#define LEVELS 19
/* Stand-in code addresses: past the game's 32 MiB, where no program has code. */
#define FOREIGN_TAG_BASE 0x0FF00000u
/* The imported classes' data: the upper half of the port's own guest memory, which nothing else
 * uses (stack, strings and statics are its first 3 MiB). */
#define FOREIGN_BLOBS (GUEST_PORT + 0x00800000u)
#define FOREIGN_BLOBS_SIZE 0x00800000u
#define MOBY_CLASS_SLOTS 224u

typedef struct {
    int oclass;
    int level;    /* the program its code is in */
    gaddr update; /* in that program; 0 until bound */
    int ran;
} Foreign;

static Foreign s_foreign[FOREIGN_MAX];
static int s_foreign_count;
static int s_registered;
/* The imported class being registered (-1: none): only it binds to a stand-in, never a class of
 * the level's own that has no update. */
static int s_registering = -1;

static void foreign_run(int k, gaddr moby) {
    Foreign* f = &s_foreign[k];
    const int loaded = openrac_game_loaded_overlay();
    if (f->update == 0 || loaded < 0) {
        return;
    }
    if (openrac_guest_foreign_enter(f->level, loaded) != 0) {
        if (!f->ran) {
            f->ran = 1;
            char message[200];
            snprintf(
                message,
                sizeof(message),
                "imported moby class %d does not run: level %d's program is missing or bigger "
                "than this level's",
                f->oclass,
                f->level
            );
            openrac_game_warn(message);
        }
        return;
    }
    if (!f->ran) {
        f->ran = 1;
        char message[160];
        snprintf(
            message,
            sizeof(message),
            "imported moby class %d runs (level %d's update %08x)",
            f->oclass,
            f->level,
            f->update
        );
        openrac_game_warn(message);
    }
    openrac_game_restore_overlay(f->level);
    openrac_guest_call_a0(f->update, moby);
    openrac_guest_foreign_leave();
    openrac_game_restore_overlay(loaded);
}

#define FOREIGN_STAND_IN(k)                                                                        \
    static void foreign_update_##k(gaddr moby) {                                                   \
        foreign_run(k, moby);                                                                      \
    }
FOREIGN_STAND_IN(0)
FOREIGN_STAND_IN(1)
FOREIGN_STAND_IN(2)
FOREIGN_STAND_IN(3)
FOREIGN_STAND_IN(4)
FOREIGN_STAND_IN(5)
FOREIGN_STAND_IN(6)
FOREIGN_STAND_IN(7)
FOREIGN_STAND_IN(8)
FOREIGN_STAND_IN(9)
FOREIGN_STAND_IN(10)
FOREIGN_STAND_IN(11)
FOREIGN_STAND_IN(12)
FOREIGN_STAND_IN(13)
FOREIGN_STAND_IN(14)
FOREIGN_STAND_IN(15)

static void register_stand_ins(void) {
    static void (*const fns[FOREIGN_MAX])(gaddr) = {
        foreign_update_0,
        foreign_update_1,
        foreign_update_2,
        foreign_update_3,
        foreign_update_4,
        foreign_update_5,
        foreign_update_6,
        foreign_update_7,
        foreign_update_8,
        foreign_update_9,
        foreign_update_10,
        foreign_update_11,
        foreign_update_12,
        foreign_update_13,
        foreign_update_14,
        foreign_update_15,
    };
    openrac_fn_entry entries[FOREIGN_MAX];
    for (int k = 0; k < FOREIGN_MAX; ++k) {
        entries[k].address = FOREIGN_TAG_BASE + (gaddr)k * 16u;
        entries[k].fn = (openrac_host_fn)fns[k];
        entries[k].name = "foreign_class_update";
    }
    openrac_guest_register(OPENRAC_OVERLAY_EXE, entries, FOREIGN_MAX);
    s_registered = 1;
}

void rac1_foreign_classes_load(int level) {
    s_foreign_count = 0;
    openrac_guest_forget_foreign();
#ifndef RAC1_FOREIGN_CLASSES
    (void)level;
    return; /* registered by the NTSC loader's wrap only, for now */
#endif
    if (level < 0 || openrac_game_mods_dir == NULL || openrac_game_mods_dir[0] == '\0') {
        return;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/level_%02d/classes.tsv", openrac_game_mods_dir, level);
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char line[128];
    int skipped = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        int oclass = -1, from = -1;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        int known = 0;
        if (sscanf(line, "%d\t%d", &oclass, &from) == 2 && oclass >= 0 && oclass < 0x800
            && from >= 0 && from < LEVELS && from != level) {
            for (int k = 0; k < s_foreign_count; ++k) {
                known |= s_foreign[k].oclass == oclass;
            }
        } else {
            known = 1;
        }
        if (known || s_foreign_count == FOREIGN_MAX) {
            ++skipped; /* a bad or repeated line, or one too many */
            continue;
        }
        s_foreign[s_foreign_count++] = (Foreign){oclass, from, 0, 0};
    }
    fclose(file);
    if (skipped > 0) {
        char message[200];
        snprintf(
            message,
            sizeof(message),
            "level %d: %d line(s) of classes.tsv left out (a bad or repeated class, its own level, "
            "or more than %d classes)",
            level,
            skipped,
            FOREIGN_MAX
        );
        openrac_game_warn(message);
    }
    if (s_foreign_count > 0 && !s_registered) {
        register_stand_ins();
    }
    char message[160];
    snprintf(
        message,
        sizeof(message),
        "level %d: %d moby class(es) imported from other levels",
        level,
        s_foreign_count
    );
    openrac_game_warn(message);
}

#ifdef RAC1_FOREIGN_CLASSES
/* print_moby_class_sizes, which the level's loader calls once it has registered its own classes:
 * the imported ones register after them, each from its blob <mods>/level_NN/classes/<class>.bin
 * (its data as its own level's core has it, editor/foreign_classes.py). Drawn without the level's
 * textures (the port draws its level's model); its update binds through assign_moby_class_data as any class. */
void func_L00_00242120(unsigned int record, int count) {
    func_L00_00242120__game(record, count);
    const int level = openrac_game_loaded_overlay();
    if (s_foreign_count == 0 || level < 0) {
        return;
    }
    static gaddr slots;
    if (slots == 0) {
        slots = openrac_guest_static(16);
        memset(G(slots), 0, 16);
    }
    const gaddr slot_count = OPENRAC_DATA(RAC1_ADDR(0x00160000u));
    const gaddr class_by_slot = OPENRAC_DATA(RAC1_ADDR(0x001B3C80u));
    const gaddr register_class = OPENRAC_CODE(RAC1_ADDR(0x00203E78u));
    gaddr at = FOREIGN_BLOBS;
    int registered = 0;
    for (int k = 0; k < s_foreign_count; ++k) {
        const Foreign* f = &s_foreign[k];
        const uint32_t used = GREF(uint32_t, slot_count);
        if (used >= MOBY_CLASS_SLOTS) {
            openrac_game_warn("no moby class slot left for the imported classes");
            break;
        }
        int own = 0;
        for (uint32_t slot = 0; slot < used; ++slot) {
            own |= GREF(int16_t, class_by_slot + slot * 2u) == f->oclass;
        }
        if (own) {
            char message[160];
            snprintf(
                message,
                sizeof(message),
                "moby class %d is this level's own: it runs as the level's, not imported",
                f->oclass
            );
            openrac_game_warn(message);
            continue;
        }
        char path[1024];
        snprintf(
            path,
            sizeof(path),
            "%s/level_%02d/classes/%d.bin",
            openrac_game_mods_dir,
            level,
            f->oclass
        );
        FILE* file = fopen(path, "rb");
        if (file == NULL) {
            char message[1100];
            snprintf(message, sizeof(message), "imported moby class %d: no %s", f->oclass, path);
            openrac_game_warn(message);
            continue;
        }
        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size < 0x50 || (gaddr)size > FOREIGN_BLOBS + FOREIGN_BLOBS_SIZE - at) {
            fclose(file);
            char message[1100];
            snprintf(
                message,
                sizeof(message),
                "imported moby class %d: %s is %s",
                f->oclass,
                path,
                size < 0x50 ? "too small to be a class" : "too big for the room left"
            );
            openrac_game_warn(message);
            continue;
        }
        const size_t read = fread(G(at), 1, (size_t)size, file);
        fclose(file);
        if (read != (size_t)size) {
            openrac_game_warn("an imported moby class could not be read");
            continue;
        }
        s_registering = k;
        GFN(void (*)(gaddr, gaddr, gaddr, int), register_class)(at, 0, slots, f->oclass);
        s_registering = -1;
        at = (at + (gaddr)size + 15u) & ~15u;
        ++registered;
    }
    char message[160];
    snprintf(
        message,
        sizeof(message),
        "level %d: %d imported moby class(es) registered",
        level,
        registered
    );
    openrac_game_warn(message);
}
#endif

/* The stand-in update of an imported class (0: not one), its update and callbacks found in its own
 * level's class table (as moby_class.c reads the loaded level's). */
gaddr rac1_foreign_class_update(int oclass) {
    for (int k = 0; k < s_foreign_count; ++k) {
        Foreign* f = &s_foreign[k];
        if (f->oclass != oclass || k != s_registering) {
            continue;
        }
        const gaddr vtbl = RAC1_MOBY_VTBL(f->level);
        for (uint32_t i = 0; vtbl != 0 && i < 196u; ++i) {
            const int32_t key = (int32_t)openrac_guest_level_word(f->level, vtbl + i * 12u);
            if (key == -1) {
                break;
            }
            if (key == oclass) {
                f->update = openrac_guest_level_word(f->level, vtbl + i * 12u + 4u);
                break;
            }
        }
        if (f->update == 0) {
            char message[160];
            snprintf(
                message,
                sizeof(message),
                "moby class %d has no update in level %d's program",
                oclass,
                f->level
            );
            openrac_game_warn(message);
            return 0;
        }
        return FOREIGN_TAG_BASE + (gaddr)k * 16u;
    }
    return 0;
}
