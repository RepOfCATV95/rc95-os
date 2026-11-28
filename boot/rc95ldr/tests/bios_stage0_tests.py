#!/usr/bin/env python3
"""Source-contract regression tests for the real-mode BIOS Stage-0 loader."""
import pathlib
import re
import unittest

STAGE0 = (pathlib.Path(__file__).resolve().parents[1] /
          "bios" / "arch" / "i386" / "stage0.S")


class BiosStage0Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = re.sub(
            r"hex\(([0-9a-f]+)\)", r"\1h",
            STAGE0.read_text(encoding="utf-8").lower())

    def test_requires_edd_extensions_and_checks_signature(self):
        self.assertRegex(self.source, r"mov\s+ah,\s*041h")
        self.assertRegex(self.source, r"int\s+013h")
        self.assertRegex(self.source, r"cmp\s+bx,\s*0a[a]55h")
        self.assertRegex(self.source, r"test\s+cl,\s*1")
        self.assertIn("no chs fallback", self.source)

    def test_reads_b95s_from_lba_one_with_edd(self):
        self.assertIn("read the single b95s header sector from lba 1", self.source)
        self.assertRegex(self.source,
                         r"mov\s+dword ptr\s+\[diskpacket \+ 8\],\s*1")
        self.assertRegex(self.source, r"mov\s+ah,\s*042h")

    def test_reads_stage1_from_lba_two_with_edd(self):
        # The source loads the immediate into EAX, then stores it in the DAP.
        self.assertRegex(self.source, r"mov\s+eax,\s*2\s+mov\s+dword ptr\s+\[diskpacket \+ 8\],\s*eax")
        self.assertRegex(self.source, r"mov\s+ah,\s*042h")
        self.assertRegex(self.source,
                         r"mov\s+word ptr\s+\[diskpacket \+ 2\],\s*ax")

    def test_validates_all_b95s_fixed_fields(self):
        self.assertRegex(self.source, r"mov\s+si,\s*0600h")
        for offset in (4, 6, 8, 12, 16, 24, 28):
            with self.subTest(offset=offset):
                self.assertIn(f"[si + {offset}]", self.source)
        self.assertIn("[si]", self.source)
        self.assertIn("05353942h", self.source)

    def test_bounds_stage1_to_cs_zero_address_window(self):
        self.assertRegex(self.source, r"cmp\s+eax,\s*08000h")
        self.assertRegex(self.source, r"cmp\s+eax,\s*10000h")
        self.assertRegex(self.source, r"cmp\s+eax,\s*0ffffh")
        self.assertRegex(self.source, r"cmp\s+eax,\s*64")

    def test_checks_exact_image_crc32_not_padding(self):
        self.assertIn("crc-32/ieee over imagelength bytes (exclude sector padding)", self.source)
        self.assertIn("0edb88320h", self.source)
        self.assertRegex(
            self.source,
            r"mov\s+si,\s*0614h\s+mov\s+edx,\s*dword ptr\s+\[si\]\s+cmp\s+eax,\s*edx")

    def test_far_transfers_to_declared_cs_zero_entry(self):
        self.assertRegex(
            self.source,
            r"mov\s+word ptr\s+\[entryfarpointer\],\s*ax")
        # FF /5 with a 16-bit absolute memory operand is JMP FAR m16:16.
        self.assertRegex(self.source, r"db\s+0ffh,\s*02eh")
        self.assertRegex(self.source, r"dw\s+offset\s+entryfarpointer")

    def test_boot_sector_signature_and_size_are_reserved(self):
        self.assertRegex(self.source, r"\.org\s+510")
        self.assertRegex(self.source, r"\.byte\s+055h,\s*0aah")


if __name__ == "__main__":
    unittest.main()
