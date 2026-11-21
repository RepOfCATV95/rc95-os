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

`include/rc95ldrwinload.h` defines the versioned request/result structures and platform callback boundary for `LoadAndBootWindows`. The same orchestration is intended for BIOS and UEFI. Platform adapters provide file reads, page allocation, normalized memory-map snapshots, and final firmware-specific handoff. The UEFI adapter must reacquire the final memory map and retry `ExitBootServices` when the map key changes; no boot-services pointer may be used after a successful exit.

The OS-family selector is explicit and enforced by `winload/profile.c`. `RC95LDR_OS_IDENTITY` binds the family to the boot-entry kind, firmware and (for Windows) NT version tuple. `{ntldr}` accepts only NT 4.0/5.0/5.1/5.2 on the legacy BIOS route; `{rosboot}` selects the independent ReactOS contract; Vista–Windows 11 requires the Windows Boot Manager winload route and NT 6.0–6.3 or 10.0 build 10240+; RC95-OS requires its own loader chain. The ABI-v2 request is rejected before opening the kernel when these fields conflict. LiberNT is rejected, not treated as an alias for ReactOS or RC95-OS. See `../docs/OS_PROFILES.md` for the profile table and tests.

## Clean-room implementation rules

The implementation is independently authored from public format and ABI documentation. Do not copy, translate, mechanically refactor, or derive implementation structure from legacy FreeLoader `winldr.c` or `resume.c`. Existing FreeLoader behavior may be used only as a black-box compatibility observation where licensing and test provenance permit; it is not an implementation source.

Implementation stages:

1. **Implemented first slice:** validate DOS/NT headers, PE32/PE32+ machine pairing, optional-header extents, alignments, section-table bounds, section overlap, executable entry-point membership, and base-relocation/import directory ranges.
2. **Implemented first slice:** map headers and sections into a caller-owned image buffer and apply x86 HIGHLOW / AMD64 DIR64 base relocations. Images with imports are rejected until an explicit import resolver and dependency policy exist.
3. **Partially integrated:** `LoadAndBootWindows` validates the OS-family/boot-entry/version profile before opening, validating, allocating, and mapping the selected kernel image. It records the mapped image base, size, and entry point, then fails closed because imports, HAL, boot-start drivers, SYSTEM hive, profile-specific loader blocks, and handoff are not yet implemented.
4. Build a versioned loader parameter block and finalized memory descriptors.
5. Perform firmware-specific shutdown and architecture-specific transfer.
6. Implement hibernation as a separately testable image-validation and restore subsystem, with format/version gates and platform memory callbacks.

No stage is complete merely because it compiles. Unsupported relocations, imports, image variants, hibernation formats, or loader-block versions must fail closed with a specific status.

## Hibernation ownership

- Vista–Windows 11 resume: dispatch to the matching Windows `winresume.exe` (BIOS) / `winresume.efi` (UEFI) application; RC95LDR must not substitute its own restore implementation for Microsoft's resume image.
- RC95-OS: resume is a mode of the RC95-OS winload implementation and uses RC95-OS-owned image metadata and restore rules.
- NT legacy and ReactOS: retain their respective compatibility semantics; do not assume the modern Windows resume format.
- LiberNT: no support path.

The current `osload.c` and `resume.c` files remain migration placeholders until the PE mapper, profile-specific loader-block construction, and restore contracts are implemented. The public entry point must return `Rc95LdrUnsupported` until its required stages are present.

## PE host tests

`rc95ldr_pe_tests` builds synthetic PE32 and PE32+ images and exercises valid mapping, HIGHLOW/DIR64 relocations, malformed DOS/NT offsets, truncated raw sections, invalid entry points, invalid alignments, unsupported machines, out-of-image relocation directories, and the current fail-closed import policy. Run the PE tests independently on the host with `cmake -S boot/rc95ldr/tests/host -B build/rc95ldr-pe-tests`, then `cmake --build build/rc95ldr-pe-tests` and `ctest --test-dir build/rc95ldr-pe-tests --output-on-failure`. The full-tree test target is also available with `RC95LDR_BUILD_TESTS=ON`.


## OS profile regression tests

`rc95ldr_profile_tests` and the standalone `rc95ldr_profile_host_tests` validate the four distinct OS families and reject boot-entry/version/firmware mismatches. The winload request ABI is version 2 because it now requires `BootEntryKind`, `OsMajorVersion`, `OsMinorVersion`, and `OsBuildNumber`.
