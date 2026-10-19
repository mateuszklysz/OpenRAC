// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The translations built into the program: port/lang/*.po and lang/game/*.po, each file's text as it is, written into
// a source file by cmake/Lang.cmake when CMake configures (a changed or new file configures again).
#pragma once

#include <span>
#include <string_view>

namespace openrac::i18n::files {

struct File {
    const char* name;  // "pl.po": the port's words; "game/pl.po": the game's texts
    std::string_view text;
};

std::span<const File> all();

}  // namespace openrac::i18n::files
