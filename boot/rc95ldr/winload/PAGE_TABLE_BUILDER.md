# RC95LDR baseline page-table construction

`Rc95LdrBuildBaselinePageTables` is the reference implementation for the RC95 native-provider `BuildPageTables` callback. The baseline callback is selected automatically when a platform does not provide an override. Native preparation must not publish an architecture handoff unless it receives a real, bounded page-table root.

## Arena and result contract

- The supplied arena must be a validated, writable, page-aligned physical allocation whose writable mapping is its identity address. The builder rejects an uninitialized arena, a nonzero initial used-byte count, an invalid architecture/mode pairing, or an arena outside the active physical-address limit.
- The root is the first page of the arena. All subordinate paging structures are allocated monotonically from the same arena, zeroed before use, and included in the page-aligned `PageTableStorageUsed` result.
- The provider validates that the root page lies completely inside the consumed prefix before it adds any PAE-window entries. It validates the final arena usage again after the PAE extension.
- The arena is bounded. Exhaustion returns `Rc95LdrBufferTooSmall`; the builder never silently truncates a RAM range or returns an empty root.

## Paging modes

| Mode | Structure | Physical limit |
|---|---|---|
| I386 non-PAE | 32-bit page directory and 4-KiB page tables | Strictly below 4 GiB; `PhysicalAddressBits` must be 32 |
| I386 PAE | Four-entry PDPT, page directories, 64-bit entries; 2-MiB leaves for identity RAM where possible | CPU-reported physical width, 33–52 bits |
| AMD64 | Four-level PML4 hierarchy with 2-MiB identity leaves where possible | CPU-reported physical width, 32–52 bits, with four-level 48-bit virtual addressing (LA57 is rejected); identity mappings are limited to the lower canonical virtual half |

The builder maps available firmware RAM only. I386 PAE identity-maps the low 4-GiB aperture and deliberately leaves the supplied PAE virtual window unmapped; the provider subsequently installs each validated high-physical-memory mapping there. AMD64 does not use the I386 PAE window.

## Required mappings and collision policy

1. Loaded-module headers and PE sections are mapped first at `ImageBase + RVA -> PhysicalBase + RVA`; gaps are left unmapped. Module virtual and physical extents may not overlap.
2. Provider-owned allocations, including the stack, heap, loader block, metadata, firmware snapshot, and page-table arena, are identity mapped.
3. Firmware ranges marked available are identity mapped. Reserved, unusable, ACPI, runtime, and temporary ranges are not mapped as ordinary RAM.
4. Module virtual and physical backing ranges are excluded from writable available-RAM identity leaves, including 2-MiB leaves. Any other conflicting translation is rejected; an identical existing translation is accepted.
5. Every address/length calculation is overflow checked, and every table walk is constrained to the supplied arena.

## PE image permissions and page conflicts

The PE validator retains each section RVA, declared virtual size, mapped span
(maximum of virtual size and raw-data size), and the READ/WRITE/EXECUTE
characteristics. It rejects malformed byte ranges and also rejects any
4-KiB-rounded overlap between headers and sections or between two sections.
This is intentional: distinct protection domains may not share one hardware
page, even when their byte ranges are adjacent or disjoint.

Every loaded module supplied to native preparation must carry a pointer to
immutable, validated section metadata that remains valid until the baseline
page-table callback returns. The kernel metadata is passed synchronously from
the validated kernel PE record; resource-only bootres metadata is retained in
its owned allocation. Platform HAL and boot-driver loaders must provide the
same metadata or native preparation fails closed. The builder maps only the
page-rounded headers and section spans; image gaps remain unmapped. Headers
are read-only. Section PTE write permission follows
IMAGE_SCN_MEM_WRITE, so read-only and RX sections are not writable and RW
sections are writable. Provider allocations and ordinary available RAM retain
their required writable mappings.

AMD64 sets EFER.NXE in the final transfer stub before loading the new
CR3, but only when the validated CPU record advertises both NX and MSR
support. I386 PAE enables EFER.NXE during architecture preparation, after
building the new tables and before the firmware transfer can activate them.
Headers and sections without IMAGE_SCN_MEM_EXECUTE receive NX; executable
sections do not. If NX is unavailable, the builder rejects
writable/non-executable sections rather than creating an unavoidable RWX
data mapping. I386 non-PAE cannot enforce execute-disable and therefore
fails closed for writable/non-executable sections. x86 also has no
read-disable PTE bit, so IMAGE_SCN_MEM_READ is retained as metadata while
present pages remain readable.

## Integration and verification

The native provider calls this implementation through the same callback contract used by platform-specific builders. The callback receives the PAE window explicitly so the baseline identity map cannot occupy addresses reserved for high-memory aliases. Host regression coverage verifies AMD64 hierarchy construction, module virtual-to-physical translation, arena bounds, and invalid-input rejection. I386 non-PAE and PAE hardware-mode boot tests remain necessary on their respective 32-bit targets.
