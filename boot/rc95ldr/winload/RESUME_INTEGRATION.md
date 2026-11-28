# RC95-OS native hibernation resume

RC95-OS resume is performed by the selected RC95-OS `winload.exe` (BIOS) or
`winload.efi` (UEFI). The RC95 profile selects
`Rc95LdrResumeApplicationRc95Winload`; it never selects Windows'
`winresume.exe` or `winresume.efi`. The Windows-modern profile's separate
resume-application identifiers are not valid for `Rc95LdrOsRc95`.

## Platform setup

The winload platform must provide the normal file callbacks and append the
resume callbacks to the size-versioned `RC95LDR_WINLOAD_PLATFORM` structure:

- `ValidateResumeRange` rejects bad, firmware-owned and loader-owned pages.
- `MapPhysicalRange` / `UnmapPhysicalRange` map a bounded physical range
  through a temporary writable window.
- `ValidateResumeProcessorState` validates the saved architecture state.

The callback contract belongs to WINLOAD platform backends. The migration is
not yet complete: ROSLOAD and UEFILDR still compile the legacy FreeLoader
resume reader, architecture trampolines and page-table setup. Do not treat a
FreeLoader adapter as the final implementation or enable this path in a
firmware target until WINLOAD installs equivalent callbacks directly.

The I386 and AMD64 mapping windows are temporary, writable, page-aligned and
limited to 64 KiB per mapping. The WINLOAD memory-map validator must permit
writes only to explicitly eligible free pages. It must reject the loader's code, stacks,
heap, page tables, firmware-owned pages and bad pages, even if a hiberfile
chunk references them. The temporary restore window is a separate mapping
and is never a hiberfile destination.

## Image validation and restore order

The reader opens the optional request `HiberFilePath` (default `\\hiberfil.sys`) and validates the complete header,
machine type, version, header checksum, page count, payload length, CPU-state
metadata and resume-flag page. It then makes a full first pass over every
chunk, checking chunk ordering, page alignment, memory-map eligibility and
the checksum of the decompressed bytes. Only after the entire image passes
does the second pass map physical ranges and restore the memory.

The bounded kernel LZ stream uses one control byte for up to eight tokens.
Clear bits are literal bytes; set bits are little-endian 16-bit references
with a 12-bit offset-minus-one and a 4-bit length-minus-three. Compressed
chunks store the original uncompressed byte count in the first four payload
bytes, and the high bit of the chunk length marks compression. Checksums
always cover the original uncompressed page bytes.

After all pages are restored, the reader sets the physical resume flag to one,
closes the hiberfile, and invokes the I386 or AMD64 processor-state callback.
The RC95LDR-owned `arch/i386/resume.S` and `arch/amd64/resume.S` trampolines
restore the saved control registers, descriptor tables, debug registers,
FPU/SSE state and execution context. The `Rc95LdrRestoreProcessorState`
callback validates the architecture/size/FX layout and invokes the matching
trampoline. A successful trampoline does not return.

## LoadAndBootWindows policy

`LoadAndBootWindows` handles `Rc95LdrResumeInsideWinload` before opening or
mapping the cold-boot kernel image. This ordering prevents temporary kernel
buffers from occupying pages that belong to the saved image. If validation,
mapping or processor-state restoration fails, the loader reports the error
and stops; it does not silently cold-boot over a requested resume and does
not fall back to either Windows resume executable.

The architecture backend and `hibernate_resume.S` trampolines must be built
into the BIOS and UEFI WINLOAD targets for I386 and AMD64. Both WINLOAD
platform backends must provide equivalent safe physical mapping services
before enabling `Rc95LdrResumeInsideWinload`. The architecture restore routine
is linked into the WINLOAD policy library and is not a platform callback.

## RC95LDR architecture trampoline ABI

`rc95ldrresume.h` declares the native non-returning entry points:
`Rc95LdrI386ResumeTrampoline(state, flags)` (I386 stdcall) and
`Rc95LdrAmd64ResumeTrampoline(state, flags)` (AMD64 native ABI). The state
buffer is the kernel's saved processor-state image; its layout is not translated
by the loader. I386 uses the 0x320-byte image with the special-register block
at 0x2cc; AMD64 uses the 0x5b0-byte image. The resume reader calls
`Rc95LdrRestoreProcessorState` directly after restoring memory and setting the
resume flag. The final transition is therefore owned by WINLOAD and cannot be
redirected through a firmware adapter or legacy FreeLoader callback.

The WINLOAD CMake target includes the matching assembly and bridge only for
native I386 or AMD64 builds. Host regression builds continue to compile the
portable reader without assembling privileged instructions. The trampoline
must execute only after the reader has restored every image page and set the
resume flag; it disables interrupts and transfers to the saved execution
context without returning.
