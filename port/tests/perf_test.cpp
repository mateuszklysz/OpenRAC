// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The performance overlay's records (game/common/perf.h), on a clock of the test's own.

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "perf.h"

namespace {

using namespace openrac::frontend::perf;
using std::chrono::milliseconds;

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-6;
}

// A tick as main.cpp and frontend.cpp mark it: the game's code, then the port's frame at the
// vertical blank (input, scene, render, present with its swap), frames between ticks, the wait.
void one_tick(Recorder& r, Clock::time_point& t, int game_ms, int render_ms, int swap_ms, int between_ms,
              int pace_ms, double budget = 20.0, int level = 3) {
    t += milliseconds(game_ms);
    r.tick(t, budget, level);
    r.begin(Stage::Host, t);
    r.begin(Stage::Input, t);
    t += milliseconds(1);
    r.end(t);
    r.begin(Stage::Render, t);
    t += milliseconds(render_ms);
    r.end(t);
    r.begin(Stage::Present, t);
    r.begin(Stage::Swap, t);
    t += milliseconds(swap_ms);
    r.end(t);
    r.end(t);
    r.begin(Stage::Between, t);
    r.begin(Stage::Render, t);  // inside Between: charged to Between
    t += milliseconds(between_ms);
    r.end(t);
    r.begin(Stage::Swap, t);  // a swap stays a swap
    t += milliseconds(1);
    r.end(t);
    r.end(t);
    r.begin(Stage::Pace, t);
    t += milliseconds(pace_ms);
    r.end(t);
    r.end(t);
}

void stages_add_up() {
    Recorder r;
    Clock::time_point t{};
    one_tick(r, t, 0, 0, 0, 0, 0);  // the first vertical blank only starts the count
    one_tick(r, t, 6, 3, 2, 4, 3);
    t += milliseconds(5);
    r.tick(t, 20.0, 3);
    const std::vector<Record> h = r.history();
    CHECK(h.size() == 2);
    const Record& rec = h.back();
    CHECK(near(rec.stage(Stage::Game), 5.0));    // the game ran 5 ms before the second blank
    CHECK(near(rec.stage(Stage::Input), 1.0));
    CHECK(near(rec.stage(Stage::Render), 3.0));
    CHECK(near(rec.stage(Stage::Swap), 3.0));    // 2 + the swap between ticks
    CHECK(near(rec.stage(Stage::Between), 4.0));
    CHECK(near(rec.stage(Stage::Pace), 3.0));
    CHECK(near(rec.total_ms, 19.0));
    CHECK(near(rec.work_ms(), 13.0));
}

void counters_grow() {
    Recorder r;
    Clock::time_point t{};
    r.set_counter(Counter::Sectors, 100);
    r.set_renderers(std::vector<RendererTime>{{"tie", 10.0, 20.0}});
    r.tick(t, 20.0, 0);
    r.set_counter(Counter::Sectors, 164);
    r.set_renderers(std::vector<RendererTime>{{"tie", 11.5, 22.0}, {"hud", 0.5, 0.25}});
    t += milliseconds(10);
    r.tick(t, 20.0, 0);
    // A new renderer starts its totals again: what it has now is this tick's.
    r.set_counter(Counter::Sectors, 4);
    r.set_renderers(std::vector<RendererTime>{{"tie", 1.0, 2.0}});
    t += milliseconds(10);
    r.tick(t, 20.0, 0);
    const std::vector<Record> h = r.history();
    CHECK(h.size() == 2);
    CHECK(h[0].count(Counter::Sectors) == 64);
    CHECK(h[0].renderers.size() == 2);
    CHECK(near(h[0].renderers[0].cpu_ms, 1.5));
    CHECK(near(h[0].renderers[1].gpu_ms, 0.25));
    CHECK(near(h[0].gpu_ms, 2.25));
    CHECK(h[1].count(Counter::Sectors) == 4);
    CHECK(near(h[1].renderers[0].gpu_ms, 2.0));
}

void verdicts() {
    {
        Recorder r;
        Clock::time_point t{};
        for (int i = 0; i < 30; ++i) {
            one_tick(r, t, 5, 3, 2, 2, 7);
        }
        const Summary s = r.summary();
        CHECK(!s.trouble);
        CHECK(s.verdict.rfind("none", 0) == 0);
        CHECK(near(s.budget_ms, 20.0));
        CHECK(r.hitches().empty());
    }
    {
        // The game's code takes most of a tick that runs late.
        Recorder r;
        Clock::time_point t{};
        for (int i = 0; i < 30; ++i) {
            one_tick(r, t, 22, 3, 1, 0, 0);
        }
        const Summary s = r.summary();
        CHECK(s.trouble);
        CHECK(s.verdict.rfind("game code", 0) == 0);
        CHECK(s.ticks_per_second < 50.0);
    }
    {
        // The renderers' GPU time fills the tick (the CPU waits in the swap).
        Recorder r;
        Clock::time_point t{};
        double gpu = 0.0;
        for (int i = 0; i < 30; ++i) {
            gpu += 21.0;
            r.set_renderers(std::vector<RendererTime>{{"tfrag", 0.5 * i, gpu}});
            one_tick(r, t, 2, 1, 20, 0, 0);
        }
        const Summary s = r.summary();
        CHECK(s.trouble);
        CHECK(s.verdict.rfind("gpu", 0) == 0);
    }
}

void hitches() {
    Recorder r;
    Clock::time_point t{};
    one_tick(r, t, 5, 3, 2, 0, 10);
    r.set_counter(Counter::Sectors, 48);
    one_tick(r, t, 40, 3, 2, 0, 0);  // a long tick: the game reads the disc
    one_tick(r, t, 5, 3, 2, 0, 10);
    t += milliseconds(30);
    r.tick(t, 20.0, 5);  // a long tick that changes level
    const auto& h = r.hitches();
    CHECK(h.size() == 2);
    if (h.size() == 2) {
        CHECK(h[0].stage == Stage::Game);
        CHECK(h[0].note == "48 sectors read");
        CHECK(h[1].note == "level change");
    }
}

void history_keeps_the_last() {
    Recorder r;
    Clock::time_point t{};
    for (std::size_t i = 0; i < Recorder::kHistory + 11; ++i) {
        t += milliseconds(20);
        r.tick(t, 20.0, 0);
    }
    const std::vector<Record> h = r.history();
    CHECK(h.size() == Recorder::kHistory);
    CHECK(h.front().tick == 10);
    CHECK(h.back().tick == Recorder::kHistory + 9);
}

}  // namespace

int main() {
    stages_add_up();
    counters_grow();
    verdicts();
    hitches();
    history_keeps_the_last();
    return openrac::test::result();
}
