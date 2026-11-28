# RC95LDR and WINLOAD Future Plans

## Purpose

This document is the consolidated planning reference for the future RC95-OS boot path. It records the intended relationship between the existing FreeLoader fallback and the planned RC95LDR and WINLOAD components, along with the capabilities that must be complete before those components can operate as standalone boot stages.

This is a forward-looking plan, not a claim that the standalone transition is already complete.

## Shipping and fallback policy

**RC95-OS will continue to ship FreeLoader as a supported fallback.** The introduction and eventual standalone operation of RC95LDR and WINLOAD does not mean that FreeLoader is being removed, silently disabled, or made unavailable.

FreeLoader remains the recovery and compatibility path while the new boot path is developed, integrated, and validated. It must remain buildable and shippable alongside the newer components. The project may select a preferred boot path on supported configurations, but a user must retain a documented way to boot through FreeLoader when the new path is unavailable or fails its eligibility checks.

The standalone milestone described below changes the deployment and execution independence of RC95LDR and WINLOAD; it does not change this FreeLoader shipping policy.

## Intended component responsibilities

- **RC95LDR** is the RC95-OS-specific boot manager / loader front end. It is responsible for discovering bootable installations and boot devices, interpreting boot policy and configuration, selecting an installation, and preparing the handoff to the OS loader.
- **WINLOAD** is the OS loader stage. It is responsible for loading and validating the selected kernel and its boot-critical dependencies, preparing the loader state expected by the kernel and transferring control through the defined kernel entry contract.
- **FreeLoader** remains the separately available fallback bootloader. It is not a prerequisite that must be removed before RC95LDR and WINLOAD can become standalone.

The exact division of low-level services between RC95LDR and WINLOAD must remain explicit in their interfaces. Shared functionality should be provided through documented contracts or reusable libraries rather than hidden assumptions about another boot stage's internal state.

## Standalone transition prerequisites

RC95LDR and WINLOAD are to become standalone only after **all** of the following capability areas are implemented, integrated, and validated. A partial implementation in one area does not satisfy the overall standalone gate.

### 1. Memory mapping and memory management

- Establish and document the physical-memory map, reserved regions, firmware-owned ranges, loader allocations, and usable RAM ranges for each supported boot environment.
- Provide the required physical-to-virtual mapping operations and page-table setup for the target architecture and boot mode.
- Track allocations and ownership across the RC95LDR-to-WINLOAD transition; prevent loader data, page tables, kernel images, and boot-driver images from overlapping.
- Define the final memory map and memory descriptors passed to the kernel, including the treatment of reclaimable loader memory.
- Validate boundary conditions, low-memory constraints, alignment, address-width limits, and failure paths.

### 2. Kernel loading and handoff

- Load the supported kernel image formats with bounds, architecture, machine, and header validation.
- Resolve image placement, relocations where required, entry-point validation, and required alignment.
- Construct the complete loader block / boot parameter structures required by the RC95-OS kernel.
- Transfer all required firmware, platform, boot-device, command-line, and memory-map information through documented structures.
- Define the CPU state, paging state, interrupt state, stack, and calling convention at the kernel entry point.
- Perform a deterministic handoff and provide diagnosable errors when loading or handoff validation fails.

### 3. Boot-time driver loading

- Discover, select, validate, and load boot-start drivers required to access the system volume and initialize the boot-critical platform.
- Preserve driver ordering and dependency information, and reject missing or incompatible required drivers with actionable diagnostics.
- Populate the kernel loader data structures with driver names, paths, image ranges, and other metadata required by the kernel.
- Ensure that boot-time drivers are available before the kernel requires their services; do not rely on a filesystem or storage stack that has not yet been initialized.
- Validate the complete boot path using configurations that require boot-critical storage and platform drivers.

### 4. Pre-OS video modes

- Provide reliable pre-OS display initialization and video-mode selection for supported firmware and hardware paths.
- Define a stable interface for querying available modes and setting a selected mode, including framebuffer geometry, pixel format, pitch, and framebuffer address.
- Preserve a usable text or recovery display path when a preferred graphics mode cannot be established.
- Pass validated display information to the kernel using the agreed loader handoff structures.
- Test mode changes, unsupported modes, framebuffer bounds, and fallback behavior before kernel entry.

### 5. Filesystem and storage support

- Implement the filesystem and storage access needed to discover installations and read every required boot artifact without depending on FreeLoader internals.
- Provide consistent device discovery and path resolution across supported firmware / boot environments.
- Validate filesystem metadata, file sizes, image bounds, and read failures; handle unavailable, corrupt, and unsupported volumes without unsafe continuation.
- Support the filesystems required by the RC95-OS installation and boot layout, including the necessary discovery and access paths for the system volume.
- Keep filesystem and block-device interfaces sufficiently modular to support additional devices and filesystems without changing the kernel handoff contract.

RC95LDR filesystem support now includes read-only FAT32 for the UEFI ESP,
read-only NTFS, and a native read-only Btrfs provider for supported
single-device volumes. The Btrfs implementation validates superblocks and
SYSTEM chunks, builds bounded chunk/root/filesystem-tree views, resolves the
default subvolume, and exposes regular-file reads through the firmware-neutral
provider ABI. UEFI has a scoped BCD-selected Btrfs provider bridge; BIOS has
partition discovery, mount/provider binding, and a Btrfs WINLOAD transaction
entrypoint. BIOS BCD/WINLOAD selection and end-to-end firmware boot validation
remain qualification work. Compressed extents, data-checksum verification,
symlink traversal, non-default subvolumes, and multi-device/RAID volumes remain
unsupported and must fail closed. This bootloader support is read-only and
does not replace the in-kernel Btrfs driver or the legacy FreeLoader fallback.
See `boot/rc95ldr/docs/BTRFS_SUPPORT.md` for the implementation boundary and
current detailed status.

## Implementation and rollout stages

The following stages are ordered gates. Work may proceed in parallel where interfaces are stable, but a later stage must not be declared complete until its dependencies have been tested together.

1. **Contract definition:** document the RC95LDR/WINLOAD boundary, loader data structures, ownership rules, error reporting, and kernel entry contract.
2. **Independent discovery and file access:** implement device discovery, filesystem access, installation selection, and robust loading of boot artifacts without relying on FreeLoader-private state.
3. **Memory and image preparation:** complete memory-map handling, allocation tracking, image placement, relocations, and final loader memory descriptors.
4. **Boot-driver preparation:** load and describe all required boot-time drivers in dependency order.
5. **Pre-OS display integration:** implement video-mode selection, fallback display behavior, and display handoff metadata.
6. **Kernel handoff:** complete and validate the architecture-specific CPU / paging state and kernel entry transition.
7. **Standalone qualification:** run end-to-end boot, failure-injection, recovery, and regression tests across the supported firmware, storage, filesystem, and display configurations.
8. **Default-path consideration:** only after qualification may RC95LDR/WINLOAD be selected as the normal boot path for configurations they support. Keep FreeLoader available and shippable as fallback.

## Standalone completion criteria

The standalone transition is complete only when all five prerequisite areas above—memory mapping, kernel handoff, boot-time driver loading, pre-OS video modes, and filesystem/storage support—are implemented and work together in a reproducible end-to-end boot.

Qualification must demonstrate at least:

- A clean boot from the supported installation and boot-device configurations.
- Correct kernel and boot-driver loading, with verified image bounds and loader metadata.
- A correct final memory map and deterministic kernel handoff.
- A usable pre-OS display path, including recovery behavior when a requested mode is unavailable.
- Correct filesystem discovery and file reads, including clear handling of corrupt or unsupported inputs.
- Useful diagnostics for failures at each stage and a working FreeLoader fallback route.
- Regression coverage showing that adding the standalone path does not break existing FreeLoader boot support.

Until these criteria are met, RC95LDR and WINLOAD must be treated as work in progress and must not be represented as a fully independent replacement for the existing boot path.

## Documentation maintenance

This file is the canonical future-planning document for RC95LDR, WINLOAD, their standalone transition, and the continued FreeLoader fallback policy. New planning notes on these subjects should be incorporated here instead of creating additional overlapping planning documents.

When older planning documents are present in the repository, migrate any unique and still-valid requirements into this document, then replace the older documents with a short pointer to this file or remove them if they are obsolete. Do not delete a document until its contents have been checked and any unique requirements have been preserved.

Implementation status belongs in code, tests, and focused implementation documentation; this file describes intended architecture, sequencing, and completion gates.


## Migration inventory and compatibility boundaries

The migration is responsibility-based; source files must not be moved mechanically without preserving behavior and tests. The intended ownership boundaries are:

| Existing responsibility | RC95LDR / WINLOAD destination |
| --- | --- |
| BIOS boot sectors, real-mode entry, BIOS disk bootstrap | RC95LDR BIOS entry and platform block-I/O adapter |
| UEFI entry, device paths, GOP, Block I/O and ExitBootServices lifecycle | RC95LDR UEFI entry and UEFI platform adapter |
| ARC path parsing and legacy device naming | RC95LDR ARC compatibility layer mapping to normalized device/partition identities |
| Disk, partition, RAM-disk and SCSI enumeration | RC95LDR storage and partition manager |
| Filesystem dispatch and file access | RC95LDR filesystem interface and filesystem adapters |
| BCD, legacy INI, boot options and OS selection | RC95LDR BCD/policy/UI; legacy INI remains a compatibility provider while needed |
| Display, video and boot graphics | RC95LDR firmware-neutral display/graphics interface with GOP and legacy framebuffer backends |
| PE image mapping, kernel/HAL preparation, registry and boot-driver preparation | WINLOAD |
| Architecture-specific page tables, CPU context and kernel entry | WINLOAD architecture backends and handoff layer |
| Resume orchestration | WINLOAD common resume logic with architecture-specific restore hooks |

Firmware-specific services must stay behind platform adapters. BIOS code must not call UEFI protocols, and UEFI code must not depend on BIOS interrupts. Shared policy, BCD, partition identity, filesystem, image-loading and OS-preparation code should use firmware-neutral interfaces.

The BIOS real-mode stage should remain minimal: establish a known CPU state, preserve boot-drive identity, perform bounded reads through the BIOS adapter, and transfer to the next execution mode. BCD parsing, filesystem policy, menus and OS selection do not belong in 16-bit startup code. UEFI must enumerate Block I/O and device paths, identify ESPs by GPT type GUID, read the BCD store, and defer ExitBootServices until no further boot-service calls are needed.

The WINLOAD migration inventory includes PE32/PE32+ validation and relocation, kernel/HAL selection, boot-start driver and INF dependency processing, SYSTEM hive/control-set preparation, loader memory descriptors, architecture-specific page tables/context, loader-block construction, final firmware quiesce and kernel handoff. IA-32 and AMD64 may share algorithms where their data models permit, but pointer-width assumptions must not leak across architectures; fixed-width on-disk and loader-block fields require layout assertions.

## Normal-entry integration policy

Rc95LdrNormalBootEntry is the intended normal-boot dispatch seam. The existing FreeLoader BootMain integration may exercise it after machine and boot-device initialization, but an unsupported or incomplete RC95LDR result must continue to fall through to the existing FreeLoader/rosload compatibility loader. Calling the new entry point during startup is not evidence that the replacement can already boot the kernel.

The current integration targets are i386 and AMD64. ARM64 remains deferred until BGRT handling, GOP integration, and the RC95LDR BGFX framework are implemented together and a separate ARM64 firmware-entry, display, memory-map and handoff implementation is reviewed. Existing ARM/ARM64 FreeLoader support is not removed or re-labelled by this schedule.

The standalone migration does not authorize deleting legacy code merely because a new module exists. Keep the existing BIOS and UEFI FreeLoader entry points, boot sectors, freeldr.sys, uefildr.efi, and compatibility routes until each replacement has equivalent tested behavior on its relevant firmware. Likewise, preserve ARC boot compatibility while callers are migrated. New executable names and installer/image-packaging references change only when the replacement target is actually built and selected by the boot flow.

## Required validation gates for retiring individual legacy paths

Before retiring a legacy implementation, require:

- Host unit tests for parsers, bounds, BCD policy, partition resolution and PE metadata; malformed input must fail closed.
- i386 and AMD64 build coverage for the supported compiler configurations.
- BIOS/CSM and UEFI virtual-machine boot tests, including separate ESP and NTFS volumes.
- Hardware coverage for disk controllers, removable media and firmware variations where available.
- Boot traces identifying the selected BCD object, resolved device, kernel, HAL, boot drivers, loader block and final entry address.
- Recovery-path and legacy ARC/INI compatibility tests.

The project must keep RC95LDR RC95-OS-specific. It is not a general-purpose bootloader SDK or a compatible loader for other operating-system forks. In particular, the defunct CATV 95 LiberNT fork is not a supported profile and must not reuse RC95LDR build targets, image packaging, configuration, loader-block structures or kernel-handoff contracts. Any genuinely OS-independent shared component requires an explicit interface review and independent validation by each consumer.
