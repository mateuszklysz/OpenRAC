import json
import tempfile
import unittest
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
            pack = root / "pack.zip"
            self.assertEqual(
                lighting_pack.make("rac1/ntsc", ours / "levels", ours / "probes", pack), 0
            )
            manifest = json.loads(__import__("zipfile").ZipFile(pack).read("pack.json"))
            self.assertEqual(sorted(manifest["levels"]), ["level_01", "level_02"])
            self.assertNotIn("probes/level_01/batch.log", manifest["levels"]["level_01"]["files"])

            # The player's export: level 01 the same, level 02 exported differently.
            level(theirs / "levels", "level_01", manifest='{"n": "level_01"}')
            level(theirs / "levels", "level_02", manifest='{"n": "other"}')
            installed = lighting_pack.install(
                pack, theirs / "levels", theirs / "probes", None, "rac1/ntsc"
            )
            self.assertEqual(installed, 1)
            self.assertEqual((theirs / "probes/level_01/sparse.orpg").read_bytes(), b"probes")
            self.assertEqual((theirs / "levels/level_01/water.bin").read_bytes(), b"water")
            self.assertFalse((theirs / "probes/level_02").exists())

            with self.assertRaises(ValueError):
                lighting_pack.install(pack, theirs / "levels", theirs / "probes", None, "rac1/pal")

    def test_format_from_profile(self):
        self.assertEqual(lighting_pack.profile_format('{"texture_compression":"BC6H UFLOAT"}'), "bc6h")
        self.assertEqual(lighting_pack.profile_format('{"texture_compression":"ASTC 8x8 HDR"}'), "astc")
        self.assertEqual(lighting_pack.profile_format('{"texture_compression":"R11G11B10"}'), "r11g11b10")


if __name__ == "__main__":
    unittest.main()
