# RC95-OS native hibernation resume

RC95-OS resumes through its own `winload.exe` (BIOS) or `winload.efi`
(UEFI). The `Rc95LdrResumeInsideWinload` policy calls
`Rc95LdrResumeFromHibernation` before cold kernel loading. That path opens
`\\hiberfil.sys` at the selected volume root (or the optional
`HiberFilePath` supplied by the platform), restores the memory image, sets
the saved resume flag, and transfers to the architecture trampoline.

The RC95 route must not discover, open, map, or execute Windows
`winresume.exe` or `winresume.efi`. The separate
`Rc95LdrResumeViaWinresume` policy is accepted only for
`Rc95LdrOsWindowsModern`.

## On-disk format

The reader consumes the version-4 `PO_HIBER_IMAGE_HEADER` and
`PO_HIBER_MEMORY_CHUNK` layout from `sdk/include/reactos/hiberfil.h`.
It verifies the XOR header checksum, signature, version, machine, page size,
complete-image flag, page count, payload byte count, CPU-state version and
single-processor flags before writing physical memory.

Each memory chunk is ordered by increasing physical address. Its checksum is
the XOR of low/high physical-address words, the original uncompressed byte
length, and the XOR checksum of the original page bytes.

When `HIBER_CHUNK_COMPRESSED` is set in `Length`, the low 31 bits are the
stored byte count. The first four stored bytes are the original byte count;
the remaining bytes are the kernel's bounded LZ token stream:

- Each group starts with a control byte, consumed least-significant bit first.
- A clear control bit is one literal byte.
- A set control bit is a little-endian 16-bit match code.
- The low 12 bits encode offset minus one (1..4096).
- The high 4 bits encode match length minus three (3..18).
- Overlapping match copies are supported.

The reader bounds stored and decoded lengths to 64 KiB chunks, validates
alignment, range arithmetic, chunk ordering and checksums, and performs a
complete validation pass before the restore pass. The restore pass repeats
chunk validation before mapping and copying each chunk.

## Architecture restore contract

`RC95LDR_WINLOAD_PLATFORM` supplies range validation, physical map/unmap,
processor-state validation and a non-returning processor restore callback.
The FreeLoader backend provides I386 and AMD64 implementations and uses the
existing `WinLdrResumeProcessorState` assembly trampolines. It validates the
saved control registers, descriptor tables, selectors, stack/instruction
pointers and page-table root. The processor context is staged in a 16-byte
aligned block, including the optional 512-byte FX save area at the exact
offset expected by the assembly trampoline.

The FreeLoader callback installer is
`Rc95LdrFreeldrInstallResumeArchitectureCallbacks`. It refuses to install
mapping callbacks until the architecture's FreeLoader page tables are active.
The firmware/platform adapter must still supply file I/O and invoke this
installer only after the matching I386/AMD64 machine-dependent page tables
have been established.

## Integration boundary

`LoadAndBootWindows` handles `Rc95LdrResumeInsideWinload` before invoking
the cold kernel-image loader. A return from the processor restore callback is
treated as failure; successful restoration must transfer directly to the
saved kernel context.

The RC95LDR platform adapter and end-to-end firmware entry path must provide
the callback table and preserve the active restore-window page tables until
the trampoline switches to the saved CR3. Do not enable this policy in a
firmware build that has not established those prerequisites. Host-side
regression coverage lives in `boot/rc95ldr/tests/resume_tests.c`.


## FreeLoader / ROSLOAD integration

The `RC95Resume` FreeLoader boot entry is a distinct ARC route. It adds the
`RC95RESUME` option to the legacy FreeLoader preparation call without changing
the legacy `LoadAndBootWindows(Argc, Argv, Envp)` ABI. After the normal loader
memory layout has been created and `WinLdrSetProcessorContext` has activated
the architecture page-table root, `winldr.c` bypasses the legacy Windows
hibernation reader and calls `Rc95LdrFreeldrBootNativeResume`.

That adapter initializes the typed `RC95LDR_WINLOAD_PLATFORM`, supplies ARC
filesystem callbacks (`FsOpenFile`, `ArcGetFileInformation`, `ArcSeek`,
`ArcRead`, and `ArcClose`), then calls
`Rc95LdrFreeldrInstallResumeArchitectureCallbacks`. The architecture installer
activates and verifies the complete I386 page directory or AMD64 PML4 before
publishing the restore-window mapping and processor-state callbacks. Finally,
the adapter enters `Rc95LdrLoadAndBootWindows`, the typed RC95LDR orchestration
entry point; the legacy ARC function and the typed orchestration function now
have distinct names.

The `AllocatePages` and `FreePages` callbacks supplied by this resume-only
adapter are deliberate failure stubs: the resume policy must not allocate a
new kernel image or take the cold-boot path. The normal RC95 resume branch
opens only the hiberfile and restores the saved processor state. Do not reuse
this adapter for cold boot.

The `RC95Resume` menu route still relies on FreeLoader's existing NT loader
preparation to load the selected kernel/HAL and prepare its memory layout before
the typed resume call. The adapter probes the selected boot volume for
`\\hiberfil.sys` after the restore-window callbacks are installed. If the file
is present, it enters the native RC95LDR validation/restore path. If it is
absent, the adapter returns `Rc95LdrNotFound` and `LoadAndBootWindowsCommon`
continues with the already-prepared cold-boot kernel; it does not pass a null
kernel path to the cold-image loader or display a spurious resume error.

This is an integration bridge, not yet a standalone firmware-native RC95LDR
executable entry independent of the legacy preparation path. Full cold-boot
handoff remains separate work.

## Hibernation image deletion and cold-boot safety

The optional `RC95LDR_WINLOAD_PLATFORM.DeleteHiberFile` callback is invoked
only after the user explicitly chooses normal boot from the hibernation
options screen. `Rc95LdrWinloadDeleteHiberFile` is the filesystem-dispatch
helper for adapters whose context can provide an `RC95LDR_VOLUME`. It
converts a bounded UTF-16 volume-relative path to the ASCII path accepted by
the filesystem interface, rejects drive-qualified paths and dot-dot
components, and dispatches only through `RC95LDR_FILESYSTEM.DeleteFile`. Platform
adapters can assign this helper to `DeleteHiberFile` and set the appended
`DeleteHiberFileContext` to the mounted volume, keeping the ordinary platform
service context unchanged. A missing delete operation is an error, never success.

The FAT32 implementation now has an optional block-write path. BIOS EDD uses
INT 13h AH=43h; UEFI uses Block I/O WriteBlocks only when the media is not
read-only. FAT32 deletion validates the complete cluster chain before
mutation, refuses directories, volume labels, open files, read-only devices,
and volumes already marked dirty, removes LFN/short directory slots before
releasing clusters, updates mirrored FAT copies (or the selected active FAT),
and brackets metadata changes with the FAT clean-shutdown flag. It invalidates
stale primary/backup FSInfo free-count and allocation hints after reclamation.
A failure leaves the volume marked dirty and returns the write error. Host tests cover
mirroring, cluster release, missing-file lookup, injected write failure,
read-only media, dirty-volume refusal, interrupted reclamation, primary/backup
FSInfo invalidation, BIOS EDD writes, UEFI Block I/O writes, and path traversal
rejection.

**NTFS remains read-only in this revision.** The NTFS reader has no
transactional metadata writer for `$MFT`, `$Bitmap`, `$I30` indexes, and
`$LogFile`. A raw MFT in-use-bit flip or bitmap edit is not a valid deletion
transaction and can corrupt a Windows volume. NTFS therefore does not publish
a `DeleteFile` operation; the WINLOAD helper returns
`Rc95LdrUnsupported`, and the normal-boot path must remain blocked rather
than booting while the hibernation image may still exist. NTFS deletion
requires a journal-aware transaction implementation and crash-recovery tests
before it can be enabled.
