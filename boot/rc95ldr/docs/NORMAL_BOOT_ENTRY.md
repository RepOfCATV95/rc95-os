# RC95LDR normal boot entry and architecture policy

## Normal entry point

`Rc95LdrNormalBootEntry` in `core/rc95ldrentry.c` is the canonical normal-boot dispatch seam. It initializes the versioned `RC95LDR_BOOT_CONTEXT`, records the selected build architecture, and reports the current implementation stage through the context's stage/error fields.

The FreeLoader `BootMain` path calls this entry after machine and boot-device initialization. A non-success result is logged and deliberately falls through to the existing FreeLoader/rosload compatibility loader. This is a migration-safe integration: the new entry point is exercised by normal startup without pretending that an incomplete replacement can already transfer control to Windows.

## Supported integration architectures

- **i386:** included in the normal-entry build and FreeLoader startup dispatch.
- **AMD64:** included in the normal-entry build and FreeLoader startup dispatch.
- **ARM64:** explicitly deferred. Do not add ARM64 to the RC95LDR normal-boot matrix until all three graphics foundations are implemented and integrated: **BGRT** (firmware boot-graphics resource table handling), **GOP** (Graphics Output Protocol), and **BGFX** (the RC95LDR boot-graphics framework). The ARM64 loader must then receive a separately reviewed firmware-entry, display, memory-map and handoff implementation; merely compiling the x86 common entry is not ARM64 support.

The existing ARM/ARM64 FreeLoader code and firmware targets are not removed or re-labelled by this policy. This document describes RC95LDR's normal-boot integration schedule only.

## Completion gates

The entry currently returns `Rc95LdrUnsupported` after initializing its context. That is intentional until the following are connected end-to-end:

1. BIOS and UEFI platform adapters with firmware-specific block I/O and memory-map ownership.
2. ESP/BCD discovery and a corresponding BIOS boot-store path.
3. Selected installation validation and a mounted-volume lifetime contract.
4. PE32/PE32+ image loading, relocations, kernel/HAL and boot-driver preparation.
5. Architecture-specific loader block, page-table setup, final firmware quiesce and kernel handoff.
6. i386 and AMD64 build plus BIOS/UEFI VM regression coverage.

Until these gates pass, the FreeLoader compatibility fallback remains enabled. No ARM64 boot claim is implied by the i386/AMD64 integration.
