"""foreign_classes.py: a moby of another level's class, placed with its own logic."""

import tempfile
import unittest
from pathlib import Path

import foreign_classes
import gameplay as gp
from foreign_classes import Donor
from mobys import FIELDS, RECORD, moby_instances
from pack import pack_level
from test_pack import gameplay, scene

FOREIGN = ('[ext_resource type="PackedScene" path="res://library/rac1/ntsc/level_03/mobys/moby_909.glb" id="big_al"]\n\n'
           '[node name="BigAl_rac1_ntsc_L03" parent="Game/Mobys" instance=ExtResource("big_al")]\n'
           'transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 4, 5, 6)\n'
           'metadata/rc1_foreign_level = 3\n')


class ForeignClasses(unittest.TestCase):
    def test_the_scenes_imported_classes(self):
        self.assertEqual(foreign_classes.wanted(FOREIGN), ({909: 3}, []))
        self.assertEqual(foreign_classes.wanted(FOREIGN.replace("metadata/rc1_foreign_level = 3\n", "")), ({}, []))

    def test_a_class_from_two_levels_or_a_bad_level_is_reported(self):
        second = FOREIGN.split("\n\n", 1)[1].replace("BigAl_rac1_ntsc_L03", "BigAl_2").replace("= 3", "= 4")
        found, problems = foreign_classes.wanted(FOREIGN + "\n" + second)
        self.assertEqual(found, {909: 3})
        self.assertIn("BigAl_2: class 909 is already imported from level 03", problems[0])
        found, problems = foreign_classes.wanted(FOREIGN.replace("= 3", '= "Kerwan"'))
        self.assertEqual(found, {})
        self.assertIn("must be a level number", problems[0])

    def test_a_class_from_the_same_level_or_no_level_is_refused(self):
        foreign_classes.check(5, 3, 909)
        foreign_classes.check(3, 5, 909)         # a bigger program comes in too
        with self.assertRaises(ValueError):
            foreign_classes.check(3, 3, 909)
        with self.assertRaises(ValueError):
            foreign_classes.check(3, 19, 909)

    def test_the_moby_takes_after_its_levels_moby_with_its_own_pvars(self):
        data = gameplay([{"x": 1.0}, {"x": 2.0}])
        record = dict(zip(FIELDS, RECORD.unpack(gameplay([{"class_id": 909, "group": 4, "spawn_id": 77}])[0x110:0x110 + 0x78])))
        donor = Donor(3, 909, record, bytes(range(16)) + b"\x05\x00\x00\x00", [], [16], [(4, b"shared!")])
        out, changes, problems = pack_level(5, data, scene(data) + "\n" + FOREIGN, None, None, None, {909: donor})
        self.assertEqual(problems, [])
        self.assertIn("BigAl_rac1_ntsc_L03: added, class 909 from level 03", changes)
        added = moby_instances(out)[-1]
        self.assertEqual(added["class_id"], 909)
        self.assertEqual(added["fields"]["group"], -1)
        self.assertNotEqual(added["fields"]["pvar_index"], -1)
        self.assertEqual(added["matrix"][12:15], [4.0, 5.0, 6.0])
        self.assertEqual(added["scale"], 1.0)
        blob, records = gp.shared(out)
        self.assertEqual(records, [(added["fields"]["pvar_index"], 4, 0)])
        self.assertEqual(blob[:7], b"shared!")

    def test_its_scale_is_the_nodes_over_its_class_scale(self):
        data = gameplay([{"x": 1.0}])
        record = dict(zip(FIELDS, RECORD.unpack(gameplay([{"class_id": 909}])[0x110:0x110 + 0x78])))
        node = FOREIGN.replace("Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1,", "Transform3D(0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.5,")
        node += "metadata/rc1_foreign_class_scale = 0.25\n"
        out, _, problems = pack_level(5, data, scene(data) + "\n" + node, None, None, None, {909: Donor(3, 909, record)})
        self.assertEqual(problems, [])
        self.assertAlmostEqual(moby_instances(out)[-1]["scale"], 2.0, places=5)

    def test_writes_the_list_and_the_blobs_and_clears_them(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "level_05"
            done = foreign_classes.write(out, {909: (3, b"\x01" * 0x60)})
            self.assertEqual(done, ["class 909: imported from level 03"])
            self.assertEqual((out / "classes.tsv").read_text().splitlines()[1], "909\t3")
            self.assertEqual((out / "classes" / "909.bin").read_bytes(), b"\x01" * 0x60)
            foreign_classes.write(out, {})
            self.assertFalse((out / "classes.tsv").exists())
            self.assertFalse((out / "classes" / "909.bin").exists())


if __name__ == "__main__":
    unittest.main()
