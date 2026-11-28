#!/usr/bin/env python3
"""Source-contract tests for the in-memory El Torito Stage-0 bootstrap."""
import pathlib
import re
import unittest

STAGE0 = (pathlib.Path(__file__).resolve().parents[1] /
          "bios" / "arch" / "i386" / "stage0_cd.S")


class BiosCdStage0Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = re.sub(r"hex\(([0-9a-f]+)\)", r"\1h", STAGE0.read_text(encoding="utf-8").lower())

    def test_normalizes_default_eltorito_load_segment(self):
        self.assertIn("normalize el torito's default cs=07c0", self.source)
        self.assertIn(".word 07c06h", self.source)
        self.assertIn(".word 0000h", self.source)

    def test_consumes_embedded_b95s_and_stage1_not_disk_lbas(self):
        for address in ("07e00h", "07e04h", "07e06h", "07e08h",
                        "07e0ch", "07e10h", "07e14h", "07e18h", "07e1ch"):
            self.assertIn(f"[{address}]", self.source)
        self.assertIn("physical 08000h", self.source)
        self.assertNotRegex(self.source, r"mov\s+ah,\s*042h")

    def test_checks_stage1_address_size_entry_and_crc(self):
        self.assertIn("05353942h", self.source)
        self.assertIn("08000h", self.source)
        self.assertIn("0edb88320h", self.source)
        self.assertRegex(self.source, r"cmp\s+eax,\s+dword ptr \[07e14h\]")

    def test_far_jumps_to_stage1_with_cs_zero(self):
        self.assertRegex(self.source, r"jmp\s+(?:fword ptr|lxdtprefix)\s+\[entryfarpointer\]")

    def test_sector_signature_and_size(self):
        self.assertRegex(self.source, r"\.org\s+510")
        self.assertRegex(self.source, r"\.byte\s+055h,\s*0aah")


if __name__ == "__main__":
    unittest.main()
