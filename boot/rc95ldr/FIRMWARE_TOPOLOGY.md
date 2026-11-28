# RC95LDR firmware topology discovery

RC95LDR's platform inventory records firmware-described processor and interrupt
controller topology. It does not program the local APIC or I/O APIC: interrupt
controller initialization, interrupt routing, and SMP startup remain the
responsibility of the selected HAL.

## ACPI

The inventory accepts either an explicit `RsdpPhysicalAddress` or an
`AcpiRoot` supplied by the firmware adapter. When
`ScanLegacyFirmwareTables` is set, the physical-memory mapper is also used to
search the EBDA, the final KiB of conventional memory, and the BIOS ROM window
for an RSDP. RSDP checksums, root-table checksums, table lengths, and table
entry alignment are validated.

The APIC (MADT) table is decoded for enabled Local APIC and x2APIC processors,
I/O APIC addresses/GSI bases, interrupt-source overrides, and the Local APIC
address override. The raw ACPI table inventory retains the physical table
address for firmware tables that the HAL or ACPI driver may need to consume.

## Intel MPS

An MPS floating pointer can be supplied with
`MpsFloatingPointerAddress`, or discovered in the EBDA, conventional-memory
tail, and BIOS ROM window when `ScanLegacyFirmwareTables` is enabled. The
floating pointer, PCMP base-table checksum, entry bounds, and extended-table
checksum are checked. Processor, I/O APIC, and interrupt-source entries are
decoded. MPS default configuration types 1-7 are represented using the
standard legacy APIC address layout.

When both formats are present, ACPI MADT topology takes precedence; MPS
presence and IMCR information are still recorded. The selected topology is
returned in `RC95LDR_PLATFORM_INVENTORY_RESULT.Topology`. On I386, native
preparation refuses to load `HALMPS.DLL` when an MPS table is present but its
revision is not 1.4. For MPS 1.2 it reports: “HALMPS.DLL is not supported in
Intel MPS version 1.2. RC95-OS must be reinstalled using an MPS 1.4-compatible
system.” The optional `ReportBootError` callback on the native-prepare request
receives this diagnostic before preparation stops. Processor and I/O
APIC records are also represented in the ARC configuration component tree.
The inventory capacity accounts for ACPI tables, PCI functions, processors,
and I/O APICs together.

## Adapter contract

`MapPhysical` must map the requested physical range for the duration of the
inventory call. BIOS scanning requires readable mappings for the BDA at
physical addresses 0x40E and 0x413 and the conventional/ROM scan windows.
UEFI callers should normally pass the RSDP address from the EFI configuration
table and may leave legacy scanning disabled.

The host regression target `rc95ldr_platforminventory_tests` exercises MADT
and MPS table parsing with synthetic firmware memory.


## Native FreeLoader firmware menu bindings

The BIOS FreeLoader build now supplies a VBE 2.x/3.x adapter in
`boot/freeldr/freeldr/arch/i386/pc/rc95ldr_biosmenu.c`. It queries the VBE
controller and mode list through INT 10h AX=4F00h/4F01h, requires a supported
linear-framebuffer mode, selects it with AX=4F02h (linear-framebuffer bit),
and reads navigation keys through INT 16h. The adapter accepts supported
RGBX/BGRX 32-bit layouts and RGB565 16-bit layouts; unsupported layouts fall
back to the existing text UI.

The UEFI FreeLoader build supplies a GOP adapter in
`boot/freeldr/freeldr/arch/uefi/rc95ldr_uefimenu.c`. It locates
`EFI_GRAPHICS_OUTPUT_PROTOCOL`, enumerates QueryMode results, supports the
standard RGB/BGR formats and equivalent PixelBitMask layouts, applies the
chosen mode with SetMode, and reads keys from Simple Text Input. Framebuffer
base, size, resolution and stride are refreshed after SetMode, rather than
reusing metadata from the previously active mode.

Both adapters are wired into `RunLoader` in `bootmgr.c`, using the existing
FreeLoader OS display-name list. A successful shared-menu selection dispatches
directly to the selected entry; if graphics setup or the shared menu is
unavailable, the legacy FreeLoader menu remains the compatibility fallback.
The BGFX and shared firmware-menu sources are included in both FreeLoader
firmware builds.

RunLoader now loads the selected boot volume's BCD store through
`Rc95LdrFreeldrLoadBcd`, walks the Boot Manager DisplayOrder with
`Rc95BcdCollectWindowsBootCandidates`, and retains the hive image until the
shared menu returns. Each candidate now carries its `Objects\\{GUID}` key
cell. The dispatcher prefers the BCD Default candidate (or the first valid
DisplayOrder candidate when no default is present) and passes that real hive
and object cell to the BIOS/UEFI menu, allowing
`Rc95BcdReadGraphicsModeDisabled` to decode the OS-loader Boolean instead
of silently assuming graphics are enabled. The image and candidate storage
are released after the menu call; all element views remain valid while used.

The visible menu entries and final `LoadOperatingSystem` dispatch still use
the existing FREELDR.INI compatibility list. Consequently the graphics policy
currently follows the BCD default candidate, not an independently matched
BCD object for every INI entry. Replacing that compatibility list with the
native BCD candidate list and dispatching the selected BCD object directly is
a separate remaining migration step. A missing, malformed, or oversized BCD
store leaves the renderer on its documented graphics-enabled default and
does not prevent the legacy FreeLoader menu from operating.

## PCI host bridges, ECAM segments, and resources

ACPI MCFG entries are validated and retained as a bounded list of PCI segment
windows (ECAM base, segment group, and inclusive start/end bus). Overlapping
bus windows within one segment, unaligned ECAM bases, invalid bus ranges, and
capacity overflow fail inventory construction. PCI enumeration walks each
declared segment and bus window and passes the segment number to the platform
configuration-space accessor. If firmware supplies no MCFG, the legacy
single-segment callback contract scans the configured segment across buses
0-255.

PCI function records now include their segment/BDF, IDs, class tuple, header
type, and an index/count into the resource array. Type-0 endpoint BARs and
Type-1 bridge BARs are decoded as I/O, memory, prefetchable, and 64-bit
resources. The inventory records assigned base addresses and register
offsets. BAR lengths are marked unknown unless the optional
QueryPciBarSize provider supplies a validated size from a trusted firmware or
resource-allocation source. The generic inventory path is read-only and never
writes all-ones to live BARs to probe sizing. PCI-to-PCI
bridges retain primary/secondary/subordinate bus numbers and decoded I/O,
memory, and prefetchable forwarding windows, including upper address fields.
The inventory is descriptive; it does not allocate or reprogram PCI resources.
Each PCI ARC configuration component serializes a fixed
RC95LDR_PCI_CONFIGURATION_DATA payload: the function header, six BAR resource
slots, and an optional decoded bridge record. Thus BAR/bridge metadata is
retained in the RC95 configuration tree rather than existing only in the
temporary inventory arrays. SMBIOS receives its own compact configuration
component, while the full SMBIOS summary remains in the inventory result.

## SMBIOS

An SMBIOS 2.x (_SM_ / _DMI_) or 3.x (_SM3_) entry point may be supplied
explicitly or found in the legacy BIOS ROM scan window. Entry-point and
intermediate checksums, table bounds, formatted-structure lengths, string-set
terminators, and optional 2.x structure counts are validated. The bounded
summary retains SMBIOS version, entry-point/table physical addresses, table
length and structure count, plus System Information manufacturer, product,
version, serial number and UUID when present. The original table remains
firmware-owned and is referenced by physical address; consumers must map it
through their own lifetime-safe platform service.

The platform inventory result's PCI segment, function, BAR, bridge and SMBIOS
arrays are caller-owned result storage. Any physical table mapping supplied
to MapPhysical must remain valid for the duration of the inventory call.

## MASM and GNU assembler dialect boundary

RC95LDR BIOS native assembly is not one source dialect that can be sent
unchanged to every assembler. The build selects the assembler language from
the toolchain:

- **MSVC:** CMake enables `ASM_MASM`; x86/x64 sources are preprocessed with
  `_USE_ML` and `__ASM__`, then assembled by `ml` or `ml64`. The MASM branch
  of `sdk/include/asm/asm.inc` supplies the compatibility macros.
- **GNU/Clang:** CMake enables `ASM`; GNU `as` receives preprocessed assembly
  with the non-`_USE_ML` branch of `asm.inc` (`.intel_syntax noprefix`, GAS
  directives, and GAS unwind directives). MASM keywords such as `PROC`,
  `ENDP`, `DWORD ... DUP (?)`, `LABEL`, and `SEGMENT/ENDS` are not GNU `as`
  syntax unless explicitly translated by a compatibility macro.

The GAS implementations are separate source files under
`arch/i386/gas/` and `arch/amd64/gas/`; they do not include `asm.inc` and
do not reuse MASM declarations. The current independent ports are:

- i386 native entry bridge: `arch/i386/gas/entry.S`
- i386 module-call bridge: `arch/i386/gas/module_transfer.S`
- AMD64 native entry bridge: `arch/amd64/gas/entry.S`
- AMD64 module-call bridge: `arch/amd64/gas/module_transfer.S`

The i386 GAS source set now also includes separate implementations of the
BIOS transition and bootstrap assembly:

- `arch/i386/gas/gateway.S`: copied low-memory protected/real-mode gateway,
  private GDT, IVT dispatch, register/flags packet, and return to the caller's
  protected-mode descriptor tables
- `arch/i386/gas/stage.S`: real-mode handoff validation, A20 enable/check,
  flat GDT setup, and protected-mode call to `Rc95LdrBiosStageMain`
- `arch/i386/gas/stage0.S`: 512-byte EDD disk bootstrap which reads and
  validates the B95S header, loads Stage 1, verifies CRC-32, and far-jumps
- `arch/i386/gas/stage0_cd.S`: 512-byte El Torito bootstrap which validates
  the resident B95S/Stage-1 image and CRC-32

GNU/Clang i386 CMake targets compile the gateway, module bridge, stage
transition and both boot-sector objects; when `CMAKE_OBJCOPY` is available,
the boot-sector objects also emit flat `.bin` artifacts. These are distinct
GAS implementations; the MASM originals remain selected for MSVC. The
boot-sector flat binaries still need to be consumed by the image packager,
and a successful assembly/link build does not replace emulator or physical
BIOS transition testing.

CMake selects the i386 GAS gateway, module bridge, protected-mode entry,
real-mode stage, and Stage-0 objects for GNU/Clang builds; MSVC continues to
select the original MASM implementations. This is an assembly-source port,
not yet proof of a bootable end-to-end image: the image packager must consume
the generated GAS Stage-0 binaries, and the transition still requires
emulator/firmware execution tests. The AMD64 entry and module bridges are
selected independently and still require GNU-target ABI/link verification.

For shared assembly, include `asm.inc` and use its cross-dialect abstraction
macros (for example `HEX()`, `PUBLIC`, `FUNC`/`ENDFUNC`, `GLOBAL_LABEL`,
`ALIGN`, `OFF()`, and the CFI/unwind wrappers). Keep unavoidable syntax
differences in explicit `#ifdef _USE_ML` branches. The separate GAS files
instead use GNU directives and Intel-syntax operand conventions, without
ELF-only `.type`/`.size` directives so they can also be assembled for GNU
COFF targets. Every port must be assembled and linked in GNU CI;
the `.S` suffix alone does not establish dialect compatibility.
