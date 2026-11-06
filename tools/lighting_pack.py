#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Ready-made Enhanced graphics lighting: packs of probe bakes, so a player's device need not bake.

The probe bake (launcher action probe-bake, openrac-enhanced-baker) lights each exported level from
the player's own disc. It takes minutes of every core, which a phone should not spend. A lighting
pack is one level's finished bake of one version (rac1/ntsc, ...) in one probe texture format, made
once from the same disc and offered as a download; a level at a time, so a player downloads only the
levels whose bake is missing and a cut download costs one level. The device installs a level from it only when its own
export is the one the pack was baked from (the same input files, byte for byte) and its baker
names the same bake (the same --print-profile); any level that does not match is left to the bake.

    lighting_pack.py make --version rac1/ntsc --levels-dir LEVELS --probes-dir PROBES --out-dir DIR
        --texture-format bc6h|astc|r11g11b10
    lighting_pack.py feed --base-url URL --out feed.json DIR/*.zip
    lighting_pack.py install --levels-dir LEVELS --probes-dir PROBES [--pack PACK.zip ... | --feed URL]
        [--version rac1/ntsc] [--baker BAKER]

A level pack (LZMA zip) holds pack.json, probes/level_NN/... (the bake's output folder, less the
baker's own working files) and levels/level_NN/... (what the bake writes beside the export: water,
ocean, the lighting environments). pack.json lists the input files it requires (with their SHA-256,
or null for a file that must not be there), the files it installs (with their SHA-256) and the
baker profile's SHA-256.

The probe textures are compressed for the GPU that baked them (BC6H on desktop cards, ASTC HDR on
phones and Apple, else R11G11B10); --print-profile names the format, so the profile check also
picks the pack of the right format. A feed (--feed) lists the level packs:
{"format": 2, "packs": [{"version": "rac1/ntsc", "format": "bc6h", "level": "level_01",
"manifest_sha256": "...", "baker_profile_sha256": "...", "url": "...", "size": N, "sha256": "..."}]}.
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

FORMAT = 2
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


def pack_name(version: str, texture_format: str, level: str) -> str:
    """A level pack's file name: rac1-ntsc-astc-level_01.zip."""
    return f"{version.replace('/', '-')}-{texture_format}-{level}.zip"


def make(version: str, levels_dir: Path, probes_dir: Path, out_dir: Path, texture_format: str) -> int:
    """Packs each level whose bake is finished and verified (its stamp is there) into its own file."""
    out_dir.mkdir(parents=True, exist_ok=True)
    made = 0
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
        sources: dict[str, Path] = {}
        for path in sorted(p for p in baked.rglob("*") if p.is_file()):
            if needed_to_play(path.relative_to(baked)):
                sources[f"probes/{name}/{path.relative_to(baked).as_posix()}"] = path
        for rel in LEVEL_OUTPUTS:
            if (level / rel).is_file():
                sources[f"levels/{name}/{rel}"] = level / rel
        manifest = {
            "format": FORMAT,
            "version": version,
            "texture_format": texture_format,
            "level": name,
            "requires": {i: (sha256_file(level / i) if (level / i).is_file() else None) for i in INPUTS},
            "files": {arc: sha256_file(path) for arc, path in sources.items()},
            "baker_profile_sha256": stamp.get("baker_profile_sha256"),
        }
        out = out_dir / pack_name(version, texture_format, name)
        partial = out.with_name(out.name + ".partial")
        # LZMA: about a fifth smaller than deflate on probe grids; Python reads it everywhere.
        with zipfile.ZipFile(partial, "w", zipfile.ZIP_LZMA) as pack:
            pack.writestr("pack.json", json.dumps(manifest, indent=1, sort_keys=True))
            for arc, path in sources.items():
                pack.write(path, arc)
        partial.replace(out)
        made += 1
        print(f"{name}: {len(sources)} files, {out.stat().st_size / 1e6:.1f} MB")
    return 0 if made else 1


def make_feed(base_url: str, packs: list[Path], out: Path) -> int:
    """Writes the feed that lists level packs uploaded under base_url (a release's download folder)."""
    entries = []
    for path in sorted(packs):
        with zipfile.ZipFile(path) as pack:
            manifest = json.loads(pack.read("pack.json"))
        entries.append(
            {
                "version": manifest["version"],
                "format": manifest["texture_format"],
                "level": manifest["level"],
                "manifest_sha256": manifest["requires"]["manifest.json"],
                "baker_profile_sha256": manifest["baker_profile_sha256"],
                "url": f"{base_url.rstrip('/')}/{path.name}",
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    out.write_text(json.dumps({"format": FORMAT, "packs": entries}, indent=1) + "\n")
    print(f"{out}: {len(entries)} level pack(s), {sum(e['size'] for e in entries) / 1e6:.1f} MB")
    return 0


def fetch(url: str, sha256: str | None, target: Path) -> Path:
    with urllib.request.urlopen(url, timeout=60) as response, target.open("wb") as f:
        shutil.copyfileobj(response, f, 1 << 20)
    if sha256 and sha256_file(target) != sha256.lower():
        raise ValueError(f"{url}: the download's SHA-256 is not the feed's")
    return target


def read_feed(feed_url: str, version: str, fmt: str) -> dict[str, dict]:
    """The feed's level packs of one version and format, by level."""
    with urllib.request.urlopen(feed_url, timeout=30) as response:
        feed = json.load(response)
    if feed.get("format") != FORMAT:
        return {}
    return {
        e["level"]: e
        for e in feed.get("packs", [])
        if e.get("version") == version and e.get("format") == fmt and isinstance(e.get("level"), str)
    }


def already_baked(probes_dir: Path, level: Path, profile_sha256: str | None) -> bool:
    """Whether the level's bake here is current and of the pack's profile: nothing to download."""
    try:
        stamp = json.loads((probes_dir / level.name / STAMP).read_text())
        verified = json.loads((probes_dir / level.name / "bake-result.json").read_text()).get("verified")
    except (OSError, ValueError):
        return False
    return (
        verified is True
        and stamp.get("manifest_sha256") == sha256_file(level / "manifest.json")
        and stamp.get("baker_profile_sha256") == profile_sha256
    )


def feeds_to_read() -> list[str]:
    repository = os.environ.get("OPENRAC_REPOSITORY", "").strip()
    order = [r for r in (repository, UPSTREAM) if r.count("/") == 1]
    return [FEED.format(r) for r in dict.fromkeys(order)]


def install(pack_path: Path, levels_dir: Path, probes_dir: Path, baker: Path | None, version: str | None) -> bool:
    """Installs a level pack if it matches this export (and baker); returns whether it did."""
    with zipfile.ZipFile(pack_path) as pack:
        entry = json.loads(pack.read("pack.json"))
        if entry.get("format") != FORMAT:
            raise ValueError("the pack is of another format")
        if version and entry.get("version") != version:
            raise ValueError(f"the pack is for {entry.get('version')}, not {version}")
        name = str(entry["level"])
        if not name.startswith("level_") or not name[6:].isdigit():
            raise ValueError(f"{name}: not a level")
        level = levels_dir / name
        if not (level / "manifest.json").is_file():
            return False
        wrong = [
            rel
            for rel, digest in entry["requires"].items()
            if (sha256_file(level / rel) if (level / rel).is_file() else None) != digest
        ]
        if wrong:
            print(f"{name}: this export differs from the pack's ({', '.join(wrong)}), left to the bake")
            return False
        if baker is not None and baker_profile(baker, level) != entry.get("baker_profile_sha256"):
            print(f"{name}: this baker makes another bake than the pack's, left to the bake")
            return False
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
    print(f"{name}: installed from the pack")
    return True


def install_from_feed(
    feed: dict[str, dict], levels_dir: Path, probes_dir: Path, baker: Path | None, version: str
) -> int:
    """Downloads and installs, one level at a time, each pack whose export matches and that is not
    baked here already; a failed level is left to the bake. Returns how many were installed."""
    installed = 0
    for name in levels_in(levels_dir):
        entry = feed.get(name)
        level = levels_dir / name
        if entry is None:
            continue
        if entry.get("manifest_sha256") != sha256_file(level / "manifest.json"):
            print(f"{name}: this export differs from the pack's, left to the bake")
            continue
        if already_baked(probes_dir, level, entry.get("baker_profile_sha256")):
            continue
        print(f"{name}: downloading {int(entry.get('size') or 0) / 1e6:.1f} MB", flush=True)
        probes_dir.mkdir(parents=True, exist_ok=True)
        target = probes_dir / f".{name}.download.zip"
        try:
            fetch(entry["url"], entry.get("sha256"), target)
            installed += install(target, levels_dir, probes_dir, baker, version)
        except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
            print(f"{name}: no lighting pack ({error}), left to the bake")
        finally:
            target.unlink(missing_ok=True)
    return installed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("make", help="pack each of a version's finished bakes into its own file")
    m.add_argument("--version", required=True)
    m.add_argument("--levels-dir", type=Path, required=True)
    m.add_argument("--probes-dir", type=Path, required=True)
    m.add_argument("--out-dir", type=Path, required=True)
    m.add_argument(
        "--texture-format",
        choices=("bc6h", "astc", "r11g11b10"),
        required=True,
        help="the probe texture format the bakes were made with (--compression)",
    )
    f = sub.add_parser("feed", help="write the feed.json for level packs uploaded to one release")
    f.add_argument("--base-url", required=True)
    f.add_argument("--out", type=Path, required=True)
    f.add_argument("packs", type=Path, nargs="+")
    i = sub.add_parser("install", help="install the level packs that match this export")
    i.add_argument("--levels-dir", type=Path, required=True)
    i.add_argument("--probes-dir", type=Path, required=True)
    source = i.add_mutually_exclusive_group()
    source.add_argument("--pack", type=Path, nargs="+")
    source.add_argument(
        "--feed", help="a feed's URL (default: the lighting-packs release of the launcher's repository, then OpenRAC's)"
    )
    i.add_argument("--version")
    i.add_argument("--baker", type=Path, help="checks each level's bake profile against the pack's")
    args = parser.parse_args(argv)
    if args.command == "make":
        return make(args.version, args.levels_dir, args.probes_dir, args.out_dir, args.texture_format)
    if args.command == "feed":
        return make_feed(args.base_url, args.packs, args.out)
    baker = args.baker if args.baker and args.baker.is_file() else None
    if args.pack:
        installed = sum(install(p, args.levels_dir, args.probes_dir, baker, args.version) for p in args.pack)
        print(f"installed {installed} level(s)")
        return 0
    levels = levels_in(args.levels_dir)
    if not levels:
        print("no exported levels yet: nothing to install")
        return 0
    if not args.version:
        parser.error("--feed needs --version")
    fmt = "r11g11b10"
    if baker is not None:
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
    feed: dict[str, dict] = {}
    for url in [args.feed] if args.feed else feeds_to_read():
        try:
            feed = read_feed(url, args.version, fmt)
        except (OSError, ValueError) as error:
            print(f"{url}: not read ({error})")
        if feed:
            break
    if not feed:
        print(f"no lighting packs for {args.version} ({fmt}); the levels are left to the bake")
        return 0
    installed = install_from_feed(feed, args.levels_dir, args.probes_dir, baker, args.version)
    print(f"installed {installed} level(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
