#include "rc95ldruefimemory.h"
#include <string.h>
#include <stdio.h>

static UINTN GetMapCalls;
static UINTN ExitCalls;
static UINTN LastExitKey;
static UINTN InvalidExitCount;
static EFI_STATUS ExitFailure;

static EFI_STATUS EFIAPI
TestGetMemoryMap(UINTN *MemoryMapSize,
                 EFI_MEMORY_DESCRIPTOR *MemoryMap,
                 UINTN *MapKey,
                 UINTN *DescriptorSize,
                 UINT32 *DescriptorVersion)
{
    UINTN Required = sizeof(EFI_MEMORY_DESCRIPTOR);

    ++GetMapCalls;
    if (!MemoryMapSize || !MapKey || !DescriptorSize ||
        !DescriptorVersion)
        return EFI_INVALID_PARAMETER;
    if (*MemoryMapSize < Required)
    {
        *MemoryMapSize = Required;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (!MemoryMap)
        return EFI_INVALID_PARAMETER;

    memset(MemoryMap, 0, sizeof(*MemoryMap));
    MemoryMap->Type = 7u; /* EfiConventionalMemory. */
    MemoryMap->PhysicalStart = 0x100000;
    MemoryMap->NumberOfPages = 256;
    *MemoryMapSize = Required;
    *MapKey = GetMapCalls;
    *DescriptorSize = sizeof(EFI_MEMORY_DESCRIPTOR);
    *DescriptorVersion = EFI_MEMORY_DESCRIPTOR_VERSION;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
TestExitBootServices(EFI_HANDLE ImageHandle, UINTN MapKey)
{
    (void)ImageHandle;
    ++ExitCalls;
    if (ExitFailure != EFI_SUCCESS)
        return ExitFailure;
    if (InvalidExitCount)
    {
        --InvalidExitCount;
        return EFI_INVALID_PARAMETER;
    }
    LastExitKey = MapKey;
    return EFI_SUCCESS;
}

static void
InitializeTables(EFI_SYSTEM_TABLE *SystemTable,
                 EFI_BOOT_SERVICES *BootServices)
{
    memset(SystemTable, 0, sizeof(*SystemTable));
    memset(BootServices, 0, sizeof(*BootServices));
    SystemTable->Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    SystemTable->Hdr.HeaderSize = sizeof(*SystemTable);
    SystemTable->BootServices = BootServices;
    BootServices->Hdr.Signature = EFI_BOOT_SERVICES_SIGNATURE;
    BootServices->Hdr.HeaderSize = sizeof(*BootServices);
    BootServices->GetMemoryMap = TestGetMemoryMap;
    BootServices->ExitBootServices = TestExitBootServices;
    GetMapCalls = 0;
    ExitCalls = 0;
    LastExitKey = 0;
    InvalidExitCount = 0;
    ExitFailure = EFI_SUCCESS;
}

static int
TestStaleMapKeyRetry(void)
{
    EFI_SYSTEM_TABLE SystemTable;
    EFI_BOOT_SERVICES BootServices;
    EFI_MEMORY_DESCRIPTOR MemoryMap[2];
    UINTN MapSize = 0, DescriptorSize = 0, MapKey = 0;
    UINT32 DescriptorVersion = 0;
    EFI_STATUS Status;

    InitializeTables(&SystemTable, &BootServices);
    InvalidExitCount = 1;
    Status = Rc95LdrUefiExitBootServices(&SystemTable,
        (EFI_HANDLE)(UINTN)1, MemoryMap, sizeof(MemoryMap),
        &MapSize, &DescriptorSize, &DescriptorVersion, &MapKey);

    return Status == EFI_SUCCESS &&
        GetMapCalls == 2 && ExitCalls == 2 &&
        LastExitKey == 2 && MapKey == 2 &&
        MapSize == sizeof(EFI_MEMORY_DESCRIPTOR) &&
        DescriptorSize == sizeof(EFI_MEMORY_DESCRIPTOR) &&
        DescriptorVersion == EFI_MEMORY_DESCRIPTOR_VERSION &&
        MemoryMap[0].PhysicalStart == 0x100000;
}

static int
TestBufferTooSmallDoesNotExit(void)
{
    EFI_SYSTEM_TABLE SystemTable;
    EFI_BOOT_SERVICES BootServices;
    EFI_MEMORY_DESCRIPTOR MemoryMap;
    UINTN MapSize = 99, DescriptorSize = 99, MapKey = 99;
    UINT32 DescriptorVersion = 99;
    EFI_STATUS Status;

    InitializeTables(&SystemTable, &BootServices);
    Status = Rc95LdrUefiExitBootServices(&SystemTable,
        (EFI_HANDLE)(UINTN)1, &MemoryMap, sizeof(MemoryMap) - 1u,
        &MapSize, &DescriptorSize, &DescriptorVersion, &MapKey);

    return Status == EFI_BUFFER_TOO_SMALL &&
        GetMapCalls == 1 && ExitCalls == 0 &&
        MapSize == sizeof(EFI_MEMORY_DESCRIPTOR) &&
        DescriptorSize == 0 && DescriptorVersion == 0 && MapKey == 0;
}

static int
TestRetryLimit(void)
{
    EFI_SYSTEM_TABLE SystemTable;
    EFI_BOOT_SERVICES BootServices;
    EFI_MEMORY_DESCRIPTOR MemoryMap[2];
    UINTN MapSize, DescriptorSize, MapKey;
    UINT32 DescriptorVersion;
    EFI_STATUS Status;

    InitializeTables(&SystemTable, &BootServices);
    InvalidExitCount = 32;
    Status = Rc95LdrUefiExitBootServices(&SystemTable,
        (EFI_HANDLE)(UINTN)1, MemoryMap, sizeof(MemoryMap),
        &MapSize, &DescriptorSize, &DescriptorVersion, &MapKey);

    return Status == EFI_ABORTED && GetMapCalls == 8 && ExitCalls == 8;
}

static int
TestNonKeyFailureIsNotRetried(void)
{
    EFI_SYSTEM_TABLE SystemTable;
    EFI_BOOT_SERVICES BootServices;
    EFI_MEMORY_DESCRIPTOR MemoryMap[2];
    UINTN MapSize, DescriptorSize, MapKey;
    UINT32 DescriptorVersion;
    EFI_STATUS Status;

    InitializeTables(&SystemTable, &BootServices);
    ExitFailure = EFI_DEVICE_ERROR;
    Status = Rc95LdrUefiExitBootServices(&SystemTable,
        (EFI_HANDLE)(UINTN)1, MemoryMap, sizeof(MemoryMap),
        &MapSize, &DescriptorSize, &DescriptorVersion, &MapKey);

    return Status == EFI_DEVICE_ERROR && GetMapCalls == 1 && ExitCalls == 1;
}

int main(void)
{
    if (!TestStaleMapKeyRetry())
    {
        puts("UEFI ExitBootServices stale-key retry failed");
        return 1;
    }
    if (!TestBufferTooSmallDoesNotExit())
    {
        puts("UEFI ExitBootServices buffer-size handling failed");
        return 1;
    }
    if (!TestRetryLimit())
    {
        puts("UEFI ExitBootServices retry limit failed");
        return 1;
    }
    if (!TestNonKeyFailureIsNotRetried())
    {
        puts("UEFI ExitBootServices non-key error handling failed");
        return 1;
    }
    puts("RC95LDR UEFI ExitBootServices tests passed");
    return 0;
}
