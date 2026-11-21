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

Microsoft `winresume` is modern Windows only (BIOS or UEFI); in-winload resume is RC95-OS only. This selector does not claim the four loader-blocks or handoff mechanisms are already implemented. It establishes non-interchangeable contracts for later kernel/HAL/import/driver/registry/loader-block work.

## Tests

`profile_tests.c` covers accepted and rejected NT 4.0/5.x, Vista, Windows 10/11-compatible version tuples, ReactOS and RC95-OS routes, wrong entry types, unsupported NT versions, and firmware mismatches. The tests are registered in the full-tree and standalone host CMake projects.
