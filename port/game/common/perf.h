// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The port's performance overlay, its measuring side: where each tick's time goes, stage by stage
// through the frame's pipeline, and what the tick did (draws, texture conversions, disc reads,
// calls to functions without C). The launcher's "Performance overlay" setting turns it on
// (OPENRAC_PERF=1, or --perf); F3 then cycles the overlay off, compact and full
// (perf_overlay.h draws it). OPENRAC_PERF_CSV=FILE also writes each tick to FILE.
//
// A tick runs from one vertical blank the game waits for to the next (main.cpp,
// openrac_game_vsync): the game's own code, then the port's frame (frontend.cpp), the frames drawn
// between ticks and the wait for the next tick. Stages nest: time is charged to the innermost one
// open, so a tick's stages add up to its length. Inside Between only Swap and Pace are told apart:
// the rest of a frame drawn between ticks is that frame's cost.
//
// Nothing here needs GL or a window: the headless program keeps the same records, and the tests
// drive a Recorder with their own clock.

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace openrac::frontend::perf {

using Clock = std::chrono::steady_clock;

enum class Stage : std::uint8_t {
    Game,     // the game's code (the decompiled C), between vertical blanks
    Host,     // the port's work at a vertical blank not counted below
    Input,    // window events and the pads
    Scene,    // the tick's scene read from the game's memory: camera, mobys, 2D packets, a level load
    Render,   // the renderers' CPU time
    Present,  // the port's menus and the picture put into the window
    Swap,     // the swap: with vsync, waiting for a refresh
    Between,  // the frames drawn between ticks, their swaps aside
    Pace,     // sleeping until the next tick is due
    Count
};

inline constexpr std::size_t kStages = static_cast<std::size_t>(Stage::Count);

const char* stage_name(Stage stage);

// Running totals the port keeps elsewhere; a tick records how much each grew.
enum class Counter : std::uint8_t {
    Frames,       // frames rendered (the tick's and those between ticks)
    DrawCalls,
    Triangles,
    Uploads,      // image transfers the game sent
    Conversions,  // textures converted to RGBA8 (new pixels or palette)
    Sectors,      // disc sectors read
    Missing,      // calls to functions without C
    Count
};

inline constexpr std::size_t kCounters = static_cast<std::size_t>(Counter::Count);

// One renderer's time (the renderer's RendererTime, without depending on the renderer).
struct RendererTime {
    std::string name;
    double cpu_ms = 0.0;
    double gpu_ms = 0.0;
};

struct Record {
    std::uint64_t tick = 0;
    int level = 0;
    double budget_ms = 0.0;  // the game's tick length
    double total_ms = 0.0;
    std::array<double, kStages> stage_ms{};
    std::array<std::uint64_t, kCounters> counts{};
    std::vector<RendererTime> renderers;  // this tick's share
    double gpu_ms = 0.0;                  // the renderers' GPU time, added up

    double stage(Stage s) const { return stage_ms[static_cast<std::size_t>(s)]; }
    std::uint64_t count(Counter c) const { return counts[static_cast<std::size_t>(c)]; }
    // The time the port and the game spent working: not sleeping, not waiting for a refresh.
    double work_ms() const { return total_ms - stage(Stage::Pace) - stage(Stage::Swap); }
};

struct Hitch {
    std::uint64_t tick = 0;
    double ms = 0.0;
    Stage stage = Stage::Game;  // the one that took longest
    double stage_ms = 0.0;
    std::string note;  // what else happened: disc reads, texture conversions, a level change
};

struct Summary {
    std::size_t ticks = 0;
    double ticks_per_second = 0.0;
    double budget_ms = 0.0;
    double total_ms = 0.0;
    double work_ms = 0.0;
    double gpu_ms = 0.0;
    double late = 0.0;  // the share of ticks over budget
    std::array<double, kStages> avg_ms{};
    std::array<double, kStages> max_ms{};
    std::array<double, kCounters> per_tick{};  // averages
    std::vector<RendererTime> renderers;       // averages per tick
    std::string verdict;                       // the bottleneck, in words
    bool trouble = false;                      // over budget
};

// Keeps the records: the last kHistory ticks and the hitches among them.
class Recorder {
public:
    static constexpr std::size_t kHistory = 240;
    static constexpr std::size_t kHitches = 4;

    void begin(Stage stage, Clock::time_point now);
    void end(Clock::time_point now);

    // The running totals as they are now, and the renderers' (also running totals).
    void set_counter(Counter counter, std::uint64_t total);
    void set_renderers(std::span<const RendererTime> totals);

    // A vertical blank: closes the tick that ends now. The first call only starts the count.
    void tick(Clock::time_point now, double budget_ms, int level);

    // The records kept, at most kHistory; record(0) is the oldest.
    std::size_t size() const { return m_ring.size(); }
    const Record& record(std::size_t i) const;

    // The last hitches, oldest first.
    const std::vector<Hitch>& hitches() const { return m_hitches; }

    // Averages over the last `ticks` ticks, and what holds the game back.
    Summary summary(std::size_t ticks = 120) const;

    // Each tick as a line of `file` from now on (a header first).
    void write_csv(std::FILE* file);

private:
    struct Open {
        Stage stage;
        Stage charge;  // whom its time goes to
    };

    void charge(Clock::time_point now);
    void count_growth(Record& r);
    void note_hitch(const Record& r);
    void write_line(const Record& r);

    std::vector<Open> m_open;
    Clock::time_point m_mark{};
    bool m_started = false;
    std::uint64_t m_ticks = 0;
    int m_level = 0;
    Record m_current;
    std::array<std::uint64_t, kCounters> m_totals{};
    std::array<std::uint64_t, kCounters> m_last_totals{};
    std::vector<RendererTime> m_renderer_totals;
    std::vector<RendererTime> m_last_renderer_totals;
    std::vector<Record> m_ring;
    std::size_t m_next = 0;
    std::vector<Hitch> m_hitches;
    std::FILE* m_csv = nullptr;
};

// ---- The program's recorder ----

// On when the launcher asked for it (OPENRAC_PERF set and not "0") or after enable().
bool enabled();
void enable();

enum class Mode : std::uint8_t { Off, Compact, Full };

// What the overlay shows: Full when perf is enabled, until F3 cycles it.
Mode mode();
void cycle_mode();

Recorder& recorder();

// A stage, from here to the end of the scope (nothing when perf is off).
class Scope {
public:
    explicit Scope(Stage stage);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    bool m_on;
};

// The game waits for a vertical blank (budget: its tick length).
void tick(double budget_ms, int level);

}  // namespace openrac::frontend::perf
