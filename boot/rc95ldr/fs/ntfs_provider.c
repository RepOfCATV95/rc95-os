#include "rc95ldr_ntfs_provider.h"
#include "rc95ldr_ntfs_volume.h"
#include "rc95ldr_ntfs_index.h"
#include "rc95ldr_ntfs_stream.h"
#include <string.h>

#define NTFS_ATTR_DATA 0x80u
#define NTFS_FILE_DIRECTORY 0x0002u
#define NTFS_FILE_IN_USE 0x0001u
#define NTFS_FILE_ATTR_COMPRESSED 0x0001u
#define NTFS_FILE_ATTR_ENCRYPTED 0x4000u
#define NTFS_FILE_ATTR_SPARSE 0x8000u
#define NTFS_PROVIDER_WORKSPACE_SECTORS 2u

static size_t
NtfsProviderSlotBytes(const RC95LDR_NTFS_VOLUME *V)
{
    size_t N = 0, T;
    if (!Rc95LdrMultiplySize(V->Geometry.FileRecordBytes, 2u, &N) ||
        !Rc95LdrAddSize(N, V->Geometry.IndexBufferBytes, &T) ||
        !Rc95LdrAddSize(T, (size_t)V->Geometry.BytesPerSector *
                           NTFS_PROVIDER_WORKSPACE_SECTORS, &N))
        return 0;
    return N;
}

RC95LDR_STATUS
Rc95LdrNtfsFileSystemWorkspaceSize(
    const RC95LDR_NTFS_VOLUME *V, size_t *WorkspaceLength)
{
    size_t SlotBytes, Total;
    if (!V || !V->Mounted || !WorkspaceLength)
        return Rc95LdrInvalidParameter;
    SlotBytes = NtfsProviderSlotBytes(V);
    if (!SlotBytes || !Rc95LdrMultiplySize(
            SlotBytes, RC95LDR_NTFS_PROVIDER_MAX_OPEN_FILES, &Total))
        return Rc95LdrIntegerOverflow;
    *WorkspaceLength = Total;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
NtfsProviderDecodePath(const char *Path, uint16_t *Wide, size_t Capacity,
                       size_t *Characters)
{
    size_t N = 0;
    const uint8_t *P = (const uint8_t *)Path;
    if (!Path || !Wide || !Characters)
        return Rc95LdrInvalidParameter;
    while (*P) {
        uint32_t C;
        if (*P < 0x80u) C = *P++;
        else if ((*P & 0xe0u) == 0xc0u && P[1] != 0 &&
                 (P[1] & 0xc0u) == 0x80u) {
            C = ((uint32_t)(P[0] & 0x1fu) << 6) | (P[1] & 0x3fu); P += 2;
            if (C < 0x80u) return Rc95LdrInvalidParameter;
        } else if ((*P & 0xf0u) == 0xe0u && P[1] != 0 && P[2] != 0 &&
                   (P[1] & 0xc0u) == 0x80u && (P[2] & 0xc0u) == 0x80u) {
            C = ((uint32_t)(P[0] & 0x0fu) << 12) |
                ((uint32_t)(P[1] & 0x3fu) << 6) | (P[2] & 0x3fu); P += 3;
            if (C < 0x800u || (C >= 0xd800u && C <= 0xdfffu))
                return Rc95LdrInvalidParameter;
        } else if ((*P & 0xf8u) == 0xf0u && P[1] != 0 && P[2] != 0 && P[3] != 0 &&
                   (P[1] & 0xc0u) == 0x80u && (P[2] & 0xc0u) == 0x80u &&
                   (P[3] & 0xc0u) == 0x80u) {
            C = ((uint32_t)(P[0] & 7u) << 18) |
                ((uint32_t)(P[1] & 0x3fu) << 12) |
                ((uint32_t)(P[2] & 0x3fu) << 6) | (P[3] & 0x3fu); P += 4;
            if (C < 0x10000u || C > 0x10ffffu)
                return Rc95LdrInvalidParameter;
        } else return Rc95LdrInvalidParameter;
        if (C <= 0xffffu) {
            if (N >= Capacity) return Rc95LdrBufferTooSmall;
            Wide[N++] = (uint16_t)C;
        } else {
            C -= 0x10000u;
            if (Capacity - N < 2u) return Rc95LdrBufferTooSmall;
            Wide[N++] = (uint16_t)(0xd800u | (C >> 10));
            Wide[N++] = (uint16_t)(0xdc00u | (C & 0x3ffu));
        }
    }
    if (!N) return Rc95LdrInvalidParameter;
    *Characters = N;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
NtfsProviderResolvePath(const RC95LDR_NTFS_VOLUME *V, const uint16_t *Path,
                        size_t PathCharacters, uint8_t *Scratch,
                        size_t ScratchLength, uint64_t *Reference)
{
    RC95LDR_NTFS_FILE_RECORD_INFO Root;
    uint64_t Parent = 0, Child = 0;
    uint16_t Flags = 0;
    size_t Offset = 0, Start, Length;
    RC95LDR_STATUS S;
    S = Rc95LdrNtfsReadMftRecord(V, 5u, Scratch, ScratchLength);
    if (S != Rc95LdrSuccess) return S;
    S = Rc95LdrNtfsGetFileRecordInfo(Scratch,
        V->Geometry.FileRecordBytes, &Root);
    if (S != Rc95LdrSuccess || !(Root.Flags & NTFS_FILE_IN_USE) ||
        !(Root.Flags & NTFS_FILE_DIRECTORY))
        return Rc95LdrCorruptData;
    Parent = ((uint64_t)Root.SequenceNumber << 48) | 5u;
    while (Offset < PathCharacters) {
        while (Offset < PathCharacters &&
               (Path[Offset] == '\\' || Path[Offset] == '/')) ++Offset;
        if (Offset == PathCharacters) break;
        Start = Offset;
        while (Offset < PathCharacters &&
               Path[Offset] != '\\' && Path[Offset] != '/') ++Offset;
        Length = Offset - Start;
        if (!Length || Length > 255u ||
            (Length == 1u && Path[Start] == '.') ||
            (Length == 2u && Path[Start] == '.' && Path[Start + 1u] == '.'))
            return Rc95LdrInvalidImage;
        {
            size_t J;
            for (J = 0; J < Length; ++J)
                if (Path[Start + J] == ':' || Path[Start + J] == 0u)
                    return Rc95LdrInvalidImage; /* ADS/device syntax is unsupported. */
        }
        S = Rc95LdrNtfsFindChildByName(V, Parent, Path + Start, Length,
            Scratch, ScratchLength, &Child, &Flags);
        if (S != Rc95LdrSuccess) return S;
        if (Offset < PathCharacters) {
            if (!(Flags & NTFS_FILE_DIRECTORY)) return Rc95LdrNotFound;
            Parent = Child;
        } else {
            if (Flags & NTFS_FILE_DIRECTORY) return Rc95LdrInvalidImage;
            *Reference = Child;
            return Rc95LdrSuccess;
        }
    }
    return Rc95LdrInvalidImage;
}

static RC95LDR_NTFS_FILE_SLOT *
NtfsProviderFindSlot(RC95LDR_NTFS_PROVIDER_CONTEXT *C, void *Handle)
{
    size_t I;
    if (!C || !C->Initialized || !C->Volume ||
        !C->Volume->Mounted || !Handle) return NULL;
    for (I = 0; I < RC95LDR_NTFS_PROVIDER_MAX_OPEN_FILES; ++I)
        if (Handle == &C->Slots[I] && C->Slots[I].InUse &&
            C->Slots[I].Owner == C)
            return &C->Slots[I];
    return NULL;
}

static RC95LDR_STATUS
NtfsProviderOpen(void *Opaque, const char *Path, void **Handle,
                 uint64_t *FileSize)
{
    RC95LDR_NTFS_PROVIDER_CONTEXT *C = Opaque;
    RC95LDR_NTFS_FILE_SLOT *Slot = NULL;
    uint16_t Wide[RC95LDR_NTFS_PROVIDER_MAX_PATH];
    size_t Characters = 0, I, RecordBytes;
    uint64_t Reference = 0;
    RC95LDR_NTFS_ATTRIBUTE Data;
    RC95LDR_STATUS S;
    uint8_t *Record, *Extension;
    if (!C || !C->Initialized || !Path || !Handle || !FileSize)
        return Rc95LdrInvalidParameter;
    *Handle = NULL; *FileSize = 0;
    for (I = 0; I < RC95LDR_NTFS_PROVIDER_MAX_OPEN_FILES; ++I)
        if (!C->Slots[I].InUse) { Slot = &C->Slots[I]; break; }
    if (!Slot) return Rc95LdrOutOfMemory;
    S = NtfsProviderDecodePath(Path, Wide,
        RC95LDR_NTFS_PROVIDER_MAX_PATH, &Characters);
    if (S != Rc95LdrSuccess) return S;
    RecordBytes = C->Volume->Geometry.FileRecordBytes;
    Record = Slot->Scratch;
    Extension = Record + RecordBytes;
    S = NtfsProviderResolvePath(C->Volume, Wide, Characters, Record,
                                Slot->ScratchLength, &Reference);
    if (S != Rc95LdrSuccess) return S;
    S = Rc95LdrNtfsReadMftRecordByReference(C->Volume, Reference,
                                            Record, RecordBytes);
    if (S != Rc95LdrSuccess) return S;
    S = Rc95LdrNtfsFindAttribute(Record, RecordBytes,
        C->Volume->Geometry.BytesPerSector, NTFS_ATTR_DATA, &Data);
    if (S != Rc95LdrSuccess) return S;
    if (Data.NameLength != 0u || Data.Flags != 0u)
        return Rc95LdrUnsupported;
    if (!Data.NonResident) {
        if (Data.ValueLength != Data.DataSize ||
            Data.ValueLength > RecordBytes)
            return Rc95LdrCorruptData;
        Slot->ResidentData = Data.Value;
        Slot->FileSize = Data.DataSize;
        Slot->Resident = 1u;
        memset(&Slot->Stream, 0, sizeof(Slot->Stream));
    } else {
        if (Data.LowestVcn != 0u || Data.Flags &
            (NTFS_FILE_ATTR_COMPRESSED | NTFS_FILE_ATTR_ENCRYPTED |
             NTFS_FILE_ATTR_SPARSE))
            return Rc95LdrUnsupported;
        S = Rc95LdrNtfsCollectStream(C->Volume, Record, RecordBytes,
            Reference, NTFS_ATTR_DATA, NULL, 0u, Extension, RecordBytes,
            &Slot->Stream);
        if (S != Rc95LdrSuccess) return S;
        Slot->ResidentData = NULL;
        Slot->FileSize = Slot->Stream.DataSize;
        Slot->Resident = 0u;
    }
    Slot->Owner = C;
    Slot->InUse = 1u;
    *Handle = Slot;
    *FileSize = Slot->FileSize;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
NtfsProviderReadAt(void *Opaque, void *Handle, uint64_t Offset,
                  void *Buffer, size_t Length)
{
    RC95LDR_NTFS_PROVIDER_CONTEXT *C = Opaque;
    RC95LDR_NTFS_FILE_SLOT *Slot = NtfsProviderFindSlot(C, Handle);
    uint8_t *Out = (uint8_t *)Buffer;
    uint32_t Sector;
    if (!Slot || (!Buffer && Length)) return Rc95LdrInvalidParameter;
    if (Offset > Slot->FileSize || (uint64_t)Length > Slot->FileSize - Offset)
        return Rc95LdrOutOfBounds;
    if (!Length) return Rc95LdrSuccess;
    if (Slot->Resident) {
        memcpy(Out, Slot->ResidentData + (size_t)Offset, Length);
        return Rc95LdrSuccess;
    }
    Sector = C->Volume->Geometry.BytesPerSector;
    while (Length) {
        uint64_t Aligned = Offset & ~((uint64_t)Sector - 1u);
        size_t Within = (size_t)(Offset - Aligned);
        size_t Take;
        uint8_t *SectorBuffer = Slot->Scratch +
            2u * C->Volume->Geometry.FileRecordBytes +
            C->Volume->Geometry.IndexBufferBytes;
        uint8_t *Bounce = SectorBuffer + Sector;
        RC95LDR_STATUS S;
        if (Within == 0u && Length >= Sector) {
            Take = Length - (Length % Sector);
            S = Rc95LdrNtfsReadStreamRange(C->Volume, &Slot->Stream,
                Offset, Out, Take, Bounce, Sector);
            if (S != Rc95LdrSuccess) return S;
        } else {
            Take = Sector - Within;
            if (Take > Length) Take = Length;
            S = Rc95LdrNtfsReadStreamRange(C->Volume, &Slot->Stream,
                Aligned, SectorBuffer, Sector, Bounce, Sector);
            if (S != Rc95LdrSuccess) return S;
            memcpy(Out, SectorBuffer + Within, Take);
        }
        Out += Take; Offset += Take; Length -= Take;
    }
    return Rc95LdrSuccess;
}

static void
NtfsProviderClose(void *Opaque, void *Handle)
{
    RC95LDR_NTFS_PROVIDER_CONTEXT *C = Opaque;
    RC95LDR_NTFS_FILE_SLOT *Slot = NtfsProviderFindSlot(C, Handle);
    if (!Slot) return;
    memset(&Slot->Stream, 0, sizeof(Slot->Stream));
    Slot->ResidentData = NULL;
    Slot->FileSize = 0;
    Slot->Resident = 0;
    Slot->InUse = 0;
    Slot->Owner = NULL;
}

RC95LDR_STATUS
Rc95LdrNtfsInitializeFileSystem(const RC95LDR_NTFS_VOLUME *V,
    RC95LDR_NTFS_PROVIDER_CONTEXT *C, void *Workspace,
    size_t WorkspaceLength)
{
    size_t Required, SlotBytes, I;
    RC95LDR_STATUS S;
    if (!C)
        return Rc95LdrInvalidParameter;
    /* A failed rebind must never leave stale handles pointing at an old volume. */
    memset(C, 0, sizeof(*C));
    if (!V || !V->Mounted || !Workspace)
        return Rc95LdrInvalidParameter;
    S = Rc95LdrNtfsFileSystemWorkspaceSize(V, &Required);
    if (S != Rc95LdrSuccess) return S;
    if (WorkspaceLength < Required) return Rc95LdrBufferTooSmall;
    SlotBytes = NtfsProviderSlotBytes(V);
    C->Volume = V; C->Workspace = Workspace;
    C->WorkspaceLength = WorkspaceLength;
    C->SlotWorkspaceLength = SlotBytes;
    for (I = 0; I < RC95LDR_NTFS_PROVIDER_MAX_OPEN_FILES; ++I) {
        C->Slots[I].Scratch = (uint8_t *)Workspace + I * SlotBytes;
        C->Slots[I].ScratchLength = SlotBytes;
    }
    C->Interface.FileSystemContext = C;
    C->Interface.OpenRead = NtfsProviderOpen;
    C->Interface.ReadAt = NtfsProviderReadAt;
    C->Interface.Close = NtfsProviderClose;
    C->Interface.DeleteFile = NULL;
    C->Initialized = 1u;
    return Rc95LdrSuccess;
}


RC95LDR_STATUS
Rc95LdrNtfsBindVolume(const RC95LDR_NTFS_VOLUME *V,
    RC95LDR_NTFS_PROVIDER_CONTEXT *C, void *Workspace,
    size_t WorkspaceLength, RC95LDR_VOLUME *Volume)
{
    RC95LDR_STATUS Status;
    if (!V || !C || !Volume)
        return Rc95LdrInvalidParameter;
    memset(Volume, 0, sizeof(*Volume));
    Status = Rc95LdrNtfsInitializeFileSystem(
        V, C, Workspace, WorkspaceLength);
    if (Status != Rc95LdrSuccess)
        return Status;
    Volume->BlockDevice = &V->DeviceStorage;
    Volume->FileSystem = &C->Interface;
    return Rc95LdrSuccess;
}

void
Rc95LdrNtfsDestroyFileSystem(RC95LDR_NTFS_PROVIDER_CONTEXT *C)
{
    if (!C) return;
    memset(C, 0, sizeof(*C));
}
