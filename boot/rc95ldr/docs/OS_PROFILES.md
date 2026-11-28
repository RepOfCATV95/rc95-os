# RC95LDR operating-system profile separation

RC95LDR selects an OS contract before loading kernel dependencies or building a loader parameter block. A PE machine type, filename, or Windows-like version number alone is not sufficient to identify an OS. The caller provides the selected boot-entry route and OS identity; contradictory combinations are rejected.

## Supported profiles

| Product family | Required boot entry | Version identity | Firmware | Kernel ABI / loader block |
| --- | --- | --- | --- | --- |
| Windows NT 4.0–Windows Server 2003 | `{ntldr}` (`Rc95LdrEntryNtldr`) | NT 4.0 = 4.0; Windows 2000 = 5.0; XP = 5.1; Server 2003 = 5.2 | BIOS only in this compatibility route | NT legacy |
| Windows Vista–Windows 11 | Windows Boot Manager Windows-loader entry (`Rc95LdrEntryWindowsWinload`) | 6.0–6.3 or 10.0 build 10240+; Windows 11 remains NT 10.0 (build 22000+) | BIOS or UEFI | Modern Windows |
| ReactOS | `{rosboot}` (`Rc95LdrEntryReactOs`) | ReactOS identity; Windows NT version fields must be zero | BIOS or UEFI adapter, subject to ReactOS platform support | ReactOS-specific |
| RC95-OS | RC95-owned loader chain (`Rc95LdrEntryRc95Winload`) | RC95 product identity; Windows NT version fields must be zero | BIOS or UEFI | RC95-OS-specific |

LiberNT is not a fifth profile and is not accepted. The defunct CATV 95 LiberNT fork is incompatible with RC95LDR.

## Loader and resume ownership

- **NT 4.0–Server 2003:** `{ntldr}` is a distinct legacy compatibility route. It must not be interpreted as Vista+ `winload`, and it does not use the modern Windows loader-parameter-block contract. It is not dispatched through the Vista+ `winresume` profile.
- **Vista–Windows 11:** the Windows Boot Manager Windows-loader entry selects `winload.exe` for BIOS or `winload.efi` for UEFI. Resume uses matching `winresume.exe` or `winresume.efi`; BIOS resume is not excluded.
- **ReactOS:** `{rosboot}` selects a ReactOS-owned compatibility contract. It is not an alias for `{ntldr}`, modern Windows, or RC95-OS. Its loader parameter block and kernel startup ABI must be independently specified and tested against ReactOS.
- **RC95-OS:** BIOS starts through `rc95ldr.exe` and `winload.exe`; UEFI starts through `rc95ldr.efi` and `winload.efi`. RC95-OS `winload` owns resume internally; it must not invoke Microsoft's `winresume`.

The boot-entry selector is explicit input. The BCD/boot-menu dispatch layer must set it from the selected object/entry type, not infer it from the kernel filename. For modern Windows, the NT version tuple is checked: 6.0, 6.1, 6.2, 6.3, or 10.0 build 10240 and later. Windows 10 and Windows 11 both report NT version 10.0; Windows 11 starts at build 22000. The version tuple remains a claimed identity until corroborated by a trusted product manifest or PE version-resource verifier.

## Enforced API

`winload/include/rc95ldrprofile.h` defines `RC95LDR_OS_IDENTITY`, `RC95LDR_OS_PROFILE`, and `Rc95LdrSelectOsProfile`. The selected profile specifies the kernel ABI, loader-block kind, and resume application. The winload request ABI is version 2 and carries the explicit boot-entry kind and version tuple. `LoadAndBootWindows` calls the selector during request validation, before opening the kernel file.

Microsoft `winresume` is modern Windows only (BIOS or UEFI); in-winload resume is RC95-OS only. The selector establishes non-interchangeable contracts; it does not by itself prove that every kernel dependency or architecture handoff is production-qualified.

## Tests

`profile_tests.c` covers accepted and rejected NT 4.0/5.x, Vista, Windows 10/11-compatible version tuples, ReactOS and RC95-OS routes, wrong entry types, unsupported NT versions, and firmware mismatches. The tests are registered in the full-tree and standalone host CMake projects. Registered tests are not evidence that CI passed for a particular commit.

## AMD64 platform prerequisites and ABI isolation

AMD64 preparation is rejected unless all three platform requirements are positively established:

1. ACPI firmware tables are present and valid.
2. An ACPI MADT is present and describes at least one I/O APIC.
3. The platform CPU provider confirms CPUID extended-feature long-mode support.

Each failure is reported separately through the boot-error callback and retained in the preparation result's error text. The CPU test is delegated to the architecture/platform provider; a missing callback is a hard failure, not an implicit pass. These checks are gated on `Rc95LdrNtArchAmd64`. I386 and non-AMD64 preparation do not query the AMD64 CPU provider and are not rejected by this AMD64-only policy.

Loader ABI ownership is profile-specific: Windows NT profiles use their versioned NT loader-block and NT processor-control providers; RC95-OS uses only RC95-owned loader-block and processor-state contracts. A common handoff envelope may carry addresses and completion flags, but it must never reinterpret one operating system's PCR/PRCB or loader-block layout as another's.

### Implementation status (2026-11-27)

The three AMD64 platform gates are implemented in native preparation. The RC95-OS native-preparation path now selects `Rc95LdrBuildRc95LoaderState` and `Rc95LdrBuildRc95NativeLoaderBlock`; Windows NT legacy and modern profiles select their NT-owned state and loader-block builders. The RC95 configuration-tree builder validates a single rooted, bounded component tree before allocation, bounds identifier and configuration-data lengths, checks the returned physical allocation/mapping extent, and releases that allocation on post-allocation validation or construction failure.

This separation is not equivalent to a completed kernel handoff. RC95-OS still requires platform-provided per-CPU and scheduler-state allocations, architecture-specific stack/page-table/CPU state, and a successful `Rc95LdrValidateKernelHandoff` result. Do not mark RC95-OS I386 or AMD64 as boot-complete merely because the loader block was constructed.

## RC95-OS versus Windows NT preparation boundary

The native preparation transaction selects different configuration-state builders:

- Windows NT legacy and modern profiles require `LoaderStateInput` and call `Rc95LdrBuildNtLoaderState`. Their configuration component tree retains the NT loader-state representation.
- RC95-OS requires `Rc95LoaderStateInput` and calls `Rc95LdrBuildRc95LoaderState`. Its input, component records, node layout, allocation result, and release routine are RC95-owned and do not include the NT loader-state header.
- The RC95 loader-block builder accepts its own `RC95LDR_RC95_LOADER_INPUT`, not the NT loader input. It has its own list, Unicode-string, module, memory-descriptor, and boot-driver structures with RC95 pointer types. It is not one of the versioned NT loader-block layouts.
- Processor-control validation is selected by both OS family and architecture. NT profiles use the NT PCR/PRCB records. RC95-OS uses its own per-CPU-data and scheduler-state address/size records for both I386 and AMD64. Supplying NT PCR/PRCB fields does not satisfy RC95 validation.

Callers preparing RC95-OS must populate `Rc95LoaderStateInput` and the RC95 processor-state member (`Rc95I386` or `Rc95Amd64`) in the architecture handoff input. NT callers continue to populate `LoaderStateInput` and the NT processor-control member. The generic handoff envelope remains common; its profile-specific state payload is not.

## BIOS firmware state and hardware inventory

The common handoff envelope now carries an RC95 BIOS snapshot address, size,
and version independently of its EFI system-table/runtime-services/memory-map
fields. The BIOS provider captures the IVT, BDA, boot drive, video/memory
summary, and E820 entries into a caller-owned persistent buffer. That buffer
must be included in the native allocation ledger as
RC95LDR_ALLOC_BIOS_FIRMWARE_STATE; BIOS handoff is not firmware-state
complete without it. UEFI handoff must leave all BIOS snapshot fields zero.

Platform inventory version 4 parses ACPI MCFG multi-segment ECAM windows,
enumerates PCI functions per segment/bus range, records endpoint/bridge BAR
bases and attributes, decodes PCI bridge bus and forwarding windows, and
validates SMBIOS 2.x/3.x entry points and table structure boundaries. BAR
sizes remain explicitly unknown because the inventory config-space callback
is read-only and does not probe devices by writing to BAR registers.
