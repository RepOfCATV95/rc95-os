/*
 * RC95-OS native hibernation reader. This code restores hiberfil.sys directly
 * from winload; it never resolves or launches winresume.exe/winresume.efi.
 */
#include "rc95ldrwinload.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define H_SIG 0x72626968u
#define H_VER 4u
#define H_CPU_VER 1u
#define H_CPU_MAX 2048u
#define H_PAGE 4096u
#define H_CHUNK_BYTES (64u * 1024u)
#define H_COMPRESSED 0x80000000u
#define H_LENGTH 0x7fffffffu
#define H_STORED_MAX (H_CHUNK_BYTES + H_CHUNK_BYTES / 8u + 20u)
#define H_PATH_MAX 1024u
#if defined(_M_AMD64)||defined(_M_X64)||defined(_AMD64_)||defined(__x86_64__)||defined(__amd64__)
#define H_PROCESSOR_STATE_SIZE 0x5B0u
#elif defined(_M_IX86)||defined(_X86_)||defined(__i386__)||defined(i386)
#define H_PROCESSOR_STATE_SIZE 0x320u
#else
#define H_PROCESSOR_STATE_SIZE 0u
#endif

typedef struct _H_HEADER {
    uint32_t Signature, Version, HeaderSize, MachineType, PageSize;
    uint32_t PageCountLow, PageCountHigh, PayloadSizeLow, PayloadSizeHigh;
    uint32_t Flags, CheckSum, RtcTime[8];
    uint32_t CpuStateVersion, CpuStateSize, ProcessorCount;
    uint32_t FxStateOffset, FxStateSize, Reserved;
    uint64_t ResumeFlagPhysicalAddress;
    uint8_t CpuState[H_CPU_MAX];
} H_HEADER;
typedef struct _H_CHUNK { uint64_t PhysicalAddress; uint32_t Length, CheckSum; } H_CHUNK;

static H_HEADER g_Header;
static uint8_t g_Stored[H_STORED_MAX];
static uint8_t g_Decoded[H_CHUNK_BYTES];

static uint32_t HChecksum(const void *p, size_t n)
{
    const uint8_t *bytes=(const uint8_t *)p;
    uint32_t v=0, word; size_t i;
    for(i=0;i<n/4u;i++) {
        memcpy(&word,bytes+i*4u,sizeof(word));
        v^=word;
    }
    return v;
}
static uint32_t HHeaderChecksum(const H_HEADER *h)
{
    const uint8_t *bytes=(const uint8_t *)h;
    size_t skip=offsetof(H_HEADER,CheckSum), i;
    uint32_t v=0, word;
    /* Read the packed on-disk bytes with memcpy.  Casting the structure to
       uint32_t* can violate effective-type/aliasing rules for CpuState[]. */
    for(i=0;i<sizeof(*h);i+=sizeof(word)) {
        if(i==skip) continue;
        memcpy(&word,bytes+i,sizeof(word));
        v^=word;
    }
    return v;
}
static int HRead(const RC95LDR_WINLOAD_PLATFORM *p,const RC95LDR_WINLOAD_FILE *f,
                 uint64_t off,void *b,size_t n)
{
    if(off>f->FileSize || (uint64_t)n>f->FileSize-off) return 0;
    return p->ReadFileAt(p->Context,f,off,b,n)==Rc95LdrSuccess;
}

/* Exact inverse of PopHiberLzCompress in ntoskrnl/po/hiber.c. */
static int HLzDecode(const uint8_t *in,size_t inLen,uint8_t *out,size_t outLen)
{
    size_t ip=0,op=0;
    if(!in||!out||!inLen||!outLen) return 0;
    while(ip<inLen && op<outLen) {
        uint8_t control=in[ip++]; unsigned token;
        for(token=0;token<8 && op<outLen;token++) {
            if(control&(1u<<token)) {
                uint16_t code; size_t offset,length,i;
                if(inLen-ip<2) return 0;
                code=(uint16_t)(in[ip]|((uint16_t)in[ip+1]<<8)); ip+=2;
                offset=(size_t)(code&0x0fffu)+1u;
                length=(size_t)(code>>12)+3u;
                if(offset>op || length>outLen-op) return 0;
                for(i=0;i<length;i++) out[op+i]=out[op+i-offset];
                op+=length;
            } else {
                if(ip>=inLen) return 0;
                out[op++]=in[ip++];
            }
            /* The kernel encoder zeroes unused control bits in the final
               token group. Reject hidden tokens beyond the declared output. */
            if(op==outLen && token<7 &&
               (control & (uint8_t)(0xFFu << (token+1u)))!=0)
                return 0;
        }
    }
    return ip==inLen && op==outLen;
}
static int HRange(const RC95LDR_WINLOAD_PLATFORM *p,uint64_t base,uint32_t len)
{
    if(!len || (base&(H_PAGE-1u)) || (len&(H_PAGE-1u)) ||
       base>UINT64_MAX-len) return 0;
#if defined(_M_IX86) || defined(_X86_) || defined(__i386__) || defined(i386)
    if(base>=0x100000000ULL || (uint64_t)len>0x100000000ULL-base) return 0;
#elif defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
    if(base>=(1ULL<<40) || (uint64_t)len>(1ULL<<40)-base) return 0;
#endif
    return p->ValidateResumeRange(p->Context,base,len)==Rc95LdrSuccess;
}
static int HStateValid(const RC95LDR_WINLOAD_PLATFORM *p,uint32_t machine)
{
    uint32_t f=g_Header.Flags;
    if(g_Header.CpuStateVersion!=H_CPU_VER ||
       g_Header.CpuStateSize!=H_PROCESSOR_STATE_SIZE ||
       !H_PROCESSOR_STATE_SIZE || g_Header.ProcessorCount!=1 ||
       !(f&RC95LDR_HIBER_FLAG_HAS_CPU_STATE) ||
       !(f&RC95LDR_HIBER_FLAG_UNIPROCESSOR) ||
       (g_Header.ResumeFlagPhysicalAddress&3u) ||
       ((g_Header.ResumeFlagPhysicalAddress&(H_PAGE-1u))>H_PAGE-4u))
        return 0;
    if(machine==RC95LDR_HIBER_MACHINE_AMD64) {
        uint64_t cr3;
        if(g_Header.CpuStateSize!=0x5b0u ||
           (f&(RC95LDR_HIBER_FLAG_HAS_FPU_STATE|RC95LDR_HIBER_FLAG_HAS_FX_STATE)) !=
           (RC95LDR_HIBER_FLAG_HAS_FPU_STATE|RC95LDR_HIBER_FLAG_HAS_FX_STATE))
            return 0;
        memcpy(&cr3,g_Header.CpuState+0x10,sizeof(cr3));
        if(!HRange(p,cr3,H_PAGE)) return 0;
    } else {
        uint32_t cr3;
        if(g_Header.CpuStateSize!=0x320u) return 0;
        memcpy(&cr3,g_Header.CpuState+0x2d4,sizeof(cr3));
        if(!HRange(p,cr3,H_PAGE)) return 0;
    }
    if(f&RC95LDR_HIBER_FLAG_HAS_FX_STATE) {
        if(!(f&RC95LDR_HIBER_FLAG_HAS_FPU_STATE) ||
           g_Header.FxStateSize!=512u ||
           g_Header.FxStateOffset!=g_Header.CpuStateSize ||
           g_Header.FxStateOffset>H_CPU_MAX ||
           g_Header.FxStateSize>H_CPU_MAX-g_Header.FxStateOffset)
            return 0;
    } else if(g_Header.FxStateOffset || g_Header.FxStateSize) return 0;
    return p->ValidateResumeProcessorState(p->Context,machine,g_Header.CpuState,
        g_Header.CpuStateSize,f,g_Header.FxStateOffset,g_Header.FxStateSize)
        ==Rc95LdrSuccess;
}
static RC95LDR_STATUS HChunk(const RC95LDR_WINLOAD_PLATFORM *p,
    const RC95LDR_WINLOAD_FILE *f,uint64_t *offset,uint64_t remain,
    H_CHUNK *chunk,uint32_t *plainLen)
{
    uint32_t stored,plain; int compressed;
    if(remain<sizeof(*chunk)||!HRead(p,f,*offset,chunk,sizeof(*chunk)))
        return Rc95LdrCorruptData;
    *offset+=sizeof(*chunk); remain-=sizeof(*chunk);
    compressed=(chunk->Length&H_COMPRESSED)!=0;
    stored=chunk->Length&H_LENGTH;
    if(!stored||stored>H_STORED_MAX||stored>remain) return Rc95LdrCorruptData;
    if(!HRead(p,f,*offset,g_Stored,stored)) return Rc95LdrIoError;
    *offset+=stored;
    if(compressed) {
        if(stored<4) return Rc95LdrCorruptData;
        memcpy(&plain,g_Stored,4);
        if(!plain||plain>H_CHUNK_BYTES||(plain&(H_PAGE-1u))||
           !HLzDecode(g_Stored+4,stored-4,g_Decoded,plain))
            return Rc95LdrCorruptData;
    } else {
        plain=stored;
        if(plain>H_CHUNK_BYTES||(plain&(H_PAGE-1u))) return Rc95LdrCorruptData;
        memcpy(g_Decoded,g_Stored,plain);
    }
    if((((uint32_t)chunk->PhysicalAddress ^
         (uint32_t)(chunk->PhysicalAddress>>32) ^ plain ^
         HChecksum(g_Decoded,plain)) != chunk->CheckSum))
        return Rc95LdrCorruptData;
    *plainLen=plain; return Rc95LdrSuccess;
}
static uint32_t HProgress(uint64_t done,uint64_t total,uint32_t base,uint32_t span)
{
    uint64_t unit=total/span, percent;
    if(!total)return base;
    percent=unit?done/unit:(done*span)/total;
    if(percent>span)percent=span;
    return base+(uint32_t)percent;
}
static RC95LDR_STATUS HValidatePayload(const RC95LDR_WINLOAD_PLATFORM *p,
    const RC95LDR_WINLOAD_FILE *f,uint64_t payload,uint64_t expectedPages,
    const RC95LDR_TEXTUI *ui)
{
    uint64_t off=sizeof(g_Header),used=0,pages=0,previousEnd=0;
    while(used<payload) {
        H_CHUNK ch; uint32_t plain; RC95LDR_STATUS s;
        s=HChunk(p,f,&off,payload-used,&ch,&plain); if(s!=Rc95LdrSuccess)return s;
        if(ch.PhysicalAddress<previousEnd||!HRange(p,ch.PhysicalAddress,plain))
            return Rc95LdrOutOfBounds;
        previousEnd=ch.PhysicalAddress+plain;
        if (pages > UINT64_MAX - plain / H_PAGE) return Rc95LdrIntegerOverflow;
        pages += plain / H_PAGE;
        used=off-sizeof(g_Header);
        if(ui)(void)Rc95LdrTextUiUpdateProgress(ui,
            "Validating hibernation memory chunks",
            HProgress(used,payload,5u,40u));
    }
    return used==payload&&pages==expectedPages?Rc95LdrSuccess:Rc95LdrCorruptData;
}
static RC95LDR_STATUS HRestorePayload(const RC95LDR_WINLOAD_PLATFORM *p,
    const RC95LDR_WINLOAD_FILE *f,uint64_t payload,const RC95LDR_TEXTUI *ui)
{
    uint64_t off=sizeof(g_Header),used=0;
    uint64_t previousEnd=0;
    while(used<payload) {
        H_CHUNK ch; uint32_t plain; void *mapping=NULL; RC95LDR_STATUS s;
        s=HChunk(p,f,&off,payload-used,&ch,&plain); if(s!=Rc95LdrSuccess)return s;
        if(ch.PhysicalAddress<previousEnd || !HRange(p,ch.PhysicalAddress,plain))
            return Rc95LdrOutOfBounds;
        previousEnd=ch.PhysicalAddress+plain;
        s=p->MapPhysicalRange(p->Context,ch.PhysicalAddress,plain,&mapping);
        if(s!=Rc95LdrSuccess||!mapping) {
            if(mapping)p->UnmapPhysicalRange(p->Context,mapping,plain);
            return s==Rc95LdrSuccess?Rc95LdrOutOfBounds:s;
        }
        memcpy(mapping,g_Decoded,plain);
        p->UnmapPhysicalRange(p->Context,mapping,plain);
        used=off-sizeof(g_Header);
        if(ui)(void)Rc95LdrTextUiUpdateProgress(ui,
            "Restoring RC95-OS memory pages",
            HProgress(used,payload,50u,49u));
    }
    return used==payload?Rc95LdrSuccess:Rc95LdrCorruptData;
}
RC95LDR_STATUS Rc95LdrResumeFromHibernation(
    const RC95LDR_WINLOAD_PLATFORM *p,const RC95LDR_WINLOAD_REQUEST *request)
{
    RC95LDR_WINLOAD_FILE file; uint16_t path[H_PATH_MAX];
    uint64_t payload,pages,flagPage; uint32_t machine,flagOffset,one=1;
    void *flagMap=NULL; RC95LDR_STATUS s; const RC95LDR_TEXTUI *ui=NULL;
    memset(&file,0,sizeof(file));
    if(!p||!request||p->StructureSize<sizeof(*p)||!p->OpenFile||
       !p->ReadFileAt||!p->CloseFile||!p->ValidateResumeRange||
       !p->MapPhysicalRange||!p->UnmapPhysicalRange||
       !p->ValidateResumeProcessorState)
        return Rc95LdrInvalidParameter;
    if(request->StructureSize >= offsetof(RC95LDR_WINLOAD_REQUEST,HiberFilePath) +
                                  sizeof(request->HiberFilePath) &&
       request->HiberFilePath) {
        size_t n=0;
        while(request->HiberFilePath[n] && n<H_PATH_MAX-1u) {
            path[n]=request->HiberFilePath[n]; ++n;
        }
        if(request->HiberFilePath[n]) return Rc95LdrOutOfBounds;
        if(!n || (path[0]!='\\' && path[0]!='/'))
            return Rc95LdrInvalidParameter;
        path[n]=0;
    } else {
        static const uint16_t defaultPath[]={'\\','h','i','b','e','r','f','i','l','.','s','y','s',0};
        size_t i;
        for(i=0;defaultPath[i];++i) path[i]=defaultPath[i];
        path[i]=0;
    }
#if defined(_M_AMD64)||defined(_M_X64)||defined(_AMD64_)||defined(__x86_64__)||defined(__amd64__)
    machine=RC95LDR_HIBER_MACHINE_AMD64;
#elif defined(_M_IX86)||defined(_X86_)||defined(__i386__)||defined(i386)
    machine=RC95LDR_HIBER_MACHINE_I386;
#else
    return Rc95LdrUnsupported;
#endif
    if(p->StructureSize>=offsetof(RC95LDR_WINLOAD_PLATFORM,TextUi)+sizeof(p->TextUi) &&
       p->TextUi && Rc95LdrTextUiValidate(p->TextUi)==Rc95LdrSuccess)
        ui=p->TextUi;
    if(ui)(void)Rc95LdrTextUiUpdateProgress(ui,"Checking the RC95-OS hibernation image",0);
    s=p->OpenFile(p->Context,path,&file);if(s!=Rc95LdrSuccess)return s;
    if(file.FileSize<sizeof(g_Header)||!HRead(p,&file,0,&g_Header,sizeof(g_Header))){
        s=Rc95LdrCorruptData;goto Done;
    }
    if(g_Header.Signature!=H_SIG||g_Header.Version!=H_VER||
       g_Header.HeaderSize!=sizeof(g_Header)||g_Header.MachineType!=machine||
       g_Header.PageSize!=H_PAGE||!(g_Header.Flags&RC95LDR_HIBER_FLAG_IMAGE_COMPLETE)||
       HHeaderChecksum(&g_Header)!=g_Header.CheckSum){
        s=Rc95LdrCorruptData;goto Done;
    }
    payload=((uint64_t)g_Header.PayloadSizeHigh<<32)|g_Header.PayloadSizeLow;
    pages=((uint64_t)g_Header.PageCountHigh<<32)|g_Header.PageCountLow;
    if(!payload||!pages||payload!=file.FileSize-sizeof(g_Header)||
       !HStateValid(p,machine)||
       !HRange(p,g_Header.ResumeFlagPhysicalAddress&~((uint64_t)H_PAGE-1u),H_PAGE)){
        s=Rc95LdrCorruptData;goto Done;
    }
    s=HValidatePayload(p,&file,payload,pages,ui);if(s!=Rc95LdrSuccess)goto Done;
    if(ui)(void)Rc95LdrTextUiUpdateProgress(ui,"Restoring the RC95-OS memory image",50);
    s=HRestorePayload(p,&file,payload,ui);if(s!=Rc95LdrSuccess)goto Done;
    flagPage=g_Header.ResumeFlagPhysicalAddress&~((uint64_t)H_PAGE-1u);
    flagOffset=(uint32_t)(g_Header.ResumeFlagPhysicalAddress&(H_PAGE-1u));
    s=p->MapPhysicalRange(p->Context,flagPage,H_PAGE,&flagMap);
    if(s!=Rc95LdrSuccess||!flagMap){if(s==Rc95LdrSuccess)s=Rc95LdrOutOfBounds;goto Done;}
    memcpy((uint8_t*)flagMap+flagOffset,&one,sizeof(one));
    p->UnmapPhysicalRange(p->Context,flagMap,H_PAGE);flagMap=NULL;
    p->CloseFile(p->Context,&file);memset(&file,0,sizeof(file));
    if(ui)(void)Rc95LdrTextUiUpdateProgress(ui,"Restoring the saved processor state",100);
    /* The architecture trampoline is part of WINLOAD itself. Do not route
       this final transition through a firmware/FreeLoader platform shim. */
    s=Rc95LdrRestoreProcessorState(p->Context,machine,g_Header.CpuState,
        g_Header.CpuStateSize,g_Header.Flags,g_Header.FxStateOffset,
        g_Header.ResumeFlagPhysicalAddress);
    /* A successful architecture trampoline is non-returning. */
    return s==Rc95LdrSuccess?Rc95LdrUnsupported:s;
Done:
    if(flagMap)p->UnmapPhysicalRange(p->Context,flagMap,H_PAGE);
    p->CloseFile(p->Context,&file);return s;
}
