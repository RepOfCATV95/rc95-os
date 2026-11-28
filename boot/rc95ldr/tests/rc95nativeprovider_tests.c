#include "rc95ldrrc95nativeprovider.h"
#include <assert.h>
#include <string.h>

static const void *DummyMap(void *Context, uint64_t Address, size_t Length)
{
    (void)Context; (void)Address; (void)Length;
    return NULL;
}
static RC95LDR_STATUS DummyReadPci(void *Context, uint16_t Segment,
    uint8_t Bus, uint8_t Device, uint8_t Function, uint16_t Offset,
    uint32_t *Value)
{
    (void)Context; (void)Segment; (void)Bus; (void)Device;
    (void)Function; (void)Offset; (void)Value;
    return Rc95LdrUnsupported;
}
static RC95LDR_STATUS DummyCpu(void *Context,
    RC95LDR_NT_TARGET_ARCH Architecture, RC95LDR_RC95_CPU_INFO *Information)
{
    (void)Context; (void)Architecture; (void)Information;
    return Rc95LdrUnsupported;
}
static RC95LDR_STATUS DummyPageTables(void *Context,
    RC95LDR_NT_TARGET_ARCH Architecture,
    const RC95LDR_RC95_CPU_INFO *CpuInformation,
    const RC95LDR_PAE_VIRTUAL_WINDOW *PaeWindow,
    const RC95LDR_PHYSMEM_ALLOCATION *Storage,
    const RC95LDR_LOADED_MODULE *Modules, uint32_t ModuleCount,
    const RC95LDR_NATIVE_ALLOCATION *Allocations, size_t AllocationCount,
    const RC95LDR_FIRMWARE_MEMORY_RANGE *Ranges, size_t RangeCount,
    size_t *PageTableStorageUsed, uint64_t *Root)
{
    (void)Context; (void)Architecture; (void)CpuInformation;
    (void)PaeWindow; (void)Storage; (void)Modules;
    (void)ModuleCount; (void)Allocations; (void)AllocationCount;
    (void)Ranges; (void)RangeCount; (void)PageTableStorageUsed; (void)Root;
    return Rc95LdrUnsupported;
}
static RC95LDR_STATUS DummyDriver(void *Context,
    const RC95LDR_HIVE_BOOT_DRIVER *Driver, RC95LDR_LOADED_MODULE *Module)
{
    (void)Context; (void)Driver; (void)Module;
    return Rc95LdrUnsupported;
}
static RC95LDR_STATUS DummyValidate(void *Context, uint64_t VirtualBase,
    uint64_t PhysicalBase, uint64_t ImageSize, uint64_t AllocationSize)
{
    (void)Context; (void)VirtualBase; (void)PhysicalBase;
    (void)ImageSize; (void)AllocationSize;
    return Rc95LdrUnsupported;
}

static void TestRejectsNull(void)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT Result;
    memset(&Result, 0xA5, sizeof(Result));
    assert(Rc95LdrPrepareRc95NativeInput(NULL, &Result) ==
        Rc95LdrInvalidParameter);
}

static void TestRejectsNonRc95Profile(void)
{
    RC95LDR_RC95_NATIVE_PROVIDER_INPUT Input;
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT Result;
    RC95LDR_OS_PROFILE Profile;
    RC95LDR_NATIVE_LOADER_INPUT Base;
    RC95LDR_PHYSMEM_OPS Memory;
    RC95LDR_FIRMWARE_MEMORY_RANGE Range;
    RC95LDR_ARC_DISK_INPUT Disk;
    unsigned char Hive[4096], Nls[16];
    memset(&Input, 0, sizeof(Input));
    memset(&Result, 0, sizeof(Result));
    memset(&Profile, 0, sizeof(Profile));
    memset(&Base, 0, sizeof(Base));
    memset(&Memory, 0, sizeof(Memory));
    memset(&Range, 0, sizeof(Range));
    memset(&Disk, 0, sizeof(Disk));
    memset(Nls, 0, sizeof(Nls));

    Profile.StructureSize = sizeof(Profile);
    Profile.Version = RC95LDR_OS_PROFILE_VERSION;
    Profile.OsFamily = Rc95LdrOsWindowsNtLegacy;
    Base.StructureSize = sizeof(Base);
    Base.Version = RC95LDR_NATIVE_LOADER_INPUT_VERSION;
    Base.Architecture = Rc95LdrNtArchPowerPc;

    Input.StructureSize = sizeof(Input);
    Input.Version = RC95LDR_RC95_PROVIDER_VERSION;
    Input.Profile = &Profile;
    Input.BaseInput = &Base;
    Input.SystemHive = Hive;
    Input.SystemHiveSize = sizeof(Hive);
    Input.SystemHivePhysicalBase = 0x100000;
    Input.SystemHiveAllocationSize = sizeof(Hive);
    Input.LoadBootDriver = DummyDriver;
    Input.ValidateModuleAllocation = DummyValidate;
    Input.PhysicalMemory = &Memory;
    Input.QueryCpu = DummyCpu;
    Input.BuildPageTables = DummyPageTables;
    Input.FirmwareRanges = &Range;
    Input.FirmwareRangeCount = 1;
    Input.Metadata.Nls.AnsiData = Nls;
    Input.Metadata.Nls.AnsiSize = sizeof(Nls);
    Input.Metadata.Nls.OemData = Nls;
    Input.Metadata.Nls.OemSize = sizeof(Nls);
    Input.Metadata.Nls.UnicodeData = Nls;
    Input.Metadata.Nls.UnicodeSize = sizeof(Nls);
    Disk.ArcName = "multi(0)disk(0)rdisk(0)partition(1)";
    Input.Metadata.Disks = &Disk;
    Input.Metadata.DiskCount = 1u;
    Input.BootDevice = "multi(0)disk(0)rdisk(0)partition(1)";
    Input.HalDevice = "multi(0)disk(0)rdisk(0)partition(1)";
    Input.SystemRoot = "\\SystemRoot";
    Input.PlatformInventory.StructureSize =
        sizeof(Input.PlatformInventory);
    Input.PlatformInventory.Version =
        RC95LDR_PLATFORM_INVENTORY_VERSION;
    Input.PlatformInventory.MapPhysical = DummyMap;
    Input.PlatformInventory.ReadPciConfig = DummyReadPci;

    assert(Rc95LdrPrepareRc95NativeInput(&Input, &Result) ==
        Rc95LdrInvalidParameter);
    assert(Result.AllocationCount == 0);
}


static void TestRejectsUnimplementedPowerPcBeforeAllocating(void)
{
    RC95LDR_RC95_NATIVE_PROVIDER_INPUT Input;
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT Result;
    RC95LDR_OS_PROFILE Profile;
    RC95LDR_NATIVE_LOADER_INPUT Base;
    RC95LDR_LOADED_MODULE Modules[2];
    RC95LDR_PE_SECTION_INFO PeSections[1];
    RC95LDR_PHYSMEM_OPS Memory;
    RC95LDR_FIRMWARE_MEMORY_RANGE Range;
    RC95LDR_ARC_DISK_INPUT Disk;
    unsigned char HiveStorage[8192], Nls[16], BiosState[16];
    unsigned char *Hive = (unsigned char *)
        (((uintptr_t)HiveStorage + 4095u) & ~(uintptr_t)4095u);
    memset(&Input, 0, sizeof(Input));
    memset(&Result, 0, sizeof(Result));
    memset(&Profile, 0, sizeof(Profile));
    memset(&Base, 0, sizeof(Base));
    memset(Modules, 0, sizeof(Modules));
    memset(PeSections, 0, sizeof(PeSections));
    PeSections[0].VirtualAddress=0x1000u;
    PeSections[0].VirtualSize=0x1000u;
    PeSections[0].MappedSize=0x1000u;
    PeSections[0].Characteristics=RC95LDR_PE_SCN_MEM_READ |
        RC95LDR_PE_SCN_MEM_EXECUTE;
    memset(&Memory, 0, sizeof(Memory));
    memset(&Range, 0, sizeof(Range));
    memset(Hive, 0, 4096u);
    memset(Nls, 0, sizeof(Nls));
    memset(BiosState, 0, sizeof(BiosState));

    Profile.StructureSize = sizeof(Profile);
    Profile.Version = RC95LDR_OS_PROFILE_VERSION;
    Profile.OsFamily = Rc95LdrOsRc95;
    Profile.Firmware = Rc95LdrFirmwareBios;
    Profile.BootEntry = Rc95LdrEntryRc95Winload;
    Profile.KernelAbi = Rc95LdrKernelAbiRc95;
    Profile.LoaderBlockKind = Rc95LdrLoaderBlockRc95;

    Modules[0].Flags = RC95LDR_MODULE_KERNEL;
    Modules[0].ImageBase = 0x100000;
    Modules[0].ImageSize = 0x20000;
    Modules[0].PhysicalBase = 0x100000;
    Modules[0].PhysicalSize = 0x20000;
    Modules[0].EntryPoint = 0x101000;
    Modules[0].PeSections = PeSections;
    Modules[0].PeSectionCount = 1u;
    Modules[0].PeSizeOfHeaders = 0x400u;
    Modules[1].Flags = RC95LDR_MODULE_HAL;
    Modules[1].ImageBase = 0x200000;
    Modules[1].ImageSize = 0x10000;
    Modules[1].PhysicalBase = 0x200000;
    Modules[1].PhysicalSize = 0x10000;
    Modules[1].PeSections = PeSections;
    Modules[1].PeSectionCount = 1u;
    Modules[1].PeSizeOfHeaders = 0x400u;

    Base.StructureSize = sizeof(Base);
    Base.Version = RC95LDR_NATIVE_LOADER_INPUT_VERSION;
    Base.Architecture = Rc95LdrNtArchPowerPc;
    Base.ModuleCount = 2;
    Base.Modules = Modules;

    Input.StructureSize = sizeof(Input);
    Input.Version = RC95LDR_RC95_PROVIDER_VERSION;
    Input.Profile = &Profile;
    Input.BaseInput = &Base;
    Input.KernelVirtualBase = 0x100000;
    Input.KernelPhysicalBase = 0x100000;
    Input.KernelImageSize = 0x20000;
    Input.KernelAllocationSize = 0x20000;
    Input.KernelEntry = 0x101000;
    Input.KernelPeSections = PeSections;
    Input.KernelPeSectionCount = 1u;
    Input.KernelPeSizeOfHeaders = 0x400u;
    Input.SystemHive = Hive;
    Input.SystemHiveSize = 4096u;
    Input.SystemHivePhysicalBase = (uint64_t)(uintptr_t)Hive;
    Input.SystemHiveAllocationSize = 4096u;
    Input.LoadBootDriver = DummyDriver;
    Input.ValidateModuleAllocation = DummyValidate;
    Input.PhysicalMemory = &Memory;
    Input.QueryCpu = DummyCpu;
    Input.BuildPageTables = DummyPageTables;
    Input.FirmwareRanges = &Range;
    Input.FirmwareRangeCount = 1;
    Input.Metadata.Nls.AnsiData = Nls;
    Input.Metadata.Nls.AnsiSize = sizeof(Nls);
    Input.Metadata.Nls.OemData = Nls;
    Input.Metadata.Nls.OemSize = sizeof(Nls);
    Input.Metadata.Nls.UnicodeData = Nls;
    Input.Metadata.Nls.UnicodeSize = sizeof(Nls);
    Disk.ArcName = "multi(0)disk(0)rdisk(0)partition(1)";
    Input.Metadata.Disks = &Disk;
    Input.Metadata.DiskCount = 1u;
    Input.BootDevice = "multi(0)disk(0)rdisk(0)partition(1)";
    Input.HalDevice = "multi(0)disk(0)rdisk(0)partition(1)";
    Input.SystemRoot = "\\SystemRoot";
    Input.PlatformInventory.StructureSize =
        sizeof(Input.PlatformInventory);
    Input.PlatformInventory.Version =
        RC95LDR_PLATFORM_INVENTORY_VERSION;
    Input.PlatformInventory.MapPhysical = DummyMap;
    Input.PlatformInventory.ReadPciConfig = DummyReadPci;
    Input.BiosFirmwareState = BiosState;
    Input.BiosFirmwareStateSize = sizeof(BiosState);
    Input.BiosFirmwareStateVersion = 1u;

    assert(Rc95LdrPrepareRc95NativeInput(&Input, &Result) ==
        Rc95LdrUnsupported);
    assert(Result.AllocationCount == 0);
}


static void TestPaeWindowRejectsModuleOverlapAndOverflow(void)
{
    RC95LDR_RC95_CPU_INFO Cpu;
    RC95LDR_PAE_VIRTUAL_WINDOW Window;
    RC95LDR_LOADED_MODULE Module;
    RC95LDR_FIRMWARE_MEMORY_RANGE Range;
    RC95LDR_PAE_PHYSICAL_MAPPING Mapping;
    size_t MappingCount = 0;

    memset(&Cpu, 0, sizeof(Cpu));
    memset(&Window, 0, sizeof(Window));
    memset(&Module, 0, sizeof(Module));
    memset(&Range, 0, sizeof(Range));
    memset(&Mapping, 0, sizeof(Mapping));

    Cpu.StructureSize = sizeof(Cpu);
    Cpu.Version = RC95LDR_RC95_CPU_INFO_VERSION;
    Cpu.PagingMode = RC95LDR_CPU_PAGING_I386_PAE;
    Cpu.FeatureFlags = RC95LDR_CPU_FEATURE_PAE |
        RC95LDR_CPU_FEATURE_CX8;
    Cpu.PhysicalAddressBits = 36u;
    Window.VirtualBase = 0x80000000ULL;
    Window.Length = 0x100000ULL;
    Range.Kind = Rc95LdrFirmwareMemoryAvailable;
    Range.Base = RC95LDR_PAE_WINDOW_PHYSICAL_MIN;
    Range.Length = 0x200000ULL;

    Module.ImageBase = Window.VirtualBase;
    Module.ImageSize = 0x1000u;
    assert(Rc95LdrBuildI386PaeWindowMappings(
        &Cpu, &Window, &Module, 1u, NULL, 0u, &Range, 1u,
        &Mapping, 1u, &MappingCount) == Rc95LdrInvalidImage);

    Module.ImageBase = UINT64_MAX - 0x7ffu;
    Module.ImageSize = 0x1000u;
    assert(Rc95LdrBuildI386PaeWindowMappings(
        &Cpu, &Window, &Module, 1u, NULL, 0u, &Range, 1u,
        &Mapping, 1u, &MappingCount) == Rc95LdrInvalidImage);
}


static void TestAttachUsesSeparatePreparationContext(void)
{
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_RC95_NATIVE_PLATFORM_CONTEXT Context;
    RC95LDR_RC95_NATIVE_PROVIDER_INPUT Input;
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT Result;
    RC95LDR_OS_PROFILE Profile;
    memset(&Platform, 0, sizeof(Platform));
    memset(&Context, 0, sizeof(Context));
    memset(&Input, 0, sizeof(Input));
    memset(&Result, 0xA5, sizeof(Result));
    memset(&Profile, 0, sizeof(Profile));
    Profile.OsFamily = Rc95LdrOsRc95;
    Profile.Firmware = Rc95LdrFirmwareBios;
    Input.Profile = &Profile;
    Context.Input = &Input;
    Context.Result = &Result;
    Platform.StructureSize = sizeof(Platform);
    Platform.Version = RC95LDR_WINLOAD_ABI_VERSION;

    assert(Rc95LdrAttachRc95NativeProvider(&Platform, &Context) ==
        Rc95LdrSuccess);
    assert(Platform.BuildNativePreparation ==
        Rc95LdrRc95NativeBuildPreparation);
    assert(Platform.ReleaseNativePreparation ==
        Rc95LdrRc95NativeReleasePreparation);
    assert(Platform.NativePreparationContext == &Context);
    assert(Result.StructureSize == 0);
}


static void TestReleaseRollsBackSharedUefiLedger(void)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT Result;
    RC95LDR_NATIVE_ALLOCATION_SET Shared;
    memset(&Result, 0, sizeof(Result));
    memset(&Shared, 0, sizeof(Shared));
    Result.StructureSize = sizeof(Result);
    Result.Version = RC95LDR_RC95_PROVIDER_VERSION;
    Result.SharedNativeAllocationSet = &Shared;
    Result.SharedNativeAllocationSetBefore.Count = 0;
    Result.SharedAllocationSetMerged = 1u;
    Shared.Count = 1u;
    Shared.Entries[0].Kind = RC95LDR_ALLOC_UEFI_MAP;
    Shared.Entries[0].PhysicalBase = 0x100000;
    Shared.Entries[0].Length = 0x1000;
    Shared.Entries[0].MemoryType = LoaderMemoryData;

    Rc95LdrReleaseRc95NativeInput(&Result);
    assert(Shared.Count == 0u);
}

static void TestReleaseIsIdempotent(void)
{
    RC95LDR_RC95_NATIVE_PROVIDER_RESULT Result;
    memset(&Result, 0, sizeof(Result));
    Rc95LdrReleaseRc95NativeInput(&Result);
    Rc95LdrReleaseRc95NativeInput(&Result);
    assert(Result.AllocationCount == 0);
}

int main(void)
{
    TestRejectsNull();
    TestRejectsNonRc95Profile();
    TestRejectsUnimplementedPowerPcBeforeAllocating();
    TestAttachUsesSeparatePreparationContext();
    TestReleaseRollsBackSharedUefiLedger();
    TestReleaseIsIdempotent();
    TestPaeWindowRejectsModuleOverlapAndOverflow();
    return 0;
}
