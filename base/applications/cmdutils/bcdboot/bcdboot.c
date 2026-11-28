/*
 * PROJECT:     Republic of CATV 95 Boot Configuration Data boot file creation tool
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Deploy BIOS and UEFI boot environment files.
 * COPYRIGHT:   Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <strsafe.h>

#include <winioctl.h>
#include <ntddvol.h>

#include "rc95bcd.h"
#include "rc95bcdwalk.h"

typedef struct _DESTINATION_PARTITION
{
    DWORD DiskNumber;
    DWORD PartitionNumber;
    LARGE_INTEGER StartingOffset;
    DWORD PartitionStyle;
    DWORD MbrSignature;
    GUID GptDiskId;
    GUID GptPartitionId;
} DESTINATION_PARTITION;

/*
 * Resolve /s to the unique physical partition backing the selected volume.
 * BCDBOOT must not guess when a volume spans disks (for example a spanned
 * dynamic volume): a single MBR/GPT partition identity cannot describe it.
 */
static BOOL
ResolveDestinationPartition(PCWSTR Destination,
                            DESTINATION_PARTITION *Partition)
{
    WCHAR InputPath[MAX_PATH * 4];
    WCHAR VolumeMountPoint[MAX_PATH * 4];
    WCHAR VolumeName[MAX_PATH * 4];
    WCHAR DiskPath[64];
    HANDLE VolumeHandle = INVALID_HANDLE_VALUE;
    HANDLE DiskHandle = INVALID_HANDLE_VALUE;
    BYTE ExtentsBuffer[FIELD_OFFSET(VOLUME_DISK_EXTENTS, Extents) +
                       16 * sizeof(DISK_EXTENT)];
    BYTE LayoutBuffer[FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) +
                      128 * sizeof(PARTITION_INFORMATION_EX)];
    PVOLUME_DISK_EXTENTS Extents = (PVOLUME_DISK_EXTENTS)ExtentsBuffer;
    PDRIVE_LAYOUT_INFORMATION_EX Layout =
        (PDRIVE_LAYOUT_INFORMATION_EX)LayoutBuffer;
    DWORD BytesReturned, Index;
    BOOL Found = FALSE;
    DWORD Error = ERROR_SUCCESS;
    GUID ZeroGuid;

    ZeroMemory(&ZeroGuid, sizeof(ZeroGuid));

    if (Destination == NULL || Partition == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (wcslen(Destination) + 2 >= _countof(InputPath))
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }

    if (wcslen(Destination) == 2 && Destination[1] == L':')
    {
        if (FAILED(StringCchPrintfW(InputPath, _countof(InputPath),
                                    L"%s\\", Destination)))
        {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return FALSE;
        }
    }
    else
    {
        if (FAILED(StringCchCopyW(InputPath, _countof(InputPath), Destination)))
        {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return FALSE;
        }
    }

    if (!GetVolumePathNameW(InputPath, VolumeMountPoint,
                            _countof(VolumeMountPoint)))
        return FALSE;

    if (!GetVolumeNameForVolumeMountPointW(VolumeMountPoint, VolumeName,
                                           _countof(VolumeName)))
        return FALSE;

    VolumeHandle = CreateFileW(VolumeName, 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (VolumeHandle == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!DeviceIoControl(VolumeHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                         NULL, 0, Extents, sizeof(ExtentsBuffer),
                         &BytesReturned, NULL))
    {
        Error = GetLastError();
        goto Cleanup;
    }

    if (Extents->NumberOfDiskExtents != 1)
    {
        Error = ERROR_NOT_SUPPORTED;
        goto Cleanup;
    }

    if (FAILED(StringCchPrintfW(DiskPath, _countof(DiskPath),
                                L"\\\\.\\PhysicalDrive%lu",
                                Extents->Extents[0].DiskNumber)))
    {
        Error = ERROR_INVALID_NAME;
        goto Cleanup;
    }

    DiskHandle = CreateFileW(DiskPath, 0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, 0, NULL);
    if (DiskHandle == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        goto Cleanup;
    }

    if (!DeviceIoControl(DiskHandle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                         NULL, 0, Layout, sizeof(LayoutBuffer),
                         &BytesReturned, NULL))
    {
        Error = GetLastError();
        goto Cleanup;
    }

    if (BytesReturned < FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry))
    {
        Error = ERROR_INVALID_DATA;
        goto Cleanup;
    }

    if (Layout->PartitionCount >
        (BytesReturned - FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry)) /
            sizeof(PARTITION_INFORMATION_EX))
    {
        Error = ERROR_INVALID_DATA;
        goto Cleanup;
    }

    for (Index = 0; Index < Layout->PartitionCount; ++Index)
    {
        const PARTITION_INFORMATION_EX *Entry = &Layout->PartitionEntry[Index];

        if (Entry->PartitionNumber != 0 &&
            Entry->StartingOffset.QuadPart ==
                Extents->Extents[0].StartingOffset.QuadPart &&
            Entry->PartitionLength.QuadPart ==
                Extents->Extents[0].ExtentLength.QuadPart)
        {
            ZeroMemory(Partition, sizeof(*Partition));
            Partition->DiskNumber = Extents->Extents[0].DiskNumber;
            Partition->PartitionNumber = Entry->PartitionNumber;
            Partition->StartingOffset = Entry->StartingOffset;
            Partition->PartitionStyle = Entry->PartitionStyle;

            if (Entry->PartitionStyle == PARTITION_STYLE_MBR)
            {
                Partition->MbrSignature = Layout->Mbr.Signature;
                if (Partition->MbrSignature == 0)
                {
                    Error = ERROR_INVALID_DATA;
                    goto Cleanup;
                }
            }
            else if (Entry->PartitionStyle == PARTITION_STYLE_GPT)
            {
                Partition->GptDiskId = Layout->Gpt.DiskId;
                Partition->GptPartitionId = Entry->Gpt.PartitionId;
                if (IsEqualGUID(&Partition->GptDiskId, &ZeroGuid) ||
                    IsEqualGUID(&Partition->GptPartitionId, &ZeroGuid))
                {
                    Error = ERROR_INVALID_DATA;
                    goto Cleanup;
                }
            }
            else
            {
                Error = ERROR_NOT_SUPPORTED;
                goto Cleanup;
            }

            Found = TRUE;
            break;
        }
    }

    if (!Found)
        Error = ERROR_NOT_FOUND;

Cleanup:
    if (DiskHandle != INVALID_HANDLE_VALUE)
        CloseHandle(DiskHandle);
    CloseHandle(VolumeHandle);

    if (!Found)
    {
        SetLastError(Error != ERROR_SUCCESS ? Error : ERROR_NOT_FOUND);
        return FALSE;
    }

    return TRUE;
}

typedef enum _BOOT_MODE
{
    BootModeBios,
    BootModeUefi,
    BootModeAll
} BOOT_MODE;

static void
Usage(void)
{
    wprintf(L"BCDBOOT - Boot environment deployment tool\n\n"
            L"Usage: bcdboot <Windows-directory> /s <system-volume> [/f BIOS|UEFI|ALL] [/v]\n\n"
            L"  <Windows-directory>  Windows installation root (for example C:\\Windows)\n"
            L"  /s <volume>          Destination system volume (for example S:)\n"
            L"  /f <firmware>        BIOS, UEFI, or ALL (default: ALL)\n"
            L"  /v                   Display copied files and resolved partition identity\n"
            L"  /?                   Display this help\n\n"
            L"The command copies the boot manager and BCD template. It does not format\n"
            L"the destination, write a partition boot sector, or modify firmware NVRAM.\n");
}

static BOOL
JoinPath(WCHAR *Buffer, SIZE_T cchBuffer, PCWSTR Left, PCWSTR Right)
{
    SIZE_T cchLeft = wcslen(Left);
    PCWSTR Separator = (cchLeft && (Left[cchLeft - 1] == L'\\' ||
                                    Left[cchLeft - 1] == L'/')) ? L"" : L"\\";
    return SUCCEEDED(StringCchPrintfW(Buffer, cchBuffer, L"%s%s%s",
                                      Left, Separator, Right));
}

static BOOL
EnsureDirectory(PCWSTR Path)
{
    WCHAR Buffer[MAX_PATH * 4];
    WCHAR *p;

    if (wcslen(Path) >= _countof(Buffer))
        return FALSE;
    wcscpy(Buffer, Path);

    /* The destination is a volume-rooted path (for example S:\Boot). */
    for (p = Buffer + 3; *p; ++p)
    {
        if (*p == L'\\' || *p == L'/')
        {
            WCHAR Saved = *p;
            *p = UNICODE_NULL;
            if (!CreateDirectoryW(Buffer, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS)
                return FALSE;
            *p = Saved;
        }
    }
    if (!CreateDirectoryW(Buffer, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;
    return TRUE;
}

static BOOL
CopyBootFile(PCWSTR SourceRoot, PCWSTR SourceRelative,
             PCWSTR DestinationRoot, PCWSTR DestinationRelative, BOOL Verbose)
{
    WCHAR Source[MAX_PATH * 4], Destination[MAX_PATH * 4], Directory[MAX_PATH * 4];
    WCHAR *Slash;

    if (!JoinPath(Source, _countof(Source), SourceRoot, SourceRelative) ||
        !JoinPath(Destination, _countof(Destination), DestinationRoot, DestinationRelative) ||
        wcslen(Destination) >= _countof(Directory))
        return FALSE;

    wcscpy(Directory, Destination);
    Slash = wcsrchr(Directory, L'\\');
    if (Slash)
    {
        *Slash = UNICODE_NULL;
        if (!EnsureDirectory(Directory))
        {
            wprintf(L"BCDBOOT: cannot create directory %s (error %lu)\n",
                    Directory, GetLastError());
            return FALSE;
        }
    }

    if (!CopyFileW(Source, Destination, FALSE))
    {
        wprintf(L"BCDBOOT: cannot copy %s to %s (error %lu)\n",
                Source, Destination, GetLastError());
        return FALSE;
    }
    if (Verbose)
        wprintf(L"  %s -> %s\n", Source, Destination);
    return TRUE;
}

/*
 * Validate BCD stores with the same REGF/HBIN parser used by RC95LDR.
 * A successful file copy is not sufficient: a truncated template or an
 * inconsistent hive must never be reported as a usable boot configuration.
 */
static BOOL
ValidateBcdStore(PCWSTR Path, BOOL Verbose, BOOL RequireWindowsLoader)
{
    HANDLE File;
    LARGE_INTEGER FileSize;
    BYTE *Image = NULL;
    DWORD BytesRead;
    RC95BCD_HIVE Hive;
    RC95BCD_KEY_INFO Root;
    uint32_t ObjectsCell, BootManagerCell;
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates = NULL;
    size_t CandidateCount = 0;
    RC95LDR_STATUS Status;
    BOOL Success = FALSE;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        wprintf(L"BCDBOOT: cannot open BCD store %s (error %lu)\n",
                Path, GetLastError());
        return FALSE;
    }

    if (!GetFileSizeEx(File, &FileSize) ||
        FileSize.QuadPart < RC95BCD_HIVE_HEADER_SIZE ||
        (ULONGLONG)FileSize.QuadPart > MAXDWORD)
    {
        wprintf(L"BCDBOOT: invalid BCD store size for %s\n", Path);
        goto Cleanup;
    }

    Image = (BYTE *)malloc((SIZE_T)FileSize.QuadPart);
    if (Image == NULL)
    {
        wprintf(L"BCDBOOT: insufficient memory to validate %s\n", Path);
        goto Cleanup;
    }

    if (!ReadFile(File, Image, (DWORD)FileSize.QuadPart, &BytesRead, NULL) ||
        BytesRead != (DWORD)FileSize.QuadPart)
    {
        wprintf(L"BCDBOOT: cannot read BCD store %s (error %lu)\n",
                Path, GetLastError());
        goto Cleanup;
    }

    Status = Rc95BcdOpenHive(Image, (SIZE_T)FileSize.QuadPart, &Hive);
    if (Status == Rc95LdrSuccess)
        Status = Rc95BcdValidateHiveBins(Image, (SIZE_T)FileSize.QuadPart);
    if (Status == Rc95LdrSuccess)
        Status = Rc95BcdGetRootKey(&Hive, &Root);
    if (Status == Rc95LdrSuccess)
        Status = Rc95BcdFindSubKey(&Hive, Root.Cell, "Objects", &ObjectsCell);
    if (Status == Rc95LdrSuccess)
        Status = Rc95BcdFindObject(&Hive, RC95BCD_BOOTMGR_GUID,
                                   &BootManagerCell);

    if (Status == Rc95LdrSuccess && RequireWindowsLoader)
    {
        /* Candidate records own bounded copies of all element payloads. Keep
         * them off the stack and reject stores whose boot order cannot be
         * projected completely by the loader's fixed policy limit. */
        Candidates = (RC95LDR_BCD_BOOT_CANDIDATE *)calloc(
            128u, sizeof(*Candidates));
        if (Candidates == NULL)
            Status = Rc95LdrOutOfMemory;
        else
            Status = Rc95BcdCollectWindowsBootCandidates(
                &Hive, Candidates, 128u, &CandidateCount);
        if (Status == Rc95LdrSuccess && CandidateCount == 0)
            Status = Rc95LdrNotFound;
    }

    if (Status != Rc95LdrSuccess)
    {
        wprintf(L"BCDBOOT: BCD store %s failed RC95LDR validation (status %u)\n",
                Path, (unsigned)Status);
        goto Cleanup;
    }

    if (Verbose)
        wprintf(L"  validated BCD hive and Boot Manager object: %s\n", Path);
    Success = TRUE;

Cleanup:
    if (Candidates != NULL)
        free(Candidates);
    if (Image != NULL)
        free(Image);
    CloseHandle(File);
    return Success;
}


static void
WriteU32LE(BYTE *Buffer, uint32_t Value)
{
    Buffer[0] = (BYTE)Value;
    Buffer[1] = (BYTE)(Value >> 8);
    Buffer[2] = (BYTE)(Value >> 16);
    Buffer[3] = (BYTE)(Value >> 24);
}

static BOOL
GuidText(const GUID *Guid, char Text[39])
{
    int Count;
    if (Guid == NULL || Text == NULL)
        return FALSE;
    Count = _snprintf(Text, 39,
        "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
        (unsigned long)Guid->Data1, (unsigned)Guid->Data2,
        (unsigned)Guid->Data3,
        (unsigned)Guid->Data4[0], (unsigned)Guid->Data4[1],
        (unsigned)Guid->Data4[2], (unsigned)Guid->Data4[3],
        (unsigned)Guid->Data4[4], (unsigned)Guid->Data4[5],
        (unsigned)Guid->Data4[6], (unsigned)Guid->Data4[7]);
    if (Count != 38)
    {
        Text[0] = '\0';
        return FALSE;
    }
    return TRUE;
}

/* Encode the BCD partition-device descriptor consumed by RC95LDR. */
static BOOL
MakePartitionDevice(const DESTINATION_PARTITION *Partition,
                     BYTE Device[64], DWORD *DeviceLength)
{
    if (Partition == NULL || Device == NULL || DeviceLength == NULL)
        return FALSE;

    ZeroMemory(Device, 64);
    WriteU32LE(Device, 2u); /* BCD partition device */
    WriteU32LE(Device + 4, 0u); /* no additional options */

    if (Partition->PartitionStyle == PARTITION_STYLE_MBR)
    {
        if (Partition->MbrSignature == 0 ||
            Partition->StartingOffset.QuadPart <= 0)
            return FALSE;
        WriteU32LE(Device + 8, Partition->MbrSignature);
        /* BCD stores the MBR partition's byte offset, not its partition
         * number or starting LBA. */
        {
            /*
             * Encode the LARGE_INTEGER as two 32-bit words.  Variable or
             * constant 64-bit shifts pull in __aullshr on this x86 build,
             * whose /NODEFAULTLIB link does not provide that helper.
             */
            DWORD OffsetLow = (DWORD)Partition->StartingOffset.LowPart;
            DWORD OffsetHigh = (DWORD)Partition->StartingOffset.HighPart;

            Device[12] = (BYTE)OffsetLow;
            Device[13] = (BYTE)(OffsetLow >> 8);
            Device[14] = (BYTE)(OffsetLow >> 16);
            Device[15] = (BYTE)(OffsetLow >> 24);
            Device[16] = (BYTE)OffsetHigh;
            Device[17] = (BYTE)(OffsetHigh >> 8);
            Device[18] = (BYTE)(OffsetHigh >> 16);
            Device[19] = (BYTE)(OffsetHigh >> 24);
        }
        *DeviceLength = 20u;
        return TRUE;
    }

    if (Partition->PartitionStyle == PARTITION_STYLE_GPT)
    {
        GUID ZeroGuid;
        ZeroMemory(&ZeroGuid, sizeof(ZeroGuid));
        if (IsEqualGUID(&Partition->GptDiskId, &ZeroGuid) ||
            IsEqualGUID(&Partition->GptPartitionId, &ZeroGuid))
            return FALSE;
        /* Windows GUID memory layout is the on-disk BCD GUID byte layout. */
        CopyMemory(Device + 8, &Partition->GptDiskId, sizeof(GUID));
        CopyMemory(Device + 24, &Partition->GptPartitionId, sizeof(GUID));
        *DeviceLength = 40u;
        return TRUE;
    }

    return FALSE;
}

static BOOL
CreateBcdElement(void *Image, SIZE_T Size, void *Scratch,
                 uint32_t ElementsCell,
                 uint32_t ElementId, uint32_t Type,
                 const void *Data, uint32_t Length)
{
    char Name[9];
    uint32_t KeyCell, ValueCell, DataCell;
    _snprintf(Name,sizeof(Name),"%08lx",(unsigned long)ElementId);
    if (Rc95BcdCreateKeyCell(Image,Size,Scratch,Size,ElementsCell,
                             Name,&KeyCell)!=Rc95LdrSuccess)
        return FALSE;
    return Rc95BcdCreateValueCell(Image,Size,Scratch,Size,KeyCell,
                                  "Element",Type,Data,Length,
                                  &ValueCell,&DataCell)==Rc95LdrSuccess;
}

/* Build a complete loader object in memory. Every registry mutation is
 * transactional; the destination BCD file is replaced only after final
 * validation succeeds. */
static BOOL
CreateWindowsLoaderObject(PCWSTR StorePath,
                          const DESTINATION_PARTITION *Partition,
                          BOOT_MODE Mode, BOOL Verbose)
{
    HANDLE File=INVALID_HANDLE_VALUE;
    LARGE_INTEGER FileSize;
    BYTE *Image=NULL,*Scratch=NULL;
    DWORD Read=0,Written=0;
    WCHAR TempPath[MAX_PATH*4];
    char ObjectName[39];
    GUID ObjectGuid;
    FILETIME Now;
    DWORD StampLow, StampHigh;
    uint32_t Objects,Manager,ObjectCell,Description,Elements;
    uint32_t ValueCell,DataCell;
    BYTE Device[64],TypeData[4];
    DWORD DeviceLength;
    WCHAR LoaderPath[96];
    static const WCHAR SystemRoot[]=L"\\Windows";
    BYTE PathBytes[sizeof(LoaderPath)],RootBytes[sizeof(SystemRoot)];
    SIZE_T PathChars,RootChars,PathLength,RootLength;
    RC95BCD_HIVE Hive;
    RC95BCD_KEY_INFO Root;
    BOOL Success=FALSE;

    File=CreateFileW(StorePath,GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(File==INVALID_HANDLE_VALUE) goto Cleanup;
    if(!GetFileSizeEx(File,&FileSize) || FileSize.QuadPart<RC95BCD_HIVE_HEADER_SIZE ||
       (ULONGLONG)FileSize.QuadPart>16u*1024u*1024u) goto Cleanup;
    Image=(BYTE*)malloc((SIZE_T)FileSize.QuadPart);
    Scratch=(BYTE*)malloc((SIZE_T)FileSize.QuadPart);
    if(!Image||!Scratch) goto Cleanup;
    if(!ReadFile(File,Image,(DWORD)FileSize.QuadPart,&Read,NULL) ||
       Read!=(DWORD)FileSize.QuadPart) goto Cleanup;
    CloseHandle(File); File=INVALID_HANDLE_VALUE;
    if(Rc95BcdOpenHive(Image,(SIZE_T)FileSize.QuadPart,&Hive)!=Rc95LdrSuccess ||
       Rc95BcdGetRootKey(&Hive,&Root)!=Rc95LdrSuccess ||
       Rc95BcdFindSubKey(&Hive,Root.Cell,"Objects",&Objects)!=Rc95LdrSuccess ||
       Rc95BcdFindObject(&Hive,RC95BCD_BOOTMGR_GUID,&Manager)!=Rc95LdrSuccess)
        goto Cleanup;

    /* Generate a collision-resistant per-deployment identifier from the
     * system clock, process identity, and resolved partition coordinates. */
    GetSystemTimeAsFileTime(&Now);

    /*
     * Keep identifier mixing in 32-bit arithmetic.  This utility is linked
     * for x86 without the compiler's 64-bit shift helpers (__allshl and
     * __aullshr), so do not combine FILETIME into a 64-bit integer here.
     */
    StampLow = Now.dwLowDateTime ^
               (GetCurrentProcessId() << 17) ^
               Partition->PartitionNumber;
    StampHigh = Now.dwHighDateTime ^
                (Partition->DiskNumber << 16) ^
                (Now.dwLowDateTime >> 16) ^
                (Partition->PartitionNumber >> 16);

    ObjectGuid.Data1 = StampLow ^ StampHigh;
    ObjectGuid.Data2 = (WORD)(StampLow >> 16);
    ObjectGuid.Data3 = (WORD)((StampHigh & 0x0fffu) | 0x4000u);
    ObjectGuid.Data4[0] = 0x80u | (BYTE)(StampHigh >> 8);
    ObjectGuid.Data4[1] = (BYTE)StampLow;
    ObjectGuid.Data4[2] = (BYTE)(Partition->DiskNumber >> 8);
    ObjectGuid.Data4[3] = (BYTE)Partition->DiskNumber;
    ObjectGuid.Data4[4] = (BYTE)(Partition->PartitionNumber >> 8);
    ObjectGuid.Data4[5] = (BYTE)Partition->PartitionNumber;
    ObjectGuid.Data4[6] = (BYTE)(StampHigh >> 24);
    ObjectGuid.Data4[7] = (BYTE)(StampLow >> 24);
    if(!GuidText(&ObjectGuid,ObjectName)) goto Cleanup;

    if(Rc95BcdCreateKeyCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        (SIZE_T)FileSize.QuadPart,Objects,ObjectName,&ObjectCell)!=Rc95LdrSuccess)
        goto Cleanup;
    if(Rc95BcdCreateKeyCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        (SIZE_T)FileSize.QuadPart,ObjectCell,"Description",&Description)!=Rc95LdrSuccess)
        goto Cleanup;
    WriteU32LE(TypeData,0x10200003u); /* Windows OS-loader application */
    if(Rc95BcdCreateValueCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        (SIZE_T)FileSize.QuadPart,Description,"Type",4u,TypeData,4u,
        &ValueCell,&DataCell)!=Rc95LdrSuccess)
        goto Cleanup;
    if(Rc95BcdCreateKeyCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        (SIZE_T)FileSize.QuadPart,ObjectCell,"Elements",&Elements)!=Rc95LdrSuccess)
        goto Cleanup;

    if(!MakePartitionDevice(Partition,Device,&DeviceLength)) goto Cleanup;
    if(Mode==BootModeUefi)
        StringCchCopyW(LoaderPath,_countof(LoaderPath),L"\\Windows\\System32\\winload.efi");
    else
        StringCchCopyW(LoaderPath,_countof(LoaderPath),L"\\Windows\\System32\\winload.exe");
    PathChars=wcslen(LoaderPath)+1u;
    RootChars=_countof(SystemRoot);
    PathLength=PathChars*2u; RootLength=RootChars*2u;
    if(PathLength>sizeof(PathBytes)||RootLength>sizeof(RootBytes)) goto Cleanup;
    { SIZE_T i; for(i=0;i<PathChars;i++){PathBytes[i*2]=(BYTE)LoaderPath[i];PathBytes[i*2+1]=(BYTE)(LoaderPath[i]>>8);}
      for(i=0;i<RootChars;i++){RootBytes[i*2]=(BYTE)SystemRoot[i];RootBytes[i*2+1]=(BYTE)(SystemRoot[i]>>8);} }

    if(!CreateBcdElement(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        Elements,0x11000001u,3u,Device,DeviceLength) ||
       !CreateBcdElement(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        Elements,0x21000001u,3u,Device,DeviceLength) ||
       !CreateBcdElement(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        Elements,0x12000002u,1u,PathBytes,(uint32_t)PathLength) ||
       !CreateBcdElement(Image,(SIZE_T)FileSize.QuadPart,Scratch,
        Elements,0x22000002u,1u,RootBytes,(uint32_t)RootLength))
        goto Cleanup;

    /* Add the new object to Boot Manager's display order and make it default.
     * Existing entries are retained; only DisplayOrder is rebuilt here. */
    {
        uint32_t ManagerElements,OrderKey,DefaultKey;
        RC95BCD_VALUE OrderValue;
        BYTE *OrderData;
        uint32_t OrderLength=16u;
        if(Rc95BcdFindSubKey(&Hive,Manager,"Elements",&ManagerElements)!=Rc95LdrSuccess)
            goto Cleanup;
        if(Rc95BcdFindSubKey(&Hive,ManagerElements,"24000001",&OrderKey)==Rc95LdrSuccess)
        {
            RC95BCD_HIVE Current;
            uint32_t ExistingCount;
            if(Rc95BcdOpenHive(Image,(SIZE_T)FileSize.QuadPart,&Current)!=Rc95LdrSuccess ||
               Rc95BcdFindValue(&Current,OrderKey,"Element",&OrderValue)!=Rc95LdrSuccess ||
               OrderValue.Length<4u || OrderValue.Length>1024u*1024u ||
               ((OrderValue.Length-4u)%16u)!=0)
                goto Cleanup;
            ExistingCount=Rc95LdrReadU32(OrderValue.Data);
            if(ExistingCount!=(OrderValue.Length-4u)/16u ||
               ExistingCount==UINT32_MAX)
                goto Cleanup;
            OrderLength=OrderValue.Length+16u;
            OrderData=(BYTE*)malloc(OrderLength);
            if(!OrderData) goto Cleanup;
            memcpy(OrderData,OrderValue.Data,OrderValue.Length);
            WriteU32LE(OrderData,ExistingCount+1u);
            CopyMemory(OrderData+OrderValue.Length,&ObjectGuid,16);
            if(Rc95BcdReplaceValueCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
                (SIZE_T)FileSize.QuadPart,OrderKey,"Element",3u,OrderData,
                OrderLength,&ValueCell,&DataCell)!=Rc95LdrSuccess)
            {free(OrderData);goto Cleanup;}
            free(OrderData);
        }
        else
        {
            uint32_t ListKey;
            BYTE ObjectList[20];
            WriteU32LE(ObjectList,1u);
            CopyMemory(ObjectList+4,&ObjectGuid,16);
            if(Rc95BcdCreateKeyCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
                (SIZE_T)FileSize.QuadPart,ManagerElements,"24000001",&ListKey)!=Rc95LdrSuccess)
                goto Cleanup;
            if(Rc95BcdCreateValueCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
                (SIZE_T)FileSize.QuadPart,ListKey,"Element",3u,ObjectList,sizeof(ObjectList),
                &ValueCell,&DataCell)!=Rc95LdrSuccess)
                goto Cleanup;
        }
        if(Rc95BcdFindSubKey(&Hive,ManagerElements,"23000003",&DefaultKey)!=Rc95LdrSuccess)
        {
            if(Rc95BcdCreateKeyCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
                (SIZE_T)FileSize.QuadPart,ManagerElements,"23000003",&DefaultKey)!=Rc95LdrSuccess)
                goto Cleanup;
            if(Rc95BcdCreateValueCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
                (SIZE_T)FileSize.QuadPart,DefaultKey,"Element",3u,
                &ObjectGuid,16u,&ValueCell,&DataCell)!=Rc95LdrSuccess)
                goto Cleanup;
        }
        else if(Rc95BcdReplaceValueCell(Image,(SIZE_T)FileSize.QuadPart,Scratch,
            (SIZE_T)FileSize.QuadPart,DefaultKey,"Element",3u,
            &ObjectGuid,16u,&ValueCell,&DataCell)!=Rc95LdrSuccess)
            goto Cleanup;
    }

    if(Rc95BcdOpenHive(Image,(SIZE_T)FileSize.QuadPart,&Hive)!=Rc95LdrSuccess ||
       Rc95BcdValidateHiveBins(Image,(SIZE_T)FileSize.QuadPart)!=Rc95LdrSuccess)
        goto Cleanup;
    if(FAILED(StringCchPrintfW(TempPath,_countof(TempPath),L"%s.rc95tmp",StorePath)))
        goto Cleanup;
    {
        HANDLE Temp=CreateFileW(TempPath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,NULL);
        if(Temp==INVALID_HANDLE_VALUE) goto Cleanup;
        if(!WriteFile(Temp,Image,(DWORD)FileSize.QuadPart,&Written,NULL) ||
           Written!=(DWORD)FileSize.QuadPart || !FlushFileBuffers(Temp))
        {CloseHandle(Temp);DeleteFileW(TempPath);goto Cleanup;}
        CloseHandle(Temp);
        if(!MoveFileExW(TempPath,StorePath,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        {DeleteFileW(TempPath);goto Cleanup;}
    }
    if(Verbose) wprintf(L"  created Windows loader object %S (%s)\n",ObjectName,
                        Mode==BootModeUefi?L"UEFI":L"BIOS");
    Success=TRUE;
Cleanup:
    if(File!=INVALID_HANDLE_VALUE) CloseHandle(File);
    if (Image)
        free(Image);
    if (Scratch)
        free(Scratch);
    return Success;
}

static BOOL
CopyBcdTemplate(PCWSTR WindowsDirectory,
                PCWSTR SourceRelative,
                PCWSTR SystemVolume,
                PCWSTR DestinationRelative,
                const DESTINATION_PARTITION *Partition,
                BOOT_MODE Mode, BOOL Verbose)
{
    WCHAR Destination[MAX_PATH * 4];

    if (!JoinPath(Destination, _countof(Destination),
                  SystemVolume, DestinationRelative))
        return FALSE;

    if (!CopyBootFile(WindowsDirectory, SourceRelative,
                      SystemVolume, DestinationRelative, Verbose))
        return FALSE;

    if (!ValidateBcdStore(Destination, Verbose, FALSE))
        return FALSE;
    if (!CreateWindowsLoaderObject(Destination, Partition, Mode, Verbose))
    {
        wprintf(L"BCDBOOT: failed to create Windows loader object in %s (error %lu)\\n",
                Destination, GetLastError());
        return FALSE;
    }
    return ValidateBcdStore(Destination, Verbose, TRUE);
}

int
wmain(int argc, WCHAR **argv)
{
    WCHAR WindowsDirectory[MAX_PATH * 4] = L"";
    WCHAR SystemVolume[MAX_PATH * 4] = L"";
    BOOT_MODE Mode = BootModeAll;
    BOOL Verbose = FALSE;
    int i;
    BOOL Success = TRUE;
    DESTINATION_PARTITION DestinationPartition;

    if (argc < 2 || wcscmp(argv[1], L"/?") == 0 ||
        wcscmp(argv[1], L"-?") == 0)
    {
        Usage();
        return argc < 2 ? 1 : 0;
    }

    if (wcslen(argv[1]) >= _countof(WindowsDirectory))
        return 1;
    wcscpy(WindowsDirectory, argv[1]);

    for (i = 2; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"/s") == 0 && i + 1 < argc)
        {
            if (wcslen(argv[++i]) >= _countof(SystemVolume))
                return 1;
            wcscpy(SystemVolume, argv[i]);
        }
        else if (_wcsicmp(argv[i], L"/f") == 0 && i + 1 < argc)
        {
            PCWSTR Value = argv[++i];
            if (_wcsicmp(Value, L"BIOS") == 0)
                Mode = BootModeBios;
            else if (_wcsicmp(Value, L"UEFI") == 0)
                Mode = BootModeUefi;
            else if (_wcsicmp(Value, L"ALL") == 0)
                Mode = BootModeAll;
            else
            {
                fwprintf(stderr, L"BCDBOOT: invalid firmware type: %s\n", Value);
                return 1;
            }
        }
        else if (_wcsicmp(argv[i], L"/v") == 0)
            Verbose = TRUE;
        else
        {
            fwprintf(stderr, L"BCDBOOT: unknown or incomplete option: %s\n", argv[i]);
            Usage();
            return 1;
        }
    }

    if (!SystemVolume[0])
    {
        fwprintf(stderr, L"BCDBOOT: /s <system-volume> is required.\n");
        return 1;
    }

    if (!ResolveDestinationPartition(SystemVolume, &DestinationPartition))
    {
        fwprintf(stderr, L"BCDBOOT: cannot resolve %s to one MBR/GPT partition (error %lu).\\n",
                 SystemVolume, GetLastError());
        return 1;
    }

    if (Verbose)
    {
        wprintf(L"Destination: disk %lu, partition %lu, offset %I64u, ",
                DestinationPartition.DiskNumber,
                DestinationPartition.PartitionNumber,
                (unsigned __int64)DestinationPartition.StartingOffset.QuadPart);
        if (DestinationPartition.PartitionStyle == PARTITION_STYLE_MBR)
            wprintf(L"MBR signature %08lx\\n", DestinationPartition.MbrSignature);
        else
            wprintf(L"GPT disk/partition GUID identity resolved\\n");
    }

    if (Mode == BootModeBios || Mode == BootModeAll)
    {
        Success = CopyBootFile(WindowsDirectory, L"Boot\\PCAT\\bootmgr",
                               SystemVolume, L"bootmgr", Verbose) && Success;
        Success = CopyBcdTemplate(WindowsDirectory, L"System32\\Config\\BCD-Template",
                                  SystemVolume, L"Boot\\BCD", &DestinationPartition, BootModeBios, Verbose) && Success;
    }

    if (Mode == BootModeUefi || Mode == BootModeAll)
    {
        Success = CopyBootFile(WindowsDirectory, L"Boot\\EFI\\bootmgfw.efi",
                               SystemVolume, L"EFI\\Microsoft\\Boot\\bootmgfw.efi",
                               Verbose) && Success;
        Success = CopyBcdTemplate(WindowsDirectory, L"System32\\Config\\BCD-Template",
                                  SystemVolume, L"EFI\\Microsoft\\Boot\\BCD",
                                  &DestinationPartition, BootModeUefi, Verbose) && Success;
    }

    if (!Success)
        return 1;
    wprintf(L"BCDBOOT: boot files copied successfully.\n");
    return 0;
}
