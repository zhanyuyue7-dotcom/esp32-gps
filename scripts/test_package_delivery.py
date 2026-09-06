import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).with_name("package_delivery.py")


class DeliveryTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), "delivery packager must exist")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "docs").mkdir()
        (self.root / "docs/wiring.md").write_text("# 接线手册\nSPI GPIO9–12\n", encoding="utf-8")
        (self.root / "third_party/atkinson-hyperlegible").mkdir(parents=True)
        (self.root / "third_party/atkinson-hyperlegible/OFL.txt").write_text(
            "SIL OPEN FONT LICENSE", encoding="utf-8"
        )
        (self.root / "firmware").mkdir()
        files = []
        for name, offset in [("bootloader.bin", 0), ("partition-table.bin", 32768), ("app.bin", 65536)]:
            data = name.encode() * 8
            (self.root / "firmware" / name).write_bytes(data)
            files.append({"path": "firmware/" + name, "offset": offset,
                          "sha256": hashlib.sha256(data).hexdigest()})
        self.manifest = {"name": "test-board", "version": "delivery-20260906",
                         "chip": "esp32s3", "files": files}
        self.save_manifest()

    def save_manifest(self):
        (self.root / "delivery.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def run_package(self):
        return subprocess.run([sys.executable, str(SCRIPT), "--root", str(self.root)],
                              capture_output=True, text=True, encoding="utf-8")

    def test_only_manual_and_three_exact_binaries(self):
        (self.root / "firmware/private.log").write_text("must not ship", encoding="utf-8")
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        archive = self.root / "dist/test-board-delivery-20260906.zip"
        with zipfile.ZipFile(archive) as zipped:
            self.assertIsNone(zipped.testzip())
            self.assertEqual(set(zipped.namelist()),
                             {"接线手册.md", "bootloader.bin", "partition-table.bin", "app.bin"})
            manual = zipped.read("接线手册.md").decode("utf-8")
            for expected in ["SPI GPIO9", "0x8000", "0x10000", "SIL OPEN FONT LICENSE"]:
                self.assertIn(expected, manual)
            for part in self.manifest["files"]:
                self.assertEqual(hashlib.sha256(zipped.read(Path(part["path"]).name)).hexdigest(),
                                 part["sha256"])
        before = archive.read_bytes()
        self.assertEqual(self.run_package().returncode, 0)
        self.assertEqual(before, archive.read_bytes(), "package must be deterministic")

    def test_modified_firmware_is_rejected(self):
        (self.root / "firmware/app.bin").write_bytes(b"wrong release")
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA256 mismatch", result.stderr)
        self.assertFalse((self.root / "dist/test-board-delivery-20260906.zip").exists())

    def test_path_escape_is_rejected(self):
        self.manifest["files"][0]["path"] = "../outside.bin"
        self.save_manifest()
        result = self.run_package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside project", result.stderr)

if __name__ == "__main__":
    unittest.main()

