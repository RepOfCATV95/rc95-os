#include "rc95ldrmemorymap.h"
#include <assert.h>
#include <string.h>

typedef struct _TEST_EFI_DESCRIPTOR {
    uint32_t Type, Pad;
    uint64_t PhysicalStart, VirtualStart, NumberOfPages, Attribute;
} TEST_EFI_DESCRIPTOR;

typedef struct _TEST_UEFI {
    TEST_EFI_DESCRIPTOR Descriptors[3];
    unsigned GetCalls, ExitCalls;
} TEST_UEFI;

static RC95LDR_STATUS BiosNext(void *ctx, uint32_t *key,
    RC95LDR_FIRMWARE_MEMORY_RANGE *range, int *done)
{
    (void)ctx;
    if (*key == 0) {
        range->Base=0x100000; range->Length=0x100000;
        range->Kind=Rc95LdrFirmwareMemoryAvailable; *key=1; *done=0;
    } else if (*key == 1) {
        range->Base=0x200000; range->Length=0x1000;
        range->Kind=Rc95LdrFirmwareMemoryUnusable; *key=2; *done=0;
    } else if (*key == 2) {
        range->Base=0x201000; range->Length=0x2ff000;
        range->Kind=Rc95LdrFirmwareMemoryAvailable; *key=3; *done=0;
    } else *done=1;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS GetMap(void *ctx, void *buffer, size_t *bytes,
    size_t *stride, uint64_t *key, uint32_t *version)
{
    TEST_UEFI *u=(TEST_UEFI*)ctx;
    size_t need=sizeof(u->Descriptors);
    ++u->GetCalls;
    if (*bytes < need) return Rc95LdrBufferTooSmall;
    memcpy(buffer,u->Descriptors,need);
    *bytes=need; *stride=sizeof(TEST_EFI_DESCRIPTOR);
    *key=u->GetCalls; *version=1;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS ExitBoot(void *ctx, uint64_t image, uint64_t key)
{
    TEST_UEFI *u=(TEST_UEFI*)ctx;
    (void)image; (void)key; ++u->ExitCalls;
    return Rc95LdrSuccess;
}

int main(void)
{
    RC95LDR_FIRMWARE_MEMORY_RANGE raw[8];
    RC95LDR_MEMORY_RESERVATION_SET reservations, nativeReservations;
    RC95LDR_NATIVE_ALLOCATION_SET nativeAllocations;
    RC95LDR_LOADED_MODULE nativeModules[5];
    size_t nativeIndex;
    int sawKernel=0,sawHal=0,sawDriver=0,sawRegistry=0,sawNls=0,sawBootResource=0,sawHeapReservation=0,sawStackReservation=0,sawPageTables=0,sawUefiMap=0;
    RC95LDR_MEMORYMAP_RESULT map;
    RC95LDR_BIOS_MEMORY_OPS bios;
    RC95LDR_UEFI_MEMORY_OPS uefi;
    MEMORY_ALLOCATION_DESCRIPTOR descriptors[8];
    LIST_ENTRY head;
    size_t count=0, rawBytes=0, stride=0;
    uint32_t version=0;
    uint8_t efiBuffer[sizeof(TEST_EFI_DESCRIPTOR)*3];
    TEST_UEFI mock;
    size_t i;
    int sawHeap=0, sawStack=0, sawBad=0, sawFree=0;

    bios.Context=NULL; bios.E820Next=BiosNext;
    assert(Rc95LdrCollectBiosE820(&bios,raw,8,&count)==Rc95LdrSuccess);
    assert(count==3);

    memset(&reservations,0,sizeof(reservations));
    memset(&nativeReservations,0,sizeof(nativeReservations));
    memset(&nativeAllocations,0,sizeof(nativeAllocations));
    memset(nativeModules,0,sizeof(nativeModules));
    nativeModules[0].ImageBase=0xFFFF800000100000ULL;
    nativeModules[0].PhysicalBase=0x1000000;
    nativeModules[0].ImageSize=0x80000; nativeModules[0].PhysicalSize=0x80000;
    nativeModules[0].Flags=RC95LDR_MODULE_KERNEL;
    nativeModules[1].ImageBase=0xFFFF800000200000ULL;
    nativeModules[1].PhysicalBase=0x1080000;
    nativeModules[1].ImageSize=0x20000; nativeModules[1].PhysicalSize=0x20000;
    nativeModules[1].Flags=RC95LDR_MODULE_HAL;
    nativeModules[2]=nativeModules[1];
    nativeModules[2].Flags=RC95LDR_MODULE_BOOT_DRIVER; /* HALMPS dual role */
    nativeModules[3].ImageBase=0xFFFF800000300000ULL;
    nativeModules[3].PhysicalBase=0x10A0000;
    nativeModules[3].ImageSize=0x10000; nativeModules[3].PhysicalSize=0x10000;
    nativeModules[3].Flags=RC95LDR_MODULE_BOOT_DRIVER;
    nativeModules[4].ImageBase=0xFFFF800000400000ULL;
    nativeModules[4].PhysicalBase=0x10B0000;
    nativeModules[4].ImageSize=0x8000; nativeModules[4].PhysicalSize=0x10000;
    nativeModules[4].Flags=RC95LDR_MODULE_RESOURCE;
    nativeModules[4].Path="\\SystemRoot\\System32\\bootres.dll";
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_REGISTRY,0x1100000,0x10000)==Rc95LdrSuccess);
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_NLS,0x1120000,0x10000)==Rc95LdrSuccess);
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_LOADER_HEAP,0x1140000,0x20000)==Rc95LdrSuccess);
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_LOADER_STACK,0x1160000,0x10000)==Rc95LdrSuccess);
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_PAGE_TABLES,0x1180000,0x10000)==Rc95LdrSuccess);
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_UEFI_MAP,0x11A0000,0x10000)==Rc95LdrSuccess);
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_LOADER_BLOCK,0x11C0000,0x10000)==Rc95LdrSuccess);
    assert(Rc95LdrTrackNativeAllocation(&nativeAllocations,RC95LDR_ALLOC_BIOS_FIRMWARE_STATE,0x11E0000,0x10000)==Rc95LdrSuccess);
    assert(Rc95LdrRegisterNativeAllocations(nativeModules,5,
        nativeAllocations.Entries,nativeAllocations.Count,1,
        &nativeReservations)==Rc95LdrSuccess);
    assert(nativeReservations.Count==12);
    /* Generic metadata storage must not satisfy the distinct NLS-table
       allocation requirement merely because both use LoaderNlsData. */
    {
        RC95LDR_NATIVE_ALLOCATION_SET incomplete;
        RC95LDR_MEMORY_RESERVATION_SET rejected;
        memset(&incomplete,0,sizeof(incomplete));
        memset(&rejected,0,sizeof(rejected));
        assert(Rc95LdrTrackNativeAllocation(&incomplete,RC95LDR_ALLOC_REGISTRY,0x1200000,0x1000)==Rc95LdrSuccess);
        assert(Rc95LdrTrackNativeAllocation(&incomplete,RC95LDR_ALLOC_METADATA,0x1202000,0x1000)==Rc95LdrSuccess);
        assert(Rc95LdrTrackNativeAllocation(&incomplete,RC95LDR_ALLOC_LOADER_HEAP,0x1204000,0x1000)==Rc95LdrSuccess);
        assert(Rc95LdrTrackNativeAllocation(&incomplete,RC95LDR_ALLOC_LOADER_STACK,0x1206000,0x1000)==Rc95LdrSuccess);
        assert(Rc95LdrTrackNativeAllocation(&incomplete,RC95LDR_ALLOC_PAGE_TABLES,0x1208000,0x1000)==Rc95LdrSuccess);
        assert(Rc95LdrRegisterNativeAllocations(0,0,incomplete.Entries,
            incomplete.Count,0,&rejected)==Rc95LdrInvalidParameter);
    }
    /* A module without its physical allocation extent must not be accepted. */
    {
        RC95LDR_LOADED_MODULE missingExtent = nativeModules[0];
        RC95LDR_MEMORY_RESERVATION_SET rejectedReservations;
        memset(&rejectedReservations,0,sizeof(rejectedReservations));
        missingExtent.PhysicalBase=0;
        assert(Rc95LdrRegisterNativeAllocations(&missingExtent,1,0,0,0,
            &rejectedReservations)==Rc95LdrInvalidParameter);
    }
    for(nativeIndex=0;nativeIndex<nativeReservations.Count;nativeIndex++) {
        TYPE_OF_MEMORY type=nativeReservations.Entries[nativeIndex].MemoryType;
        if(type==LoaderSystemCode)sawKernel=1;
        if(type==LoaderHalCode)sawHal=1;
        if(type==LoaderBootDriver)sawDriver=1;
        if(type==LoaderRegistryData)sawRegistry=1;
        if(type==LoaderNlsData)sawNls=1;
        if(type==LoaderMemoryData &&
           nativeReservations.Entries[nativeIndex].Base==0x10B0000)
            sawBootResource=1;
        if(type==LoaderOsloaderHeap)sawHeapReservation=1;
        if(type==LoaderOsloaderStack)sawStackReservation=1;
        if(type==LoaderMemoryData) {
            if(nativeReservations.Entries[nativeIndex].Base==0x1180000)sawPageTables=1;
            if(nativeReservations.Entries[nativeIndex].Base==0x11A0000)sawUefiMap=1;
        }
    }
    assert(sawKernel && sawHal && sawDriver && sawRegistry && sawNls && sawBootResource &&
        sawHeapReservation && sawStackReservation && sawPageTables && sawUefiMap);
    assert(Rc95LdrReserveMemoryRange(&reservations,0x180000,0x2000,
        LoaderOsloaderHeap)==Rc95LdrSuccess);
    assert(Rc95LdrReserveMemoryRange(&reservations,0x300000,0x3000,
        LoaderOsloaderStack)==Rc95LdrSuccess);
    assert(Rc95LdrBuildFinalMemoryMap(raw,count,reservations.Entries,
        reservations.Count,&map)==Rc95LdrSuccess);
    for(i=0;i<map.RangeCount;i++) {
        assert(map.Ranges[i].PageCount != 0);
        if(map.Ranges[i].Type==LoaderOsloaderHeap) sawHeap=1;
        if(map.Ranges[i].Type==LoaderOsloaderStack) sawStack=1;
        if(map.Ranges[i].Type==LoaderBad) sawBad=1;
        if(map.Ranges[i].Type==LoaderFree) sawFree=1;
    }
    assert(sawHeap && sawStack && sawBad && sawFree);
    assert(Rc95LdrBuildArcMemoryDescriptorList(&map,descriptors,8,&head)==Rc95LdrSuccess);
    assert(head.Flink==&descriptors[0].ListEntry);
    assert(head.Blink==&descriptors[map.RangeCount-1].ListEntry);

    memset(&mock,0,sizeof(mock));
    mock.Descriptors[0].Type=7; /* EfiConventionalMemory */
    mock.Descriptors[0].PhysicalStart=0x100000;
    mock.Descriptors[0].NumberOfPages=0x100;
    mock.Descriptors[1].Type=8; /* EfiUnusableMemory */
    mock.Descriptors[1].PhysicalStart=0x200000;
    mock.Descriptors[1].NumberOfPages=0x10;
    mock.Descriptors[2].Type=4; /* EfiBootServicesData */
    mock.Descriptors[2].PhysicalStart=0x300000;
    mock.Descriptors[2].NumberOfPages=0x20;
    uefi.Context=&mock; uefi.ImageHandle=1;
    uefi.GetMemoryMap=GetMap; uefi.ExitBootServices=ExitBoot;
    assert(Rc95LdrUefiFinalizeMemoryMap(&uefi,efiBuffer,sizeof(efiBuffer),
        raw,8,&count,&rawBytes,&stride,&version)==Rc95LdrSuccess);
    assert(mock.GetCalls==1 && mock.ExitCalls==1);
    assert(count==3 && rawBytes==sizeof(mock.Descriptors));
    assert(raw[0].Kind==Rc95LdrFirmwareMemoryAvailable);
    assert(raw[1].Kind==Rc95LdrFirmwareMemoryUnusable);
    assert(raw[2].Kind==Rc95LdrFirmwareMemoryLoaderTemporary);

    /* The handoff variant builds NT descriptors from the final map before
       the successful ExitBootServices call, using only preallocated buffers. */
    mock.GetCalls=0; mock.ExitCalls=0;
    assert(Rc95LdrUefiFinalizeNtMemoryMap(&uefi,efiBuffer,sizeof(efiBuffer),
        raw,8,reservations.Entries,reservations.Count,&map,descriptors,8,
        &head,&rawBytes,&stride,&version)==Rc95LdrSuccess);
    assert(mock.GetCalls==1 && mock.ExitCalls==1);
    assert(map.RangeCount==6);
    assert(descriptors[0].MemoryType==LoaderFree);
    assert(descriptors[1].MemoryType==LoaderOsloaderHeap);
    assert(descriptors[2].MemoryType==LoaderFree);
    assert(descriptors[3].MemoryType==LoaderBad);
    return 0;
}
