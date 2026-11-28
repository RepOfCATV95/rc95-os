#include "rc95ldrnativeprepare.h"
#include "rc95ldrmemorymap.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef union _LOADER_BUFFER {
    RC95LDR_RC95_NATIVE_LOADER_BLOCK Align;
    uint8_t Bytes[8192];
} LOADER_BUFFER;

typedef struct _ALLOCATION_LEDGER {
    uint32_t Calls;
    uint64_t VirtualBase, PhysicalBase, ImageSize, AllocationSize;
    RC95LDR_STATUS Result;
} ALLOCATION_LEDGER;

static RC95LDR_STATUS ValidateAllocation(void *opaque,
    uint64_t virtualBase, uint64_t physicalBase,
    uint64_t imageSize, uint64_t allocationSize)
{
    ALLOCATION_LEDGER *ledger=(ALLOCATION_LEDGER *)opaque;
    ++ledger->Calls;
    ledger->VirtualBase=virtualBase;
    ledger->PhysicalBase=physicalBase;
    ledger->ImageSize=imageSize;
    ledger->AllocationSize=allocationSize;
    return ledger->Result;
}

static const RC95LDR_PE_SECTION_INFO TestSections[1] = {
    { 0x1000u, 0x1000u, 0x1000u,
      RC95LDR_PE_SCN_MEM_READ | RC95LDR_PE_SCN_MEM_EXECUTE }
};

static void SetTestPeMetadata(RC95LDR_LOADED_MODULE *module)
{
    module->PeSections=TestSections;
    module->PeSectionCount=1u;
    module->PeSizeOfHeaders=0x400u;
}

static void InitModules(RC95LDR_LOADED_MODULE modules[3])
{
    memset(modules,0,3*sizeof(modules[0]));
    modules[0].ImageBase=0x100000;
    modules[0].ImageSize=0x20000;
    modules[0].EntryPoint=0x101000;
    modules[0].PhysicalBase=0x100000;
    modules[0].PhysicalSize=0x20000;
    modules[0].Flags=RC95LDR_MODULE_KERNEL;
    modules[0].Path="\\SystemRoot\\system32\\ntoskrnl.exe";
    SetTestPeMetadata(&modules[0]);

    modules[1].ImageBase=0x200000;
    modules[1].ImageSize=0x10000;
    modules[1].EntryPoint=0x201000;
    modules[1].PhysicalBase=0x200000;
    modules[1].PhysicalSize=0x10000;
    modules[1].Flags=RC95LDR_MODULE_HAL;
    modules[1].Path="\\SystemRoot\\system32\\hal.dll";
    SetTestPeMetadata(&modules[1]);

    modules[2].ImageBase=0x400000;
    modules[2].ImageSize=0x18000;
    modules[2].EntryPoint=0;
    modules[2].PhysicalBase=0x400000;
    modules[2].PhysicalSize=0x18000;
    modules[2].Flags=RC95LDR_MODULE_RESOURCE;
    modules[2].Path="\\SystemRoot\\System32\\bootres.dll";
    SetTestPeMetadata(&modules[2]);
}

static void InitNativeInput(RC95LDR_NATIVE_LOADER_INPUT *input,
    const RC95LDR_LOADED_MODULE *modules, uint32_t count,
    const uint8_t *hive, RC95LDR_MEMORY_RANGE *memory)
{
    memset(input,0,sizeof(*input));
    input->StructureSize=sizeof(*input);
    input->Version=RC95LDR_NATIVE_LOADER_INPUT_VERSION;
    input->OsMajorVersion=10;
    input->OsMinorVersion=0;
    input->OsBuildNumber=10240;
    input->Architecture=Rc95LdrNtArchAmd64;
    input->MachineType=0x8664;
    input->RegistryBase=hive;
    input->RegistryLength=32;
    input->Modules=modules;
    input->ModuleCount=count;
    input->MemoryRanges=memory;
    input->MemoryRangeCount=1;
    input->ArcBootDevice="multi(0)disk(0)rdisk(0)partition(1)";
    input->ArcHalDevice="multi(0)disk(0)rdisk(0)partition(1)";
    input->NtBootPath="\\RC95";
    input->NtHalPath="\\";
    input->LoadOptions="/NOEXECUTE=OPTIN";
}

static void VerifyBuiltLoadOrder(const RC95LDR_LOADED_MODULE *modules,
    uint32_t count, int expectResource)
{
    RC95LDR_NATIVE_LOADER_INPUT input;
    RC95LDR_MEMORY_RANGE memory={0x100,0x400,1,0};
    uint8_t hive[32]={0};
    LOADER_BUFFER buffer;
    RC95LDR_RC95_NATIVE_LOADER_BLOCK *block;
    RC95LDR_RC95_LIST_ENTRY *link;
    uint32_t seen=0, resourceSeen=0;
    size_t used=0;

    InitNativeInput(&input,modules,count,hive,&memory);
    assert(Rc95LdrBuildRc95NativeLoaderBlock(&input,buffer.Bytes,
        sizeof(buffer.Bytes),&used)==Rc95LdrSuccess);
    assert(used>sizeof(*block));
    block=(RC95LDR_RC95_NATIVE_LOADER_BLOCK *)(void *)buffer.Bytes;
    assert(block->Signature==0x4c393552u);
    assert(block->ModuleCount==count);
    link=(RC95LDR_RC95_LIST_ENTRY *)(uintptr_t)block->LoadOrderListHead.Flink;
    while(link!=(RC95LDR_RC95_LIST_ENTRY *)(void *)&block->LoadOrderListHead) {
        RC95LDR_RC95_MODULE_ENTRY *entry=(RC95LDR_RC95_MODULE_ENTRY *)
            ((uint8_t *)link - offsetof(RC95LDR_RC95_MODULE_ENTRY,LoadOrderLinks));
        assert(seen<count);
        assert(entry->ImageBase==modules[seen].ImageBase);
        assert(entry->ImageSize==modules[seen].ImageSize);
        assert(entry->Attributes==modules[seen].Flags);
        if(entry->Attributes&RC95LDR_MODULE_RESOURCE) {
            assert(strcmp(modules[seen].Path,
                "\\SystemRoot\\System32\\bootres.dll")==0);
            assert(entry->EntryPoint==0);
            ++resourceSeen;
        }
        ++seen;
        link=(RC95LDR_RC95_LIST_ENTRY *)(uintptr_t)link->Flink;
    }
    assert(seen==count);
    assert(resourceSeen==(expectResource?1u:0u));
}

static void TestGraphicsEnabledResourceHandoff(void)
{
    RC95LDR_LOADED_MODULE modules[3], resource;
    ALLOCATION_LEDGER ledger;
    uint32_t count=2, added=0;
    InitModules(modules);
    resource=modules[2];
    memset(&ledger,0,sizeof(ledger));
    ledger.Result=Rc95LdrSuccess;

    assert(Rc95LdrAppendBootResourceModule(&resource,ValidateAllocation,
        &ledger,modules+2,&count,1,&added)==Rc95LdrSuccess);
    assert(added==1 && count==3);
    assert(ledger.Calls==1);
    assert(ledger.VirtualBase==0x400000);
    assert(ledger.PhysicalBase==0x400000);
    assert(ledger.ImageSize==0x18000);
    assert(ledger.AllocationSize==0x18000);
    assert(modules[2].Flags==RC95LDR_MODULE_RESOURCE);
    assert(modules[2].EntryPoint==0);
    VerifyBuiltLoadOrder(modules,count,1);
}

static void TestGraphicsModeDisabledHandoff(void)
{
    RC95LDR_LOADED_MODULE modules[3];
    ALLOCATION_LEDGER ledger;
    uint32_t count=2, added=99;
    InitModules(modules);
    memset(&ledger,0,sizeof(ledger));
    ledger.Result=Rc95LdrSuccess;

    /* RC95LDR passes NULL when BCD graphicsmodedisabled is set. */
    assert(Rc95LdrAppendBootResourceModule(NULL,NULL,
        &ledger,modules+2,&count,1,&added)==Rc95LdrSuccess);
    assert(added==0 && count==2);
    assert(ledger.Calls==0);
    VerifyBuiltLoadOrder(modules,count,0);
}

static void TestInvalidOptionalResourceIsOmitted(void)
{
    RC95LDR_LOADED_MODULE modules[3], resource;
    ALLOCATION_LEDGER ledger;
    uint32_t count=2, added=99;
    InitModules(modules);
    resource=modules[2];
    memset(&ledger,0,sizeof(ledger));
    ledger.Result=Rc95LdrOutOfBounds;

    assert(Rc95LdrAppendBootResourceModule(&resource,ValidateAllocation,
        &ledger,modules+2,&count,1,&added)==Rc95LdrSuccess);
    assert(added==0 && count==2);
    assert(ledger.Calls==1);
    VerifyBuiltLoadOrder(modules,count,0);
}

static void TestRequiredModulesKeepCapacity(void)
{
    RC95LDR_LOADED_MODULE modules[2], resource;
    ALLOCATION_LEDGER ledger;
    uint32_t count=2, added=99;
    memset(modules,0,sizeof(modules));
    modules[0].Flags=RC95LDR_MODULE_KERNEL;
    modules[1].Flags=RC95LDR_MODULE_HAL;
    resource.ImageBase=0x400000;
    resource.ImageSize=0x18000;
    resource.PhysicalBase=0x400000;
    resource.PhysicalSize=0x18000;
    resource.Flags=RC95LDR_MODULE_RESOURCE;
    resource.Path="\\SystemRoot\\System32\\bootres.dll";
    SetTestPeMetadata(&resource);
    memset(&ledger,0,sizeof(ledger));
    ledger.Result=Rc95LdrSuccess;
    assert(Rc95LdrAppendBootResourceModule(&resource,ValidateAllocation,
        &ledger,modules,&count,2,&added)==Rc95LdrSuccess);
    assert(added==0 && count==2);
}

static void TestRamdiskAllocationClassification(void)
{
    RC95LDR_NATIVE_ALLOCATION_SET Set;
    memset(&Set,0,sizeof(Set));
    assert(Rc95LdrTrackNativeAllocation(&Set,RC95LDR_ALLOC_RAMDISK,
        0x800000,0x20000)==Rc95LdrSuccess);
    assert(Set.Count==1);
    assert(Set.Entries[0].Kind==RC95LDR_ALLOC_RAMDISK);
    assert(Set.Entries[0].MemoryType==LoaderXIPRom);
    assert(Rc95LdrTrackNativeAllocation(&Set,RC95LDR_ALLOC_RAMDISK,
        0x810000,0x10000)==Rc95LdrCorruptData);
}

int main(void)
{
    TestRamdiskAllocationClassification();
#if UINTPTR_MAX != UINT64_MAX
    puts("bootres handoff fixture requires the RC95 amd64 host ABI");
    return 0;
#else
    TestGraphicsEnabledResourceHandoff();
    TestGraphicsModeDisabledHandoff();
    TestInvalidOptionalResourceIsOmitted();
    TestRequiredModulesKeepCapacity();
    puts("RC95LDR bootres end-to-end loader-list handoff tests passed");
    return 0;
#endif
}
