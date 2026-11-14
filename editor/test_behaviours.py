"""behaviours.py: simple movements given to mobys, written beside the packed level for the mod."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import behaviours

GODOT = os.environ.get("OPENRAC_GODOT")
HERE = Path(__file__).parent


def scene(meta: str) -> str:
    return ('[gd_scene format=3]\n\n'
            '[ext_resource type="PackedScene" path="res://levels/level_05/mobys/moby_10.tscn" id="1_v"]\n\n'
            '[node name="Level" type="Node3D"]\n\n[node name="Game" type="Node3D" parent="."]\n\n'
            '[node name="Mobys" type="Node3D" parent="Game"]\n\n'
            '[node name="Vendor_0001" parent="Game/Mobys" instance=ExtResource("1_v")]\n'
            f'{meta}\n\n'
            '[node name="Vendor_0003" parent="Game/Mobys" instance=ExtResource("1_v")]\n'
            'metadata/rc1_index = 3\n')


class Behaviours(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.out = Path(self.temp.name) / "level_05"

    def tearDown(self):
        self.temp.cleanup()

    def test_written_with_defaults(self):
        text = scene('metadata/rc1_behaviour = "orbit"\nmetadata/rc1_behaviour_radius = 6.5\n'
                     'metadata/rc1_behaviour_sequence = 2')
        changes, problems = behaviours.write_behaviours(text, self.out, {"Vendor_0001": 181, "Vendor_0003": 3})
        self.assertEqual((changes, problems), (["Vendor_0001: behaviour orbit"], []))
        doc = json.loads((self.out / "behaviours.json").read_text())
        self.assertEqual(doc, {"format": 1, "mobys": [
            {"spawn_id": 181, "preset": "orbit", "radius": 6.5, "speed": 45.0, "sequence": 2}]})

    def test_problems(self):
        (self.out).mkdir(parents=True)
        (self.out / "behaviours.json").write_text("{}")
        _, problems = behaviours.write_behaviours(scene('metadata/rc1_behaviour = "dance"'), self.out, {"Vendor_0001": 1})
        self.assertIn("no preset 'dance'", problems[0])
        self.assertFalse((self.out / "behaviours.json").exists())
        _, problems = behaviours.write_behaviours(scene('metadata/rc1_behaviour = "spin"'), self.out, {})
        self.assertIn("no spawn id", problems[0])
        _, problems = behaviours.write_behaviours(
            scene('metadata/rc1_behaviour = "spin"\nmetadata/rc1_behaviour_speed = "fast"'), self.out, {"Vendor_0001": 1})
        self.assertIn("must be a number", problems[0])


@unittest.skipUnless(GODOT, "set OPENRAC_GODOT")
class Inspector(unittest.TestCase):
    """The Inspector's Behaviour section sets the metadata behaviours.py reads, in Godot's editor:
    OPENRAC_GODOT=/path/to/godot python3 -m unittest test_behaviours"""

    def test_section(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp)
            (project / "project.godot").write_text('[application]\nconfig/name="t"\n')
            shutil.copytree(HERE / "addons" / "openrac_library", project / "addons" / "openrac_library")
            shutil.copy(HERE / "test_behaviour_inspector.gd", project / "t.gd")
            out = project / "out.json"
            subprocess.run([GODOT, "--headless", "--editor", "--path", str(project), "--script", "res://t.gd",
                            "--", str(out)], capture_output=True, timeout=300)
            steps = json.loads(out.read_text())
        self.assertEqual(steps[:2], [True, False])
        self.assertEqual(steps[2], {"rc1_behaviour": "follow", "rc1_behaviour_speed": 4.0,
                                    "rc1_behaviour_distance": 3.0, "rc1_behaviour_range": 30.0,
                                    "rc1_behaviour_sequence": -1})
        # A new preset keeps the numbers both have (speed, the animation), the rest go.
        self.assertEqual(steps[3], {"rc1_behaviour": "orbit", "rc1_behaviour_radius": 4.0,
                                    "rc1_behaviour_speed": 7.5, "rc1_behaviour_sequence": 3})
        self.assertEqual(steps[4], 5)   # title, preset, radius, speed, animation
        self.assertEqual(steps[5], {})
        # What it sets packs as the mod reads it.
        node = {"name": "Vendor", "meta": {k[len("rc1_"):]: v for k, v in steps[3].items()}}
        self.assertEqual(behaviours.entry(node), {"preset": "orbit", "radius": 4.0, "speed": 7.5, "sequence": 3})


if __name__ == "__main__":
    unittest.main()
