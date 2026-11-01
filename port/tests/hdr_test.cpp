// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// HDR output's arithmetic and choices (renderer/hdr.h): PQ against ST 2084's published points, the
// game's white at paper white whatever the output, the levels the settings and the display give,
// and the output each mode asks for (Off is SDR, whatever the display says).
#include <cmath>

#include "renderer/hdr.h"
#include "tests/check.h"

namespace {

namespace hdr = openrac::renderer::hdr;
using hdr::Output;

bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// ST 2084's values: 0 and 10000 nits at the ends, 100 nits at 0.5081, 203 at 0.5806, 1000 at 0.7518.
void test_pq_points() {
    CHECK(near(hdr::pq_encode(0.0f), 7.3e-7f, 1e-5f));
    CHECK(near(hdr::pq_encode(10000.0f), 1.0f, 1e-6f));
    CHECK(near(hdr::pq_encode(100.0f), 0.5081f, 2e-4f));
    CHECK(near(hdr::pq_encode(203.0f), 0.5806f, 2e-4f));
    CHECK(near(hdr::pq_encode(1000.0f), 0.7518f, 2e-4f));
    // Beyond the range: clamped.
    CHECK(hdr::pq_encode(20000.0f) == hdr::pq_encode(10000.0f));
    CHECK(hdr::pq_encode(-5.0f) == hdr::pq_encode(0.0f));
}

void test_pq_round_trip_and_order() {
    float last = -1.0f;
    for (float nits = 0.0f; nits <= 10000.0f; nits += 37.0f) {
        const float signal = hdr::pq_encode(nits);
        CHECK(signal > last);
        last = signal;
        CHECK(near(hdr::pq_decode(signal), nits, std::max(0.05f, nits * 2e-4f)));
    }
}

// The game's white is paper white in every output, and black is black.
void test_white_at_paper_white() {
    const hdr::Levels levels{250.0f, 1000.0f};
    const hdr::Rgb white{1.0f, 1.0f, 1.0f};
    const hdr::Rgb pq = hdr::encode(white, Output::Hdr10, levels);
    for (float c : pq) {
        CHECK(near(hdr::pq_decode(c), 250.0f, 0.1f));
    }
    const hdr::Rgb sc = hdr::encode(white, Output::ScRgb, levels);
    for (float c : sc) {
        CHECK(near(c * hdr::kScRgbUnitNits, 250.0f, 1e-3f));
    }
    const hdr::Rgb black = hdr::encode({0.0f, 0.0f, 0.0f}, Output::ScRgb, levels);
    CHECK(black[0] == 0.0f && black[1] == 0.0f && black[2] == 0.0f);
}

// SDR passes the bytes through unchanged.
void test_sdr_is_untouched() {
    const hdr::Rgb rgb{0.25f, 0.5f, 0.75f};
    CHECK(hdr::encode(rgb, Output::Sdr, {}) == rgb);
}

// Mid grey through the 2.2 gamma, and a saturated BT.709 red stays inside BT.2020 (all positive).
void test_gamma_and_gamut() {
    const hdr::Levels levels{100.0f, 1000.0f};
    const hdr::Rgb grey = hdr::encode({0.5f, 0.5f, 0.5f}, Output::ScRgb, levels);
    CHECK(near(grey[0] * hdr::kScRgbUnitNits, 100.0f * std::pow(0.5f, 2.2f), 1e-3f));
    const hdr::Rgb red = hdr::encode({1.0f, 0.0f, 0.0f}, Output::Hdr10, levels);
    CHECK(near(hdr::pq_decode(red[0]), 62.74f, 0.05f));
    CHECK(near(hdr::pq_decode(red[1]), 6.91f, 0.05f));
    CHECK(near(hdr::pq_decode(red[2]), 1.64f, 0.05f));
}

void test_levels() {
    const hdr::Display unknown;
    hdr::Levels l = hdr::resolve_levels(0, 0, 0, unknown);
    CHECK(l.paper_white == hdr::kReferenceWhiteNits && l.peak == hdr::kDefaultPeakNits);
    // The system's SDR white and peak where the display says them.
    const hdr::Display windows{true, true, 240.0f, 800.0f};
    l = hdr::resolve_levels(0, 0, 0, windows);
    CHECK(l.paper_white == 240.0f && l.peak == 800.0f);
    // The player's own numbers, clamped to the settings' ranges.
    l = hdr::resolve_levels(5000, 50000, 0, windows);
    CHECK(l.paper_white == hdr::kPaperWhiteMax && l.peak == hdr::kPeakMax);
    l = hdr::resolve_levels(10, 10, 0, windows);
    CHECK(l.paper_white == hdr::kPaperWhiteMin && l.peak == hdr::kPeakMin);
    // A peak below paper white is raised to it.
    l = hdr::resolve_levels(600, 400, 0, windows);
    CHECK(l.peak == 600.0f);
    // Highlights as a fraction, clamped.
    CHECK(hdr::resolve_levels(0, 0, 50, unknown).highlights == 0.5f);
    CHECK(hdr::resolve_levels(0, 0, 400, unknown).highlights == 1.0f);
    CHECK(hdr::resolve_levels(0, 0, -5, unknown).highlights == 0.0f);
}

// Highlights: nothing below the knee changes, the game's white goes to paper white + the asked
// share of the headroom, the curve only rises, and hues keep their ratios.
void test_highlights() {
    const hdr::Levels flat{200.0f, 1000.0f, 0.0f};
    const hdr::Levels half{200.0f, 1000.0f, 0.5f};
    const hdr::Levels full{200.0f, 1000.0f, 1.0f};
    CHECK(near(hdr::highlight_boost(half), 2.0f, 1e-5f));
    CHECK(near(hdr::max_content_nits(half), 600.0f, 1e-3f));
    CHECK(near(hdr::max_content_nits(full), 1000.0f, 1e-3f));
    CHECK(near(hdr::max_content_nits(flat), 200.0f, 1e-3f));
    // Peak at paper white: no headroom, nothing lifted.
    CHECK(hdr::highlight_boost({400.0f, 400.0f, 1.0f}) == 0.0f);
    const hdr::Rgb white{1.0f, 1.0f, 1.0f};
    for (float c : hdr::encode(white, Output::ScRgb, half)) {
        CHECK(near(c * hdr::kScRgbUnitNits, 600.0f, 1e-2f));
    }
    for (float c : hdr::encode(white, Output::Hdr10, full)) {
        CHECK(near(hdr::pq_decode(c), 1000.0f, 1.0f));
    }
    // Below the knee (linear 0.5, about byte 0.73) the picture is as with no highlights.
    const hdr::Rgb mid{0.6f, 0.6f, 0.6f};
    CHECK(hdr::encode(mid, Output::ScRgb, full) == hdr::encode(mid, Output::ScRgb, flat));
    float last = 0.0f;
    for (int i = 0; i <= 255; ++i) {
        const float v = static_cast<float>(i) / 255.0f;
        const float out = hdr::encode({v, v, v}, Output::ScRgb, half)[0];
        CHECK(out >= last);
        last = out;
    }
    const hdr::Rgb warm{1.0f, 0.9f, 0.6f};
    const hdr::Rgb a = hdr::encode(warm, Output::ScRgb, flat);
    const hdr::Rgb b = hdr::encode(warm, Output::ScRgb, half);
    CHECK(
        b[0] > a[0] && near(b[1] / b[0], a[1] / a[0], 1e-5f)
        && near(b[2] / b[0], a[2] / a[0], 1e-5f)
    );
}

void test_wanted_output() {
    const hdr::Display unknown;
    const hdr::Display sdr_display{true, false, 0.0f, 0.0f};
    const hdr::Display hdr_display{true, true, 203.0f, 1000.0f};
    CHECK(hdr::wanted_output(hdr::Mode::Off, hdr_display, false) == Output::Sdr);
    CHECK(hdr::wanted_output(hdr::Mode::Auto, unknown, false) == Output::Sdr);
    CHECK(hdr::wanted_output(hdr::Mode::Auto, sdr_display, false) == Output::Sdr);
    CHECK(hdr::wanted_output(hdr::Mode::Auto, hdr_display, false) == Output::Hdr10);
    CHECK(hdr::wanted_output(hdr::Mode::Auto, hdr_display, true) == Output::ScRgb);
    CHECK(hdr::wanted_output(hdr::Mode::On, unknown, false) == Output::Hdr10);
}

}  // namespace

int main() {
    test_pq_points();
    test_pq_round_trip_and_order();
    test_white_at_paper_white();
    test_sdr_is_untouched();
    test_gamma_and_gamut();
    test_levels();
    test_highlights();
    test_wanted_output();
    return openrac::test::result();
}
