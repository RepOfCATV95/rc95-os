#include "include/rc95ldruefiwinloadflow.h"
#include "rc95ldr_wim_provider.h"
#include <string.h>
#include <stddef.h>

typedef struct _RC95LDR_UEFI_WIM_ALLOCATOR_CONTEXT {
    const RC95LDR_UEFI_DISCOVERY_OPS *Ops;
} RC95LDR_UEFI_WIM_ALLOCATOR_CONTEXT;

static void *UefiWimAllocate(void *Opaque,size_t Bytes)
{
    RC95LDR_UEFI_WIM_ALLOCATOR_CONTEXT *C=Opaque;
    void *Allocation=NULL;
    if (!C||!C->Ops||!C->Ops->AllocatePool||
        C->Ops->AllocatePool(C->Ops->Context,Bytes,&Allocation)!=Rc95LdrSuccess)
        return NULL;
    return Allocation;
}
static void UefiWimFree(void *Opaque,void *Allocation)
{
    RC95LDR_UEFI_WIM_ALLOCATOR_CONTEXT *C=Opaque;
    if (C&&C->Ops&&C->Ops->FreePool&&Allocation)
        C->Ops->FreePool(C->Ops->Context,Allocation);
}
static RC95LDR_STATUS UefiFlowBuildRequest(void *Opaque,RC95LDR_WINLOAD_REQUEST *Request)
{
    RC95LDR_UEFI_WINLOAD_FLOW_CONTEXT *Flow=Opaque;
    if (!Flow||!Flow->BuildBootRequest||!Request) return Rc95LdrInvalidParameter;
    {
        RC95LDR_STATUS Status=Flow->BuildBootRequest(Flow,Request);
        const RC95LDR_WINDOWS_INSTALLATION *I=Flow->SelectedInstallation;
        if(Status!=Rc95LdrSuccess)return Status;
        if(I&&I->HasRamdiskSdi) {
            if(Request->StructureSize<sizeof(*Request)||!I->RamdiskSdiImage||
               !I->RamdiskSdiImageSize||!I->RamdiskSdiDataSize||
               I->RamdiskSdiDataOffset>I->RamdiskSdiImageSize||
               I->RamdiskSdiDataSize>I->RamdiskSdiImageSize-I->RamdiskSdiDataOffset||
               (I->RamdiskSdiRamdiskKind!=Rc95LdrSdiRamdiskPartition&&
                I->RamdiskSdiRamdiskKind!=Rc95LdrSdiRamdiskDisk))
                return Rc95LdrInvalidImage;
            Request->RamdiskSdiImage=I->RamdiskSdiImage;
            Request->RamdiskSdiImageSize=I->RamdiskSdiImageSize;
            Request->RamdiskSdiDiskOffset=I->RamdiskSdiDataOffset;
            Request->RamdiskSdiDiskLength=I->RamdiskSdiDataSize;
            Request->RamdiskSdiMemoryType=I->RamdiskSdiMemoryType;
            Request->RamdiskSdiFlags=1u;
        }
        return Rc95LdrSuccess;
    }
}
static void UefiFlowReleaseRequest(void *Opaque,RC95LDR_WINLOAD_REQUEST *Request)
{
    RC95LDR_UEFI_WINLOAD_FLOW_CONTEXT *Flow=Opaque;
    if (Flow&&Flow->ReleaseBootRequest) Flow->ReleaseBootRequest(Flow,Request);
}
static int UefiAsciiEqual(char A,char B)
{
    if (A>='A'&&A<='Z') A=(char)(A+('a'-'A'));
    if (B>='A'&&B<='Z') B=(char)(B+('a'-'A'));
    return A==B;
}
static int UefiMatchToken(const uint16_t *Text,size_t Length,size_t At,const char *Token)
{
    size_t I;
    for (I=0;Token[I];++I)
        if (At+I>=Length||Text[At+I]>0x7f||
            !UefiAsciiEqual((char)Text[At+I],Token[I])) return 0;
    return 1;
}
/* Parse the exact BCD ramdisk target. The GUID is not treated as an opaque
 * suffix: require canonical {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} syntax. */
static int UefiHexValue(uint16_t Ch)
{
    if (Ch>='0'&&Ch<='9') return (int)(Ch-'0');
    if (Ch>='a'&&Ch<='f') return (int)(Ch-'a'+10);
    if (Ch>='A'&&Ch<='F') return (int)(Ch-'A'+10);
    return -1;
}
RC95LDR_STATUS Rc95LdrUefiParseBcdRamdiskTarget(
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    char *Path,size_t Capacity,char GuidText[39])
{
    const uint16_t *Options; size_t N,I,Start,End,GuidStart,Out=0,G;
    if (!Installation||!Path||Capacity<2||!GuidText)
        return Rc95LdrInvalidParameter;
    GuidText[0]=0; Path[0]=0;
    Options=Installation->BootOptionsUtf16; N=Installation->BootOptionsCharacters;
    if (N>=RC95LDR_MAX_BCD_STRING_CHARS) return Rc95LdrCorruptData;
    for (I=0;I+8<=N;++I)
        if (UefiMatchToken(Options,N,I,"ramdisk=")) break;
    if (I+8>N) return Rc95LdrNotFound;
    Start=I+8; End=Start;
    while (End<N&&Options[End]!=','&&Options[End]!=' '&&
           Options[End]!='\t'&&Options[End]!=0) ++End;
    if (End==Start||End>=N||Options[End]!=',')
        return Rc95LdrCorruptData;
    if (End-Start>=6&&UefiMatchToken(Options,End,Start,"[boot]"))
        Start+=6;
    while (Start<End&&(Options[Start]=='\\'||Options[Start]=='/')) ++Start;
    if (Start==End) return Rc95LdrCorruptData;
    for (I=Start;I<End;++I) {
        uint16_t Ch=Options[I];
        if (Ch>0x7f||Ch<0x20||Ch==':'||Ch=='*'||Ch=='?'||Ch=='"'||
            Ch=='<'||Ch=='>'||Ch=='|') return Rc95LdrCorruptData;
        if (Out+1>=Capacity) return Rc95LdrBufferTooSmall;
        Path[Out++]=(Ch=='/'?'\\':(char)Ch);
    }
    Path[Out]=0;
    {
        size_t Component=0,At;
        for (At=0;At<=Out;++At) if (At==Out||Path[At]=='\\') {
            size_t Length=At-Component;
            if (!Length||(Length==1&&Path[Component]=='.')||
                (Length==2&&Path[Component]=='.'&&Path[Component+1]=='.'))
                return Rc95LdrCorruptData;
            Component=At+1;
        }
    }
    GuidStart=End+1;
    if (GuidStart+38>N||Options[GuidStart]!='{'||
        Options[GuidStart+37]!='}')
        return Rc95LdrCorruptData;
    for (G=0;G<38;++G) {
        uint16_t Ch=Options[GuidStart+G];
        if (G==0) { if (Ch!='{') return Rc95LdrCorruptData; }
        else if (G==37) { if (Ch!='}') return Rc95LdrCorruptData; }
        else if (G==9||G==14||G==19||G==24) {
            if (Ch!='-') return Rc95LdrCorruptData;
        } else if (UefiHexValue(Ch)<0) return Rc95LdrCorruptData;
        GuidText[G]=(char)Ch;
    }
    GuidText[38]=0;
    if (GuidStart+38<N&&Options[GuidStart+38]!=0&&
        Options[GuidStart+38]!=' '&&Options[GuidStart+38]!='\t')
        return Rc95LdrCorruptData;
    for (G=1;G<37;++G)
        if (GuidText[G]>='A'&&GuidText[G]<='F')
            GuidText[G]=(char)(GuidText[G]+('a'-'A'));
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrUefiParseBcdWimPath(
    const RC95LDR_WINDOWS_INSTALLATION *Installation,char *Path,size_t Capacity)
{
    char Guid[39];
    return Rc95LdrUefiParseBcdRamdiskTarget(Installation,Path,Capacity,Guid);
}
RC95LDR_STATUS Rc95LdrUefiResolveBcdRamdiskOptions(
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    const void *HiveImage,size_t HiveImageSize,uint32_t *ObjectCell)
{
    RC95BCD_HIVE Hive;
    char Path[512],Guid[39];
    RC95LDR_STATUS Status;
    if (!Installation||!HiveImage||!HiveImageSize||!ObjectCell)
        return Rc95LdrInvalidParameter;
    *ObjectCell=0;
    Status=Rc95LdrUefiParseBcdRamdiskTarget(Installation,Path,sizeof(Path),Guid);
    if (Status!=Rc95LdrSuccess) return Status;
    {
        const uint8_t *Bytes=(const uint8_t*)HiveImage;
        uint32_t Bins;
        size_t ExactSize;
        if (HiveImageSize<RC95BCD_HIVE_HEADER_SIZE ||
            Rc95LdrReadU32(Bytes)!=0x66676572u)
            return Rc95LdrInvalidImage;
        Bins=Rc95LdrReadU32(Bytes+0x28);
        if (!Bins || Bins>HiveImageSize-RC95BCD_HIVE_HEADER_SIZE)
            return Rc95LdrCorruptData;
        ExactSize=RC95BCD_HIVE_HEADER_SIZE+(size_t)Bins;
        Status=Rc95BcdOpenHive(HiveImage,ExactSize,&Hive);
    }
    if (Status!=Rc95LdrSuccess) return Status;
    return Rc95BcdFindObject(&Hive,Guid,ObjectCell);
}

#define RC95BCD_RAMDISK_SDI_DEVICE 0x31000003u
#define RC95BCD_RAMDISK_SDI_PATH   0x32000004u

static int UefiPartitionMatchesBcdDevice(
    const RC95LDR_PARTITION_INFO *Partition,
    const RC95BCD_PARTITION_DEVICE *Device)
{
    uint64_t Offset;
    if (!Partition || !Device) return 0;
    if (Device->Encoding==Rc95BcdPartitionGpt)
        return Partition->Style==Rc95LdrPartitionGpt &&
            memcmp(Partition->DiskGuid,Device->GptDiskGuid,16)==0 &&
            memcmp(Partition->UniqueGuid,Device->Identity.GptUniqueGuid,16)==0;
    if (Device->Encoding!=Rc95BcdPartitionMbr ||
        Partition->Style!=Rc95LdrPartitionMbr ||
        Partition->MbrDiskSignature!=Device->Identity.MbrDiskSignature ||
        (Partition->BlockSize && Partition->StartLba>
            UINT64_MAX/(uint64_t)Partition->BlockSize))
        return 0;
    Offset=Partition->StartLba*(uint64_t)Partition->BlockSize;
    return Offset==Device->Identity.MbrPartitionOffsetBytes;
}

/* Decode both required elements from the GUID-resolved object. The device
 * must resolve exactly to the BCD boot-files partition or selected OS volume;
 * unknown/ambiguous devices are rejected rather than guessed. */
static RC95LDR_STATUS UefiDecodeRamdiskSdiOptions(
    const RC95BCD_HIVE *Hive, uint32_t ObjectCell,
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_WINDOWS_INSTALLATION *Decoded)
{
    RC95BCD_VALUE DeviceValue,PathValue;
    RC95BCD_ELEMENT_VIEW DeviceElement,PathElement;
    RC95BCD_PARTITION_DEVICE PartitionDevice;
    uint32_t I,Component=0,Start=0,Out=0;
    int MatchesBoot,MatchesOs;
    RC95LDR_STATUS Status;
    if (!Hive||!Installation||!Decoded) return Rc95LdrInvalidParameter;
    Status=Rc95BcdFindElement(Hive,ObjectCell,RC95BCD_RAMDISK_SDI_DEVICE,&DeviceValue);
    if (Status!=Rc95LdrSuccess) return Status;
    Status=Rc95BcdDecodeElement(RC95BCD_RAMDISK_SDI_DEVICE,&DeviceValue,&DeviceElement);
    if (Status!=Rc95LdrSuccess) return Status;
    Status=Rc95BcdDecodePartitionDevice(&DeviceElement,&PartitionDevice);
    if (Status!=Rc95LdrSuccess) return Status;
    Status=Rc95BcdFindElement(Hive,ObjectCell,RC95BCD_RAMDISK_SDI_PATH,&PathValue);
    if (Status!=Rc95LdrSuccess) return Status;
    Status=Rc95BcdDecodeElement(RC95BCD_RAMDISK_SDI_PATH,&PathValue,&PathElement);
    if (Status!=Rc95LdrSuccess) return Status;
    if (PathElement.Kind!=Rc95BcdElementString ||
        !PathElement.StringCharacters ||
        PathElement.StringCharacters>=RC95LDR_ARRAY_COUNT(Decoded->RamdiskSdiPathUtf16))
        return Rc95LdrCorruptData;
    memset(Decoded->RamdiskSdiPathUtf16,0,sizeof(Decoded->RamdiskSdiPathUtf16));
    while (Start<PathElement.StringCharacters &&
           (Rc95LdrReadU16(PathElement.StringData+Start*2)=='\\' ||
            Rc95LdrReadU16(PathElement.StringData+Start*2)=='/')) ++Start;
    for (I=Start;I<PathElement.StringCharacters;++I) {
        uint16_t Ch=Rc95LdrReadU16(PathElement.StringData+I*2);
        if (Ch>0x7f||Ch<0x20||Ch==':'||Ch=='*'||Ch=='?'||Ch=='"'||
            Ch=='<'||Ch=='>'||Ch=='|')
            return Rc95LdrCorruptData;
        Decoded->RamdiskSdiPathUtf16[Out++]=(Ch=='/'?'\\':Ch);
    }
    if (!Out) return Rc95LdrCorruptData;
    for (I=0;I<=Out;++I)
        if (I==Out||Decoded->RamdiskSdiPathUtf16[I]=='\\') {
            uint32_t N=I-Component;
            if (!N||(N==1&&Decoded->RamdiskSdiPathUtf16[Component]=='.')||
                (N==2&&Decoded->RamdiskSdiPathUtf16[Component]=='.'&&
                 Decoded->RamdiskSdiPathUtf16[Component+1]=='.'))
                return Rc95LdrCorruptData;
            Component=I+1;
        }
    MatchesBoot=UefiPartitionMatchesBcdDevice(&Installation->BootPartition,&PartitionDevice);
    MatchesOs=(Installation->PartitionIndex==Installation->OsPartitionIndex)?0:
        UefiPartitionMatchesBcdDevice(&Installation->OsPartition,&PartitionDevice);
    if (MatchesBoot==MatchesOs) return Rc95LdrUnsupported;
    Decoded->RamdiskSdiPathCharacters=Out;
    Decoded->RamdiskSdiDeviceIsOsVolume=(uint8_t)(MatchesOs!=0);
    Decoded->HasRamdiskSdi=1;
    return Rc95LdrSuccess;
}


#define RC95LDR_UEFI_MAX_SDI_IMAGE (64u * 1024u * 1024u)
#define RC95LDR_SDI_HEADER_MINIMUM 4096u

#define RC95LDR_SDI_HEADER_SIZE 4096u
#define RC95LDR_SDI_TOC_OFFSET 1024u
#define RC95LDR_SDI_TOC_ENTRY_SIZE 64u
#define RC95LDR_SDI_TOC_ENTRIES 8u
#define RC95LDR_SDI_TYPE_BOOT 0x544f4f42u
#define RC95LDR_SDI_TYPE_LOAD 0x44414f4cu
#define RC95LDR_SDI_TYPE_PART 0x54524150u
#define RC95LDR_SDI_TYPE_DISK 0x4b534944u
static uint64_t UefiSdiReadU64(const uint8_t *P)
{
    return (uint64_t)Rc95LdrReadU32(P)|((uint64_t)Rc95LdrReadU32(P+4)<<32);
}
static int UefiSdiRangeValid(uint64_t O,uint64_t N,size_t T)
{
    return N!=0&&O>=RC95LDR_SDI_HEADER_SIZE&&O<=(uint64_t)T&&N<=(uint64_t)T-O;
}
RC95LDR_STATUS Rc95LdrUefiParseSdiImage(
    const void *Image,size_t ImageSize,RC95LDR_SDI_IMAGE_INFO *Info)
{
    const uint8_t *B=(const uint8_t*)Image;
    uint32_t I,Type; size_t J; int Part=0,Disk=0,Boot=0;
    if(!Image||!Info)return Rc95LdrInvalidParameter;
    memset(Info,0,sizeof(*Info));
    if(ImageSize<RC95LDR_SDI_HEADER_SIZE||ImageSize>RC95LDR_UEFI_MAX_SDI_IMAGE)
        return Rc95LdrCorruptData;
    if(memcmp(B,"$SDI0001",8)!=0)return Rc95LdrInvalidImage;
    Info->MemoryType=Rc95LdrReadU32(B+8);
    if(Info->MemoryType!=1u&&Info->MemoryType!=2u)return Rc95LdrCorruptData;
    Info->BootCodeOffset=UefiSdiReadU64(B+16); Info->BootCodeSize=UefiSdiReadU64(B+24);
    if((Info->BootCodeOffset==0)!=(Info->BootCodeSize==0))return Rc95LdrCorruptData;
    if(Info->BootCodeSize&&!UefiSdiRangeValid(Info->BootCodeOffset,Info->BootCodeSize,ImageSize))
        return Rc95LdrCorruptData;
    memcpy(Info->RuntimeGuid,B+80,16);
    Info->PageAlignmentFactor=Rc95LdrReadU32(B+112);
    if(Info->PageAlignmentFactor>0x10000u)return Rc95LdrCorruptData;
    for(I=0;I<8u;++I){
        const uint8_t *E=B+1024u+(size_t)I*64u;
        RC95LDR_SDI_SECTION *S=&Info->Sections[I];
        Type=Rc95LdrReadU32(E); S->Type=Type; S->Attributes=Rc95LdrReadU32(E+8);
        S->Offset=UefiSdiReadU64(E+16); S->Size=UefiSdiReadU64(E+24);
        if(!Type){if(S->Offset||S->Size||S->Attributes)return Rc95LdrCorruptData;continue;}
        if(Type!=RC95LDR_SDI_TYPE_BOOT&&Type!=RC95LDR_SDI_TYPE_LOAD&&
           Type!=RC95LDR_SDI_TYPE_PART&&Type!=RC95LDR_SDI_TYPE_DISK)
            return Rc95LdrUnsupported;
        if(!UefiSdiRangeValid(S->Offset,S->Size,ImageSize))return Rc95LdrCorruptData;
        if(Type==RC95LDR_SDI_TYPE_BOOT)Boot=1;
        if(Type==RC95LDR_SDI_TYPE_PART){
            if(Part)return Rc95LdrCorruptData; Part=1;
            Info->RamdiskOffset=S->Offset;Info->RamdiskSize=S->Size;
            Info->RamdiskKind=Rc95LdrSdiRamdiskPartition;Info->RamdiskPartitionType=E[32];
        }
        if(Type==RC95LDR_SDI_TYPE_DISK){
            if(Disk)return Rc95LdrCorruptData;Disk=1;
            if(!Part){Info->RamdiskOffset=S->Offset;Info->RamdiskSize=S->Size;
                Info->RamdiskKind=Rc95LdrSdiRamdiskDisk;}
        }
        for(J=0;J<I;++J){
            const RC95LDR_SDI_SECTION *P=&Info->Sections[J];
            if(P->Type&&S->Offset<P->Offset+P->Size&&P->Offset<S->Offset+S->Size)
                return Rc95LdrCorruptData;
        }
    }
    if(!Info->RamdiskSize)return Rc95LdrCorruptData;
    if(Info->BootCodeSize){
        int Found=0;
        for(I=0;I<8u;++I)if(Info->Sections[I].Type==RC95LDR_SDI_TYPE_BOOT&&
            Info->Sections[I].Offset==Info->BootCodeOffset&&
            Info->Sections[I].Size==Info->BootCodeSize)Found=1;
        if(!Found)return Rc95LdrCorruptData;
    }else if(Boot)return Rc95LdrCorruptData;
    for(I=0;I<8u;++I)if(Info->Sections[I].Type)++Info->SectionCount;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrUefiValidateSdiImage(const void *Image,size_t ImageSize)
{
    RC95LDR_SDI_IMAGE_INFO Info;
    return Rc95LdrUefiParseSdiImage(Image,ImageSize,&Info);
}

static RC95LDR_STATUS UefiLoadRamdiskSdi(
    const RC95LDR_UEFI_DISCOVERY_OPS *Ops,
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    RC95LDR_VOLUME *WindowsVolume,int *WindowsVolumeMounted,
    void **ImageBytes,size_t *ImageSize)
{
    const RC95LDR_PARTITION_INFO *Partition;
    RC95LDR_VOLUME SdiVolume;
    RC95LDR_VOLUME *ReadVolume;
    void *Handle=NULL,*Buffer=NULL;
    uint64_t FileSize=0;
    char Path[260];
    size_t I;
    int OwnMount=0;
    RC95LDR_STATUS Status;
    if (ImageBytes) *ImageBytes=NULL;
    if (ImageSize) *ImageSize=0;
    if (!Ops||!Ops->MountVolume||!Ops->UnmountVolume||
        !Ops->AllocatePool||!Ops->FreePool||!Installation||
        !WindowsVolume||!WindowsVolumeMounted||!ImageBytes||!ImageSize||
        !Installation->HasRamdiskSdi||!Installation->RamdiskSdiPathCharacters)
        return Rc95LdrInvalidParameter;
    Partition=Installation->RamdiskSdiDeviceIsOsVolume?
        &Installation->OsPartition:&Installation->BootPartition;
    for (I=0;I<Installation->RamdiskSdiPathCharacters;++I) {
        uint16_t Ch=Installation->RamdiskSdiPathUtf16[I];
        if (!Ch||Ch>0x7f||Ch=='/'||Ch==':'||Ch<0x20||I+1>=sizeof(Path))
            return Rc95LdrCorruptData;
        Path[I]=(char)Ch;
    }
    Path[Installation->RamdiskSdiPathCharacters]=0;
    memset(&SdiVolume,0,sizeof(SdiVolume));
    if ((Installation->RamdiskSdiDeviceIsOsVolume ||
         Installation->PartitionIndex==Installation->OsPartitionIndex) &&
        *WindowsVolumeMounted) {
        ReadVolume=WindowsVolume;
    } else {
        /* The native backend currently has one NTFS mount slot. Release the
           discovery OS mount before mounting a distinct SDI source volume. */
        if (Partition->FileSystem==Rc95LdrFsNtfs && *WindowsVolumeMounted) {
            Ops->UnmountVolume(Ops->Context,WindowsVolume);
            memset(WindowsVolume,0,sizeof(*WindowsVolume));
            *WindowsVolumeMounted=0;
        }
        Status=Ops->MountVolume(Ops->Context,Partition,Partition->FileSystem,&SdiVolume);
        if (Status!=Rc95LdrSuccess) return Status;
        ReadVolume=&SdiVolume;
        OwnMount=1;
    }
    if (!ReadVolume->FileSystem||!ReadVolume->FileSystem->OpenRead||
        !ReadVolume->FileSystem->ReadAt||!ReadVolume->FileSystem->Close) {
        Status=Rc95LdrUnsupported; goto Done;
    }
    Status=ReadVolume->FileSystem->OpenRead(
        ReadVolume->FileSystem->FileSystemContext,Path,&Handle,&FileSize);
    if (Status!=Rc95LdrSuccess) goto Done;
    if (!Handle||FileSize<RC95LDR_SDI_HEADER_MINIMUM||
        FileSize>RC95LDR_UEFI_MAX_SDI_IMAGE||FileSize>(uint64_t)SIZE_MAX) {
        Status=Rc95LdrCorruptData; goto Done;
    }
    Status=Ops->AllocatePool(Ops->Context,(size_t)FileSize,&Buffer);
    if (Status!=Rc95LdrSuccess) goto Done;
    if (!Buffer) { Status=Rc95LdrOutOfMemory; goto Done; }
    Status=ReadVolume->FileSystem->ReadAt(
        ReadVolume->FileSystem->FileSystemContext,Handle,0,Buffer,(size_t)FileSize);
    if (Status!=Rc95LdrSuccess) goto Done;
    /* Parse the Windows SDI header and every TOC extent before retaining it. */
    Status=Rc95LdrUefiValidateSdiImage(Buffer,(size_t)FileSize);
    if (Status!=Rc95LdrSuccess) goto Done;
    *ImageBytes=Buffer; *ImageSize=(size_t)FileSize; Buffer=NULL;
    Status=Rc95LdrSuccess;
Done:
    if (Handle) ReadVolume->FileSystem->Close(ReadVolume->FileSystem->FileSystemContext,Handle);
    if (Buffer) Ops->FreePool(Ops->Context,Buffer);
    if (OwnMount) Ops->UnmountVolume(Ops->Context,&SdiVolume);
    return Status;
}

static int UefiSamePartition(const RC95LDR_WINDOWS_INSTALLATION *I)
{
    return I->PartitionIndex==I->OsPartitionIndex;
}
/* Prefer the named Windows Setup image in installation media. If it is not
 * present (for example an ADK WinPE WIM), select the first catalogue image. */
static RC95LDR_STATUS UefiChooseWimImage(
    RC95LDR_WIM_CONTEXT *Wim,size_t *Selected)
{
    size_t Count,I; WIM_READER_STATUS Ws;
    int HaveFirst=0;
    if (!Wim||!Selected) return Rc95LdrInvalidParameter;
    Ws=Rc95LdrWimGetImageCount(Wim,&Count);
    if (Ws!=WIM_READER_OK||!Count) return Rc95LdrCorruptData;
    *Selected=0;
    for (I=0;I<Count;++I) {
        WIM_IMAGE_INFO Info;
        memset(&Info,0,sizeof(Info));
        Ws=Rc95LdrWimGetImageInfo(Wim,I,&Info);
        if (Ws!=WIM_READER_OK) return Rc95LdrCorruptData;
        if (!HaveFirst) HaveFirst=1;
        if (Info.Name) {
            const char *P=Info.Name;
            while (*P) {
                if (P[0]&&P[1]&&P[2]&&P[3]&&P[4]&&
                    UefiAsciiEqual(P[0],'s')&&UefiAsciiEqual(P[1],'e')&&
                    UefiAsciiEqual(P[2],'t')&&UefiAsciiEqual(P[3],'u')&&
                    UefiAsciiEqual(P[4],'p')) { *Selected=I; WimImageInfoFree(Wim->Reader,&Info); return Rc95LdrSuccess; }
                ++P;
            }
        }
        WimImageInfoFree(Wim->Reader,&Info);
    }
    return HaveFirst?Rc95LdrSuccess:Rc95LdrInvalidImage;
}
static RC95LDR_STATUS UefiFlowRunWinload(
    const RC95LDR_WINLOAD_STARTUP *Startup,
    const RC95LDR_WINDOWS_INSTALLATION *Installation,
    const RC95LDR_VOLUME *Volume,int IsSelectedOsVolume,
    RC95LDR_WINLOAD_RESULT *Result)
{
    RC95LDR_VOLUME_PROVIDER_CONTEXT ProviderContext;
    RC95LDR_FILE_PROVIDER Provider;
    RC95LDR_FILE_PROVIDER_SCOPE Scope;
    RC95LDR_UEFI_WINLOAD_FLOW_CONTEXT Flow;
    RC95LDR_WINLOAD_STARTUP Bound;
    RC95LDR_WINLOAD_PLATFORM BoundPlatform;
    RC95LDR_STATUS Status,Cleanup;
    memset(&ProviderContext,0,sizeof(ProviderContext));
    memset(&Provider,0,sizeof(Provider)); memset(&Scope,0,sizeof(Scope));
    memset(&Flow,0,sizeof(Flow));
    Status=Rc95LdrInitializeVolumeFileProvider(Volume,&ProviderContext,&Provider);
    if (Status!=Rc95LdrSuccess) return Status;
    Status=Rc95LdrBeginFileProviderScope(&Provider,&Scope);
    if (Status!=Rc95LdrSuccess) return Status;
    Flow.CallerContext=Startup->Context; Flow.SelectedInstallation=Installation;
    Flow.FileProviderVolume=Volume; Flow.BuildBootRequest=Startup->BuildBootRequest;
    Flow.ReleaseBootRequest=Startup->ReleaseBootRequest;
    Bound=*Startup;
    memset(&BoundPlatform,0,sizeof(BoundPlatform));
    {
        size_t PlatformBytes=Startup->Platform->StructureSize;
        if (PlatformBytes>sizeof(BoundPlatform)) PlatformBytes=sizeof(BoundPlatform);
        memcpy(&BoundPlatform,Startup->Platform,PlatformBytes);
    }
    Bound.Platform=&BoundPlatform;
    Bound.Context=&Flow; Bound.BuildBootRequest=UefiFlowBuildRequest;
    Bound.ReleaseBootRequest=UefiFlowReleaseRequest;
    Bound.SelectedInstallation=Installation;
    Status=Rc95LdrUefiSetWinloadProviderAttestation(
        &BoundPlatform,Installation,IsSelectedOsVolume);
    if (Status==Rc95LdrSuccess)
        Status=Rc95LdrUefiWinloadEntry(&Bound,Result);
    Cleanup=Rc95LdrEndFileProviderScope(&Scope);
    if (Status==Rc95LdrSuccess) Status=Cleanup;
    return Status;
}

RC95LDR_STATUS
Rc95LdrUefiDiscoverAndStartWindows(
    const RC95LDR_UEFI_DISCOVERY_OPS *DiscoveryOps,
    RC95LDR_PARTITION_INFO *Partitions,size_t PartitionCapacity,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates,size_t CandidateCapacity,
    size_t *CandidateCount,void *HiveBuffer,size_t HiveCapacity,
    const RC95LDR_WINLOAD_STARTUP *Startup,RC95LDR_WINLOAD_RESULT *Result,
    RC95LDR_WINDOWS_INSTALLATION *SelectedInstallation,size_t *SelectedEspIndex)
{
    RC95LDR_WINDOWS_INSTALLATION Installation;
    RC95LDR_VOLUME WindowsVolume,BootVolume,WimVolume;
    RC95LDR_WIM_CONTEXT Wim;
    RC95LDR_WIM_ALLOCATOR Allocator;
    RC95LDR_UEFI_WIM_ALLOCATOR_CONTEXT AllocatorContext;
    char WimPath[512],RamdiskGuid[39]; size_t ImageIndex=0; uint32_t RamdiskOptionsCell=0;
    int WindowsVolumeMounted=1,BootVolumeMounted=0,WimOpened=0;
    void *SdiImageBytes=NULL; size_t SdiImageSize=0;
    RC95LDR_STATUS Status;
    if (!DiscoveryOps||!Partitions||!PartitionCapacity||!Candidates||
        !CandidateCapacity||!CandidateCount||!HiveBuffer||!HiveCapacity||
        !Startup||!Result||!SelectedInstallation||!SelectedEspIndex||
        Startup->StructureSize<sizeof(*Startup)||Startup->Version!=RC95LDR_STARTUP_ABI_VERSION||
        Startup->Firmware!=Rc95LdrFirmwareUefi||!Startup->BootContext||!Startup->Platform||
        !Startup->BuildBootRequest||!Startup->ReleaseBootRequest||!DiscoveryOps->UnmountVolume)
        return Rc95LdrInvalidParameter;
    memset(&Installation,0,sizeof(Installation)); memset(&WindowsVolume,0,sizeof(WindowsVolume));
    memset(&BootVolume,0,sizeof(BootVolume)); memset(&WimVolume,0,sizeof(WimVolume));
    memset(&Wim,0,sizeof(Wim)); *CandidateCount=0; *SelectedEspIndex=(size_t)-1;
    Status=Rc95LdrDiscoverUefiWindows(DiscoveryOps,Partitions,PartitionCapacity,
        Candidates,CandidateCapacity,CandidateCount,HiveBuffer,HiveCapacity,
        &Installation,&WindowsVolume,SelectedEspIndex);
    if (Status!=Rc95LdrSuccess) return Status;
    *SelectedInstallation=Installation;

    Status=Rc95LdrUefiParseBcdRamdiskTarget(&Installation,WimPath,sizeof(WimPath),RamdiskGuid);
    if (Status==Rc95LdrNotFound) {
        Status=UefiFlowRunWinload(Startup,&Installation,&WindowsVolume,1,Result);
        goto Cleanup;
    }
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    /* Resolve against the exact BCD hive image loaded during ESP discovery. */
    Status=Rc95LdrUefiResolveBcdRamdiskOptions(&Installation,HiveBuffer,HiveCapacity,&RamdiskOptionsCell);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    {
        RC95BCD_HIVE OptionsHive;
        uint32_t HiveBins;
        size_t ExactHiveSize;
        if (HiveCapacity<RC95BCD_HIVE_HEADER_SIZE) {
            Status=Rc95LdrCorruptData; goto Cleanup;
        }
        HiveBins=Rc95LdrReadU32((const uint8_t*)HiveBuffer+0x28);
        if (!HiveBins || HiveBins>HiveCapacity-RC95BCD_HIVE_HEADER_SIZE) {
            Status=Rc95LdrCorruptData; goto Cleanup;
        }
        ExactHiveSize=RC95BCD_HIVE_HEADER_SIZE+(size_t)HiveBins;
        Status=Rc95BcdOpenHive(HiveBuffer,ExactHiveSize,&OptionsHive);
        if (Status!=Rc95LdrSuccess) goto Cleanup;
        Status=UefiDecodeRamdiskSdiOptions(&OptionsHive,RamdiskOptionsCell,
                                           &Installation,&Installation);
        if (Status!=Rc95LdrSuccess) goto Cleanup;
    }
    memcpy(Installation.RamdiskOptionsGuid,RamdiskGuid,sizeof(RamdiskGuid));
    Installation.RamdiskOptionsObjectCell=RamdiskOptionsCell;
    Installation.HasRamdiskOptions=1;
    *SelectedInstallation=Installation;
    if (!DiscoveryOps->AllocatePool||!DiscoveryOps->FreePool) {
        Status=Rc95LdrUnsupported; goto Cleanup;
    }
    Status=UefiLoadRamdiskSdi(DiscoveryOps,&Installation,&WindowsVolume,
        &WindowsVolumeMounted,&SdiImageBytes,&SdiImageSize);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    {
        RC95LDR_SDI_IMAGE_INFO SdiInfo;
        uint32_t Si;
        Status=Rc95LdrUefiParseSdiImage(SdiImageBytes,SdiImageSize,&SdiInfo);
        if (Status!=Rc95LdrSuccess) goto Cleanup;
        Installation.RamdiskSdiMemoryType=SdiInfo.MemoryType;
        Installation.RamdiskSdiPageAlignmentFactor=SdiInfo.PageAlignmentFactor;
        Installation.RamdiskSdiSectionCount=SdiInfo.SectionCount;
        Installation.RamdiskSdiRamdiskKind=SdiInfo.RamdiskKind;
        memcpy(Installation.RamdiskSdiRuntimeGuid,SdiInfo.RuntimeGuid,16);
        Installation.RamdiskSdiPartitionType=SdiInfo.RamdiskPartitionType;
        Installation.RamdiskSdiBootCodeOffset=SdiInfo.BootCodeOffset;
        Installation.RamdiskSdiBootCodeSize=SdiInfo.BootCodeSize;
        Installation.RamdiskSdiDataOffset=SdiInfo.RamdiskOffset;
        Installation.RamdiskSdiDataSize=SdiInfo.RamdiskSize;
        for (Si=0;Si<8u;++Si) {
            Installation.RamdiskSdiSectionTypes[Si]=SdiInfo.Sections[Si].Type;
            Installation.RamdiskSdiSectionAttributes[Si]=SdiInfo.Sections[Si].Attributes;
            Installation.RamdiskSdiSectionOffsets[Si]=SdiInfo.Sections[Si].Offset;
            Installation.RamdiskSdiSectionSizes[Si]=SdiInfo.Sections[Si].Size;
        }
    }
    Installation.RamdiskSdiImage=SdiImageBytes;
    Installation.RamdiskSdiImageSize=SdiImageSize;
    *SelectedInstallation=Installation;
    if (UefiSamePartition(&Installation)) BootVolume=WindowsVolume;
    else {
        /* The native UEFI mount backend has one NTFS mount slot. If both
           BCD devices are NTFS, release the OS mount before mounting the
           distinct boot-files partition; the WIM copy owns its bytes. */
        if (Installation.BootPartition.FileSystem==Rc95LdrFsNtfs && WindowsVolumeMounted) {
            DiscoveryOps->UnmountVolume(DiscoveryOps->Context,&WindowsVolume);
            memset(&WindowsVolume,0,sizeof(WindowsVolume));
            WindowsVolumeMounted=0;
        }
        Status=DiscoveryOps->MountVolume(DiscoveryOps->Context,
            &Installation.BootPartition,Installation.BootPartition.FileSystem,&BootVolume);
        if (Status!=Rc95LdrSuccess) goto Cleanup;
        BootVolumeMounted=1;
    }
    AllocatorContext.Ops=DiscoveryOps;
    Allocator.Context=&AllocatorContext; Allocator.Allocate=UefiWimAllocate;
    Allocator.Free=UefiWimFree;
    Status=Rc95LdrWimOpenFile(&Wim,&BootVolume,WimPath,
        RC95LDR_WIM_DEFAULT_MAX_IMAGE_SIZE,RC95LDR_WIM_DEFAULT_MAX_RESOURCE_SIZE,
        RC95LDR_WIM_DEFAULT_MAX_FILE_SIZE,&Allocator,NULL);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    WimOpened=1;
    /* OpenFile owns a complete bounded WIM copy, so neither source volume
       needs to stay mounted while the image tree is exposed to WINLOAD. */
    if (BootVolumeMounted) {
        DiscoveryOps->UnmountVolume(DiscoveryOps->Context,&BootVolume);
        BootVolumeMounted=0;
    } else if (UefiSamePartition(&Installation) && WindowsVolumeMounted) {
        DiscoveryOps->UnmountVolume(DiscoveryOps->Context,&WindowsVolume);
        memset(&WindowsVolume,0,sizeof(WindowsVolume));
        WindowsVolumeMounted=0;
    }
    Status=UefiChooseWimImage(&Wim,&ImageIndex);
    if (Status!=Rc95LdrSuccess) goto Cleanup;
    Status=Rc95LdrWimSelectImage(&Wim,ImageIndex,&WimVolume,NULL);
    if (Status!=Rc95LdrSuccess) goto Cleanup;

    /* WINLOAD now resolves every boot file against the BCD-selected WIM image.
       The WIM, source partition and provider remain alive through the complete
       synchronous load/preparation/handoff transaction. */
    Status=UefiFlowRunWinload(Startup,&Installation,&WimVolume,0,Result);
Cleanup:
    if (WimOpened) Rc95LdrWimUnmount(&Wim);
    if (SdiImageBytes) DiscoveryOps->FreePool(DiscoveryOps->Context,SdiImageBytes);
    Installation.RamdiskSdiImage=NULL;
    Installation.RamdiskSdiImageSize=0;
    SelectedInstallation->RamdiskSdiImage=NULL;
    SelectedInstallation->RamdiskSdiImageSize=0;
    if (BootVolumeMounted) DiscoveryOps->UnmountVolume(DiscoveryOps->Context,&BootVolume);
    if (WindowsVolumeMounted) DiscoveryOps->UnmountVolume(DiscoveryOps->Context,&WindowsVolume);
    return Status;
}
