// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "perf_overlay.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "perf.h"
#include "picture.h"

namespace openrac::frontend::perf_overlay {

namespace {

using perf::Counter;
using perf::Stage;

// Drawn as one of the game's menu panels (picture.h): light blue for what a row is, yellow for its
// numbers, red for what is wrong.
constexpr std::uint32_t kBad = rgba(255, 96, 64);
constexpr std::uint32_t kTrack = kMenuHighlight;  // behind the bars and the graph

// One colour per stage, beside its row and in the graph.
constexpr std::uint32_t kStageColour[perf::kStages] = {
    rgba(255, 170, 40),   // game code: the bolts' orange
    kMenuFrame,           // port, other
    rgba(190, 120, 255),  // input
    rgba(80, 210, 255),   // scene from memory
    rgba(90, 130, 255),   // renderers
    rgba(70, 200, 170),   // present
    rgba(100, 100, 140),  // swap
    rgba(255, 110, 190),  // between ticks
    rgba(40, 40, 60),     // wait for tick
};

constexpr int kWidth = 344;
constexpr int kLeft = 10;
constexpr int kLine = 10;
constexpr int kAvgX = 136;
constexpr int kMaxX = 172;
constexpr int kBarX = 214;
constexpr int kBarWidth = kWidth - kBarX - kLeft;  // the tick's budget
constexpr int kGraphWidth = static_cast<int>(perf::Recorder::kHistory);
constexpr int kGraphHeight = 60;  // two budgets
constexpr int kRendererColumns = 3;
constexpr int kRendererColumnWidth = 108;

std::string ms(double v) {
    return std::format("{:5.1f}", v);
}

// A count per tick, short: 412, 38k, 1.2m.
std::string amount(double v) {
    if (v >= 1.0e6) {
        return std::format("{:.1f}m", v / 1.0e6);
    }
    if (v >= 1.0e4) {
        return std::format("{:.0f}k", v / 1.0e3);
    }
    return std::format("{:.0f}", v);
}

// "label value" in the menu's colours; returns the x after it and a gap.
int pair(Picture& p, int x, int y, std::string_view label, const std::string& value,
         std::uint32_t colour = kMenuYellow) {
    x = p.text(x, y, label, kMenuText);
    return p.text(x + 6, y, value, colour) + 12;
}

void rule(Picture& p, int y) {
    p.fill(8, y, p.width - 8, y + 1, kMenuFrame);
}

int summary_rows(Picture& p, int y, const perf::Summary& sum, int level) {
    int x = pair(p, kLeft, y, "ticks/s", std::format("{:.1f}", sum.ticks_per_second));
    x = pair(p, x, y, "budget", std::format("{:.1f} ms", sum.budget_ms));
    pair(p, x, y, level < 0 ? "menus" : "level", level < 0 ? std::string() : std::to_string(level));
    y += kLine;
    pair(p, kLeft, y, "bottleneck", sum.verdict, sum.trouble ? kBad : kMenuYellow);
    return y + kLine + 2;
}

// Each stage: its average and worst, and its average against the tick's budget.
int stage_rows(Picture& p, int y, const perf::Summary& sum) {
    p.text(kLeft + 10, y, "stage", kMenuHint);
    p.text(kAvgX, y, "  avg", kMenuHint);
    p.text(kMaxX, y, "  max", kMenuHint);
    p.text(kBarX, y, "of the budget", kMenuHint);
    y += kLine;
    for (std::size_t i = 0; i < perf::kStages; ++i) {
        p.fill(kLeft, y, kLeft + 6, y + 7, kStageColour[i]);
        p.text(kLeft + 10, y, perf::stage_name(static_cast<Stage>(i)), kMenuText);
        p.text(kAvgX, y, ms(sum.avg_ms[i]), kMenuYellow);
        p.text(kMaxX, y, ms(sum.max_ms[i]), kMenuYellow);
        const double share = sum.budget_ms > 0.0 ? std::min(1.0, sum.avg_ms[i] / sum.budget_ms) : 0.0;
        p.fill(kBarX, y + 1, kBarX + kBarWidth, y + 6, kTrack);
        p.fill(kBarX, y + 1, kBarX + static_cast<int>(std::lround(share * kBarWidth)), y + 6, kStageColour[i]);
        y += kLine;
    }
    return y + 2;
}

// Each renderer's CPU and GPU time per tick, three to a row.
int renderer_rows(Picture& p, int y, const perf::Summary& sum) {
    p.text(kLeft, y, "renderers, cpu/gpu ms a tick", kMenuHint);
    y += kLine;
    for (std::size_t i = 0; i < sum.renderers.size(); ++i) {
        const perf::RendererTime& t = sum.renderers[i];
        const int x = kLeft + static_cast<int>(i % kRendererColumns) * kRendererColumnWidth;
        const int row = y + static_cast<int>(i / kRendererColumns) * kLine;
        pair(p, x, row, t.name.substr(0, 8), std::format("{:.1f}/{:.1f}", t.cpu_ms, t.gpu_ms));
    }
    const int rows = static_cast<int>((sum.renderers.size() + kRendererColumns - 1) / kRendererColumns);
    return y + rows * kLine + 2;
}

// What a tick does; calls to functions without C in red, as the game may be wrong there.
int count_rows(Picture& p, int y, const perf::Summary& sum) {
    const auto per = [&](Counter c) { return sum.per_tick[static_cast<std::size_t>(c)]; };
    int x = pair(p, kLeft, y, "frames", std::format("{:.1f}", per(Counter::Frames)));
    x = pair(p, x, y, "draws", amount(per(Counter::DrawCalls)));
    pair(p, x, y, "triangles", amount(per(Counter::Triangles)));
    y += kLine;
    x = pair(p, kLeft, y, "textures in", std::format("{:.1f}", per(Counter::Uploads)));
    x = pair(p, x, y, "converted", std::format("{:.1f}", per(Counter::Conversions)));
    x = pair(p, x, y, "sectors", std::format("{:.1f}", per(Counter::Sectors)));
    const double missing = per(Counter::Missing);
    pair(p, x, y, "no c", amount(missing), missing > 0.0 ? kBad : kMenuYellow);
    return y + kLine + 2;
}

// The last ticks, a column each: their stages stacked (the wait for the next tick left out), the
// budget the yellow line half way up, a red mark over each hitch.
int graph(Picture& p, int y, const perf::Recorder& rec, double budget) {
    const int bottom = y + kGraphHeight;
    p.fill(kLeft - 1, y - 1, kLeft + kGraphWidth + 1, bottom + 1, kMenuFrame);
    p.fill(kLeft, y, kLeft + kGraphWidth, bottom, kTrack);
    const auto height_of = [&](double v) {
        return static_cast<int>(std::min(v / (2.0 * budget), 1.0) * kGraphHeight);
    };
    const int first = kLeft + kGraphWidth - static_cast<int>(rec.size());
    for (std::size_t i = 0; i < rec.size(); ++i) {
        const perf::Record& r = rec.record(i);
        const int x = first + static_cast<int>(i);
        double at = 0.0;
        for (std::size_t k = 0; k < perf::kStages; ++k) {
            if (static_cast<Stage>(k) != Stage::Pace) {
                p.fill(x, bottom - height_of(at + r.stage_ms[k]), x + 1, bottom - height_of(at), kStageColour[k]);
                at += r.stage_ms[k];
            }
        }
        if (r.total_ms > 1.25 * budget) {
            p.fill(x, y, x + 1, y + 3, kBad);
        }
    }
    const int middle = bottom - kGraphHeight / 2;
    p.fill(kLeft, middle, kLeft + kGraphWidth, middle + 1, kMenuYellow);
    const int labels = kLeft + kGraphWidth + 6;
    p.text(labels, y, std::format("{:.0f} ms", 2.0 * budget), kMenuHint);
    p.text(labels, middle - 3, std::format("{:.0f} ms", budget), kMenuYellow);
    p.text(labels, bottom - 7, "0", kMenuHint);
    return bottom + 6;
}

// The last hitches: the stage that took longest, and what else happened then.
int hitch_rows(Picture& p, int y, const std::vector<perf::Hitch>& hitches) {
    p.text(kLeft, y, "hitches, over 1.25 budgets", kMenuHint);
    y += kLine;
    if (hitches.empty()) {
        p.text(kLeft, y, "none", kMenuText);
        return y + kLine;
    }
    const std::size_t room = static_cast<std::size_t>((kWidth - 2 * kLeft) / 6);
    for (auto it = hitches.rbegin(); it != hitches.rend(); ++it) {
        std::string line =
            std::format("tick {} {:.0f} ms: {} {:.0f} ms", it->tick, it->ms, perf::stage_name(it->stage), it->stage_ms);
        if (!it->note.empty()) {
            line += ", " + it->note;
        }
        p.text(kLeft, y, line.substr(0, room), kBad);
        y += kLine;
    }
    return y;
}

Picture full(const perf::Recorder& rec, const perf::Summary& sum) {
    const int renderer_lines = static_cast<int>((sum.renderers.size() + kRendererColumns - 1) / kRendererColumns);
    const int hitch_lines = std::max(1, static_cast<int>(rec.hitches().size()));
    const int height = 27 + 2 * kLine + 6                  // the summary
                       + (1 + static_cast<int>(perf::kStages)) * kLine + 6  // the stages
                       + (1 + renderer_lines) * kLine + 6  // the renderers
                       + 2 * kLine + 6                      // the counts
                       + kGraphHeight + 10                  // the graph
                       + (1 + hitch_lines) * kLine + 4      // the hitches
                       + kLine + 6;                         // the keys
    Picture p(kWidth, height);
    int y = p.panel("OPENRAC PERFORMANCE");
    const int level = rec.size() > 0 ? rec.record(rec.size() - 1).level : 0;
    y = summary_rows(p, y, sum, level);
    rule(p, y);
    y = stage_rows(p, y + 4, sum);
    rule(p, y);
    y = renderer_rows(p, y + 4, sum);
    rule(p, y);
    y = count_rows(p, y + 4, sum);
    rule(p, y);
    y = graph(p, y + 6, rec, sum.budget_ms > 0.0 ? sum.budget_ms : 1000.0 / 60.0);
    y = hitch_rows(p, y, rec.hitches());
    p.text(kLeft, p.height - 12, "F3 COMPACT / OFF / FULL", kMenuHint);
    return p;
}

// One line: the tick rate, the work against the budget, and what holds the game back.
Picture compact(const perf::Summary& sum) {
    const std::string rate = std::format("{:.1f} ticks/s", sum.ticks_per_second);
    const std::string work = std::format("{:.1f}/{:.1f} ms", sum.work_ms, sum.budget_ms);
    const std::string verdict = sum.trouble ? sum.verdict : "ok";
    const int width = static_cast<int>(rate.size() + work.size() + verdict.size()) * 6 + 2 * 12 + 2 * kLeft;
    Picture p(width, 7 + 2 * 7);
    p.fill(0, 0, p.width, p.height, kMenuFrame);
    p.fill(2, 2, p.width - 2, p.height - 2, kMenuNavy);
    int x = p.text(kLeft, 7, rate, kMenuYellow) + 12;
    x = p.text(x, 7, work, kMenuText) + 12;
    p.text(x, 7, verdict, sum.trouble ? kBad : kMenuYellow);
    return p;
}

struct State {
    PictureBlitter blitter;
    perf::Mode shown = perf::Mode::Off;
    int age = 0;  // frames since the picture was made
    int width = 0;  // the picture's size
    int height = 0;
};
State s;

}  // namespace

void draw(int window_width, int window_height) {
    const perf::Mode mode = perf::mode();
    if (mode == perf::Mode::Off || window_width <= 0 || window_height <= 0) {
        s.shown = perf::Mode::Off;
        return;
    }
    // Made again every tenth frame: often enough to follow, seldom enough to read, and only then
    // copied to the GPU.
    if (mode != s.shown || ++s.age >= 10) {
        const perf::Summary sum = perf::recorder().summary();
        const Picture p = mode == perf::Mode::Compact ? compact(sum) : full(perf::recorder(), sum);
        s.blitter.upload(p);
        s.width = p.width;
        s.height = p.height;
        s.shown = mode;
        s.age = 0;
    }
    // Whole pixels: twice the size from 1440 lines up, three times from 2160.
    const int scale = std::max(1, window_height / 720);
    s.blitter.blit(0, window_height, 8, 8, std::min(window_width, s.width * scale),
                   std::min(window_height, s.height * scale));
}

void release() {
    s.blitter.release();
    s.shown = perf::Mode::Off;
}

}  // namespace openrac::frontend::perf_overlay
