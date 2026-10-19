/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The port's rows in the game's options (port/game/common/options_menu.cpp): their texts. */
#include "rac1.h"

/* The text of an id (func_001FE540, matched C in the decompilation: the language's table, or the
 * empty string for an id it does not have). The port's own ids, which the table never has, are
 * answered with the port's strings (libraries.tsv, port = wrap), and the game's with the
 * translation the player chose, where its file has one (game/common/game_text.h). */
RAC1_TYPE_func_001FE540_result func_001FE540(int id) {
    const gaddr own = openrac_game_text(id);
    if (own != 0) {
        return (RAC1_TYPE_func_001FE540_result)own;
    }
    const RAC1_TYPE_func_001FE540_result text = func_001FE540__game(id);
    const gaddr translated = openrac_game_text_translated(id, (gaddr)text);
    return translated != 0 ? (RAC1_TYPE_func_001FE540_result)translated : text;
}

/* The text table the game reads now (D_0015F780: the level's, or the menus' while they show the
 * help texts) and its count (the help state's +0x2C): the help window takes its text from the
 * table itself, not through func_001FE540, so the table's own pointers are what it draws. Both are
 * the executable's words, a level's copy while one is loaded. */
void rac1_translate_texts(void) {
    const gaddr table = GREF(gaddr, OPENRAC_DATA(RAC1_ADDR(0x0015F780u)));
    const int count = GREF(int, OPENRAC_DATA(RAC1_ADDR(0x001997FCu)));
    openrac_game_texts_in_table(table, count);
}
