#include "rc95ldruefinativediscovery.h"
#include <string.h>

static RC95LDR_STATUS
EnumerateNativePartitions(void *Opaque, RC95LDR_PARTITION_INFO *Partitions,
                          size_t Capacity, size_t *Count)
{
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context =
        (RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *)Opaque;
    size_t Required;
    if (Count) *Count = 0;
    if (!Context || !Context->Initialized || !Partitions || !Count)
        return Rc95LdrInvalidParameter;
    Required = (size_t)Context->FirmwareServices.PartitionCount;
    if (Required > RC95LDR_UEFI_MAX_PARTITIONS)
        return Rc95LdrCorruptData;
    if (Required > Capacity)
        return Rc95LdrBufferTooSmall;
    if (Required)
        memcpy(Partitions, Context->FirmwareServices.Partitions,
               Required * sizeof(*Partitions));
    *Count = Required;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
MountNativePartition(void *Opaque, const RC95LDR_PARTITION_INFO *Partition,
                     RC95LDR_FILESYSTEM_KIND FileSystem,
                     RC95LDR_VOLUME *Volume)
{
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context =
        (RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *)Opaque;
    if (!Context || !Context->Initialized)
        return Rc95LdrInvalidParameter;
    return Rc95LdrUefiMountNativeVolume(&Context->Mounts, Partition,
                                        FileSystem, Volume);
}

static void
UnmountNativePartition(void *Opaque, RC95LDR_VOLUME *Volume)
{
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context =
        (RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *)Opaque;
    if (!Context || !Context->Initialized) return;
    Rc95LdrUefiUnmountNativeVolume(&Context->Mounts, Volume);
}

static RC95LDR_STATUS
ValidateNativeWindowsPartition(void *Opaque, const RC95LDR_VOLUME *Volume,
                               const RC95LDR_WINDOWS_INSTALLATION *Installation)
{
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context =
        (RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *)Opaque;
    if (!Context || !Context->Initialized)
        return Rc95LdrInvalidParameter;
    return Rc95LdrUefiValidateNativeWindowsVolume(
        &Context->Mounts, Volume, Installation);
}

RC95LDR_STATUS
Rc95LdrUefiInitializeNativeDiscovery(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context,
    const RC95LDR_UEFI_IMAGE_CONTEXT *Image,
    uint32_t Architecture)
{
    RC95LDR_STATUS Status;
    if (!Context || !Image || !Image->BootServices)
        return Rc95LdrInvalidParameter;
    memset(Context, 0, sizeof(*Context));

    Status = Rc95LdrUefiInitializeModuleServicesV2(
        Image, Architecture, &Context->FirmwareServices,
        &Context->ModuleServices);
    if (Status != Rc95LdrSuccess) goto Failure;

    Status = Rc95LdrUefiGetFirmwareOps(
        &Context->FirmwareServices, &Context->FirmwareOps);
    if (Status != Rc95LdrSuccess) goto FailureServices;

    Status = Rc95LdrUefiInitializeNativeMountsFromBootServices(
        &Context->Mounts, &Context->FirmwareOps, Image->BootServices);
    if (Status != Rc95LdrSuccess) goto FailureServices;

    Context->Initialized = 1;
    return Rc95LdrSuccess;

FailureServices:
    Rc95LdrUefiDestroyModuleServices(&Context->FirmwareServices);
Failure:
    memset(Context, 0, sizeof(*Context));
    return Status;
}

RC95LDR_STATUS
Rc95LdrUefiGetNativeDiscoveryOps(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context,
    RC95LDR_UEFI_DISCOVERY_OPS *Operations)
{
    if (!Context || !Context->Initialized || !Operations)
        return Rc95LdrInvalidParameter;
    memset(Operations, 0, sizeof(*Operations));
    Operations->Context = Context;
    Operations->EnumeratePartitions = EnumerateNativePartitions;
    Operations->MountVolume = MountNativePartition;
    Operations->UnmountVolume = UnmountNativePartition;
    Operations->ValidateWindowsVolume = ValidateNativeWindowsPartition;
    Operations->AllocatePool = Context->Mounts.AllocatePool;
    Operations->FreePool = Context->Mounts.FreePool;
    return Rc95LdrSuccess;
}

void
Rc95LdrUefiDestroyNativeDiscovery(
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT *Context)
{
    if (!Context || !Context->Initialized) return;
    Rc95LdrUefiDestroyNativeMounts(&Context->Mounts);
    Rc95LdrUefiDestroyModuleServices(&Context->FirmwareServices);
    memset(Context, 0, sizeof(*Context));
}
