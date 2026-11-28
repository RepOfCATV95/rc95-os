#!/usr/bin/env python3
"""Regression tests for the RC95LDR BIOS image packager."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

PACKER = pathlib.Path(__file__).resolve().parents[1] / "bios" / "tools" / "mkrc95biosimage.py"
B95S = struct.Struct("<IHHIIIIII")
B95I = struct.Struct("<IHHIIQ")
SEGMENT = struct.Struct("<QQQII")


class BiosImagePackerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.boot = self.root / "boot.bin"
        self.stage1 = self.root / "stage1.bin"
        self.payload = self.root / "rc95ldr.bin"
        self.output = self.root / "disk.img"
        self.cd_boot_sector = self.root / "cd-stage0.bin"
        self.cd_boot_image = self.root / "rc95ldr-eltorito.img"
        boot = bytearray(512)
        boot[510:512] = bytes((0x55, 0xAA))
        self.boot.write_bytes(boot)
        self.cd_boot_sector.write_bytes(boot)
        self.stage1.write_bytes(b"REALMODE-STAGE1\x00" * 37)
        self.payload.write_bytes(b"RC95LDR-protected-stage\x00" * 37)

    def tearDown(self):
        self.temp.cleanup()

    def command(self, *extra):
        return [
            sys.executable, str(PACKER),
            "--boot-sector", str(self.boot),
            "--stage1", str(self.stage1),
            "--output", str(self.output),
            "--entry", "0x100000",
            "--segment", f"0x100000:{self.payload}",
            *extra,
        ]

    def run_packer(self, *extra, expect=0):
        result = subprocess.run(self.command(*extra), capture_output=True, text=True)
        self.assertEqual(result.returncode, expect, result.stdout + result.stderr)
        return result

    def test_b95s_header_and_b95i_manifest_are_separate(self):
        self.run_packer()
        image = self.output.read_bytes()
        self.assertEqual(len(image) % 512, 0)
        self.assertEqual(image[:512][510:512], bytes((0x55, 0xAA)))

        signature, version, header_size, load, length, entry, crc, flags, reserved = B95S.unpack_from(image, 512)
        self.assertEqual((signature, version, header_size), (0x53353942, 1, 32))
        self.assertEqual((load, length, entry, flags, reserved),
                         (0x8000, len(self.stage1.read_bytes()), 0, 0, 0))
        self.assertEqual(crc, zlib.crc32(self.stage1.read_bytes()) & 0xFFFFFFFF)

        stage1_lba = 2
        stage1_sectors = (length + 511) // 512
        stage1_bytes = image[stage1_lba * 512:stage1_lba * 512 + length]
        self.assertEqual(stage1_bytes, self.stage1.read_bytes())
        manifest_lba = stage1_lba + stage1_sectors
        signature, version, header_size, count, flags, image_entry = B95I.unpack_from(
            image, manifest_lba * 512)
        self.assertEqual((signature, version, header_size, count, flags, image_entry),
                         (0x49353942, 1, 536, 1, 0, 0x100000))
        lba, byte_length, address, payload_crc, segment_flags = SEGMENT.unpack_from(
            image, manifest_lba * 512 + B95I.size)
        self.assertEqual((lba, address, segment_flags), (manifest_lba + 2, 0x100000, 0))
        payload = image[lba * 512:lba * 512 + byte_length]
        self.assertEqual(byte_length % 512, 0)
        self.assertEqual(payload_crc, zlib.crc32(payload) & 0xFFFFFFFF)
        self.assertTrue(payload.startswith(self.payload.read_bytes()))
        self.assertEqual(payload[len(self.payload.read_bytes()):],
                         bytes(byte_length - self.payload.stat().st_size))


    def test_builds_complete_eltorito_no_emulation_boot_image(self):
        self.run_packer("--cd-boot-sector", str(self.cd_boot_sector),
                        "--cd-boot-image", str(self.cd_boot_image))
        image = self.cd_boot_image.read_bytes()
        stage1 = self.stage1.read_bytes()
        padded = ((len(stage1) + 511) // 512) * 512
        self.assertEqual(image[:512], self.cd_boot_sector.read_bytes())
        fields = B95S.unpack_from(image, 512)
        self.assertEqual(fields[0:3], (0x53353942, 1, 32))
        self.assertEqual(fields[3:6], (0x8000, len(stage1), 0))
        self.assertEqual(fields[6], zlib.crc32(stage1) & 0xFFFFFFFF)
        self.assertEqual(image[1024:1024 + len(stage1)], stage1)
        self.assertEqual(len(image), 1024 + padded)
        self.assertEqual(len(image) % 512, 0)

    def test_requires_cd_boot_sector_and_output_together(self):
        result = subprocess.run(
            self.command("--cd-boot-sector", str(self.cd_boot_sector)),
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be supplied together", result.stderr)

    def test_rejects_nonstandard_cd_stage1_load_address(self):
        result = subprocess.run(
            self.command("--stage1-load-address", "0x9000",
                         "--cd-boot-sector", str(self.cd_boot_sector),
                         "--cd-boot-image", str(self.cd_boot_image)),
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Stage-1 at 0x8000", result.stderr)

    def test_rejects_non_512_byte_bios_sector_size(self):
        for sector_size in (1024, 2048, 4096):
            with self.subTest(sector_size=sector_size):
                boot = bytearray(sector_size)
                boot[510:512] = bytes((0x55, 0xAA))
                self.boot.write_bytes(boot)
                result = subprocess.run(self.command("--sector-size", str(sector_size)),
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires 512-byte sectors", result.stderr)
                self.assertFalse(self.output.exists())

    def test_accepts_maximum_cs_zero_stage1_extent(self):
        stage1 = bytes((i * 17) & 0xFF for i in range(0x8000))
        self.stage1.write_bytes(stage1)
        self.run_packer("--stage1-entry-offset", "0x7fff")
        image = self.output.read_bytes()
        fields = B95S.unpack_from(image, 512)
        self.assertEqual(fields[3:6], (0x8000, 0x8000, 0x7fff))
        self.assertEqual(fields[6], zlib.crc32(stage1) & 0xFFFFFFFF)
        self.assertEqual(image[2 * 512:2 * 512 + len(stage1)], stage1)

    def test_rejects_stage1_extent_one_byte_past_cs_zero_limit(self):
        self.stage1.write_bytes(bytes(0x8001))
        result = subprocess.run(self.command(), capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("end at or below 0x10000", result.stderr)
        self.assertFalse(self.output.exists())

    def test_rejects_stage1_entry_above_16_bit_ip(self):
        self.stage1.write_bytes(bytes(0x8000))
        result = subprocess.run(self.command("--stage1-entry-offset", "0x8000"),
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("entry offset", result.stderr)
        self.assertFalse(self.output.exists())

    def test_rejects_missing_boot_signature(self):
        self.boot.write_bytes(bytes(512))
        self.run_packer(expect=1)
        self.assertFalse(self.output.exists())

    def test_rejects_stage1_entry_outside_image(self):
        result = subprocess.run(
            self.command("--stage1-entry-offset", str(len(self.stage1.read_bytes()))),
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("entry offset", result.stderr)
        self.assertFalse(self.output.exists())

    def test_rejects_stage1_below_reserved_floor(self):
        result = subprocess.run(self.command("--stage1-load-address", "0x7000"),
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("at least 0x8000", result.stderr)

    def test_rejects_stage1_outside_cs_zero_window(self):
        result = subprocess.run(self.command("--stage1-load-address", "0x9fff0"),
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("0x10000", result.stderr)

    def test_rejects_entry_outside_manifest_segments(self):
        command = self.command("--entry", "0x200000")
        command[command.index("--entry") + 1] = "0x200000"
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("entry address", result.stderr)
        self.assertFalse(self.output.exists())

    def test_rejects_overlapping_manifest_load_ranges(self):
        second = self.root / "second.bin"
        second.write_bytes(b"second")
        result = subprocess.run(self.command("--segment", f"0x100000:{second}"),
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("overlapping physical", result.stderr)

    def test_rejects_empty_manifest_segment(self):
        empty = self.root / "empty.bin"
        empty.write_bytes(b"")
        result = subprocess.run(self.command("--segment", f"0x100000:{empty}"),
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("empty image", result.stderr)


if __name__ == "__main__":
    unittest.main()
