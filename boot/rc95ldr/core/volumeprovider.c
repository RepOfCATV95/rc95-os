#include "rc95ldr_volumeprovider.h"
#include <string.h>

#define RC95LDR_HANDLE_INDEX_BITS 4u
#define RC95LDR_HANDLE_INDEX_MASK ((((uintptr_t)1u) << RC95LDR_HANDLE_INDEX_BITS) - 1u)

static void *
Rc95LdrEncodeVolumeHandle(size_t Index, uintptr_t Generation)
{
    return (void *)((Generation << RC95LDR_HANDLE_INDEX_BITS) |
                    (uintptr_t)(Index + 1u));
}

static RC95LDR_VOLUME_PROVIDER_SLOT *
Rc95LdrDecodeVolumeHandle(RC95LDR_VOLUME_PROVIDER_CONTEXT *Context,
                          void *Handle)
{
    uintptr_t Value = (uintptr_t)Handle;
    uintptr_t SlotCode, Generation;
    size_t Index;
    RC95LDR_VOLUME_PROVIDER_SLOT *Slot;
    if (!Context || !Value)
        return NULL;
    SlotCode = Value & RC95LDR_HANDLE_INDEX_MASK;
    Generation = Value >> RC95LDR_HANDLE_INDEX_BITS;
    if (!SlotCode || SlotCode > RC95LDR_VOLUME_PROVIDER_SLOTS || !Generation)
        return NULL;
    Index = (size_t)(SlotCode - 1u);
    Slot = &Context->Slots[Index];
    if (!Slot->InUse || Slot->Generation != Generation)
        return NULL;
    return Slot;
}

static RC95LDR_STATUS
Rc95LdrVolumeProviderOpen(void *Opaque, const char *Path, void **Handle)
{
    RC95LDR_VOLUME_PROVIDER_CONTEXT *Context = Opaque;
    const RC95LDR_FILESYSTEM *Fs;
    RC95LDR_VOLUME_PROVIDER_SLOT *Slot = NULL;
    void *FileHandle = NULL;
    uint64_t FileSize = 0;
    size_t Index;
    RC95LDR_STATUS Status;
    if (!Context || !Context->Volume || !Path || !*Path || !Handle)
        return Rc95LdrInvalidParameter;
    *Handle = NULL;
    Fs = Context->Volume->FileSystem;
    if (!Fs || !Fs->OpenRead || !Fs->ReadAt || !Fs->Close)
        return Rc95LdrUnsupported;
    for (Index = 0; Index < RC95LDR_VOLUME_PROVIDER_SLOTS; ++Index)
        if (!Context->Slots[Index].InUse) { Slot = &Context->Slots[Index]; break; }
    if (!Slot)
        return Rc95LdrOutOfMemory;
    Status = Fs->OpenRead(Fs->FileSystemContext, Path, &FileHandle, &FileSize);
    if (Status != Rc95LdrSuccess)
    {
        /* A filesystem may acquire resources before discovering an error.
           Its returned handle remains owned by the wrapper and must not leak. */
        if (FileHandle)
            Fs->Close(Fs->FileSystemContext, FileHandle);
        return Status;
    }
    if (!FileHandle)
        return Rc95LdrInvalidImage;
    Slot->Generation = (Slot->Generation + 1u) &
        (UINTPTR_MAX >> RC95LDR_HANDLE_INDEX_BITS);
    if (!Slot->Generation) Slot->Generation = 1u;
    Slot->FileHandle = FileHandle;
    Slot->FileSize = FileSize;
    Slot->Offset = 0;
    Slot->InUse = 1;
    *Handle = Rc95LdrEncodeVolumeHandle(Index, Slot->Generation);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrVolumeProviderInformation(void *Opaque, void *Handle,
                                 RC95LDR_FILE_INFORMATION *Information)
{
    RC95LDR_VOLUME_PROVIDER_CONTEXT *Context = Opaque;
    RC95LDR_VOLUME_PROVIDER_SLOT *Slot =
        Rc95LdrDecodeVolumeHandle(Context, Handle);
    if (!Slot || !Information) return Rc95LdrInvalidParameter;
    Information->Size = Slot->FileSize;
    Information->Attributes = 0;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrVolumeProviderRead(void *Opaque, void *Handle, void *Buffer,
                          size_t Length, size_t *BytesRead)
{
    RC95LDR_VOLUME_PROVIDER_CONTEXT *Context = Opaque;
    RC95LDR_VOLUME_PROVIDER_SLOT *Slot =
        Rc95LdrDecodeVolumeHandle(Context, Handle);
    const RC95LDR_FILESYSTEM *Fs;
    RC95LDR_STATUS Status;
    if (!Slot || (!Buffer && Length) || !BytesRead)
        return Rc95LdrInvalidParameter;
    *BytesRead = 0;
    /* Keep subtraction below defined even if a corrupted slot ever has
       an offset beyond the advertised file size. */
    if (Slot->Offset > Slot->FileSize ||
        (uint64_t)Length > Slot->FileSize - Slot->Offset)
        return Rc95LdrOutOfBounds;
    if (!Length) return Rc95LdrSuccess;
    Fs = Context->Volume->FileSystem;
    Status = Fs->ReadAt(Fs->FileSystemContext, Slot->FileHandle,
                        Slot->Offset, Buffer, Length);
    if (Status != Rc95LdrSuccess) return Status;
    Slot->Offset += Length;
    *BytesRead = Length;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrVolumeProviderSeek(void *Opaque, void *Handle, uint64_t Offset)
{
    RC95LDR_VOLUME_PROVIDER_CONTEXT *Context = Opaque;
    RC95LDR_VOLUME_PROVIDER_SLOT *Slot =
        Rc95LdrDecodeVolumeHandle(Context, Handle);
    if (!Slot) return Rc95LdrInvalidParameter;
    if (Offset > Slot->FileSize) return Rc95LdrOutOfBounds;
    Slot->Offset = Offset;
    return Rc95LdrSuccess;
}

static void
Rc95LdrVolumeProviderClose(void *Opaque, void *Handle)
{
    RC95LDR_VOLUME_PROVIDER_CONTEXT *Context = Opaque;
    RC95LDR_VOLUME_PROVIDER_SLOT *Slot =
        Rc95LdrDecodeVolumeHandle(Context, Handle);
    const RC95LDR_FILESYSTEM *Fs;
    if (!Slot) return;
    Fs = Context->Volume->FileSystem;
    Fs->Close(Fs->FileSystemContext, Slot->FileHandle);
    Slot->FileHandle = NULL;
    Slot->FileSize = 0;
    Slot->Offset = 0;
    Slot->InUse = 0;
}

RC95LDR_STATUS
Rc95LdrInitializeVolumeFileProvider(const RC95LDR_VOLUME *Volume,
    RC95LDR_VOLUME_PROVIDER_CONTEXT *Context, RC95LDR_FILE_PROVIDER *Provider)
{
    if (!Volume || !Volume->FileSystem || !Context || !Provider ||
        !Volume->FileSystem->OpenRead || !Volume->FileSystem->ReadAt ||
        !Volume->FileSystem->Close)
        return Rc95LdrInvalidParameter;
    memset(Context, 0, sizeof(*Context));
    Context->Volume = Volume;
    memset(Provider, 0, sizeof(*Provider));
    Provider->StructureSize = sizeof(*Provider);
    Provider->VolumeContext = Context;
    Provider->Open = Rc95LdrVolumeProviderOpen;
    Provider->GetFileInformation = Rc95LdrVolumeProviderInformation;
    Provider->Read = Rc95LdrVolumeProviderRead;
    Provider->Seek = Rc95LdrVolumeProviderSeek;
    Provider->Close = Rc95LdrVolumeProviderClose;
    return Rc95LdrSuccess;
}
