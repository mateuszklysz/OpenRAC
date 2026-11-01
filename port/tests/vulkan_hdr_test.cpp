// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// HDR output's encode pass (renderer/vulkan/hdr_output.cpp, shaders/hdr_encode.comp) against its
// CPU arithmetic (renderer/hdr.h). A swapchain cannot be read back and the test machine has no HDR
// screen, so vkgl::test_hdr_encode runs the pass on the default framebuffer into a stand-in of the
// screen's image, HDR10 and scRGB, and the test compares every pixel, turned the right way up,
// with renderer::hdr::encode of the bytes the window reads back.
//
// Skipped (77) where Vulkan cannot open.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "renderer/gl.h"
#include "renderer/hdr.h"
#include "renderer/vulkan/vkgl.h"
#include "tests/check.h"
#include "tests/renderer_gl_context.h"

namespace {

using namespace openrac::gl;
namespace hdr = openrac::renderer::hdr;

constexpr int kWidth = 64;
constexpr int kHeight = 48;

// Paints the window: four quarters of different colours and a grey ramp along the bottom rows, so
// that both the orientation and the curve show.
void paint_window() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, kWidth, kHeight);
    glEnable(GL_SCISSOR_TEST);
    constexpr std::array<std::array<float, 3>, 4> kQuarters = {{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
    }};
    for (int n = 0; n < 4; ++n) {
        glScissor((n % 2) * kWidth / 2, (n / 2) * kHeight / 2, kWidth / 2, kHeight / 2);
        glClearColor(kQuarters[n][0], kQuarters[n][1], kQuarters[n][2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    for (int x = 0; x < kWidth; ++x) {
        const float v = static_cast<float>(x) / (kWidth - 1);
        glScissor(x, 0, 1, 4);
        glClearColor(v, v * 0.5f, 1.0f - v, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
}

float half_to_float(std::uint16_t h) {
    const int exponent = (h >> 10) & 0x1F;
    const int mantissa = h & 0x3FF;
    const float sign = (h & 0x8000) != 0 ? -1.0f : 1.0f;
    if (exponent == 0) {
        return sign * std::ldexp(static_cast<float>(mantissa), -24);
    }
    if (exponent == 31) {
        return sign * INFINITY;
    }
    return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, exponent - 15);
}

// The pass's pixels against the CPU's, for `output` at `levels`: a 10-bit step for HDR10 (the
// GPU's pow and the rounding), a half float's precision for scRGB.
void test_encode(
    const std::vector<std::uint8_t>& window, hdr::Output output, const hdr::Levels& levels
) {
    std::vector<std::uint8_t> screen;
    CHECK(openrac::vkgl::test_hdr_encode(output, levels, kWidth, kHeight, screen));
    const std::size_t texel = output == hdr::Output::Hdr10 ? 4 : 8;
    CHECK(screen.size() == static_cast<std::size_t>(kWidth * kHeight) * texel);
    if (screen.size() != static_cast<std::size_t>(kWidth * kHeight) * texel) {
        return;
    }
    int bad = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            // The screen's row 0 is its top, the window's its bottom.
            const std::uint8_t* in =
                &window[(static_cast<std::size_t>(kHeight - 1 - y) * kWidth + x) * 4];
            const hdr::Rgb want =
                hdr::encode({in[0] / 255.0f, in[1] / 255.0f, in[2] / 255.0f}, output, levels);
            const std::uint8_t* got = &screen[(static_cast<std::size_t>(y) * kWidth + x) * texel];
            for (int c = 0; c < 3; ++c) {
                float value = 0.0f;
                float tolerance = 0.0f;
                if (output == hdr::Output::Hdr10) {
                    std::uint32_t word = 0;
                    std::memcpy(&word, got, 4);
                    value = static_cast<float>((word >> (10 * c)) & 0x3FF) / 1023.0f;
                    tolerance = 1.5f / 1023.0f;
                } else {
                    std::uint16_t h = 0;
                    std::memcpy(&h, got + 2 * c, 2);
                    value = half_to_float(h);
                    tolerance = std::max(2e-3f * want[c], 1e-4f);
                }
                if (std::fabs(value - want[c]) > tolerance && bad++ < 8) {
                    std::fprintf(
                        stderr,
                        "%s pixel %d,%d channel %d: %f, want %f\n",
                        hdr::name(output),
                        x,
                        y,
                        c,
                        value,
                        want[c]
                    );
                }
            }
        }
    }
    CHECK(bad == 0);
}

// A screen of another size than the window: the pass scales as the blit does; every pixel is
// inside PQ's range and the top left quarter (the window's top left, white) is paper white.
void test_scaled() {
    std::vector<std::uint8_t> screen;
    const hdr::Levels levels{203.0f, 1000.0f};
    CHECK(openrac::vkgl::test_hdr_encode(hdr::Output::Hdr10, levels, 96, 80, screen));
    if (screen.size() != 96u * 80u * 4u) {
        CHECK(false);
        return;
    }
    std::uint32_t word = 0;
    std::memcpy(&word, &screen[(10 * 96 + 70) * 4], 4);
    const float red = static_cast<float>(word & 0x3FF) / 1023.0f;
    CHECK(std::fabs(hdr::pq_decode(red) - 203.0f) < 2.0f);
}

}  // namespace

int main() {
    openrac::test::GlContext context;
    std::string error;

    // No Vulkan here (or it fell back to OpenGL): nothing of the backend to test.
    if (!context.open(error, kWidth, kHeight) || !openrac::vkgl::active()) {
        std::printf("SKIP: Vulkan unavailable: %s\n", error.c_str());
        return openrac::test::kSkip;
    }

    paint_window();
    std::vector<std::uint8_t> window(static_cast<std::size_t>(kWidth * kHeight * 4));
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, window.data());
    test_encode(window, hdr::Output::Hdr10, {203.0f, 1000.0f});
    test_encode(window, hdr::Output::Hdr10, {480.0f, 1500.0f});
    test_encode(window, hdr::Output::ScRgb, {203.0f, 1000.0f});
    test_encode(window, hdr::Output::ScRgb, {80.0f, 400.0f});
    test_scaled();
    // Without an HDR request the screen is sent SDR, and a hidden window has no HDR to offer.
    CHECK(openrac::vkgl::hdr_output() == hdr::Output::Sdr);
    CHECK(openrac::vkgl::submission_counters().validation_errors == 0);
    return openrac::test::result();
}
