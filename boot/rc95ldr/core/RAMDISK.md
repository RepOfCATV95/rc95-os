# RC95LDR RAM-disk write path

The common core exposes a caller-owned memory image as a normal RC95LDR_BLOCK_DEVICE through Rc95LdrInitializeRamDisk.

## Contract

- The image is a contiguous byte buffer whose size must be an exact multiple of the logical block size.
- The caller owns the buffer and must keep it alive for the lifetime of the RAM-disk context and every consumer of the block-device callbacks.
- Reads and writes use the common Rc95LdrReadBlocks and Rc95LdrWriteBlocks checked block-I/O entry points.
- A writable instance exposes a write callback that copies sectors directly into the backing RAM image. Subsequent reads observe those writes.
- A read-only instance has no write callback and returns Rc95LdrUnsupported for write requests.
- LBA multiplication, byte ranges, image bounds, and exact transfer lengths are validated before memory is accessed.

## Persistence and ownership

This is a volatile write path. It modifies the in-memory image only; it does not write back to the .sdi source file, the host filesystem, or the physical boot device. The loader must allocate or otherwise reserve the image buffer for the full lifetime required by the OS handoff and mark the corresponding physical pages as loader/ramdisk-owned in the final memory map. The buffer must not be freed when the loader's temporary file handle is closed.

The implementation is firmware-neutral and can be initialized by either the BIOS or UEFI loader after the SDI disk payload has been loaded and validated. It does not itself parse SDI section tables, allocate firmware pages, build a BCD ramdisk descriptor, or create a Windows loader-block ramdisk handoff. Those integrations remain separate steps; the core write callback is ready for the SDI-backed device to use.

## Tests

ramdisk_tests.c verifies writable and read-only modes, write/read coherency, geometry, exact transfer sizes, and out-of-range LBAs.
