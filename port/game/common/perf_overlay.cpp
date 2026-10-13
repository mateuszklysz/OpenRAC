// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "perf_overlay.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "perf.h"
#include "picture.h"

namespace openrac::frontend::perf_overlay {

namespace {

using perf::Stage;

constexpr std::uint32_t kBack = rgba(10, 10, 20);
constexpr std::uint32_t kEdge = rgba(70, 70, 100);
constexpr std::uint32_t kTitle = rgba(230, 230, 230);
constexpr std::uint32_t kText = rgba(190, 200, 220);
constexpr std::uint32_t kHint = rgba(120, 120, 140);
constexpr std::uint32_t kGood = rgba(110, 220, 110);
constexpr std::uint32_t kBad = rgba(255, 90, 80);
constexpr std::uint32_t kBudget = rgba(255, 255, 255);

// One colour per stage, in the graph and beside the stage's row.
constexpr std::uint32_t kStageColour[perf::kStages] = {
    rgba(255, 170, 50),   // game code
    rgba(150, 150, 150),  // port, other
    rgba(200, 120, 255),  // input
    rgba(80, 200, 255),   // scene
    rgba(80, 120, 255),   // renderers
    rgba(60, 200, 160),   // present
    rgba(90, 90, 110),    // swap
    rgba(255, 110, 180),  // between
    rgba(40, 40, 50),     // wait for tick
};

constexpr int kPad = 6;
constexpr int kLine = 10;
constexpr int kWidth = 344;
constexpr int kBarX = 210;
constexpr int kBarWidth = kWidth - kBarX - kPad;  // the tick's budget
constexpr int kGraphHeight = 60;                  // two budgets

struct State {
    PictureBlitter blitter;
    Picture picture;
    perf::Mode shown = perf::Mode::Off;
    int age = 0;  // draws since the picture was made
};
State s;

std::string fixed(double v, int width = 5, int decimals = 1) {
    return std::format("{:{}.{}f}", v, width, decimals);
}

// A count per tick, short: 380k, 1.2m.
std::string amount(double v) {
    if (v >= 1.0e6) {
        return std::format("{:.1f}m", v / 1.0e6);
    }
    if (v >= 1.0e4) {
        return std::format("{:.0f}k", v / 1.0e3);
    }
    return std::format("{:.0f}", v);
}

void frame_box(Picture& p) {
    p.fill(0, 0, p.width, p.height, kEdge);
    p.fill(1, 1, p.width - 1, p.height - 1, kBack);
}

Picture compact(const perf::Summary& sum) {
    const std::string rate = std::format("{:.1f} ticks/s  {:.1f}/{:.1f} ms  ", sum.ticks_per_second, sum.work_ms,
                                         sum.budget_ms);
    const std::string verdict = sum.trouble ? "slow: " + sum.verdict : "ok";
    Picture p(static_cast<int>(rate.size() + verdict.size()) * 6 + 2 * kPad, 7 + 2 * kPad);
    frame_box(p);
    const int x = p.text(kPad, kPad, rate, kText);
    p.text(x, kPad, verdict, sum.trouble ? kBad : kGood);
    return p;
}

Picture full(const perf::Recorder& rec, const perf::Summary& sum) {
    const std::vector<perf::Record> history = rec.history();
    const auto& hitches = rec.hitches();
    const int renderer_rows = static_cast<int>((sum.renderers.size() + 2) / 3);
    const int height = kPad + 2 * kLine + 4                 // title, verdict
                       + kLine * (1 + static_cast<int>(perf::kStages)) + 4  // stages
                       + kLine * (1 + renderer_rows) + 4    // renderers
                       + 2 * kLine + 4                      // counts
                       + kGraphHeight + 6                   // graph
                       + kLine * (1 + std::max<int>(1, static_cast<int>(hitches.size())))  // hitches
                       + kLine + kPad;                      // keys
    Picture p(kWidth, height);
    frame_box(p);
    int y = kPad;
    const int level = history.empty() ? 0 : history.back().level;
    p.text(kPad, y, std::format("openrac perf  {:.1f} ticks/s  budget {:.1f} ms  {}", sum.ticks_per_second,
                                sum.budget_ms, level < 0 ? std::string("menus") : std::format("level {}", level)),
           kTitle);
    y += kLine;
    p.text(kPad, y, "bottleneck: " + sum.verdict, sum.trouble ? kBad : kGood);
    y += kLine + 4;

    // The stages: average and worst, and the average against the tick's budget.
    p.text(kPad + 10, y, "stage", kHint);
    p.text(130, y, "  avg", kHint);
    p.text(166, y, "  max", kHint);
    p.text(kBarX, y, std::format("of {:.1f} ms", sum.budget_ms), kHint);
    y += kLine;
    for (std::size_t i = 0; i < perf::kStages; ++i) {
        p.fill(kPad, y, kPad + 6, y + 7, kStageColour[i]);
        p.text(kPad + 10, y, perf::stage_name(static_cast<Stage>(i)), kText);
        p.text(130, y, fixed(sum.avg_ms[i]), kText);
        p.text(166, y, fixed(sum.max_ms[i]), kText);
        if (sum.budget_ms > 0.0) {
            const int w = static_cast<int>(std::lround(std::min(1.0, sum.avg_ms[i] / sum.budget_ms) * kBarWidth));
            p.fill(kBarX, y + 1, kBarX + kBarWidth, y + 6, rgba(25, 25, 40));
            p.fill(kBarX, y + 1, kBarX + w, y + 6, kStageColour[i]);
        }
        y += kLine;
    }
    y += 4;

    // The renderers, three to a row: CPU / GPU milliseconds per tick.
    p.text(kPad, y, "renderers per tick, cpu/gpu ms", kHint);
    y += kLine;
    for (std::size_t i = 0; i < sum.renderers.size(); ++i) {
        const perf::RendererTime& t = sum.renderers[i];
        const int x = kPad + static_cast<int>(i % 3) * 112;
        std::string name = t.name.substr(0, 8);
        p.text(x, y + static_cast<int>(i / 3) * kLine,
               std::format("{} {:.1f}/{:.1f}", name, t.cpu_ms, t.gpu_ms), kText);
    }
    y += kLine * renderer_rows + 4;

    // What a tick does.
    const auto per = [&](perf::Counter c) { return sum.per_tick[static_cast<std::size_t>(c)]; };
    p.text(kPad, y, std::format("frames {:.1f}  draws {}  tris {}", per(perf::Counter::Frames),
                                amount(per(perf::Counter::DrawCalls)), amount(per(perf::Counter::Triangles))),
           kText);
    y += kLine;
    const double missing = per(perf::Counter::Missing);
    const int x = p.text(kPad, y, std::format("tex up {:.1f}  conv {:.1f}  sectors {:.1f}  ", per(perf::Counter::Uploads),
                                              per(perf::Counter::Conversions), per(perf::Counter::Sectors)),
                         kText);
    p.text(x, y, std::format("no c {}", amount(missing)), missing > 0.0 ? kBad : kText);
    y += kLine + 4;

    // The last ticks, a column each: their stages stacked (the wait for the next tick left out),
    // the budget the white line half way up.
    const int graph_x = kPad;
    const int graph_w = static_cast<int>(perf::Recorder::kHistory);
    p.fill(graph_x, y, graph_x + graph_w, y + kGraphHeight, rgba(20, 20, 32));
    const double budget = sum.budget_ms > 0.0 ? sum.budget_ms : 1000.0 / 60.0;
    const int bottom = y + kGraphHeight;
    const int offset = graph_w - static_cast<int>(history.size());
    for (std::size_t i = 0; i < history.size(); ++i) {
        const perf::Record& r = history[i];
        double at = 0.0;
        for (std::size_t k = 0; k < perf::kStages; ++k) {
            if (static_cast<Stage>(k) == Stage::Pace) {
                continue;
            }
            const double next = at + r.stage_ms[k];
            const int y0 = bottom - static_cast<int>(std::min(next / (2.0 * budget), 1.0) * kGraphHeight);
            const int y1 = bottom - static_cast<int>(std::min(at / (2.0 * budget), 1.0) * kGraphHeight);
            const int col = graph_x + offset + static_cast<int>(i);
            p.fill(col, y0, col + 1, y1, kStageColour[k]);
            at = next;
        }
        if (r.total_ms > 1.25 * budget) {
            const int col = graph_x + offset + static_cast<int>(i);
            p.fill(col, y, col + 1, y + 2, kBad);  // a hitch, marked at the top
        }
    }
    p.fill(graph_x, bottom - kGraphHeight / 2, graph_x + graph_w, bottom - kGraphHeight / 2 + 1, kBudget);
    p.text(graph_x + graph_w + 4, y, fixed(2.0 * budget, 4, 0), kHint);
    p.text(graph_x + graph_w + 4, bottom - kGraphHeight / 2 - 3, fixed(budget, 4, 0), kHint);
    p.text(graph_x + graph_w + 4, bottom - 7, "   0", kHint);
    y = bottom + 6;

    // The last hitches: the stage that took longest, and what else happened then.
    p.text(kPad, y, "hitches (over 1.25 budgets)", kHint);
    y += kLine;
    if (hitches.empty()) {
        p.text(kPad, y, "none", kGood);
        y += kLine;
    }
    for (auto it = hitches.rbegin(); it != hitches.rend(); ++it) {
        std::string line = std::format("{} {:.0f} ms {} {:.0f}", it->tick, it->ms, perf::stage_name(it->stage),
                                       it->stage_ms);
        if (!it->note.empty()) {
            line += ", " + it->note;
        }
        p.text(kPad, y, line.substr(0, (kWidth - 2 * kPad) / 6), kBad);
        y += kLine;
    }
    p.text(kPad, y, "f3: compact, off, full", kHint);
    return p;
}

}  // namespace

void draw(int window_width, int window_height) {
    const perf::Mode mode = perf::mode();
    if (mode == perf::Mode::Off || window_width <= 0 || window_height <= 0) {
        s.shown = perf::Mode::Off;
        return;
    }
    // Made again every tenth frame: often enough to follow, seldom enough to read and to cost
    // nothing much.
    if (mode != s.shown || ++s.age >= 10 || s.picture.width == 0) {
        const perf::Summary sum = perf::recorder().summary();
        s.picture = mode == perf::Mode::Compact ? compact(sum) : full(perf::recorder(), sum);
        s.shown = mode;
        s.age = 0;
    }
    // Whole pixels: twice the size from 1440 lines up, three times from 2160.
    const int scale = std::max(1, window_height / 720);
    const int w = std::min(window_width, s.picture.width * scale);
    const int h = std::min(window_height, s.picture.height * scale);
    s.blitter.blit(s.picture, 0, window_height, 8, 8, w, h);
}

void release() {
    s.blitter.release();
}

}  // namespace openrac::frontend::perf_overlay
