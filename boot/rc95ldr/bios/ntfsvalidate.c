#include "include/rc95ldrbiosdisk.h"
#include <string.h>

#define BIOS_VALIDATE_PATH_CHARS 260u
#define BIOS_VALIDATE_PROBE_BYTES 4096u

static RC95LDR_STATUS
BiosReadPath(const RC95LDR_NTFS_VOLUME *Volume,
    RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT *Context,
    const uint16_t *Path,size_t Characters,size_t *ReadBytes,uint64_t *FileSize)
{
    if(!Context->Scratch||!Context->ProbeBuffer||
       Context->ProbeCapacity<BIOS_VALIDATE_PROBE_BYTES)
        return Rc95LdrBufferTooSmall;
    return Rc95LdrNtfsReadFilePrefix(Volume,Path,Characters,
        Context->Scratch,Context->ScratchLength,Context->ProbeBuffer,
        Context->ProbeCapacity,ReadBytes,FileSize);
}

static RC95LDR_STATUS
BiosValidatePe(const RC95LDR_NTFS_VOLUME *Volume,
    RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT *Context,
    const uint16_t *Path,size_t Characters)
{
    size_t ReadBytes=0;
    uint64_t FileSize=0;
    uint32_t PeOffset;
    RC95LDR_STATUS Status=BiosReadPath(Volume,Context,Path,Characters,
                                      &ReadBytes,&FileSize);
    const uint8_t *Bytes=(const uint8_t*)Context->ProbeBuffer;
    if(Status!=Rc95LdrSuccess)return Status;
    if(FileSize<64u||ReadBytes<64u||Bytes[0]!='M'||Bytes[1]!='Z')
        return Rc95LdrInvalidImage;
    PeOffset=Rc95LdrReadU32(Bytes+0x3cu);
    if(PeOffset<64u||PeOffset>ReadBytes-4u||
       Bytes[PeOffset]!='P'||Bytes[PeOffset+1]!='E'||
       Bytes[PeOffset+2]!=0||Bytes[PeOffset+3]!=0)
        return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
BiosBuildSystemPath(const uint16_t *Root,size_t RootChars,
    const uint16_t *Suffix,size_t SuffixChars,
    uint16_t Path[BIOS_VALIDATE_PATH_CHARS],size_t *PathChars)
{
    size_t I,Offset=0;
    if(!Root||!RootChars||RootChars>=BIOS_VALIDATE_PATH_CHARS||
       !Suffix||!SuffixChars||!PathChars)
        return Rc95LdrInvalidParameter;
    for(I=0;I<RootChars;I++) {
        uint16_t Ch=Root[I];
        if(!Ch||Ch=='/'||Ch<0x20u)return Rc95LdrInvalidImage;
        Path[Offset++]=Ch;
    }
    if(Path[Offset-1u]!='\\')Path[Offset++]='\\';
    for(I=0;I<SuffixChars;I++) {
        if(!Suffix[I]||Suffix[I]=='/'||Suffix[I]<0x20u||
           Offset>=BIOS_VALIDATE_PATH_CHARS)
            return Rc95LdrInvalidImage;
        Path[Offset++]=Suffix[I];
    }
    *PathChars=Offset;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosValidateWindowsVolume(void *Opaque,
    const RC95LDR_NTFS_VOLUME *Volume,
    const RC95LDR_WINDOWS_INSTALLATION *Installation)
{
    static const uint16_t KernelSuffix[]={
        's','y','s','t','e','m','3','2','\\','n','t','o','s','k','r','n','l','.','e','x','e'};
    static const uint16_t HiveSuffix[]={
        's','y','s','t','e','m','3','2','\\','c','o','n','f','i','g','\\','S','Y','S','T','E','M'};
    RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT *Context=
        (RC95LDR_BIOS_NTFS_VALIDATION_CONTEXT*)Opaque;
    uint16_t Path[BIOS_VALIDATE_PATH_CHARS];
    size_t PathChars,ReadBytes;
    uint64_t FileSize;
    RC95LDR_STATUS Status;
    const uint8_t *Bytes;
    if(!Context||!Volume||!Volume->Mounted||!Installation||
       !Installation->LoaderPathCharacters||
       Installation->LoaderPathCharacters>=BIOS_VALIDATE_PATH_CHARS||
       !Installation->SystemRootCharacters||
       Installation->SystemRootCharacters>=BIOS_VALIDATE_PATH_CHARS)
        return Rc95LdrInvalidParameter;
    Status=BiosValidatePe(Volume,Context,Installation->LoaderPathUtf16,
                          Installation->LoaderPathCharacters);
    if(Status!=Rc95LdrSuccess)return Status;

    Status=BiosBuildSystemPath(Installation->SystemRootUtf16,
        Installation->SystemRootCharacters,KernelSuffix,
        RC95LDR_ARRAY_COUNT(KernelSuffix),Path,&PathChars);
    if(Status!=Rc95LdrSuccess)return Status;
    Status=BiosValidatePe(Volume,Context,Path,PathChars);
    if(Status!=Rc95LdrSuccess)return Status;

    Status=BiosBuildSystemPath(Installation->SystemRootUtf16,
        Installation->SystemRootCharacters,HiveSuffix,
        RC95LDR_ARRAY_COUNT(HiveSuffix),Path,&PathChars);
    if(Status!=Rc95LdrSuccess)return Status;
    Status=BiosReadPath(Volume,Context,Path,PathChars,&ReadBytes,&FileSize);
    if(Status!=Rc95LdrSuccess)return Status;
    Bytes=(const uint8_t*)Context->ProbeBuffer;
    if(FileSize<4096u||ReadBytes<4u||
       Bytes[0]!='r'||Bytes[1]!='e'||Bytes[2]!='g'||Bytes[3]!='f')
        return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}
