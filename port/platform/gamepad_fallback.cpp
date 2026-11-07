// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
#include "platform/gamepad_fallback.h"

#include <array>
#include <format>

namespace openrac::platform {

namespace {

// One gamepad button and the joystick button it is read from. SDL's a, b, x, y are places
// (south, east, west, north), whatever the caps say.
struct Slot {
    const char* name;
    int button;
};

// 8BitDo's DirectInput mode, the same on every pad of theirs SDL lists (Pro 2, Pro 3, SN30 Pro,
// Ultimate...): b2 and b5 are the back paddles where there are any.
constexpr std::array<Slot, 13> k8BitDo{{
    {"a", 0},
    {"b", 1},
    {"x", 3},
    {"y", 4},
    {"leftshoulder", 6},
    {"rightshoulder", 7},
    {"lefttrigger", 8},
    {"righttrigger", 9},
    {"back", 10},
    {"start", 11},
    {"guide", 12},
    {"leftstick", 13},
    {"rightstick", 14},
}};

// The usual DirectInput pad (Logitech's D mode, most unbranded USB pads): 1 west, 2 south,
// 3 east, 4 north, then the shoulders, the triggers, Select, Start and the stick clicks.
constexpr std::array<Slot, 13> kGeneric{{
    {"x", 0},
    {"a", 1},
    {"b", 2},
    {"y", 3},
    {"leftshoulder", 4},
    {"rightshoulder", 5},
    {"lefttrigger", 6},
    {"righttrigger", 7},
    {"back", 8},
    {"start", 9},
    {"leftstick", 10},
    {"rightstick", 11},
    {"guide", 12},
}};

}  // namespace

std::string fallback_gamepad_mapping(
    std::string_view guid, std::string_view name, const JoystickShape& shape
) {
    // Four face buttons and something to move with, or it is not a pad.
    if (shape.other_kind || shape.buttons < 4 || (shape.axes < 2 && shape.hats < 1)) {
        return {};
    }
    std::string mapping{guid};
    mapping += ',';
    // The name ends at the first comma in SDL's format.
    for (const char c : name.empty() ? std::string_view{"Controller"} : name) {
        mapping += c == ',' ? ' ' : c;
    }
    mapping += ',';
    const bool eight_bit_do = shape.vendor == kVendor8BitDo;
    for (const Slot& slot : eight_bit_do ? k8BitDo : kGeneric) {
        if (slot.button < shape.buttons) {
            mapping += std::format("{}:b{},", slot.name, slot.button);
        }
    }
    if (shape.hats >= 1) {
        mapping += "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,";
    }
    if (shape.axes >= 2) {
        mapping += "leftx:a0,lefty:a1,";
    }
    // The right stick: 8BitDo's pads on Windows have an axis between the sticks (a2), the others
    // follow the left one.
    const int right = eight_bit_do && shape.axes >= 5 ? 3 : 2;
    if (shape.axes >= right + 2) {
        mapping += std::format("rightx:a{},righty:a{},", right, right + 1);
    }
    return mapping;
}

}  // namespace openrac::platform
