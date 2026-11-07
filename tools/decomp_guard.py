#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
Finds changes to the decompilation trees that OpenRAC copies from other repositories
(games/*/game.json "source", docs/SOURCES.md). Those trees are synced in from their own
projects, so a decomp change belongs in that project's repository, not here.

  python3 tools/decomp_guard.py                 files changed since develop (git diff develop...HEAD)
  python3 tools/decomp_guard.py --base SHA      files changed since SHA
  python3 tools/decomp_guard.py FILE...         just these paths
  python3 tools/decomp_guard.py --base SHA --files-from LIST    the paths in LIST (- for stdin)

Paths OpenRAC keeps itself inside a synced tree are listed in that version's "local" in
game.json (port patches, OpenRAC's notes); they never count. With --strict the script exits 1
when anything else is changed, unless --title starts with chore(import) or chore(sync), or
--labels holds decomp-sync: the CI job (.github/workflows/decomp-guard.yml) passes --strict for
pull requests from outside contributors and only warns for maintainers. The manifests are read
at --base when it is given, so a pull request can't widen "local" for itself.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # --root moves it (CI runs the base's copy of this file)
SYNC_TITLES = ("chore(import)", "chore(sync)")
SYNC_LABEL = "decomp-sync"


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout


def manifests(base: str | None) -> list[dict]:
    """Every games/*/game.json, from the working tree or as it was at BASE."""
    if base is None:
        return [json.loads(p.read_text(encoding="utf-8")) for p in sorted((ROOT / "games").glob("*/game.json"))]
    names = git("ls-tree", "--name-only", f"{base}:games").split()
    out = []
    for name in sorted(names):
        try:
            out.append(json.loads(git("show", f"{base}:games/{name}/game.json")))
        except subprocess.CalledProcessError:
            continue
    return out


def synced_trees(games: list[dict]) -> list[dict]:
    """One entry per synced tree: its directory, project, repository, branch and local paths.
    Versions that share another's tree ("shared") are left out; the longest directory first, so
    games/rac1/ntsc wins over games/rac1 (the PAL tree holds the NTSC one)."""
    trees = []
    for game in games:
        for name, version in game["versions"].items():
            source = version.get("source") or {}
            if not source.get("repo") or source.get("shared"):
                continue
            path = version.get("dir", f"games/{game['id']}/{name}").rstrip("/")
            trees.append({"key": f"{game['id']}/{name}", "dir": path, "name": source.get("name", ""),
                          "repo": source["repo"], "branch": source.get("branch", ""),
                          "local": [f"{path}/{p}" for p in source.get("local", [])]
                          + [f"games/{game['id']}/game.json"]})
    return sorted(trees, key=lambda t: len(t["dir"]), reverse=True)


def inside(path: str, prefix: str) -> bool:
    prefix = prefix.rstrip("/")
    return path == prefix or path.startswith(prefix + "/")


def find(paths: list[str], trees: list[dict]) -> dict[str, list[str]]:
    """tree key -> the changed paths in that tree that are not OpenRAC's own."""
    hits: dict[str, list[str]] = {}
    for path in paths:
        tree = next((t for t in trees if inside(path, t["dir"])), None)
        if tree and not any(inside(path, local) for local in tree["local"]):
            hits.setdefault(tree["key"], []).append(path)
    return hits


def allowed(title: str, labels: list[str]) -> bool:
    """A sync from the source projects, or a change a maintainer marked as one."""
    return title.strip().lower().startswith(SYNC_TITLES) or SYNC_LABEL in labels


def report(hits: dict[str, list[str]], trees: list[dict]) -> str:
    by_key = {t["key"]: t for t in trees}
    lines = ["## Changes to synced decompilation trees", "",
             "These files come from other repositories and are synced into OpenRAC from time to time",
             "([docs/SOURCES.md](docs/SOURCES.md)). A change made only here is overwritten or left as a",
             "conflict at the next sync. Please open the decomp change in its own project instead:", ""]
    for key, files in hits.items():
        tree = by_key[key]
        lines.append(f"- `{tree['dir']}` comes from **{tree['name']}**: {tree['repo']} (branch `{tree['branch']}`)")
        lines += [f"  - `{f}`" for f in files[:20]]
        if len(files) > 20:
            lines.append(f"  - and {len(files) - 20} more")
    lines += ["", "A change the native port needs (not the decomp itself) stays here: wrap the function in",
              "the port instead of editing the decomp C, or, where an edit is unavoidable, list it with its",
              "reason as docs/SOURCES.md describes. Maintainers mark a sync with a `chore(import)` title or",
              f"the `{SYNC_LABEL}` label."]
    return "\n".join(lines) + "\n"


def changed_files(base: str) -> list[str]:
    return [p for p in git("diff", "--name-only", "--no-renames", f"{base}...HEAD").splitlines() if p]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="*", help="paths to check instead of a diff")
    ap.add_argument("--base", help="diff from this commit (default: develop); manifests are read there")
    ap.add_argument("--files-from", help="read the paths to check from this file, one per line (- for stdin)")
    ap.add_argument("--root", type=Path, help="the top of OpenRAC (default: this file's checkout)")
    ap.add_argument("--strict", action="store_true", help="exit 1 when a synced tree is changed")
    ap.add_argument("--title", default="", help="the pull request's title")
    ap.add_argument("--labels", default="", help="the pull request's labels, comma-separated")
    args = ap.parse_args()
    global ROOT
    if args.root:
        ROOT = args.root.resolve()

    if args.files_from:
        text = sys.stdin.read() if args.files_from == "-" else Path(args.files_from).read_text(encoding="utf-8")
        args.files = [line.strip() for line in text.splitlines() if line.strip()]
    trees = synced_trees(manifests(args.base))
    paths = args.files if args.files or args.files_from else changed_files(args.base or "develop")
    hits = find(paths, trees)
    if not hits:
        print("No synced decompilation tree is changed.")
        return 0
    text = report(hits, trees)
    print(text)
    labels = [s.strip() for s in args.labels.split(",") if s.strip()]
    ok = allowed(args.title, labels)
    fail = args.strict and not ok
    if os.environ.get("GITHUB_ACTIONS"):
        kind = "error" if fail else "warning"
        for f in [f for files in hits.values() for f in files][:50]:
            print(f"::{kind} file={f}::This file is synced from another repository (docs/SOURCES.md).")
        summary = os.environ.get("GITHUB_STEP_SUMMARY")
        if summary:
            with open(summary, "a", encoding="utf-8") as out:
                out.write(text)
    if ok:
        print("Marked as a sync: allowed.")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
