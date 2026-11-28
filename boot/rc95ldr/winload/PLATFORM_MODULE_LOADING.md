# RC95LDR platform HAL and boot-driver PE loading

The native page-table builder requires the validated PE section table for every
mapped executable module. Platform adapters must not synthesize a uniform
permission record or keep a pointer to a temporary `RC95LDR_PE_INFO`.

## Common loader

Use `Rc95LdrWinloadLoadPeModule` from
`winload/include/rc95ldrwinload.h` in both the HAL-loading portion of
`BuildNativePreparation` and the platform's `LoadBootDriver` callback.

The helper:

1. Opens and reads the requested file through the platform file provider.
2. Calls `Rc95LdrPeValidate` for the requested I386/AMD64 machine.
3. Allocates a section-aligned image extent and maps it with
   `Rc95LdrPeMapEx`, using the adapter's explicit import resolver.
4. Copies the validated `RC95LDR_PE_SECTION_INFO[]` immediately after the
   page-rounded image, inside the same allocator-owned extent.
5. Returns a complete `RC95LDR_LOADED_MODULE`: `ImageBase`, `ImageSize`,
   `EntryPoint`, allocator-returned `PhysicalBase`/`PhysicalSize`, flags,
   stable path, and `PeSections`/`PeSectionCount`/`PeSizeOfHeaders`.

The metadata pointer therefore remains valid until the module allocation is
released. The platform allocator's WINLOAD contract requires allocations to
remain identity-mapped and writable until handoff; the helper does not cast an
unrelated virtual address into a physical address.

## HAL integration

The platform's `BuildNativePreparation` implementation should load the HAL
with `ModuleFlags = RC95LDR_MODULE_HAL`, then place the returned record in
its persistent `BaseInput.Modules[]` array before calling
`Rc95LdrRc95NativeBuildPreparation` or the relevant native provider. Keep the
record and its metadata allocation alive through baseline page-table
construction and handoff.

For I386 multiprocessor HALs, native preparation may create a second
boot-driver *record* by copying the HAL record. That alias must retain the
same section metadata and backing extent. It is not a second allocation and
must not be released separately.

## Boot-driver integration

The platform's `LoadBootDriver` callback should resolve the registry
`ImagePath` using its filesystem/SystemRoot policy, convert the resolved path
to the WINLOAD UTF-16 path form, and call the same helper with
`ModuleFlags = RC95LDR_MODULE_BOOT_DRIVER`. Supply a stable ANSI module path
whose storage outlives native preparation. Return the helper's complete
module record to `Rc95LdrPrepareNativeLoaderBlock`; do not copy only the
image addresses and discard the PE metadata.

The helper accepts an explicit import resolver. A platform must provide a
resolver that enforces the target OS's dependency policy; it must not bind
imports to arbitrary symbols or return addresses outside validated dependency
images.

## Ownership

Each successful helper call transfers ownership of one complete physical
allocation to the returned module record. Release it exactly once with
`Rc95LdrWinloadReleasePeModule`, after native preparation no longer needs its
image or section metadata. If a HAL record is aliased into the boot-driver
list, release only the original allocation. Do not release a module while
`BaseInput.Modules[]` or the page-table builder still references it.

The resource-only `bootres.dll` path remains separate: it uses
`Rc95LdrPeValidateResource`/`Rc95LdrPeMapResource` and stores its own
validated section metadata in its resource allocation.

## Adapter implementation boundary

The BIOS and UEFI entry files in this branch are entry shims. The actual
`BuildNativePreparation` and `LoadBootDriver` callbacks are supplied by the
platform startup adapter, not implemented in those shims. The shared helper
provides the common implementation those callbacks must call; the provider
rejects missing metadata rather than accepting an unsafe fallback.
