# RC95LDR Winload

This directory owns firmware-neutral OS image loading, preparation, and the single `LoadAndBootWindows` orchestration entry point. It does not own firmware entry, BCD hive parsing, partition discovery, or filesystem implementation.

## Supported boot-selection contract

| BCD/application selection | Ownership |
| --- | --- |
| `{ntldr}` | Compatibility dispatch for Windows NT 4.0–2003 |
| `{rosboot}` | ReactOS-specific compatibility dispatch |
| Vista–Windows 11 | Modern Windows profile: BIOS uses `winload.exe`; UEFI uses `winload.efi` |
| RC95-OS | BIOS chain: `rc95ldr.exe` then `winload.exe`; UEFI chain: `rc95ldr.efi` then `winload.efi` |
| LiberNT | Unsupported, including all builds of the defunct CATV 95 LiberNT fork |

The modern Windows profile uses the matching Windows resume application (`winresume.exe` for BIOS or `winresume.efi` for UEFI), selected according to firmware and image architecture. Windows 11 follows the Vista–10 profile. The BIOS loader filename is `winload.exe`.

RC95-OS deliberately differs: its `winload.exe` / `winload.efi` owns hibernation resume internally. RC95-OS must not be routed through a separate `winresume` executable.

## Common orchestration and firmware boundary

`include/rc95ldrwinload.h` defines the versioned request/result structures and platform callback boundary for `LoadAndBootWindows`. The same orchestration is used for BIOS and UEFI. The portable policy library is present, but there is not yet a standalone WINLOAD executable or production BIOS/UEFI platform adapter. The legacy FreeLoader targets remain the only integrated firmware entry paths. Do not interpret their existing ARC/page-allocation services as completed RC95LDR firmware integration. The native-input provider, firmware exit and architecture transfer still require production implementations and target validation. UEFI must eventually reacquire the final EFI memory map and retry `ExitBootServices` when the map key changes; no boot-services pointer may be used after a successful exit.

The OS-family selector is explicit and enforced by `winload/profile.c`. `RC95LDR_OS_IDENTITY` binds the family to the boot-entry kind, firmware and (for Windows) NT version tuple. `{ntldr}` accepts only NT 4.0/5.0/5.1/5.2 on the legacy BIOS route; `{rosboot}` selects the independent ReactOS contract; Vista–Windows 11 requires the Windows Boot Manager winload route and NT 6.0–6.3 or 10.0 build 10240+; RC95-OS requires its own loader chain. The ABI-v3 versioned request is rejected before opening the kernel when these fields conflict. LiberNT is rejected, not treated as an alias for ReactOS or RC95-OS. See `../docs/OS_PROFILES.md` for the profile table and tests.

## Clean-room implementation rules

The implementation is independently authored from public format and ABI documentation. Do not copy, translate, mechanically refactor, or derive implementation structure from legacy FreeLoader `winldr.c` or `resume.c`. Existing FreeLoader behavior may be used only as a black-box compatibility observation where licensing and test provenance permit; it is not an implementation source.

Implementation stages:

1. **Implemented first slice:** validate DOS/NT headers, PE32/PE32+ machine pairing, optional-header extents, alignments, section-table bounds, section overlap, executable entry-point membership, and base-relocation/import directory ranges.
2. **Implemented first slice:** map headers and sections into a caller-owned image buffer and apply x86 HIGHLOW / AMD64 DIR64 base relocations. `Rc95LdrPeMapEx` now parses bounded import descriptors and name/ordinal thunks, then delegates each binding to an explicit platform resolver. The resolver contract requires OS-specific module policy and a validated loaded dependency image; the compatibility `Rc95LdrPeMap` entry point still rejects import-bearing images. The FreeLoader adapter does not yet provide this resolver, so kernel imports remain fail-closed there.
3. **Connected preparation transaction:** `LoadAndBootWindows` maps the kernel, invokes `BuildNativePreparation`, verifies the kernel record if a provider supplies one, and passes a completed request through `Rc95LdrPrepareNativeLoaderBlock`. The FreeLoader adapter implements ARC file access, aligned allocation and a loader allocation-map snapshot. Its current native-input provider returns `Rc95LdrUnsupported` rather than inventing HAL, NLS, loader-extension or processor state. An RC95LDR boot-entry dispatcher and concrete BIOS/UEFI adapters still need to be implemented; no `Rc95LdrFreeLoaderLoadAndBootWindows` wrapper is present in this tree.
4. Complete the remaining target-specific loader-extension, dependency/group ordering, NLS, firmware-resource, and architecture-state requirements. The orchestration only invokes `ExitFirmwareAndHandoff` when the native result is explicitly `HandoffReady`; it never treats a successful PE map or a partial loader block as permission to transfer control.
5. Complete architecture-specific processor state, stack, page tables, PCR/PRCB and register-frame preparation. The handoff preflight is a validator, not a transfer stub; a mapped PE entry point must never be called as a normal C function.
6. Perform firmware-specific shutdown and architecture-specific transfer. UEFI must reacquire the final memory map and retry `ExitBootServices` when the map key changes; no boot-services pointer may be used after a successful exit.
7. Keep hibernation as a separately testable image-validation and restore subsystem, with format/version gates and platform memory callbacks.

No stage is complete merely because it compiles. Unsupported relocations, imports, image variants, hibernation formats, or loader-block versions must fail closed with a specific status.

## Hibernation ownership

- Vista–Windows 11 resume: dispatch to the matching Windows `winresume.exe` (BIOS) / `winresume.efi` (UEFI) application; RC95LDR must not substitute its own restore implementation for Microsoft's resume image.
- RC95-OS: resume is a mode of the RC95-OS winload implementation and uses RC95-OS-owned image metadata and restore rules.
- NT legacy and ReactOS: retain their respective compatibility semantics; do not assume the modern Windows resume format.
- LiberNT: no support path.

`osload.c` now runs the cold-boot transaction through the platform callbacks. With no production firmware adapter wired yet, the current orchestration is not a bootable executable path. The policy library must remain fail-closed until a platform supplies complete native preparation and non-returning handoff. `handoff.c` performs preflight validation only. `archprepare.c` constructs a register-frame description but does not execute a processor transition. Neither is permission to jump to the kernel. The processor restore path is linked into WINLOAD and invoked directly; the legacy platform restore callback slot is reserved and ignored.

## Native dependency catalog (initial implementation)

`dependency.c` and `include/rc95ldrdependency.h` provide an immutable, indexed candidate catalog. The first implementation target is NT 5.0 through NT 10.0 (Windows 2000 through Windows 11); NT 4.0 `ntoskrnl.exe` and `hal.dll` records are separately tagged `NT4_ONLY` so that the legacy path can be implemented after the modern resolver.

The catalog covers kernel and HAL candidates (`ntoskrnl.exe`, `ntkrnlmp.exe`, `hal.dll`, `halacpi.dll`, `halaacpi.dll`, and the listed i386 HAL variants), boot video, security, audio, PCI IDE, filesystem, USB controller, native runtime, Win32 runtime and win32k roles. It filters by I386/AMD64 and NT major version. The XHCI candidate begins at NT 6.x; UHCI is currently I386-only in this inventory.

This is deliberately a name/role inventory, **not** a completed dependency loader. A candidate is not proof that a file exists in the selected installation, is configured as boot-start, matches the installed OS build, has the correct PE machine/subsystem, or is required by detected hardware. The loader must use SYSTEM service configuration (including `Start`, `Type`, `Group`, `Tag`, `ImagePath`, `DependOnService`, and `DependOnGroup`), PE validation/import resolution, platform inventory and storage/USB device matching before mapping it. Kernel/HAL selection must produce one validated compatible pair; optional filesystem, audio, graphics and USB candidates must not be loaded just because they appear in this catalog. `ntdll.dll` and `kernel32.dll` are user-mode subsystem images, not boot-driver list entries.

The catalog has a standalone `rc95ldr_dependency_host_tests` target in addition to being compiled into `rc95ldr_winload_policy`. The test exercises candidate enumeration for NT 5.x, NT 6.x, I386/AMD64 and unsupported architectures.

## RC95 BIOS NTFS boot-driver callback

When the BIOS NTFS transaction binds a BCD-selected
`RC95LDR_WINDOWS_INSTALLATION`, the live `RC95LDR_FILE_PROVIDER`, and the
original `RC95LDR_WINLOAD_PLATFORM` into the RC95 native-provider input,
`rc95nativeprovider.c` installs `Rc95LdrRc95NativeLoadBootDriver` as the
native-preparation `LoadBootDriver` callback. Its opaque callback context is
the transaction's `RC95LDR_RC95_NATIVE_PROVIDER_RESULT`; that context retains
the selected installation, scoped file provider, allocator and allocation
attestation service, loaded-module records, and stable module-path storage.

The adapter accepts only image paths rooted in the selected installation's
BCD `SystemRoot` (including `\\SystemRoot\\...` and
`%SystemRoot%\\...` registry forms). Drive-letter/device paths, traversal
components, and non-ASCII paths are rejected rather than redirected to a
different volume. Reads are served by the bound NTFS file provider. The image
is then passed through `Rc95LdrWinloadLoadPeModule`, using the platform's
page allocator, allocation validator, and optional OS import resolver. PE
validation and section metadata therefore use the common loader path; no
second PE mapper is introduced.

Loaded driver images and their stable path strings remain owned by the native
provider result until preparation returns. If the transaction returns rather
than transferring control, `Rc95LdrRc95NativeReleaseBootDrivers` releases
those images through the same platform allocator. The adapter fails closed
when the selected architecture is not I386/AMD64, when a path cannot be
resolved safely, or when an import-bearing image has no suitable platform
resolver. A custom `LoadBootDriver` callback remains in use when no
transaction-scoped NTFS provider is bound.

## PE host tests

`rc95ldr_pe_tests` builds synthetic PE32 and PE32+ images and exercises valid mapping, HIGHLOW/DIR64 relocations, malformed DOS/NT offsets, truncated raw sections, invalid entry points, invalid alignments, unsupported machines, out-of-image relocation directories, import descriptor and string bounds, resolver success/failure, and the no-resolver fail-closed path. The host CI job builds and runs this target. Run the PE tests independently on the host with `cmake -S boot/rc95ldr/tests/host -B build/rc95ldr-pe-tests`, then `cmake --build build/rc95ldr-pe-tests` and `ctest --test-dir build/rc95ldr-pe-tests --output-on-failure`. The full-tree test target is also available with `RC95LDR_BUILD_TESTS=ON`.


## OS profile regression tests

`rc95ldr_profile_tests` and the standalone `rc95ldr_profile_host_tests` validate the four distinct OS families and reject boot-entry/version/firmware mismatches. The winload request ABI is version 3 because it now requires `BootEntryKind`, `OsMajorVersion`, `OsMinorVersion`, and `OsBuildNumber`.

## Hibernation regression coverage

The host-side `rc95ldr_resume_tests` target exercises both compressed and raw
(uncompressed) memory chunks. Negative fixtures cover chunk checksum mismatch,
invalid back-reference distance, nonzero unused final control bits, header
checksum corruption, truncated LZ input, and a decoded length that is not
page-aligned. Malformed-image cases assert that physical mapping never begins;
a separate injected mapping-I/O failure verifies error propagation and file
cleanup after restoration has started.

The `rc95ldr_textui_tests` target covers Spacebar/F8 entry to the advanced
hibernation menu, selecting normal boot, direct Enter-to-resume, Escape-to-
resume, and propagation of keyboard-provider failures. The normal-boot
selection remains fail-closed: the platform must provide `DeleteHiberFile`,
and deletion failure or an unavailable deletion service must not fall through
to cold boot while the hibernation image may remain present.

These are host regression fixtures, not proof of firmware compatibility or a
successful processor-state restore. Run the full host suite with the
repository's `RC95LDR_BUILD_TESTS=ON` CMake configuration and inspect the
CTest results for the exact build being evaluated.
