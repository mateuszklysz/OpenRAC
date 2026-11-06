import json
import tempfile
import unittest
import zipfile
from pathlib import Path

import lighting_pack


def level(root: Path, name: str, manifest: str = "{}") -> Path:
    folder = root / name
    (folder / "bake-inputs").mkdir(parents=True)
    (folder / "manifest.json").write_text(manifest)
    (folder / "probe-bake.json").write_text('{"version": 1}')
    (folder / "collision.glb").write_bytes(b"glb")
    return folder


def bake(probes: Path, levels: Path, name: str, verified: bool = True) -> None:
    folder = probes / name
    folder.mkdir(parents=True)
    (folder / "sparse.orpg").write_bytes(b"probes")
    (folder / "batch.log").write_text("diagnostics")
    (folder / "bake-result.json").write_text(json.dumps({"verified": verified}))
    stamp = {
        "version": 5,
        "manifest_sha256": lighting_pack.sha256_file(levels / name / "manifest.json"),
        "baker_profile_sha256": "abc",
    }
    (folder / "openrac-bake.json").write_text(json.dumps(stamp))
    (levels / name / "water.bin").write_bytes(b"water")


class TestLightingPack(unittest.TestCase):
    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ours, theirs = root / "ours", root / "theirs"
            for name in ("level_01", "level_02", "level_03"):
                level(ours / "levels", name, manifest=f'{{"n": "{name}"}}')
            bake(ours / "probes", ours / "levels", "level_01")
            bake(ours / "probes", ours / "levels", "level_02")
            bake(ours / "probes", ours / "levels", "level_03", verified=False)
            packs = root / "packs"
            self.assertEqual(
                lighting_pack.make("rac1/ntsc", ours / "levels", ours / "probes", packs, "bc6h"), 0
            )
            made = sorted(p.name for p in packs.iterdir())
            self.assertEqual(made, ["rac1-ntsc-bc6h-level_01.zip", "rac1-ntsc-bc6h-level_02.zip"])
            with zipfile.ZipFile(packs / made[0]) as z:
                manifest = json.loads(z.read("pack.json"))
                self.assertEqual(z.getinfo("probes/level_01/sparse.orpg").compress_type, zipfile.ZIP_LZMA)
            self.assertEqual((manifest["level"], manifest["texture_format"]), ("level_01", "bc6h"))
            self.assertNotIn("probes/level_01/batch.log", manifest["files"])

            feed_path = root / "feed.json"
            lighting_pack.make_feed("https://example.invalid/dl/", sorted(packs.iterdir()), feed_path)
            # The feed's URLs point at the local files for the test.
            feed = json.loads(feed_path.read_text())
            for entry in feed["packs"]:
                entry["url"] = (packs / entry["url"].rsplit("/", 1)[1]).as_uri()
            feed_path.write_text(json.dumps(feed))
            chosen = lighting_pack.read_feed(feed_path.as_uri(), "rac1/ntsc", "bc6h")
            self.assertEqual(sorted(chosen), ["level_01", "level_02"])
            self.assertEqual(lighting_pack.read_feed(feed_path.as_uri(), "rac1/ntsc", "astc"), {})

            # The player's export: level 01 the same, level 02 exported differently.
            level(theirs / "levels", "level_01", manifest='{"n": "level_01"}')
            level(theirs / "levels", "level_02", manifest='{"n": "other"}')
            installed = lighting_pack.install_from_feed(chosen, theirs / "levels", theirs / "probes", None, "rac1/ntsc")
            self.assertEqual(installed, 1)
            self.assertEqual((theirs / "probes/level_01/sparse.orpg").read_bytes(), b"probes")
            self.assertEqual((theirs / "levels/level_01/water.bin").read_bytes(), b"water")
            self.assertFalse((theirs / "probes/level_02").exists())
            self.assertEqual(sorted(p.name for p in (theirs / "probes").iterdir()), ["level_01"])
            # Installed and current: nothing is downloaded again.
            self.assertEqual(
                lighting_pack.install_from_feed(chosen, theirs / "levels", theirs / "probes", None, "rac1/ntsc"), 0
            )

            with self.assertRaises(ValueError):
                lighting_pack.install(packs / made[0], theirs / "levels", theirs / "probes", None, "rac1/pal")

    def test_needed_to_play(self):
        self.assertTrue(lighting_pack.needed_to_play(Path("region-001.orpg")))
        self.assertTrue(lighting_pack.needed_to_play(Path("interior-lights-report.json")))
        self.assertFalse(lighting_pack.needed_to_play(Path("region-001.orpg.txt")))
        self.assertFalse(lighting_pack.needed_to_play(Path("interior-input.json")))
        self.assertFalse(lighting_pack.needed_to_play(Path("adaptive-reference/region-001.orpg")))

    def test_format_from_profile(self):
        self.assertEqual(lighting_pack.profile_format('{"texture_compression":"BC6H UFLOAT"}'), "bc6h")
        self.assertEqual(lighting_pack.profile_format('{"texture_compression":"ASTC 8x8 HDR"}'), "astc")
        self.assertEqual(lighting_pack.profile_format('{"texture_compression":"R11G11B10"}'), "r11g11b10")


if __name__ == "__main__":
    unittest.main()
