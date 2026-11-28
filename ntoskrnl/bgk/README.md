# BGK (Boot Graphics Kernel) layout

The BGK implementation is split into eight focused subdirectories. The
bitmap, framebuffer, rendering, resource, and indeterminate-animation paths
are implemented as the initial RC95-OS graphics pipeline. Determinate progress
rendering and broader pixel-format support remain follow-up work.

| Directory | Responsibility |
|---|---|
| `core/` | BGFX coordinator, bootres ownership checks, INBV fallback |
| `animation/` | Boot animation sequencing and frame timing |
| `bitmap/` | Bitmap decoding and pixel conversion |
| `framebuffer/` | Framebuffer state, bounds, and handoff validation |
| `rendering/` | Drawing primitives and compositing |
| `progress/` | Boot progress state and rendering |
| `resource/` | bootres resource lookup and decoding helpers |
| `include/` | BGK internal/public declarations shared by the modules |

## bootres.dll ownership marker contract

The kernel searches the loader's `LoadOrderListHead` for `bootres.dll` and
looks for an `RT_RCDATA` resource whose integer name is `0x95`. The resource
must be exactly this 36-byte little-endian structure:

| Offset | Field | Required value |
|---:|---|---|
| 0 | `Signature` (ULONG) | `0x35394352` (bytes `RC95`) |
| 4 | `Version` (ULONG) | `1` |
| 8 | `HeaderSize` (ULONG) | `36` |
| 12 | `TotalSize` (ULONG) | `36` |
| 16 | `OwnerId[16]` | `95 43 41 54 56 2D 52 43 39 35 2D 4F 53 00 00 01` |
| 32 | `Checksum` (ULONG) | CRC-32 of bytes 0 through 31 |

`Checksum` is at offset 32.
The checksum is `RtlComputeCrc32(0, Marker, FIELD_OFFSET(..., Checksum))`.

`IsBootResMissingOrCorrupt()` returns TRUE if the module or marker is absent,
unreadable, or structurally invalid. A valid marker with a different OwnerId
is not classified as corrupt, but `IsBootResGenuine()` rejects it.

The marker identifies the RC95-OS resource producer and catches accidental
resource substitution/corruption. It is **not a cryptographic signature** and
must not be treated as a security boundary. Authenticity against deliberate
tampering must come from the signed boot image / platform trust chain.

`ntoskrnl/ex/init.c` routes the normal graphical boot-logo path through
`DisplayGopBitmap()`. A missing/corrupt marker, foreign owner, unsupported
framebuffer, or failed logo decode selects the existing INBV
`DisplayBootBitmap()` path. For a genuine RC95-OS resource DLL, BGK now
initializes the ARC framebuffer, draws the centered logo, and starts the
indeterminate animation. INBV remains the permanent safe fallback; BGK
resource or display failures must never stop boot.


## RC95-OS boot graphics assets

The RC95-OS resource set has two separate visual roles:

- `RC95OSLOGO.BMP`: the static boot logo, drawn once after a valid framebuffer
  has been bound.
- `RC95OSANIM00.BMP` through `RC95OSANIM07.BMP`: eight ordered frames for
  the indeterminate boot activity indicator. They are animation frames, not
  eight progress milestones; the animation cycles modulo eight while boot
  activity is indeterminate.

The current source asset directory still stores the animation bitmaps under
legacy filenames `RC95OS-ANIM00.BMP` through `RC95OS-ANIM07.BMP` (with a
hyphen). The resource build must expose the canonical no-hyphen names above,
or the source assets must be renamed in a binary-safe repository operation.
The static logo already uses the requested `RC95OSLOGO.BMP` name.

The bitmap decoder must validate each BMP's file/DIB headers, dimensions,
planes, compression, pixel depth, row-stride arithmetic, and pixel-data bounds
before exposing pixels to the renderer. A malformed or missing logo/animation
resource is a BGK rendering failure and must select INBV rather than stop boot.

## Framebuffer validation and rendering contract

`bgk/include/bgfx_framebuffer.h` defines the BGK framebuffer contract.
`BgfxValidateFramebuffer()` is implemented in the pure
`framebuffer/bgfx_framebuffer_validate.c` module. It consumes the existing
`CM_FRAMEBUF_DEVICE_DATA` plus the video-RAM length and checks nonzero
dimensions, scanline width, supported 32-bpp RGB masks, pitch arithmetic,
framebuffer-size arithmetic, and the framebuffer offset/range using 64-bit
intermediate calculations. It does not map physical memory. The host tests
compile this production validator directly and exercise valid padded surfaces,
unsupported masks/formats, invalid geometry, and arithmetic/range failures;
see `ntoskrnl/bgk/tests/README.md`.

The display provider remains responsible for discovering the existing boot
display configuration, translating its bus address, and mapping video RAM.
It passes the mapped video-RAM base and the validated descriptor to
`BgfxSetFramebuffer()`; BGK applies the validated framebuffer offset.
`BgfxFillRectangle()` is the first bounded rendering primitive. It rejects
out-of-range rectangles and writes only inside the validated visible surface.
The initial implementation intentionally supports 32-bpp formats with
non-overlapping 8-bit RGB channel masks; other formats return
`STATUS_NOT_SUPPORTED` and select the INBV fallback. The static
`RC95OSLOGO.BMP` is drawn at
`((screen width - logo width) / 2, (screen height - logo height) / 2)`.
Animation frames are drawn horizontally centered near the bottom edge and
advance at 100 ms intervals using `KeQueryInterruptTime()`. The frame index
wraps modulo eight. `BgfxAnimationStart()` decodes and validates all eight
frames, presents frame zero, then arms a periodic kernel timer/DPC. The timer
is authoritative, so the animation continues during long initialization
phases even when no progress callback is issued. Legacy progress paths may
still call the time-throttled `BgfxAnimationTick()`; an interlocked gate
prevents duplicate concurrent frame presentations. The DPC only presents
already-decoded nonpaged pixels and performs no resource lookup or allocation.

`BgfxAnimationStop()` is a PASSIVE_LEVEL teardown operation: it clears the
running state, cancels the timer, drains queued DPCs, waits for an in-flight
progress tick, and releases every cached bitmap. INBV calls it when display
ownership is lost, then calls `BgfxResetFramebuffer()` to unmap the BGK MMIO
window before the boot video driver cleans or hands off the surface. The stop
routine asserts PASSIVE_LEVEL because DPC draining and pool release are not
valid from an arbitrary interrupt context. Failed frame presentation stops
the timer; cached storage remains owned until the normal stop path frees it.
Missing/invalid resources still leave the static logo and INBV fallback
behavior unchanged.

The implementation does not change the loader-block ABI and does not depend
on FreeLoader GOP/VBE interfaces. The ARC discovery helper is compiled into
the kernel for this BGK path.


## Resource-loader integration boundary

The bootres CMake target now emits a resource-only `bootres.dll` containing
the logo, all eight animation frames (under the canonical no-hyphen resource
names), and the 36-byte owner marker at integer resource ID `0x95`. The source
BMP filenames remain unchanged. The loader must place this DLL in its loaded
module list as `bootres.dll` before the kernel's graphical initialization;
the kernel deliberately does not open files or perform filesystem I/O to
locate it. RC95LDR admits it only when the selected RC95-OS BCD object has a
valid graphics policy and `graphicsmodedisabled` is zero. When
`graphicsmodedisabled` is one, the loader does not open, allocate, map, or add
`bootres.dll` to the module list. The kernel consequently sees no bootres
module, `IsBootResMissingOrCorrupt()` selects the existing INBV presentation,
and the OS continues without the optional BGK logo/animation. A missing DLL,
resource validation failure, or unsupported framebuffer has the same safe
fallback behavior. The loader's module validation also checks the resource
module's zero entry point, exact resource flag, bounded `bootres.dll` path,
page-aligned physical extent, and platform allocation attestation before
inserting it after required boot drivers.
