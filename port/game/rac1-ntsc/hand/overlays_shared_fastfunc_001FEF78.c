/* US check 2026-10-17: FUN_L00_001fefc8 and FUN_L00_001ff290 match the US assembly for behaviour (FUN_L00_001ff290 writes xyz only, w stays a). */
/* Carried from port/game/rac1-pal/hand/overlays_shared_fastfunc_001FEF78.c through the version map (../README.md): names and
 * addresses are this version's, raw addresses in comments are PAL's. Not yet checked in play.
 * Checked against the US assembly (confidence high): FUN_L00_00200228 now clamps the whole part to the int range, as the EE's cvt.w.s saturates. */
#ifndef MACRO_ADDR
#define MACRO_ADDR
#endif
#ifndef NOT_SDA
#define NOT_SDA
#endif
#ifndef SDATA
#define SDATA(symbol) __asm__(#symbol)
#endif
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 the OpenRAC contributors */

/*
 * Ratchet & Clank (PAL): the game's small vector and number routines, in C.
 *
 * In the retail program these are hand-written assembly for the EE's vector
 * unit (VU0 macro instructions) and FPU, so the decompilation keeps them as
 * assembly: no C compiles to them. The port needs them in C all the same, and
 * they are the most called code in the game. Each function here was written
 * from the instructions of the routine it is named after and does what those
 * do to memory and to the result register, operation for operation and in the
 * same order, so that rounding agrees; the build's check runs both and
 * compares (runtime/port/README.md).
 *
 * What they do not reproduce is what the routines leave in the vector unit's
 * own registers. A routine whose result depends on what an earlier one left
 * there (the cross product stores a fourth field it never computed) is not
 * here.
 *
 * A vector is four floats, x first. The assembly loads all 128 bits of its
 * operands before it stores, so every function reads all it needs first: the
 * result may be one of the operands.
 */
#include "types.h"

/* How many game ticks one sixtieth of a second is (1.0 at 60 Hz, 1.2 at 50 Hz), read through $gp. */
extern float D_0015ED60;

/* a = a + b in x, y and z; w stays a's. The first argument is not used. (vadd.xyz, stored back to a) */
void FUN_L00_001ff290(void *unused, float *a, float *b) {
    float x = a[0] + b[0];
    float y = a[1] + b[1];
    float z = a[2] + b[2];
    (void)unused;
    a[0] = x;
    a[1] = y;
    a[2] = z;
}

/* a less the whole multiples of b in it: the quotient's fraction times b. (vdiv, vftoi0, vitof0) */
float FUN_L00_00200260(float a, float b) {
    float quotient = 0.0f + a / b;
    float whole = (float)(int)quotient;
    return (quotient - whole) * b;
}

/* The level programs' copies of fast_vec_normalize (0x1FF500) and set_vector_length_xy
 * (0x1FF550) have no C here: their places belong to FUN_001f9bf8 and FUN_001f9c48
 * (game_fastfunc.c), which hostgen binds at every level's copy. */

/*
 * Counts a byte down by one: 1 if it was zero already (nothing is stored), 0 if it is still
 * above zero after the step, 2 if the step brought it to zero.
 */
int FUN_L00_001fefc8(unsigned char *counter) {
    int value = *counter;
    if (value == 0) {
        return 1;
    }
    value = value - 1;
    *counter = (unsigned char)value;
    return value > 0 ? 0 : 2;
}

/*
 * Splits x: the whole part, cut towards zero, goes to *whole and the rest is returned. The EE's
 * cvt.w.s always truncates and saturates out of range (to 0x7FFFFFFF or 0x80000000), so the
 * whole part is clamped to the int range before the cast; a plain (int) cast is undefined there.
 */
float FUN_L00_00200228(float *whole, float x) {
    float cut;
    if (x >= 2147483648.0f) {
        cut = 2147483648.0f;
    } else if (x < -2147483648.0f) {
        cut = -2147483648.0f;
    } else {
        cut = (float)(int)x;
    }
    *whole = cut;
    return x - cut;
}
