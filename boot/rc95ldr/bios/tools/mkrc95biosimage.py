#!/usr/bin/env python3
"""Build an RC95LDR BIOS image with a low-memory bootstrap and B95I payload manifest.

Disk layout (logical sectors):
  LBA 0       BIOS boot sector (Stage 0)
  LBA 1       B95S Stage-1 bootstrap header (32 bytes, sector padded)
  LBA 2..     Stage-1 image, loaded below 1 MiB by Stage 0
  following  B95I manifest (536 bytes, sector padded)
  following  high-memory payload extents described by B95I

Stage 0 consumes B95S. Rc95LdrBiosLoadManifestImage consumes B95I only.
"""
import argparse
import pathlib
import struct
import zlib

IMAGE_SIGNATURE = 0x49353942       # B95I
IMAGE_VERSION = 1
MAX_SEGMENTS = 16
IMAGE_HEADER = struct.Struct("<IHHIIQ")
SEGMENT = struct.Struct("<QQQII")
MANIFEST_SIZE = IMAGE_HEADER.size + MAX_SEGMENTS * SEGMENT.size

BOOTSTRAP_SIGNATURE = 0x53353942   # B95S
BOOTSTRAP_VERSION = 1
BOOTSTRAP_HEADER = struct.Struct("<IHHIIIIII")
BOOTSTRAP_HEADER_SIZE = BOOTSTRAP_HEADER.size
BOOTSTRAP_MIN_ADDRESS = 0x8000
BOOTSTRAP_MAX_END = 0x10000


def fail(message):
    raise SystemExit("mkrc95biosimage: " + message)


def read_nonempty(path, label):
    if not path.is_file():
        fail(label + " file does not exist: " + str(path))
    data = path.read_bytes()
    if not data:
        fail(label + " empty image: " + str(path))
    return data


def parse_segment(value):
    try:
        address_text, filename = value.split(":", 1)
        address = int(address_text, 0)
    except (ValueError, TypeError):
        fail("segment must be LOAD_ADDRESS:FILE")
    path = pathlib.Path(filename)
    return address, path, read_nonempty(path, "segment")


def overlaps(a, alen, b, blen):
    return a < b + blen and b < a + alen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot-sector", required=True, type=pathlib.Path)
    parser.add_argument("--stage1", required=True, type=pathlib.Path,
                        help="real-mode Stage-1 binary loaded by Stage 0")
    parser.add_argument("--stage1-load-address", type=lambda x: int(x, 0),
                        default=BOOTSTRAP_MIN_ADDRESS)
    parser.add_argument("--stage1-entry-offset", type=lambda x: int(x, 0),
                        default=0)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--cd-boot-sector", type=pathlib.Path,
                        help="optional El Torito Stage-0 sector for a no-emulation boot image")
    parser.add_argument("--cd-boot-image", type=pathlib.Path,
                        help="output combined El Torito boot image (512-byte load sectors)")
    parser.add_argument("--entry", required=True, type=lambda x: int(x, 0),
                        help="protected-mode B95I image entry address")
    parser.add_argument("--sector-size", type=int, default=512)
    parser.add_argument("--segment", action="append", default=[],
                        metavar="LOAD_ADDRESS:FILE")
    args = parser.parse_args()

    # Stage 0 is a legacy BIOS boot sector and its EDD DAP math uses 512-byte
    # sectors. Larger logical-sector images cannot be booted by this first stage.
    if args.sector_size != 512:
        fail("BIOS Stage 0 currently requires 512-byte sectors")
    boot = read_nonempty(args.boot_sector, "boot sector")
    if len(boot) != args.sector_size:
        fail("boot sector must be exactly one logical sector")
    if boot[510:512] != bytes((0x55, 0xAA)):
        fail("boot sector is missing the 0xAA55 signature")
    if not 1 <= len(args.segment) <= MAX_SEGMENTS:
        fail("provide between 1 and 16 --segment arguments")

    stage1 = read_nonempty(args.stage1, "Stage-1")
    stage1_address = args.stage1_load_address
    if stage1_address < BOOTSTRAP_MIN_ADDRESS:
        fail("Stage-1 load address must be at least 0x8000")
    if stage1_address + len(stage1) > BOOTSTRAP_MAX_END:
        fail("Stage-1 image must end at or below 0x10000")
    if not 0 <= args.stage1_entry_offset < len(stage1):
        fail("Stage-1 entry offset is outside the Stage-1 image")
    if stage1_address + args.stage1_entry_offset > 0xFFFF:
        fail("Stage-1 physical entry must be reachable with CS=0 and a 16-bit IP")

    stage1_padded_size = ((len(stage1) + args.sector_size - 1) //
                          args.sector_size) * args.sector_size
    stage1_padded = stage1 + bytes(stage1_padded_size - len(stage1))
    stage1_lba = 2
    manifest_lba = stage1_lba + stage1_padded_size // args.sector_size
    manifest_sectors = (MANIFEST_SIZE + args.sector_size - 1) // args.sector_size
    next_lba = manifest_lba + manifest_sectors

    records = []
    payloads = []
    for value in args.segment:
        address, path, data = parse_segment(value)
        padded_size = ((len(data) + args.sector_size - 1) //
                       args.sector_size) * args.sector_size
        padded = data + bytes(padded_size - len(data))
        if address < 0x100000 or address > 0xffffffff:
            fail(f"manifest load address outside supported physical range: {path}")
        if padded_size > 0x100000000 - address:
            fail(f"segment exceeds 4-GiB physical limit: {path}")
        for old_address, old_size, *_ in records:
            if overlaps(address, padded_size, old_address, old_size):
                fail(f"overlapping physical load range: {path}")
        blocks = padded_size // args.sector_size
        records.append((address, padded_size, next_lba,
                        zlib.crc32(padded) & 0xffffffff, 0))
        payloads.append((next_lba, padded))
        next_lba += blocks

    if not any(args.entry >= address and args.entry < address + size
               for address, size, *_ in records):
        fail("entry address is not inside any loaded manifest segment")

    # B95S is a separate Stage-0 contract. CRC covers exact (unpadded) bytes.
    bootstrap_header = BOOTSTRAP_HEADER.pack(
        BOOTSTRAP_SIGNATURE, BOOTSTRAP_VERSION, BOOTSTRAP_HEADER_SIZE,
        stage1_address, len(stage1), args.stage1_entry_offset,
        zlib.crc32(stage1) & 0xffffffff, 0, 0)
    bootstrap_area = bootstrap_header + bytes(
        args.sector_size - BOOTSTRAP_HEADER_SIZE)

    manifest = bytearray(IMAGE_HEADER.pack(
        IMAGE_SIGNATURE, IMAGE_VERSION, MANIFEST_SIZE,
        len(records), 0, args.entry))
    for record in records:
        # B95I stores disk LBA, byte length, physical load address, CRC and flags.
        # Keep the in-memory tuple ordered for overlap and entry-point checks.
        manifest.extend(SEGMENT.pack(record[2], record[1], record[0],
                                     record[3], record[4]))
    for _ in range(MAX_SEGMENTS - len(records)):
        manifest.extend(bytes(SEGMENT.size))
    if len(manifest) != MANIFEST_SIZE:
        fail("internal manifest layout mismatch")
    manifest += bytes((manifest_sectors * args.sector_size) - len(manifest))

    image = bytearray(next_lba * args.sector_size)
    image[0:args.sector_size] = boot
    image[args.sector_size:2 * args.sector_size] = bootstrap_area
    image[stage1_lba * args.sector_size:
          stage1_lba * args.sector_size + len(stage1_padded)] = stage1_padded
    manifest_offset = manifest_lba * args.sector_size
    image[manifest_offset:manifest_offset + len(manifest)] = manifest
    for lba, payload in payloads:
        offset = lba * args.sector_size
        image[offset:offset + len(payload)] = payload

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"wrote {args.output} ({len(image)} bytes, {len(records)} B95I segments)")
    print(f"B95S header: LBA 1; Stage-1: LBA {stage1_lba}, "
          f"{stage1_padded_size // args.sector_size} sectors at {stage1_address:#x}")
    print(f"B95I manifest: LBA {manifest_lba}, {manifest_sectors} sectors")
    print(f"payload begins: LBA {manifest_lba + manifest_sectors}")

    if bool(args.cd_boot_sector) != bool(args.cd_boot_image):
        fail("--cd-boot-sector and --cd-boot-image must be supplied together")
    if args.cd_boot_sector:
        if args.sector_size != 512 or stage1_address != BOOTSTRAP_MIN_ADDRESS:
            fail("El Torito boot image requires 512-byte sectors and Stage-1 at 0x8000")
        cd_boot = read_nonempty(args.cd_boot_sector, "CD boot sector")
        if len(cd_boot) != 512 or cd_boot[510:512] != bytes((0x55, 0xAA)):
            fail("CD boot sector must be exactly 512 bytes with the 0xAA55 signature")
        cd_image = cd_boot + bootstrap_area + stage1_padded
        if len(cd_image) > 0x10000 - 0x7C00:
            fail("El Torito boot image exceeds the 64-KiB real-mode load window")
        args.cd_boot_image.parent.mkdir(parents=True, exist_ok=True)
        args.cd_boot_image.write_bytes(cd_image)
        print(f"El Torito boot image: {args.cd_boot_image} ({len(cd_image)} bytes)")
        print(f"El Torito load segment: 0x07C0; load count: {(len(cd_image) + 511) // 512} 512-byte sectors")


if __name__ == "__main__":
    main()
