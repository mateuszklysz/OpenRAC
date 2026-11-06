#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Ready-made Enhanced graphics lighting: packs of probe bakes, so a player's device need not bake.

The probe bake (launcher action probe-bake, openrac-enhanced-baker) lights each exported level from
the player's own disc. It takes minutes of every core, which a phone should not spend. A lighting
pack is one version's finished bakes (rac1/ntsc, ...) in one probe texture format, made once from
the same disc and offered as a download. The device installs a level from it only when its own
export is the one the pack was baked from (the same input files, byte for byte) and its baker
names the same bake (the same --print-profile); any level that does not match is left to the bake.

    lighting_pack.py make --version rac1/ntsc --levels-dir LEVELS --probes-dir PROBES --out PACK.zip
    lighting_pack.py install --levels-dir LEVELS --probes-dir PROBES [--pack PACK.zip | --feed URL]
        [--version rac1/ntsc] [--baker BAKER]

A pack holds pack.json, probes/level_NN/... (the bake's output folder) and levels/level_NN/...
(what the bake writes beside the export: water, ocean, the lighting environments). pack.json lists,
for each level, the input files it requires (with their SHA-256, or null for a file that must not
be there), the files it installs (with their SHA-256) and the baker profile's SHA-256.

The probe textures are compressed for the GPU that baked them (BC6H on desktop cards, ASTC HDR on
phones and Apple, else R11G11B10); --print-profile names the format, so the profile check also
picks the pack of the right format. A feed (--feed) is a JSON list of packs:
{"packs": [{"version": "rac1/ntsc", "format": "bc6h", "url": "...", "sha256": "..."}]}.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

FORMAT = 1
UPSTREAM = "OpenRAC/OpenRAC"
# The feed lives in the release tagged lighting-packs of the repository that built the launcher
# (OPENRAC_REPOSITORY, set by the launcher), then of OpenRAC's own.
FEED = "https://github.com/{}/releases/download/lighting-packs/feed.json"
# The export's files a bake reads (openrac-bake.json, water-bake.json and ocean-bake.json name them
# as inputs). A level is installed only where all of them are the pack's, or absent as in the pack.
INPUTS = (
    "manifest.json",
    "probe-bake.json",
    "collision.glb",
    "placements.json",
    "water-boundaries.json",
    "ocean-profile.json",
    "bake-inputs/overlay.bin",
    "bake-inputs/gameplay.bin",
)
# What the bake writes into the export folder beside its inputs.
LEVEL_OUTPUTS = (
    "water.bin",
    "water.mesh",
    "water-bake.json",
    "ocean.wave",
    "ocean-bake.json",
    "lighting-environments.json",
    ".lighting-environments.sha256",
)
STAMP = "openrac-bake.json"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def levels_in(folder: Path) -> list[str]:
    if not folder.is_dir():
        return []
    return sorted(
        p.name
        for p in folder.iterdir()
        if p.is_dir() and p.name.startswith("level_") and p.name[6:].isdigit() and (p / "manifest.json").is_file()
    )


def baker_profile(baker: Path, level: Path) -> str | None:
    """The SHA-256 of what `baker` says it bakes for `level`, as the launcher reads it (bakes.rs)."""
    try:
        out = subprocess.run(
            [str(baker), "--level-dir", str(level), "--print-profile"], capture_output=True, check=True, timeout=120
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return sha256_bytes(out.stdout.decode("utf-8", "replace").strip().encode())


def profile_format(profile_text: str) -> str:
    """The probe texture format a --print-profile text names: bc6h, astc or r11g11b10."""
    try:
        name = str(json.loads(profile_text).get("texture_compression", "")).lower()
    except json.JSONDecodeError:
        name = ""
    if "bc6" in name:
        return "bc6h"
    if "astc" in name:
        return "astc"
    return "r11g11b10"


# What the baker leaves beside the probes only for itself (--update, its own inputs) or for people
# reading a bake; the game and the launcher never open them, so a pack leaves them out.
BAKER_ONLY = {
    "interior-input.json",
    "interior-inference.json",
    "seed-plan.json",
    "emission-audit.json",
    "probe-density-report.json",
    "environment-report.json",
    "environment-placement-report.json",
}


def needed_to_play(rel: Path) -> bool:
    """Whether a bake's file (relative to its level's folder) goes into a pack."""
    return not (
        rel.suffix in (".log", ".txt")
        or rel.name in BAKER_ONLY
        or rel.parts[0] == "adaptive-reference"
    )


def make(version: str, levels_dir: Path, probes_dir: Path, out: Path, texture_format: str) -> int:
    """Packs every level whose bake is finished and verified (its stamp is there)."""
    manifest: dict = {"format": FORMAT, "version": version, "texture_format": texture_format, "levels": {}}
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as pack:
        for name in levels_in(levels_dir):
            level, baked = levels_dir / name, probes_dir / name
            stamp_path = baked / STAMP
            result_path = baked / "bake-result.json"
            if not stamp_path.is_file() or not result_path.is_file():
                print(f"{name}: no finished bake, left out")
                continue
            stamp = json.loads(stamp_path.read_text())
            if json.loads(result_path.read_text()).get("verified") is not True:
                print(f"{name}: bake not verified, left out")
                continue
            if stamp.get("manifest_sha256") != sha256_file(level / "manifest.json"):
                print(f"{name}: bake older than its export, left out")
                continue
            requires = {i: (sha256_file(level / i) if (level / i).is_file() else None) for i in INPUTS}
            files: dict[str, str] = {}
            for path in sorted(p for p in baked.rglob("*") if p.is_file()):
                if not needed_to_play(path.relative_to(baked)):
                    continue
                arc = f"probes/{name}/{path.relative_to(baked).as_posix()}"
                files[arc] = sha256_file(path)
                pack.write(path, arc)
            for rel in LEVEL_OUTPUTS:
                path = level / rel
                if path.is_file():
                    arc = f"levels/{name}/{rel}"
                    files[arc] = sha256_file(path)
                    pack.write(path, arc)
            manifest["levels"][name] = {
                "requires": requires,
                "files": files,
                "baker_profile_sha256": stamp.get("baker_profile_sha256"),
            }
            print(f"{name}: {len(files)} files")
        manifest["level_count"] = len(manifest["levels"])
        pack.writestr("pack.json", json.dumps(manifest, indent=1, sort_keys=True))
    print(f"{out}: {len(manifest['levels'])} levels, {out.stat().st_size / 1e6:.1f} MB, sha256 {sha256_file(out)}")
    return 0 if manifest["levels"] else 1


def make_feed(base_url: str, packs: list[Path], out: Path) -> int:
    """Writes the feed that lists packs uploaded under base_url (a release's download folder)."""
    entries = []
    for path in packs:
        with zipfile.ZipFile(path) as pack:
            manifest = json.loads(pack.read("pack.json"))
        entries.append(
            {
                "version": manifest["version"],
                "format": manifest["texture_format"],
                "url": f"{base_url.rstrip('/')}/{path.name}",
                "sha256": sha256_file(path),
            }
        )
    out.write_text(json.dumps({"packs": entries}, indent=1) + "\n")
    print(f"{out}: {len(entries)} pack(s)")
    return 0


def fetch(url: str, sha256: str | None, into: Path) -> Path:
    target = into / "pack.zip"
    with urllib.request.urlopen(url, timeout=60) as response, target.open("wb") as f:
        shutil.copyfileobj(response, f, 1 << 20)
    if sha256 and sha256_file(target) != sha256.lower():
        raise ValueError(f"{url}: the download's SHA-256 is not the feed's")
    return target


def choose_from_feed(feed_url: str, version: str, fmt: str) -> dict | None:
    with urllib.request.urlopen(feed_url, timeout=30) as response:
        feed = json.load(response)
    for entry in feed.get("packs", []):
        if entry.get("version") == version and entry.get("format") == fmt:
            return entry
    return None


def feeds_to_read() -> list[str]:
    repository = os.environ.get("OPENRAC_REPOSITORY", "").strip()
    order = [r for r in (repository, UPSTREAM) if r.count("/") == 1]
    return [FEED.format(r) for r in dict.fromkeys(order)]


def install(pack_path: Path, levels_dir: Path, probes_dir: Path, baker: Path | None, version: str | None) -> int:
    """Installs each level of the pack that matches this export (and baker); returns how many."""
    installed = 0
    with zipfile.ZipFile(pack_path) as pack:
        manifest = json.loads(pack.read("pack.json"))
        if manifest.get("format") != FORMAT:
            raise ValueError("the pack is of another format")
        if version and manifest.get("version") != version:
            raise ValueError(f"the pack is for {manifest.get('version')}, not {version}")
        for name, entry in sorted(manifest["levels"].items()):
            level = levels_dir / name
            if not (level / "manifest.json").is_file():
                continue
            wrong = [
                rel
                for rel, digest in entry["requires"].items()
                if (sha256_file(level / rel) if (level / rel).is_file() else None) != digest
            ]
            if wrong:
                print(f"{name}: this export differs from the pack's ({', '.join(wrong)}), left to the bake")
                continue
            if baker is not None and baker_profile(baker, level) != entry.get("baker_profile_sha256"):
                print(f"{name}: this baker makes another bake than the pack's, left to the bake")
                continue
            # Read and check every file first; nothing is replaced unless all of them are right.
            data = {}
            for arc, digest in entry["files"].items():
                if ".." in Path(arc).parts or not arc.startswith((f"probes/{name}/", f"levels/{name}/")):
                    raise ValueError(f"{arc}: outside its level")
                blob = pack.read(arc)
                if sha256_bytes(blob) != digest:
                    raise ValueError(f"{arc}: its SHA-256 is not the pack's")
                data[arc] = blob
            probes_dir.mkdir(parents=True, exist_ok=True)
            staging = Path(tempfile.mkdtemp(prefix=f".{name}.pack-", dir=probes_dir))
            try:
                for arc, blob in data.items():
                    if arc.startswith("probes/"):
                        target = staging / Path(arc).relative_to(f"probes/{name}")
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(blob)
                baked = probes_dir / name
                old = probes_dir / f".{name}.old"
                shutil.rmtree(old, ignore_errors=True)
                if baked.exists():
                    baked.rename(old)
                staging.rename(baked)
                shutil.rmtree(old, ignore_errors=True)
            finally:
                shutil.rmtree(staging, ignore_errors=True)
            for arc, blob in data.items():
                if arc.startswith("levels/"):
                    target = level / Path(arc).relative_to(f"levels/{name}")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    temporary = target.with_name(target.name + ".pack")
                    temporary.write_bytes(blob)
                    temporary.replace(target)
            for marker in (probes_dir / f".{name}.failed", probes_dir / f".{name}.partial"):
                shutil.rmtree(marker, ignore_errors=True)
            installed += 1
            print(f"{name}: installed from the pack")
    print(f"installed {installed} level(s)")
    return installed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("make", help="pack a version's finished bakes")
    m.add_argument("--version", required=True)
    m.add_argument("--levels-dir", type=Path, required=True)
    m.add_argument("--probes-dir", type=Path, required=True)
    m.add_argument("--out", type=Path, required=True)
    m.add_argument("--texture-format", choices=("bc6h", "astc", "r11g11b10"), required=True,
                   help="the probe texture format the bakes were made with (--compression)")
    f = sub.add_parser("feed", help="write the feed.json for packs uploaded to one release")
    f.add_argument("--base-url", required=True)
    f.add_argument("--out", type=Path, required=True)
    f.add_argument("packs", type=Path, nargs="+")
    i = sub.add_parser("install", help="install the levels of a pack that match this export")
    i.add_argument("--levels-dir", type=Path, required=True)
    i.add_argument("--probes-dir", type=Path, required=True)
    source = i.add_mutually_exclusive_group()
    source.add_argument("--pack", type=Path)
    source.add_argument(
        "--feed", help="a feed's URL (default: the lighting-packs release of the launcher's repository, then OpenRAC's)"
    )
    i.add_argument("--version")
    i.add_argument("--baker", type=Path, help="checks each level's bake profile against the pack's")
    args = parser.parse_args(argv)
    if args.command == "make":
        return make(args.version, args.levels_dir, args.probes_dir, args.out, args.texture_format)
    if args.command == "feed":
        return make_feed(args.base_url, args.packs, args.out)
    baker = args.baker if args.baker and args.baker.is_file() else None
    if args.pack:
        install(args.pack, args.levels_dir, args.probes_dir, baker, args.version)
        return 0
    if not levels_in(args.levels_dir):
        print("no exported levels yet: nothing to install")
        return 0
    if not args.version:
        parser.error("--feed needs --version")
    levels = levels_in(args.levels_dir)
    fmt = "r11g11b10"
    if baker is not None and levels:
        try:
            out = subprocess.run(
                [str(baker), "--level-dir", str(args.levels_dir / levels[0]), "--print-profile"],
                capture_output=True,
                check=True,
                timeout=120,
            )
            fmt = profile_format(out.stdout.decode("utf-8", "replace"))
        except (OSError, subprocess.SubprocessError):
            pass
    feeds = [args.feed] if args.feed else feeds_to_read()
    entry = None
    for feed in feeds:
        try:
            entry = choose_from_feed(feed, args.version, fmt)
        except (OSError, ValueError) as error:
            print(f"{feed}: not read ({error})")
        if entry is not None:
            break
    if entry is None:
        print(f"no lighting pack for {args.version} ({fmt}); the levels are left to the bake")
        return 0
    with tempfile.TemporaryDirectory(dir=args.probes_dir.parent if args.probes_dir.parent.is_dir() else None) as tmp:
        try:
            pack = fetch(entry["url"], entry.get("sha256"), Path(tmp))
        except (OSError, ValueError) as error:
            print(f"no lighting pack: {error}; the levels are left to the bake")
            return 0
        install(pack, args.levels_dir, args.probes_dir, baker, args.version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
