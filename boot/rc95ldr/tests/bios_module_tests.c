#include "rc95ldrbiosmodule.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t Storage[8192 + 4096];
static uint8_t FileData[64];
static unsigned OpenCount, ReadCount, CloseCount, AllocateCount;
static unsigned FreeCount, ValidateCount;

static RC95LDR_STATUS OpenFile(void *Context, const uint16_t *Path,
                               RC95LDR_WINLOAD_FILE *File)
{
    (void)Context;
    assert(Path && Path[0] == 'X');
    ++OpenCount;
    File->FileContext = FileData;
    File->FileSize = sizeof(FileData);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS ReadFileAt(void *Context,
    const RC95LDR_WINLOAD_FILE *File, uint64_t Offset,
    void *Buffer, size_t Length)
{
    (void)Context;
    assert(File->FileContext == FileData);
    if (Offset > sizeof(FileData) || Length > sizeof(FileData) - (size_t)Offset)
        return Rc95LdrOutOfBounds;
    memcpy(Buffer, FileData + (size_t)Offset, Length);
    ++ReadCount;
    return Rc95LdrSuccess;
}

static void CloseFile(void *Context, RC95LDR_WINLOAD_FILE *File)
{
    (void)Context; (void)File;
    ++CloseCount;
}

static RC95LDR_STATUS AllocatePages(void *Context, uint64_t Size,
                                    uint64_t Alignment, uint64_t *Base)
{
    uintptr_t Address;
    (void)Context;
    assert(Alignment == 4096);
    assert(Size <= sizeof(Storage));
    Address = ((uintptr_t)Storage + 4095u) & ~(uintptr_t)4095u;
    *Base = (uint64_t)Address;
    ++AllocateCount;
    return Rc95LdrSuccess;
}

static void FreePages(void *Context, uint64_t Base, uint64_t Size)
{
    (void)Context;
    assert(Base != 0 && Size == 4096);
    ++FreeCount;
}

static RC95LDR_STATUS ValidateExtent(void *Context, uint64_t Physical,
    uint64_t Virtual, uint64_t Size, uint64_t Alignment)
{
    (void)Context;
    assert(Physical == Virtual);
    assert((Physical & (Alignment - 1u)) == 0);
    assert(Size <= sizeof(Storage));
    ++ValidateCount;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS RC95LDR_MODULE_ABI DummyTransfer(
    void *Context, uint64_t EntryPoint,
    const RC95LDR_MODULE_HANDOFF *Handoff)
{
    (void)Context; (void)EntryPoint; (void)Handoff;
    return Rc95LdrSuccess;
}

int main(void)
{
    static const uint16_t Path[] = { 'X', 0 };
    RC95LDR_WINLOAD_PLATFORM Platform;
    RC95LDR_MODULE_HANDOFF Parent;
    RC95LDR_STATUS Status;

    memset(&Platform, 0, sizeof(Platform));
    memset(FileData, 0, sizeof(FileData));
    Platform.StructureSize = sizeof(Platform);
    Platform.Version = RC95LDR_WINLOAD_ABI_VERSION;
    Platform.OpenFile = OpenFile;
    Platform.ReadFileAt = ReadFileAt;
    Platform.CloseFile = CloseFile;
    Platform.AllocatePages = AllocatePages;
    Platform.FreePages = FreePages;
    Platform.ValidateAllocationExtent = ValidateExtent;

    Status = Rc95LdrBiosLoadAndInvokeModule(&Platform, Path,
        Rc95LdrModuleVeneer, Rc95LdrModuleI386, NULL, 0,
        NULL, NULL);
    assert(Status == Rc95LdrInvalidParameter);
    assert(OpenCount == 0);

    memset(&Parent, 0, sizeof(Parent));
    Parent.Signature = RC95LDR_MODULE_SIGNATURE;
    Parent.StructureSize = sizeof(Parent);
    Parent.Version = RC95LDR_MODULE_HANDOFF_VERSION;
    Parent.ModuleKind = Rc95LdrModuleRc95Ldr;
    Parent.Firmware = Rc95LdrModuleBios;
    Parent.Architecture = Rc95LdrModuleI386;
    Parent.ImageBase = (uint64_t)(uintptr_t)Storage;
    Parent.ImageSize = 4096;
    Parent.EntryPoint = Parent.ImageBase + 16;
    Status = Rc95LdrBiosLoadAndInvokeModule(&Platform, Path,
        Rc95LdrModuleVeneer, Rc95LdrModuleI386, &Parent,
        sizeof(Parent) - 1u, DummyTransfer, NULL);
    assert(Status == Rc95LdrInvalidParameter);
    assert(OpenCount == 0);

    Status = Rc95LdrBiosLoadAndInvokeModule(&Platform, Path,
        Rc95LdrModuleVeneer, Rc95LdrModuleI386, NULL, 0,
        DummyTransfer, NULL);
    assert(Status == Rc95LdrInvalidImage);
    assert(OpenCount == 1 && ReadCount == 1 && CloseCount == 1);
    assert(AllocateCount == 1 && FreeCount == 1 && ValidateCount == 1);
    return 0;
}
