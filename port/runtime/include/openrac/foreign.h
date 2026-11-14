/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 the OpenRAC contributors */
/* Imported moby classes: another level's program run in the level loaded (runtime/guest.cpp). */
#ifndef OPENRAC_FOREIGN_H
#define OPENRAC_FOREIGN_H

#include "openrac/guest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A moby class of level `from`'s program run in level `loaded` (an imported class): its code is
 * that level's, on that level's addresses. enter swaps `from`'s program in over the loaded one,
 * carrying the engine's globals over (their pointers moved to its places), and makes it the program
 * calls go to; leave carries what the call changed back and puts the loaded program in again. The
 * foreign program keeps its own data between calls. enter returns 0, or -1 (no such program). */
int openrac_guest_foreign_enter(int from, int loaded);
void openrac_guest_foreign_leave(void);
/* A word of level `level`'s program as the disc has it (0 outside it). */
uint32_t openrac_guest_level_word(int level, gaddr address);
/* Drops the foreign programs' state (a new level load). */
void openrac_guest_forget_foreign(void);

#ifdef __cplusplus
}
#endif

#endif
