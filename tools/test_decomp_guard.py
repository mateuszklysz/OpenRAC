# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Tests for tools/decomp_guard.py: which changed paths fall in a synced decomp tree, what counts
as OpenRAC's own, and when a pull request passes. Synthetic manifests only: no git, no network."""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import decomp_guard  # noqa: E402

GAMES = [
    {"id": "rac1", "versions": {
        "pal": {"dir": "games/rac1", "source": {"name": "a-decomp", "repo": "https://example.invalid/a",
                                                 "branch": "main", "local": ["VERSIONS.md"]}},
        "ntsc": {"source": {"name": "b-decomp", "repo": "https://example.invalid/b", "branch": "main",
                            "local": ["OPENRAC_PATCHES.md", "host"]}},
        "jp": {"dir": "games/rac1", "source": {"shared": "pal", "repo": "https://example.invalid/a"}}}},
    {"id": "rac9", "versions": {"ntsc": {}}},
]


class Find(unittest.TestCase):
    def setUp(self):
        self.trees = decomp_guard.synced_trees(GAMES)

    def test_trees(self):
        self.assertEqual([t["key"] for t in self.trees], ["rac1/ntsc", "rac1/pal"])

    def test_nested_tree_wins(self):
        hits = decomp_guard.find(["games/rac1/ntsc/src/a.c", "games/rac1/src/b.c"], self.trees)
        self.assertEqual(hits, {"rac1/ntsc": ["games/rac1/ntsc/src/a.c"], "rac1/pal": ["games/rac1/src/b.c"]})

    def test_local_paths_and_outside(self):
        paths = ["games/rac1/ntsc/OPENRAC_PATCHES.md", "games/rac1/ntsc/host/run.sh", "games/rac1/game.json",
                 "games/rac1/VERSIONS.md", "port/main.cpp", "games/rac9/ntsc/x.c", "games/rac1/ntsc/hostile.c"]
        self.assertEqual(decomp_guard.find(paths, self.trees), {"rac1/ntsc": ["games/rac1/ntsc/hostile.c"]})

    def test_report_names_the_repository(self):
        text = decomp_guard.report({"rac1/ntsc": ["games/rac1/ntsc/src/a.c"]}, self.trees)
        self.assertIn("https://example.invalid/b", text)
        self.assertIn("games/rac1/ntsc/src/a.c", text)


class Allowed(unittest.TestCase):
    def test_sync_titles_and_label(self):
        self.assertTrue(decomp_guard.allowed("chore(import): sync rac2/ntsc to rac2-decomp abc", []))
        self.assertTrue(decomp_guard.allowed("chore(sync): rac1", []))
        self.assertTrue(decomp_guard.allowed("fix: x", ["decomp-sync"]))
        self.assertFalse(decomp_guard.allowed("fix(rac2): a match", ["bug"]))


if __name__ == "__main__":
    unittest.main()
