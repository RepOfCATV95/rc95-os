# RC95LDR WIM image provider

The WIM provider is a read-only filesystem adapter over `sdk/lib/libwim`.
It reuses the clean-room XPRESS, LZX and LZMS codec registry, WIM lookup
catalogue, metadata/tree parser and SHA-1 lookup-resource verification.

## Mount and lifetime

Call `Rc95LdrWimOpenFile()` (or `Rc95LdrWimOpenMemory()` when the caller
already has the bytes) to validate the WIM and load its image catalogue
without choosing an image. `Rc95LdrWimGetImageCount()` and
`Rc95LdrWimGetImageInfo()` then enumerate the available editions; select the
zero-based image with `Rc95LdrWimSelectImage()`. `Rc95LdrWimMountFile()` is the convenience path that reads a bounded WIM
copy (default 1 GiB) and selects an image in one call. Individual decoded resources default to a
512 MiB cap and each opened file defaults to 256 MiB. The caller supplies the
allocator used for the WIM reader, image copy and open-file buffers. The mount
owns the copy and retains it until `Rc95LdrWimUnmount()`. `ImageIndex` is
zero-based; the `WIM_IMAGE_INFO.Index` field is the WIM's one-based display
index. Free returned image-info strings with `WimImageInfoFree()` before
unmounting.

`Rc95LdrWimMountMemory()` is also available as a one-call open-and-select
operation when the platform already owns a stable image buffer. The buffer must remain valid until unmount. The returned
`RC95LDR_VOLUME` exposes a normal read-only `RC95LDR_FILESYSTEM` interface and
can be bound with `Rc95LdrInitializeVolumeFileProvider()` for existing WINLOAD
file consumers. It is an image filesystem, not a block device; no GPT, MBR,
sector geometry or synthetic boot sector is produced.

## Filesystem semantics

Paths are root-relative, ASCII case-insensitive, and accept either slash
convention. Absolute paths, traversal, ADS syntax and Win32 wildcard/device
characters are rejected. Each opened file is decoded into a bounded buffer;
`ReadAt` then provides checked arbitrary-offset reads. Eight simultaneous
handles are supported. Unmount closes outstanding handles before releasing
the tree, reader and owned image.

The reader rejects malformed headers, out-of-range resources, corrupt lookup
entries and unsupported split/solid resources. The WIM reader's existing
codec capabilities remain authoritative; the adapter does not claim support
for resource types the reader rejects.

## Boot-flow boundary

The native UEFI flow now reads the selected installation's BCD `ramdisk=`
option, resolves `[boot]` against that BCD object's boot-files partition,
mounts the exact partition, opens the bounded WIM copy, and exposes the chosen
image through the same scoped file-provider ABI used by WINLOAD. It prefers
the catalogue image named for Windows Setup and otherwise selects the first
image (ADK WinPE fallback). The WIM copy and reader remain alive throughout
the synchronous WINLOAD transaction; source-volume mounts are released after
the WIM has been copied. BCD paths are checked for traversal and invalid path
characters. The non-WIM path remains unchanged.

This connects BCD WIM file discovery and file access to the native UEFI
WINLOAD request builder. The flow also resolves the `ramdiskoptions` GUID
against the same loaded BCD hive and decodes the required
`BcdLibraryDevice_RamdiskSdiDevice` (0x31000003) and
`BcdLibraryString_RamdiskSdiPath` (0x32000004) elements. The SDI path is
normalized to a root-relative path and checked for traversal and invalid
characters. The device descriptor must identify exactly the selected
boot-files partition or OS partition; unsupported or ambiguous device
identities fail closed. The decoded path, device-volume choice and object
identity are retained in the selected installation record for the request
builder.

The UEFI flow mounts the partition named by the decoded SDI device (or reuses
the mounted OS volume when it is the same partition), opens the root-relative
SDI path, and reads the complete file into a loader-pool allocation. The
parser enforces a 4 KiB minimum header, a 64 MiB maximum, the eight-byte
`$SDI0001` signature, and validates the supported TOC section types and
file-relative extents. It rejects unsupported types, out-of-range or
overlapping blobs, inconsistent boot-code metadata, and missing PART/DISK
RAM-disk extents.

WINLOAD now allocates a page-aligned physical copy of the complete SDI image,
attests it with the platform allocator, and reserves it as `LoaderXIPRom`.
The NT loader block receives `ArcBootDeviceName = "ramdisk(0)"` and the
selected PART/DISK extent through `RDIMAGEOFFSET` and `RDIMAGELENGTH`
load options. The typed handoff context carries and validates the retained
physical allocation and selected extent. The allocation is not freed after a
successful transfer, so early kernel initialization can consume it; returning
error paths release it.

This implements the ARC `RAMDISK_BOOT_DISK` contract used by this source
tree's `IopStartRamdisk()` implementation. It is not yet a complete retail
Windows PE boot-application handoff: the WIM contents are currently exposed
to WINLOAD as a file provider, but are not yet transferred as a persistent
kernel-visible WIM-backed disk. BIOS WIM boot, split WIM sets, Windows
boot-application persistence semantics, and real-media boot validation remain
outstanding.


## Windows RAMDISK_BOOT_DISK handoff

For a decoded SDI image, the UEFI request adapter passes the validated full
image and its parsed PART/DISK extent into WINLOAD. WINLOAD allocates a
page-aligned physical copy, attests it with the platform allocator, and tracks
it as `RC95LDR_ALLOC_RAMDISK`. The final NT memory map exposes the allocation
as `LoaderXIPRom`, the descriptor consumed by this tree's
`ntoskrnl/io/iomgr/ramdisk.c` boot path.

For Windows NT legacy and modern profiles, the native loader-block builder
sets `ArcBootDeviceName = "ramdisk(0)"` and appends
`RDIMAGEOFFSET=<blob offset> RDIMAGELENGTH=<blob length>` to the original
load options. This matches `IopStartRamdisk()`: it scans for a
`LoaderXIPRom` descriptor and passes its base, offset and length to
`FSCTL_CREATE_RAM_DISK`. The typed handoff context carries the allocation
and selected extent under `RC95LDR_HANDOFF_RAMDISK`.

The SDI pages remain allocated after successful transfer for early kernel
I/O-manager initialization. If preparation or firmware transfer returns with
an error, the WINLOAD transaction releases them. This implements the ARC
RAM-disk contract in this source tree; it does not assert that every retail
Windows build uses identical private loader extensions. Real firmware boot
validation remains necessary.
