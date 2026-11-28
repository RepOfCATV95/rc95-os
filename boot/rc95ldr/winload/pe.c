#include "rc95ldrpe.h"
#include <string.h>

#define PE_DOS_MAGIC 0x5a4du
#define PE_NT_MAGIC 0x00004550u
#define PE_OPT32_MAGIC 0x010bu
#define PE_OPT64_MAGIC 0x020bu
#define PE_SCN_EXECUTE 0x20000000u
#define PE_DIRECTORY_RESOURCE 2u
#define PE_REL_ABSOLUTE 0u
#define PE_REL_HIGHLOW 3u
#define PE_REL_DIR64 10u
#define PE_IMPORT_ORDINAL32 0x80000000u
#define PE_IMPORT_ORDINAL64 0x8000000000000000ull

static int AddU32(uint32_t a, uint32_t b, uint32_t *r)
{
    if (b > UINT32_MAX - a) return 0;
    *r = a + b; return 1;
}
static int IsPowerOfTwo(uint32_t v) { return v && !(v & (v - 1)); }
static int RangesOverlap32(uint32_t First, uint32_t FirstLength,
                            uint32_t Second, uint32_t SecondLength)
{
    uint32_t FirstEnd, SecondEnd;
    if (!AddU32(First, FirstLength, &FirstEnd) ||
        !AddU32(Second, SecondLength, &SecondEnd))
        return -1;
    return First < SecondEnd && Second < FirstEnd;
}
static int Range32(uint32_t total, uint32_t off, uint32_t len)
{
    return off <= total && len <= total - off;
}
static RC95LDR_STATUS
PeValidateInternal(const void *FileData, size_t FileSize,
                   uint16_t ExpectedMachine, RC95LDR_PE_INFO *Info,
                   int AllowResourceOnly)
{
    const uint8_t *f=(const uint8_t *)FileData, *o, *s;
    uint32_t nt, optSize, sectionTable;
    size_t tableBytes;
    uint16_t sections, machine, magic, i;
    uint32_t entry, imageSize, headers, secAlign, fileAlign, dirs, dirOffset, headerPageEnd;
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
        (headers%fileAlign)!=0 || (imageSize%secAlign)!=0 ||
        !AddU32(headers,0xfffu,&headerPageEnd))
        return Rc95LdrInvalidImage;
    headerPageEnd &= ~0xfffu;
    if (headerPageEnd > imageSize) return Rc95LdrInvalidImage;
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
        uint32_t va,vs,raw,rs,end,rounded,span,roundingAdd;
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
                int RawOverlap;
                if (!AddU32(pva,pspan,&pend)) return Rc95LdrInvalidImage;
                if (span && pspan && va<pend && pva<end) return Rc95LdrInvalidImage;
                RawOverlap = (rs && prs)
                    ? RangesOverlap32(raw,rs,praw,prs) : 0;
                if (RawOverlap != 0) return Rc95LdrInvalidImage;
            }
            if (!AddU32(end,secAlign-1u,&roundingAdd))
                return Rc95LdrInvalidImage;
            rounded=roundingAdd&~(secAlign-1u);
            if (rounded<end || rounded>imageSize) return Rc95LdrInvalidImage;
            if (va<headers) return Rc95LdrInvalidImage;
            /* Hardware protections are page-granular. A byte-disjoint
               section may still share a 4-KiB page with headers or another
               section; reject that image instead of merging permissions. */
            {
                uint32_t pageStart=va&~0xfffu, pageEnd;
                uint32_t headerEnd, previous;
                if (!AddU32(end,0xfffu,&pageEnd)) return Rc95LdrInvalidImage;
                pageEnd &= ~0xfffu;
                if (pageEnd > imageSize) return Rc95LdrInvalidImage;
                if (!AddU32(headers,0xfffu,&headerEnd)) return Rc95LdrInvalidImage;
                headerEnd &= ~0xfffu;
                if (pageStart<headerEnd) return Rc95LdrInvalidImage;
                for (previous=0;previous<i;previous++) {
                    const uint8_t *p=f+sectionTable+previous*40u;
                    uint32_t pva=Rc95LdrReadU32(p+12);
                    uint32_t pspan=Rc95LdrReadU32(p+8);
                    uint32_t prs=Rc95LdrReadU32(p+16), pend, pstart, ppageEnd;
                    pspan=pspan>prs?pspan:prs;
                    if (!pspan) continue;
                    if (!AddU32(pva,pspan,&pend) ||
                        !AddU32(pend,0xfffu,&ppageEnd))
                        return Rc95LdrInvalidImage;
                    pstart=pva&~0xfffu; ppageEnd&=~0xfffu;
                    if (pageStart<ppageEnd && pstart<pageEnd)
                        return Rc95LdrInvalidImage;
                }
            }
        }
        if (entry>=va && entry-va<(vs>rs?vs:rs) && (Rc95LdrReadU32(s+36)&PE_SCN_EXECUTE))
            entryExecutable=1;
    }
    if (AllowResourceOnly) {
        if (entry != 0) return Rc95LdrInvalidImage;
    } else if (!entry || entry>=imageSize || !entryExecutable) {
        return Rc95LdrInvalidImage;
    }
    memset(Info,0,sizeof(*Info));
    Info->Format=magic==PE_OPT32_MAGIC?Rc95LdrPe32:Rc95LdrPe32Plus;
    Info->Machine=machine; Info->SectionCount=sections;
    Info->Characteristics=Rc95LdrReadU16(f+nt+22);
    Info->EntryPointRva=entry; Info->SizeOfImage=imageSize;
    Info->SizeOfHeaders=headers; Info->SectionAlignment=secAlign;
    Info->FileAlignment=fileAlign; Info->PreferredImageBase=imageBase;
    for (i=0;i<sections;i++) {
        const uint8_t *section=f+sectionTable+(uint32_t)i*40u;
        uint32_t virtualSize=Rc95LdrReadU32(section+8);
        uint32_t rawSize=Rc95LdrReadU32(section+16);
        Info->Sections[i].VirtualAddress=Rc95LdrReadU32(section+12);
        Info->Sections[i].VirtualSize=virtualSize;
        Info->Sections[i].MappedSize=virtualSize>rawSize?virtualSize:rawSize;
        Info->Sections[i].Characteristics=Rc95LdrReadU32(section+36);
    }
    if (dirs>PE_DIRECTORY_RESOURCE && optSize>=dirOffset+3*8) {
        Info->ResourceRva=Rc95LdrReadU32(o+dirOffset+PE_DIRECTORY_RESOURCE*8);
        Info->ResourceSize=Rc95LdrReadU32(o+dirOffset+PE_DIRECTORY_RESOURCE*8+4);
    }
    if (dirs>RC95LDR_PE_DIRECTORY_BASERELOC && optSize>=dirOffset+6*8) {
        Info->RelocationRva=Rc95LdrReadU32(o+dirOffset+5*8);
        Info->RelocationSize=Rc95LdrReadU32(o+dirOffset+5*8+4);
    }
    if (dirs>RC95LDR_PE_DIRECTORY_IMPORT && optSize>=dirOffset+2*8) {
        Info->ImportRva=Rc95LdrReadU32(o+dirOffset+8);
        Info->ImportSize=Rc95LdrReadU32(o+dirOffset+12);
    }
    if ((Info->RelocationRva==0)!=(Info->RelocationSize==0) ||
        (Info->ImportRva==0)!=(Info->ImportSize==0) ||
        (Info->ResourceRva==0)!=(Info->ResourceSize==0))
        return Rc95LdrInvalidImage;
    if (Info->RelocationSize &&
        (Info->RelocationRva>=imageSize || Info->RelocationSize>imageSize-Info->RelocationRva))
        return Rc95LdrInvalidImage;
    if (Info->ImportSize &&
        (Info->ImportRva>=imageSize || Info->ImportSize>imageSize-Info->ImportRva))
        return Rc95LdrInvalidImage;
    if (Info->ResourceSize &&
        (Info->ResourceRva>=imageSize || Info->ResourceSize>imageSize-Info->ResourceRva))
        return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrPeValidate(const void *FileData, size_t FileSize,
                  uint16_t ExpectedMachine, RC95LDR_PE_INFO *Info)
{
    return PeValidateInternal(FileData,FileSize,ExpectedMachine,Info,0);
}

RC95LDR_STATUS
Rc95LdrPeValidateResource(const void *FileData, size_t FileSize,
                          uint16_t ExpectedMachine, RC95LDR_PE_INFO *Info)
{
    RC95LDR_STATUS Status = PeValidateInternal(
        FileData,FileSize,ExpectedMachine,Info,1);
    if (Status != Rc95LdrSuccess) return Status;
    if (!(Info->Characteristics & 0x2000u) ||
        Info->EntryPointRva != 0 || Info->ImportRva != 0 ||
        Info->ImportSize != 0 || !Info->ResourceRva || !Info->ResourceSize)
        return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}


static int ImageString(const uint8_t *Image, uint32_t ImageSize,
                       uint32_t Rva, const char **String)
{
    uint32_t i;
    if (!Image || !String || Rva >= ImageSize) return 0;
    for (i=Rva; i<ImageSize; ++i) {
        uint8_t ch=Image[i];
        if (ch==0) {
            if (i==Rva) return 0;
            *String=(const char *)(Image+Rva);
            return 1;
        }
        if (ch<0x20u || ch>0x7eu) return 0;
    }
    return 0;
}

static RC95LDR_STATUS
ResolveImageImports(uint8_t *Image, uint32_t ImageSize,
                    const RC95LDR_PE_INFO *Info,
                    RC95LDR_PE_IMPORT_RESOLVER Resolver, void *Context)
{
    uint32_t cursor, end, descriptorCount=0;
    uint32_t pointerSize=Info->Format==Rc95LdrPe32Plus?8u:4u;
    if (!Info->ImportSize) return Rc95LdrSuccess;
    if (!Resolver) return Rc95LdrUnsupported;
    if (!Range32(ImageSize,Info->ImportRva,Info->ImportSize) ||
        Info->ImportSize<20u) return Rc95LdrInvalidImage;
    cursor=Info->ImportRva;
    end=cursor+Info->ImportSize;
    for (;;) {
        uint32_t original, nameRva, first, thunkRva, iatRva;
        const char *module;
        if (!Range32(end,cursor,20u)) return Rc95LdrInvalidImage;
        original=Rc95LdrReadU32(Image+cursor);
        nameRva=Rc95LdrReadU32(Image+cursor+12);
        first=Rc95LdrReadU32(Image+cursor+16);
        if (!original && !Rc95LdrReadU32(Image+cursor+4) &&
            !Rc95LdrReadU32(Image+cursor+8) && !nameRva && !first)
            return Rc95LdrSuccess;
        if (!nameRva || !first || ++descriptorCount>Info->ImportSize/20u ||
            !ImageString(Image,ImageSize,nameRva,&module))
            return Rc95LdrInvalidImage;
        thunkRva=original?original:first;
        iatRva=first;
        for (;;) {
            uint64_t thunk, resolved=0;
            const char *symbol=NULL;
            uint16_t ordinal=0;
            int byOrdinal;
            if (!Range32(ImageSize,thunkRva,pointerSize) ||
                !Range32(ImageSize,iatRva,pointerSize))
                return Rc95LdrInvalidImage;
            thunk=pointerSize==8u?Rc95LdrReadU64(Image+thunkRva):
                                  Rc95LdrReadU32(Image+thunkRva);
            if (!thunk) break;
            byOrdinal=pointerSize==8u?
                ((thunk&PE_IMPORT_ORDINAL64)!=0):
                ((thunk&PE_IMPORT_ORDINAL32)!=0);
            if (byOrdinal) {
                uint64_t mask=pointerSize==8u?PE_IMPORT_ORDINAL64:PE_IMPORT_ORDINAL32;
                ordinal=(uint16_t)(thunk&0xffffu);
                if ((thunk&~(mask|0xffffu))!=0) return Rc95LdrInvalidImage;
            } else {
                if (thunk>UINT32_MAX || !Range32(ImageSize,(uint32_t)thunk,3u) ||
                    !ImageString(Image,ImageSize,(uint32_t)thunk+2u,&symbol))
                    return Rc95LdrInvalidImage;
            }
            {
                RC95LDR_STATUS status=Resolver(Context,module,symbol,ordinal,
                                                byOrdinal,&resolved);
                if (status!=Rc95LdrSuccess) return status;
            }
            if (!resolved || (pointerSize==4u && resolved>UINT32_MAX))
                return Rc95LdrOutOfBounds;
            if (pointerSize==8u) {
                unsigned k;
                for(k=0;k<8u;k++) Image[iatRva+k]=(uint8_t)(resolved>>(k*8u));
            } else {
                uint32_t value=(uint32_t)resolved;
                Image[iatRva]=(uint8_t)value;
                Image[iatRva+1]=(uint8_t)(value>>8);
                Image[iatRva+2]=(uint8_t)(value>>16);
                Image[iatRva+3]=(uint8_t)(value>>24);
            }
            if (thunkRva>UINT32_MAX-pointerSize ||
                iatRva>UINT32_MAX-pointerSize)
                return Rc95LdrInvalidImage;
            thunkRva+=pointerSize;
            iatRva+=pointerSize;
        }
        /* Advance to the next IMAGE_IMPORT_DESCRIPTOR. Without this, the
           same descriptor is resolved repeatedly until the bounds guard fires. */
        cursor+=20u;
    }
}

static RC95LDR_STATUS
PeMapExInternal(const void *FileData, size_t FileSize, const RC95LDR_PE_INFO *Info,
                void *ImageBuffer, size_t ImageBufferSize, uint64_t DestinationBase,
                RC95LDR_PE_IMPORT_RESOLVER Resolver, void *ResolverContext,
                int AllowResourceOnly)
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
    if (PeValidateInternal(FileData,FileSize,Info->Machine,&Verified,
                           AllowResourceOnly)!=Rc95LdrSuccess)
        return Rc95LdrInvalidImage;
    if (AllowResourceOnly &&
        (!(Verified.Characteristics & 0x2000u) ||
         Verified.EntryPointRva != 0 || Verified.ImportRva != 0 ||
         Verified.ImportSize != 0 || !Verified.ResourceRva ||
         !Verified.ResourceSize))
        return Rc95LdrInvalidImage;
    PeInfo=&Verified;
    if (ImageBufferSize<PeInfo->SizeOfImage) return Rc95LdrBufferTooSmall;
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
    if (DestinationBase!=PeInfo->PreferredImageBase) {
    if (!PeInfo->RelocationRva || !PeInfo->RelocationSize) {
        /* A resource-only image contains no executable code, imports, or
           absolute data pointers; its resource directory is RVA-relative. */
        if (!AllowResourceOnly) return Rc95LdrInvalidImage;
    } else {
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
    }
    }
    return ResolveImageImports(image,PeInfo->SizeOfImage,PeInfo,Resolver,ResolverContext);
}

RC95LDR_STATUS
Rc95LdrPeMapEx(const void *FileData, size_t FileSize, const RC95LDR_PE_INFO *Info,
               void *ImageBuffer, size_t ImageBufferSize, uint64_t DestinationBase,
               RC95LDR_PE_IMPORT_RESOLVER Resolver, void *ResolverContext)
{
    return PeMapExInternal(FileData,FileSize,Info,ImageBuffer,ImageBufferSize,
                           DestinationBase,Resolver,ResolverContext,0);
}

RC95LDR_STATUS
Rc95LdrPeMapResource(const void *FileData, size_t FileSize,
                     const RC95LDR_PE_INFO *Info, void *ImageBuffer,
                     size_t ImageBufferSize, uint64_t DestinationBase)
{
    return PeMapExInternal(FileData,FileSize,Info,ImageBuffer,ImageBufferSize,
                           DestinationBase,NULL,NULL,1);
}

RC95LDR_STATUS
Rc95LdrPeMap(const void *FileData, size_t FileSize, const RC95LDR_PE_INFO *Info,
             void *ImageBuffer, size_t ImageBufferSize, uint64_t DestinationBase)
{
    return Rc95LdrPeMapEx(FileData,FileSize,Info,ImageBuffer,ImageBufferSize,
                          DestinationBase,NULL,NULL);
}
