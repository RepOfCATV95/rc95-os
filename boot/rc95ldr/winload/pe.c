#include "rc95ldrpe.h"
#include <string.h>

#define PE_DOS_MAGIC 0x5a4du
#define PE_NT_MAGIC 0x00004550u
#define PE_OPT32_MAGIC 0x010bu
#define PE_OPT64_MAGIC 0x020bu
#define PE_SCN_EXECUTE 0x20000000u
#define PE_REL_ABSOLUTE 0u
#define PE_REL_HIGHLOW 3u
#define PE_REL_DIR64 10u

static int AddU32(uint32_t a, uint32_t b, uint32_t *r)
{
    if (b > UINT32_MAX - a) return 0;
    *r = a + b; return 1;
}
static int IsPowerOfTwo(uint32_t v) { return v && !(v & (v - 1)); }
static int Range32(uint32_t total, uint32_t off, uint32_t len)
{
    return off <= total && len <= total - off;
}
RC95LDR_STATUS
Rc95LdrPeValidate(const void *FileData, size_t FileSize,
                  uint16_t ExpectedMachine, RC95LDR_PE_INFO *Info)
{
    const uint8_t *f=(const uint8_t *)FileData, *o, *s;
    uint32_t nt, optSize, sectionTable;
    size_t tableBytes;
    uint16_t sections, machine, magic, i;
    uint32_t entry, imageSize, headers, secAlign, fileAlign, dirs, dirOffset;
    uint64_t imageBase;
    int entryExecutable=0;
    if (!f || !Info || FileSize < 64 || FileSize > UINT32_MAX)
        return Rc95LdrInvalidParameter;
    if (Rc95LdrReadU16(f)!=PE_DOS_MAGIC) return Rc95LdrInvalidImage;
    nt=Rc95LdrReadU32(f+0x3c);
    if (!Range32((uint32_t)FileSize,nt,24) ||
        Rc95LdrReadU32(f+nt)!=PE_NT_MAGIC) return Rc95LdrInvalidImage;
    machine=Rc95LdrReadU16(f+nt+4);
    sections=Rc95LdrReadU16(f+nt+6);
    optSize=Rc95LdrReadU16(f+nt+20);
    if (!sections || sections>RC95LDR_PE_MAX_SECTIONS ||
        (ExpectedMachine && machine!=ExpectedMachine))
        return Rc95LdrUnsupported;
    if (!Range32((uint32_t)FileSize,nt+24,optSize))
        return Rc95LdrInvalidImage;
    o=f+nt+24; magic=Rc95LdrReadU16(o);
    if (magic==PE_OPT32_MAGIC) {
        if (optSize<96) return Rc95LdrInvalidImage;
        imageBase=Rc95LdrReadU32(o+28); dirs=Rc95LdrReadU32(o+92); dirOffset=96;
    } else if (magic==PE_OPT64_MAGIC) {
        if (optSize<112) return Rc95LdrInvalidImage;
        imageBase=Rc95LdrReadU64(o+24); dirs=Rc95LdrReadU32(o+108); dirOffset=112;
    } else return Rc95LdrInvalidImage;
    if ((machine==RC95LDR_PE_MACHINE_I386 && magic!=PE_OPT32_MAGIC) ||
        (machine==RC95LDR_PE_MACHINE_AMD64 && magic!=PE_OPT64_MAGIC) ||
        (machine!=RC95LDR_PE_MACHINE_I386 && machine!=RC95LDR_PE_MACHINE_AMD64))
        return Rc95LdrUnsupported;
    entry=Rc95LdrReadU32(o+16); secAlign=Rc95LdrReadU32(o+32);
    fileAlign=Rc95LdrReadU32(o+36); imageSize=Rc95LdrReadU32(o+56);
    headers=Rc95LdrReadU32(o+60);
    if (!IsPowerOfTwo(secAlign) || !IsPowerOfTwo(fileAlign) ||
        fileAlign>65536 || secAlign<fileAlign ||
        (secAlign<4096u && secAlign!=fileAlign) || !imageSize ||
        !headers || headers>imageSize || headers>FileSize ||
        !(Rc95LdrReadU16(f+nt+22)&0x0002u) || !imageBase || (imageBase&0xffffu) ||
        (headers%fileAlign)!=0 || (imageSize%secAlign)!=0)
        return Rc95LdrInvalidImage;
    if (!AddU32(nt,24u,&sectionTable) || !AddU32(sectionTable,optSize,&sectionTable))
        return Rc95LdrInvalidImage;
    if (dirs > (optSize-dirOffset)/8u)
        return Rc95LdrInvalidImage;
    if (!Rc95LdrMultiplySize(sections,40,&tableBytes) ||
        tableBytes>UINT32_MAX || !Range32((uint32_t)FileSize,sectionTable,(uint32_t)tableBytes))
        return Rc95LdrInvalidImage;
    if (sectionTable>headers || tableBytes>headers-sectionTable)
        return Rc95LdrInvalidImage;
    for (i=0;i<sections;i++) {
        uint32_t va,vs,raw,rs,end,rounded,span;
        s=f+sectionTable+(uint32_t)i*40;
        vs=Rc95LdrReadU32(s+8); va=Rc95LdrReadU32(s+12);
        rs=Rc95LdrReadU32(s+16); raw=Rc95LdrReadU32(s+20);
        if (rs && ((raw%fileAlign)!=0 || !Range32((uint32_t)FileSize,raw,rs)))
            return Rc95LdrInvalidImage;
        if (va%secAlign || (rs && raw<headers)) return Rc95LdrInvalidImage;
        if (rs && (raw%fileAlign)!=0) return Rc95LdrInvalidImage;
        if (vs || rs) {
            uint16_t j;
            span=vs>rs?vs:rs;
            if (!AddU32(va,span,&end) || end>imageSize) return Rc95LdrInvalidImage;
            for (j=0;j<i;j++) {
                const uint8_t *p=f+sectionTable+(uint32_t)j*40;
                uint32_t pva=Rc95LdrReadU32(p+12), pvs=Rc95LdrReadU32(p+8);
                uint32_t prs=Rc95LdrReadU32(p+16), pspan=pvs>prs?pvs:prs, pend;
                uint32_t praw=Rc95LdrReadU32(p+20);
                if (!AddU32(pva,pspan,&pend)) return Rc95LdrInvalidImage;
                if (span && pspan && va<pend && pva<end) return Rc95LdrInvalidImage;
                if (rs && prs && raw<praw+prs && praw<raw+rs) return Rc95LdrInvalidImage;
            }
            rounded=(end+secAlign-1u)&~(secAlign-1u);
            if (rounded<end || rounded>imageSize) return Rc95LdrInvalidImage;
            if (va<headers) return Rc95LdrInvalidImage;
        }
        if (entry>=va && entry-va<(vs>rs?vs:rs) && (Rc95LdrReadU32(s+36)&PE_SCN_EXECUTE))
            entryExecutable=1;
    }
    if (!entry || entry>=imageSize || !entryExecutable) return Rc95LdrInvalidImage;
    memset(Info,0,sizeof(*Info));
    Info->Format=magic==PE_OPT32_MAGIC?Rc95LdrPe32:Rc95LdrPe32Plus;
    Info->Machine=machine; Info->SectionCount=sections;
    Info->Characteristics=Rc95LdrReadU16(f+nt+22);
    Info->EntryPointRva=entry; Info->SizeOfImage=imageSize;
    Info->SizeOfHeaders=headers; Info->SectionAlignment=secAlign;
    Info->FileAlignment=fileAlign; Info->PreferredImageBase=imageBase;
    if (dirs>PE_DIRECTORY_BASERELOC && optSize>=dirOffset+6*8) {
        Info->RelocationRva=Rc95LdrReadU32(o+dirOffset+5*8);
        Info->RelocationSize=Rc95LdrReadU32(o+dirOffset+5*8+4);
    }
    if (dirs>PE_DIRECTORY_IMPORT && optSize>=dirOffset+2*8) {
        Info->ImportRva=Rc95LdrReadU32(o+dirOffset+8);
        Info->ImportSize=Rc95LdrReadU32(o+dirOffset+12);
    }
    if ((Info->RelocationRva==0)!=(Info->RelocationSize==0) ||
        (Info->ImportRva==0)!=(Info->ImportSize==0))
        return Rc95LdrInvalidImage;
    if (Info->RelocationSize &&
        (Info->RelocationRva>=imageSize || Info->RelocationSize>imageSize-Info->RelocationRva))
        return Rc95LdrInvalidImage;
    if (Info->ImportSize &&
        (Info->ImportRva>=imageSize || Info->ImportSize>imageSize-Info->ImportRva))
        return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrPeMap(const void *FileData, size_t FileSize, const RC95LDR_PE_INFO *Info,
             void *ImageBuffer, size_t ImageBufferSize, uint64_t DestinationBase)
{
    const uint8_t *f=(const uint8_t *)FileData;
    uint8_t *image=(uint8_t *)ImageBuffer;
    RC95LDR_PE_INFO Verified;
    const RC95LDR_PE_INFO *PeInfo;
    uint32_t nt, optSize, sectionTable;
    uint16_t i;
    int64_t delta;
    uint64_t deltaMagnitude;
    if (!f || !Info || !image || FileSize<64 || FileSize>UINT32_MAX)
        return Rc95LdrInvalidParameter;
    if (Rc95LdrPeValidate(FileData,FileSize,Info->Machine,&Verified)!=Rc95LdrSuccess)
        return Rc95LdrInvalidImage;
    PeInfo=&Verified;
    if (ImageBufferSize<PeInfo->SizeOfImage) return Rc95LdrBufferTooSmall;
    if (PeInfo->ImportSize) return Rc95LdrUnsupported;
    if (DestinationBase>UINT64_MAX-PeInfo->SizeOfImage ||
        (DestinationBase&(PeInfo->SectionAlignment-1u))!=0)
        return Rc95LdrOutOfBounds;
    nt=Rc95LdrReadU32(f+0x3c);
    if (!Range32((uint32_t)FileSize,nt,24)) return Rc95LdrInvalidImage;
    optSize=Rc95LdrReadU16(f+nt+20);
    if (!AddU32(nt,24u,&sectionTable) || !AddU32(sectionTable,optSize,&sectionTable))
        return Rc95LdrInvalidImage;
    if (!Range32((uint32_t)FileSize,sectionTable,(uint32_t)PeInfo->SectionCount*40))
        return Rc95LdrInvalidImage;
    memset(image,0,PeInfo->SizeOfImage);
    memcpy(image,f,PeInfo->SizeOfHeaders);
    for (i=0;i<PeInfo->SectionCount;i++) {
        const uint8_t *s=f+sectionTable+(uint32_t)i*40;
        uint32_t va=Rc95LdrReadU32(s+12), raw=Rc95LdrReadU32(s+20);
        uint32_t rs=Rc95LdrReadU32(s+16);
        if (!Range32(PeInfo->SizeOfImage,va,rs) ||
            !Range32((uint32_t)FileSize,raw,rs)) return Rc95LdrInvalidImage;
        if (rs) memcpy(image+va,f+raw,rs);
    }
    if (DestinationBase==PeInfo->PreferredImageBase) return Rc95LdrSuccess;
    if (!PeInfo->RelocationRva || !PeInfo->RelocationSize) return Rc95LdrInvalidImage;
    if (DestinationBase>=PeInfo->PreferredImageBase) {
        deltaMagnitude=DestinationBase-PeInfo->PreferredImageBase;
        if (deltaMagnitude>INT64_MAX) return Rc95LdrIntegerOverflow;
        delta=(int64_t)deltaMagnitude;
    } else {
        deltaMagnitude=PeInfo->PreferredImageBase-DestinationBase;
        if (deltaMagnitude>INT64_MAX) return Rc95LdrIntegerOverflow;
        delta=-(int64_t)deltaMagnitude;
    }
    if (PeInfo->Format==Rc95LdrPe32 &&
        (DestinationBase>UINT32_MAX || PeInfo->PreferredImageBase>UINT32_MAX))
        return Rc95LdrOutOfBounds;
    {
        uint32_t cursor=PeInfo->RelocationRva, end=cursor+PeInfo->RelocationSize;
        while (cursor<end) {
            uint32_t page, blockSize, j;
            if (!Range32(PeInfo->SizeOfImage,cursor,8)) return Rc95LdrInvalidImage;
            page=Rc95LdrReadU32(image+cursor);
            if ((page&0xfffu)!=0 || page>=PeInfo->SizeOfImage)
                return Rc95LdrInvalidImage;
            blockSize=Rc95LdrReadU32(image+cursor+4);
            if (blockSize<8 || (blockSize&1) || blockSize>end-cursor)
                return Rc95LdrInvalidImage;
            for (j=8;j<blockSize;j+=2) {
                uint16_t item=Rc95LdrReadU16(image+cursor+j);
                uint16_t type=item>>12, off=item&0xfff;
                uint32_t target;
                if (type==PE_REL_ABSOLUTE) continue;
                if (!AddU32(page,off,&target)) return Rc95LdrInvalidImage;
                if (PeInfo->Format==Rc95LdrPe32 && type==PE_REL_HIGHLOW) {
                    uint32_t v;
                    if (!Range32(PeInfo->SizeOfImage,target,4)) return Rc95LdrInvalidImage;
                    v=Rc95LdrReadU32(image+target)+(uint32_t)delta;
                    image[target]=(uint8_t)v; image[target+1]=(uint8_t)(v>>8);
                    image[target+2]=(uint8_t)(v>>16); image[target+3]=(uint8_t)(v>>24);
                } else if (PeInfo->Format==Rc95LdrPe32Plus && type==PE_REL_DIR64) {
                    uint64_t v;
                    if (!Range32(PeInfo->SizeOfImage,target,8)) return Rc95LdrInvalidImage;
                    v=Rc95LdrReadU64(image+target)+(uint64_t)delta;
                    { unsigned k; for(k=0;k<8;k++) image[target+k]=(uint8_t)(v>>(k*8)); }
                } else return Rc95LdrUnsupported;
            }
            cursor+=blockSize;
        }
        if (cursor!=end) return Rc95LdrInvalidImage;
    }
    return Rc95LdrSuccess;
}
