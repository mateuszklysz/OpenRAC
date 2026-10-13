// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "perf.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>

namespace openrac::frontend::perf {

const char* stage_name(Stage stage) {
    switch (stage) {
        case Stage::Game:
            return "game code";
        case Stage::Host:
            return "port, other";
        case Stage::Input:
            return "input";
        case Stage::Scene:
            return "scene from memory";
        case Stage::Render:
            return "renderers (cpu)";
        case Stage::Present:
            return "present";
        case Stage::Swap:
            return "swap / vsync";
        case Stage::Between:
            return "between ticks";
        case Stage::Pace:
            return "wait for tick";
        case Stage::Count:
            break;
    }
    return "?";
}

namespace {

constexpr const char* kCounterNames[kCounters] = {"frames",  "draw_calls", "triangles", "uploads",
                                                  "conversions", "sectors", "missing"};

// How much a running total grew; a total that went down was started again (a new renderer).
std::uint64_t grown(std::uint64_t now, std::uint64_t before) {
    return now >= before ? now - before : now;
}

double grown(double now, double before) {
    return now >= before ? now - before : now;
}

}  // namespace

void Recorder::charge(Clock::time_point now) {
    if (m_started) {
        const Stage to = m_open.empty() ? Stage::Game : m_open.back().charge;
        m_current.stage_ms[static_cast<std::size_t>(to)] +=
            std::chrono::duration<double, std::milli>(now - m_mark).count();
    }
    m_mark = now;
}

void Recorder::begin(Stage stage, Clock::time_point now) {
    charge(now);
    // A frame drawn between ticks is charged as a whole, but for its swap and any wait.
    const bool inside_between = !m_open.empty() && m_open.back().charge == Stage::Between;
    const bool own = stage == Stage::Swap || stage == Stage::Pace;
    m_open.push_back({stage, inside_between && !own ? Stage::Between : stage});
}

void Recorder::end(Clock::time_point now) {
    charge(now);
    if (!m_open.empty()) {
        m_open.pop_back();
    }
}

void Recorder::set_counter(Counter counter, std::uint64_t total) {
    m_totals[static_cast<std::size_t>(counter)] = total;
}

void Recorder::set_renderers(std::span<const RendererTime> totals) {
    m_renderer_totals.assign(totals.begin(), totals.end());
}

void Recorder::tick(Clock::time_point now, double budget_ms, int level) {
    charge(now);
    if (!m_started) {
        m_started = true;
        m_last_totals = m_totals;
        m_last_renderer_totals = m_renderer_totals;
        m_level = level;
        m_current = Record{};
        return;
    }
    Record& r = m_current;
    r.tick = m_ticks++;
    r.level = level;
    r.budget_ms = budget_ms;
    for (double ms : r.stage_ms) {
        r.total_ms += ms;
    }
    count_growth(r);
    note_hitch(r);
    write_line(r);
    m_level = level;

    if (m_ring.size() < kHistory) {
        m_ring.push_back(std::move(r));
    } else {
        m_ring[m_next] = std::move(r);
    }
    m_next = (m_next + 1) % kHistory;
    m_current = Record{};
}

// What the running totals grew by since the last tick.
void Recorder::count_growth(Record& r) {
    for (std::size_t i = 0; i < kCounters; ++i) {
        r.counts[i] = grown(m_totals[i], m_last_totals[i]);
    }
    m_last_totals = m_totals;
    for (const RendererTime& now : m_renderer_totals) {
        RendererTime d = now;
        for (const RendererTime& before : m_last_renderer_totals) {
            if (before.name == now.name) {
                d.cpu_ms = grown(now.cpu_ms, before.cpu_ms);
                d.gpu_ms = grown(now.gpu_ms, before.gpu_ms);
                break;
            }
        }
        r.gpu_ms += d.gpu_ms;
        r.renderers.push_back(std::move(d));
    }
    m_last_renderer_totals = m_renderer_totals;
}

// A hitch: a tick a quarter longer than the game's, kept with the stage that took longest and
// what else happened in it.
void Recorder::note_hitch(const Record& r) {
    if (r.budget_ms <= 0.0 || r.total_ms <= 1.25 * r.budget_ms) {
        return;
    }
    Hitch h{r.tick, r.total_ms, Stage::Game, 0.0, {}};
    for (std::size_t i = 0; i < kStages; ++i) {
        const auto s = static_cast<Stage>(i);
        if (s != Stage::Pace && r.stage_ms[i] > h.stage_ms) {
            h.stage = s;
            h.stage_ms = r.stage_ms[i];
        }
    }
    if (r.level != m_level) {
        h.note = "level change";
    } else if (const auto sectors = r.count(Counter::Sectors); sectors > 0) {
        h.note = std::format("{} sectors read", sectors);
    } else if (const auto conversions = r.count(Counter::Conversions); conversions > 0) {
        h.note = std::format("{} textures converted", conversions);
    }
    m_hitches.push_back(std::move(h));
    if (m_hitches.size() > kHitches) {
        m_hitches.erase(m_hitches.begin());
    }
}

void Recorder::write_line(const Record& r) {
    if (m_csv == nullptr) {
        return;
    }
    std::string line = std::format("{},{},{:.3f},{:.3f}", r.tick, r.level, r.budget_ms, r.total_ms);
    for (double ms : r.stage_ms) {
        line += std::format(",{:.3f}", ms);
    }
    for (std::uint64_t n : r.counts) {
        line += std::format(",{}", n);
    }
    line += std::format(",{:.3f}\n", r.gpu_ms);
    std::fputs(line.c_str(), m_csv);
}

const Record& Recorder::record(std::size_t i) const {
    // Until the ring is full its records are in order; then the oldest is the next to be replaced.
    return m_ring.size() < kHistory ? m_ring[i] : m_ring[(m_next + i) % kHistory];
}

Summary Recorder::summary(std::size_t ticks) const {
    Summary s;
    const std::size_t n = std::min(ticks, size());
    if (n == 0) {
        s.verdict = "measuring";
        return s;
    }
    s.ticks = n;
    std::size_t late = 0;
    for (std::size_t i = size() - n; i < size(); ++i) {
        const Record& r = record(i);
        s.budget_ms = r.budget_ms;
        s.total_ms += r.total_ms;
        s.work_ms += r.work_ms();
        s.gpu_ms += r.gpu_ms;
        late += r.budget_ms > 0.0 && r.total_ms > 1.1 * r.budget_ms ? 1 : 0;
        for (std::size_t k = 0; k < kStages; ++k) {
            s.avg_ms[k] += r.stage_ms[k];
            s.max_ms[k] = std::max(s.max_ms[k], r.stage_ms[k]);
        }
        for (std::size_t k = 0; k < kCounters; ++k) {
            s.per_tick[k] += static_cast<double>(r.counts[k]);
        }
        for (const RendererTime& t : r.renderers) {
            auto it = std::find_if(s.renderers.begin(), s.renderers.end(),
                                   [&](const RendererTime& x) { return x.name == t.name; });
            if (it == s.renderers.end()) {
                s.renderers.push_back(t);
            } else {
                it->cpu_ms += t.cpu_ms;
                it->gpu_ms += t.gpu_ms;
            }
        }
    }
    const double count = static_cast<double>(n);
    s.ticks_per_second = s.total_ms > 0.0 ? 1000.0 * count / s.total_ms : 0.0;
    s.total_ms /= count;
    s.work_ms /= count;
    s.gpu_ms /= count;
    s.late = static_cast<double>(late) / count;
    for (double& v : s.avg_ms) {
        v /= count;
    }
    for (double& v : s.per_tick) {
        v /= count;
    }
    for (RendererTime& t : s.renderers) {
        t.cpu_ms /= count;
        t.gpu_ms /= count;
    }

    // What holds the game back: nothing while the work fits the tick with a little to spare and
    // ticks are seldom late; else the GPU when the renderers' GPU time alone fills most of the
    // tick and outweighs their CPU time; else the stage that works longest.
    s.trouble = s.budget_ms > 0.0 && (s.late > 0.05 || s.work_ms > 0.95 * s.budget_ms);
    if (!s.trouble) {
        s.verdict = std::format("none, {:.1f} ms to spare", std::max(0.0, s.budget_ms - s.work_ms));
        return s;
    }
    if (s.gpu_ms > 0.8 * s.budget_ms && s.gpu_ms > s.avg_ms[static_cast<std::size_t>(Stage::Render)]) {
        s.verdict = std::format("gpu, renderers {:.1f} ms", s.gpu_ms);
        return s;
    }
    std::size_t worst = 0;
    for (std::size_t k = 0; k < kStages; ++k) {
        const auto st = static_cast<Stage>(k);
        if (st != Stage::Pace && st != Stage::Swap && s.avg_ms[k] > s.avg_ms[worst]) {
            worst = k;
        }
    }
    s.verdict = std::format("{} {:.1f} ms", stage_name(static_cast<Stage>(worst)), s.avg_ms[worst]);
    return s;
}

void Recorder::write_csv(std::FILE* file) {
    m_csv = file;
    if (m_csv == nullptr) {
        return;
    }
    std::string header = "tick,level,budget_ms,total_ms";
    for (std::size_t i = 0; i < kStages; ++i) {
        std::string name = stage_name(static_cast<Stage>(i));
        std::replace_if(name.begin(), name.end(), [](char c) { return c == ' ' || c == ',' || c == '/'; }, '_');
        header += "," + name + "_ms";
    }
    for (const char* name : kCounterNames) {
        header += std::string(",") + name;
    }
    header += ",gpu_ms\n";
    std::fputs(header.c_str(), m_csv);
}

// ---- The program's recorder ----

namespace {

bool g_enabled = [] {
    const char* v = std::getenv("OPENRAC_PERF");
    return v != nullptr && v[0] != '\0' && std::strcmp(v, "0") != 0;
}();
Mode g_mode = Mode::Full;

}  // namespace

bool enabled() {
    return g_enabled;
}

void enable() {
    g_enabled = true;
}

Mode mode() {
    return g_enabled ? g_mode : Mode::Off;
}

void cycle_mode() {
    g_mode = g_mode == Mode::Off ? Mode::Compact : g_mode == Mode::Compact ? Mode::Full : Mode::Off;
}

Recorder& recorder() {
    static Recorder r;
    return r;
}

Scope::Scope(Stage stage) : m_on(g_enabled) {
    if (m_on) {
        recorder().begin(stage, Clock::now());
    }
}

Scope::~Scope() {
    if (m_on) {
        recorder().end(Clock::now());
    }
}

namespace {

// OPENRAC_PERF_CSV=FILE: each tick, as a line of FILE (left open until the program exits).
void open_csv() {
    const char* path = std::getenv("OPENRAC_PERF_CSV");
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    if (std::FILE* f = std::fopen(path, "w")) {
        recorder().write_csv(f);
    }
}

}  // namespace

void tick(double budget_ms, int level) {
    if (!g_enabled) {
        return;
    }
    static bool csv_opened = false;
    if (!csv_opened) {
        csv_opened = true;
        open_csv();
    }
    recorder().tick(Clock::now(), budget_ms, level);
}

}  // namespace openrac::frontend::perf
