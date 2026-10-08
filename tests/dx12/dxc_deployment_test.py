import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location("deployment",
    pathlib.Path(__file__).resolve().parents[2] / "tools/check_dxc_deployment.py")
deployment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deployment)


class DeploymentTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = pathlib.Path(self.temporary.name)
        (self.source / "LICENSE.txt").write_text("fixture notice")
        self.image = bytearray(512)
        self.image[:2] = b"MZ"
        struct.pack_into("<I", self.image, 60, 64)
        self.image[64:68] = b"PE\0\0"
        struct.pack_into("<H", self.image, 68, 0x8664)
        struct.pack_into("<HHH", self.image, 84, 240, 0x2000, 0x20B)
        self.publish()

    def publish(self):
        for name in ("dxcompiler.dll", "dxil.dll"):
            (self.source / name).write_bytes(self.image)

    def test_matching_architecture(self):
        deployment.check(self.source, "x86_64")

    def test_wrong_architecture(self):
        with self.assertRaisesRegex(ValueError, "incompatible PE"):
            deployment.check(self.source, "x86")

    def test_missing_notice(self):
        (self.source / "LICENSE.txt").write_text("")
        with self.assertRaisesRegex(ValueError, "nonempty"):
            deployment.check(self.source, "x86_64")

    def test_bad_header(self):
        struct.pack_into("<I", self.image, 60, 0xFFFFFFFF)
        self.publish()
        with self.assertRaisesRegex(ValueError, "invalid PE"):
            deployment.check(self.source, "x86_64")

    def test_not_a_dll(self):
        struct.pack_into("<H", self.image, 86, 0)
        self.publish()
        with self.assertRaisesRegex(ValueError, "invalid DLL"):
            deployment.check(self.source, "x86_64")

    def test_relative_directory(self):
        with self.assertRaisesRegex(ValueError, "absolute"):
            deployment.check("relative", "x86_64")


if __name__ == "__main__":
    unittest.main()
