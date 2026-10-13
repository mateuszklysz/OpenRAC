// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The performance overlay (perf.h measures, this draws): in the window's top left corner, over
// the finished picture, at the window's own pixels. Compact: one line, the tick rate, the work
// against the tick's budget and what holds the game back. Full: each stage of the frame's
// pipeline (average and worst over the last 120 ticks, against the budget), each renderer's CPU
// and GPU time, what a tick does (frames, draws, texture uploads and conversions, disc reads,
// calls to functions without C), a graph of the last 240 ticks and the last hitches.

#pragma once

namespace openrac::frontend::perf_overlay {

// Over the window's framebuffer (0), before the swap; nothing when the overlay is off.
void draw(int window_width, int window_height);

// GL objects, before the context goes.
void release();

}  // namespace openrac::frontend::perf_overlay
