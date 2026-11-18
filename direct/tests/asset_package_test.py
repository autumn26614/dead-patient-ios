"""Archive integrity, reproducibility, and hostile-path checks; stdlib only."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("assets", Path(__file__).resolve().parents[2] / "tools/package_game_assets.py")
assets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assets)


class AssetPackageTest(unittest.TestCase):
    def test_exact_reproducible_split_round_trip_and_corruption(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "original"
            (source / "BGM").mkdir(parents=True)
            (source / "chap1").mkdir()
            (source / "BGM/a.pak").write_bytes(bytes(range(256)) * 2200)
            (source / "chap1/project.dat").write_bytes(bytes(reversed(range(256))) * 2200)
            (source / "empty").write_bytes(b"")
            first = assets.package(source, root / "package-a", 1024 ** 2)
            second = assets.package(source, root / "package-b", 1024 ** 2)
            self.assertEqual(first, second)
            self.assertEqual(len(first["parts"]), 2)
            self.assertEqual(first["file_count"], 3)
            for item in first["parts"]:
                self.assertEqual((root / "package-a" / item["name"]).read_bytes(),
                                 (root / "package-b" / item["name"]).read_bytes())
            assets.check_package(root / "package-a", root / "extracted")
            assets.verify_tree(root / "extracted", root / "package-a" / assets.MANIFEST)
            self.assertEqual((root / "extracted/BGM/a.pak").read_bytes(), (source / "BGM/a.pak").read_bytes())
            with self.assertRaises(ValueError):
                assets.check_package(root / "package-a", root / "extracted")
            archive = root / "package-a" / first["parts"][0]["name"]
            data = bytearray(archive.read_bytes())
            data[len(data) // 2] ^= 1
            archive.write_bytes(data)
            with self.assertRaises(ValueError):
                assets.check_package(root / "package-a", root / "bad-extraction")
            self.assertFalse((root / "bad-extraction").exists())

    def test_rejects_ambiguous_and_unsafe_paths(self):
        for names in (["BGM/a", "bgm/b"], ["x", "x/y"], ["x/y", "x"],
                      ["a", "a"], ["../escape"], ["/absolute"], ["C:/drive"],
                      ["a\\b"], ["a/./b"], ["a//b"], ["a\x00b"],
                      ["café/a", "cafe\u0301/b"]):
            with self.subTest(names=names), self.assertRaises(ValueError):
                assets.check_names(names)

    def test_manifest_mutation_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "original"
            source.mkdir()
            (source / "file").write_bytes(b"unchanged")
            assets.package(source, root / "package", 1024 ** 2)
            manifest = root / "package" / assets.MANIFEST
            record = json.loads(manifest.read_text())
            record["files"][0]["path"] = "../outside"
            manifest.write_text(json.dumps(record))
            with self.assertRaises(ValueError):
                assets.check_package(root / "package", root / "extracted")
            self.assertFalse((root / "extracted").exists())


if __name__ == "__main__":
    unittest.main()
