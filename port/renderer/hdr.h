// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// HDR output: the game's SDR picture shown on an HDR screen at the player's paper white, so that it
// looks as it does in SDR (docs/port/HDR.md). This file is the arithmetic and the choices only, with
// no GPU or SDL in it, so that tests/hdr_test.cpp can pin them; the Vulkan backend's encode pass
// (renderer/vulkan/shaders/hdr_encode.comp) does the same per pixel and is checked against it.
//
// With HDR off nothing here runs and the picture goes to the screen as it always did.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace openrac::renderer::hdr {

// The player's setting (`hdr` in the settings file).
enum class Mode {
    Off,   // SDR, as always (the default)
    Auto,  // HDR when the system says the window's display shows HDR, else SDR
    On,    // HDR whenever the screen offers it, also where the system cannot say (X11)
    Count,
};

// What the screen is sent.
enum class Output {
    Sdr,    // the picture's bytes as they are
    Hdr10,  // PQ (SMPTE ST 2084) in BT.2020 primaries, 10 bits a channel
    ScRgb,  // linear BT.709 in half floats, 1.0 = 80 nits (Windows' scRGB, macOS's EDR)
    Count,
};

// What the system says of the window's display (SDL's window properties; Android's activity).
// `known` is false where nothing can tell (X11): Mode::Auto then stays SDR.
struct Display {
    bool known = false;
    bool hdr_enabled = false;
    float sdr_white_nits = 0.0f;  // the system's SDR content brightness; 0 when unknown
    float peak_nits = 0.0f;       // the brightest the display shows; 0 when unknown
};

// Nits of the SDR white an HDR picture of SDR content is usually given (ITU-R BT.2408), and of
// scRGB's 1.0.
inline constexpr float kReferenceWhiteNits = 203.0f;
inline constexpr float kScRgbUnitNits = 80.0f;
// A peak brightness for the metadata where the display does not say.
inline constexpr float kDefaultPeakNits = 1000.0f;
// PQ's range.
inline constexpr float kPqMaxNits = 10000.0f;
// The settings' ranges (0 in the file: the system's, or the default).
inline constexpr int kPaperWhiteMin = 80;
inline constexpr int kPaperWhiteMax = 1000;
inline constexpr int kPeakMin = 400;
inline constexpr int kPeakMax = 10000;
// The picture's bytes are display-referred: a monitor shows them with a 2.2 gamma (an sRGB
// monitor's real response; the piecewise sRGB curve would lift the game's dark scenes).
inline constexpr float kSdrGamma = 2.2f;

// Highlights (`hdr_highlights`, percent): how far the picture's brightest parts are lifted above
// paper white towards the peak. 0 keeps the picture exactly as in SDR.
inline constexpr int kHighlightsMax = 100;
inline constexpr int kDefaultHighlights = 50;
// Linear light (1.0 = the game's white) below which nothing is lifted: the game's mid tones and
// dark scenes stay as they are, only what is near white (lamps, explosions, sky) gets brighter.
inline constexpr float kHighlightKnee = 0.5f;

// The HDR picture's brightness: the game's white at `paper_white` nits, its highlights lifted by
// `highlights` (0..1) of the way to `peak`; `peak` also goes into the screen's metadata.
struct Levels {
    float paper_white = kReferenceWhiteNits;
    float peak = kDefaultPeakNits;
    float highlights = 0.0f;
};

// How much brighter than paper white the game's white is shown, less one (0: not lifted).
inline float highlight_boost(const Levels& levels) {
    const float headroom = std::max(levels.peak / levels.paper_white - 1.0f, 0.0f);
    return std::clamp(levels.highlights, 0.0f, 1.0f) * headroom;
}

// The brightest the picture gets, in nits (the screen's MaxCLL).
inline float max_content_nits(const Levels& levels) {
    return levels.paper_white * (1.0f + highlight_boost(levels));
}

// The levels for the player's settings (`paper_white`, `peak`: nits, 0 for the system's) on
// `display`. The system's SDR white is what the player set for SDR content in the system's own
// settings (Windows' "SDR content brightness"), so the game looks like the desktop around it.
inline Levels resolve_levels(int paper_white, int peak, int highlights, const Display& display) {
    Levels levels;
    levels.highlights =
        static_cast<float>(std::clamp(highlights, 0, kHighlightsMax)) / kHighlightsMax;
    if (paper_white > 0) {
        levels.paper_white =
            static_cast<float>(std::clamp(paper_white, kPaperWhiteMin, kPaperWhiteMax));
    } else if (display.sdr_white_nits > 0.0f) {
        levels.paper_white = std::clamp(
            display.sdr_white_nits,
            static_cast<float>(kPaperWhiteMin),
            static_cast<float>(kPaperWhiteMax)
        );
    }
    if (peak > 0) {
        levels.peak = static_cast<float>(std::clamp(peak, kPeakMin, kPeakMax));
    } else if (display.peak_nits > 0.0f) {
        levels.peak = std::min(display.peak_nits, kPqMaxNits);
    }
    // A peak below paper white would tell the screen the picture is dimmer than it is.
    levels.peak = std::max(levels.peak, levels.paper_white);
    return levels;
}

// The output the player's mode asks for on `display`: SDR, or the HDR kind this platform prefers
// (`prefer_scrgb`: macOS, whose EDR is linear). Whether the screen offers it is the swapchain's
// to find out (renderer/vulkan/surface_format.h), which falls back to the other kind or to SDR.
inline Output wanted_output(Mode mode, const Display& display, bool prefer_scrgb) {
    const bool hdr =
        mode == Mode::On || (mode == Mode::Auto && display.known && display.hdr_enabled);
    if (!hdr) {
        return Output::Sdr;
    }
    return prefer_scrgb ? Output::ScRgb : Output::Hdr10;
}

// ---- The arithmetic, as the encode pass does it per pixel ----

// SMPTE ST 2084's inverse EOTF: `nits` (0..10000) to the PQ signal (0..1).
inline float pq_encode(float nits) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 4096.0f * 128.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 4096.0f * 32.0f;
    constexpr float c3 = 2392.0f / 4096.0f * 32.0f;
    const float y = std::pow(std::clamp(nits / kPqMaxNits, 0.0f, 1.0f), m1);
    return std::pow((c1 + c2 * y) / (1.0f + c3 * y), m2);
}

// ST 2084's EOTF: the PQ signal to nits.
inline float pq_decode(float signal) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 4096.0f * 128.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 4096.0f * 32.0f;
    constexpr float c3 = 2392.0f / 4096.0f * 32.0f;
    const float e = std::pow(std::clamp(signal, 0.0f, 1.0f), 1.0f / m2);
    return kPqMaxNits * std::pow(std::max(e - c1, 0.0f) / (c2 - c3 * e), 1.0f / m1);
}

using Rgb = std::array<float, 3>;

// Linear BT.709 to linear BT.2020, both D65 (ITU-R BT.2087). Rows sum to 1, so white stays white.
inline constexpr std::array<Rgb, 3> kBt709ToBt2020 = {{
    {0.627403896f, 0.329283038f, 0.043313066f},
    {0.069097289f, 0.919540395f, 0.011362316f},
    {0.016391439f, 0.088013308f, 0.895595253f},
}};

// A channel of the picture (0..1, as stored) to linear light, 1.0 being the game's white.
inline float sdr_to_linear(float value) {
    return std::pow(std::clamp(value, 0.0f, 1.0f), kSdrGamma);
}

// Lifts a pixel's linear light (1.0 = the game's white) above the knee by `boost` at most (the
// game's white comes out at 1 + boost). The colour's luminance is lifted along a curve that
// starts flat at the knee, and all three channels are scaled alike, so hues stay as they are.
inline Rgb lift_highlights(const Rgb& linear, float boost) {
    const float luma = 0.2126f * linear[0] + 0.7152f * linear[1] + 0.0722f * linear[2];
    if (boost <= 0.0f || luma <= kHighlightKnee) {
        return linear;
    }
    const float t = (std::min(luma, 1.0f) - kHighlightKnee) / (1.0f - kHighlightKnee);
    const float scale = (luma + boost * t * t) / luma;
    return {linear[0] * scale, linear[1] * scale, linear[2] * scale};
}

// The value the screen is sent for one pixel of the picture (`rgb`, 0..1 as stored): PQ signals
// for Output::Hdr10, scRGB values for Output::ScRgb, the input itself for Output::Sdr.
inline Rgb encode(const Rgb& rgb, Output output, const Levels& levels) {
    if (output == Output::Sdr) {
        return rgb;
    }
    const Rgb linear = lift_highlights(
        {sdr_to_linear(rgb[0]), sdr_to_linear(rgb[1]), sdr_to_linear(rgb[2])},
        highlight_boost(levels)
    );
    Rgb out{};
    if (output == Output::ScRgb) {
        const float scale = levels.paper_white / kScRgbUnitNits;
        for (int c = 0; c < 3; ++c) {
            out[c] = linear[c] * scale;
        }
        return out;
    }
    for (int c = 0; c < 3; ++c) {
        const Rgb& row = kBt709ToBt2020[c];
        const float wide = row[0] * linear[0] + row[1] * linear[1] + row[2] * linear[2];
        out[c] = pq_encode(wide * levels.paper_white);
    }
    return out;
}

// The settings file's and the menus' names.
inline constexpr const char* kModeKeys[] = {"off", "auto", "on"};
inline constexpr const char* kModeNames[] = {"Off", "Auto", "On"};
inline constexpr const char* kOutputNames[] = {"SDR", "HDR10", "scRGB"};

inline const char* name(Output output) {
    return kOutputNames[std::clamp(static_cast<int>(output), 0, 2)];
}

}  // namespace openrac::renderer::hdr
