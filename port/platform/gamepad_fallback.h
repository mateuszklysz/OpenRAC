// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// A layout for a controller SDL does not know as a gamepad. On Windows a controller in DirectInput
// mode (an 8BitDo pad switched to D, an older or cheap USB pad) is a gamepad to SDL only when its
// list of mappings names that model; any other was never opened, so it played nothing and was not
// even listed in the controls. Such a controller now gets the layout most of its kind use, and
// the player can rebind what lands on the wrong button. A mapping of the player's own
// (gamecontrollerdb.txt, docs/port/CONTROLS.md) still replaces it.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openrac::platform {

// What SDL reports of a joystick it has no mapping for.
struct JoystickShape {
    std::uint16_t vendor = 0;  // USB vendor id, 0 when unknown
    int buttons = 0;
    int axes = 0;
    int hats = 0;
    bool other_kind = false;  // a wheel, a flight stick, a dance mat...: not a gamepad
};

inline constexpr std::uint16_t kVendor8BitDo = 0x2dc8;

// SDL's mapping line for it ("<guid>,<name>,a:b0,...", as in gamecontrollerdb.txt), or empty when
// it does not look like a gamepad.
std::string fallback_gamepad_mapping(
    std::string_view guid, std::string_view name, const JoystickShape& shape
);

}  // namespace openrac::platform
