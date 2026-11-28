#include "rc95ldrwinload.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define TEST_HIBER_SIG 0x72626968u
#define TEST_HIBER_PAGE 4096u
#define TEST_HIBER_FX 0x10u
#define TEST_HIBER_FPU 0x20u
#define TEST_HIBER_CPU 0x04u
#define TEST_HIBER_UP 0x08u
#define TEST_HIBER_COMPLETE 0x02u

typedef struct _TEST_HIBER_HEADER {
    uint32_t Signature,Version,HeaderSize,MachineType,PageSize;
    uint32_t PageCountLow,PageCountHigh,PayloadSizeLow,PayloadSizeHigh;
    uint32_t Flags,CheckSum,RtcTime[8];
    uint32_t CpuStateVersion,CpuStateSize,ProcessorCount;
    uint32_t FxStateOffset,FxStateSize,Reserved;
    uint64_t ResumeFlagPhysicalAddress;
    uint8_t CpuState[2048];
} TEST_HIBER_HEADER;
typedef struct _TEST_HIBER_CHUNK {
    uint64_t PhysicalAddress; uint32_t Length,CheckSum;
} TEST_HIBER_CHUNK;

static uint8_t Image[8192];
static uint8_t Page[TEST_HIBER_PAGE];
static size_t ImageSize;
static unsigned MapCalls,RestoreCalls,CloseCalls,PathCalls,FailMapAt;
static const uint16_t HiberPath[]={'\\','h','i','b','e','r','f','i','l','.','s','y','s',0};

static uint32_t XorWords(const void *data,size_t bytes)
{
    const uint8_t *p=(const uint8_t *)data; uint32_t x=0,word; size_t i;
    for(i=0;i<bytes/4;i++){memcpy(&word,p+i*4,sizeof(word));x^=word;}
    return x;
}
static void EmitMatch(uint8_t *out,size_t *pos,unsigned length)
{
    uint16_t code=(uint16_t)((length-3u)<<12);
    out[(*pos)++]=(uint8_t)code; out[(*pos)++]=(uint8_t)(code>>8);
}
static size_t BuildCompressedZeroPage(uint8_t *out)
{
    size_t n=0; unsigned g,t;
    out[n++]=0xFE; out[n++]=0; /* literal zero, followed by seven matches */
    for(t=0;t<7;t++)EmitMatch(out,&n,18);
    for(g=0;g<27;g++) {
        out[n++]=0xFF;
        for(t=0;t<8;t++)EmitMatch(out,&n,18);
    }
    out[n++]=0x1F;
    for(t=0;t<4;t++)EmitMatch(out,&n,18);
    EmitMatch(out,&n,9);
    return n;
}
static int BuildImage(void)
{
    TEST_HIBER_HEADER *h=(TEST_HIBER_HEADER *)Image;
    TEST_HIBER_CHUNK *ch=(TEST_HIBER_CHUNK *)(Image+sizeof(*h));
    uint8_t *stored=(uint8_t *)(ch+1);
    size_t stream;
    uint32_t original=TEST_HIBER_PAGE;
    uint32_t machine;
    memset(Image,0,sizeof(Image)); memset(Page,0xCC,sizeof(Page));
    stream=BuildCompressedZeroPage(stored+4);
#if defined(_M_AMD64)||defined(_M_X64)||defined(_AMD64_)||defined(__x86_64__)||defined(__amd64__)
    machine=RC95LDR_HIBER_MACHINE_AMD64;
    h->CpuStateSize=0x5B0u; h->Flags=TEST_HIBER_FX|TEST_HIBER_FPU;
    h->FxStateOffset=0x5B0u; h->FxStateSize=512u;
    { uint64_t cr3=0x1000; memcpy(h->CpuState+0x10,&cr3,sizeof(cr3)); }
#else
    machine=RC95LDR_HIBER_MACHINE_I386;
    h->CpuStateSize=0x320u;
    { uint32_t cr3=0x1000; memcpy(h->CpuState+0x2D4,&cr3,sizeof(cr3)); }
#endif
    memcpy(stored,&original,sizeof(original));
    ch->PhysicalAddress=0x3000;
    ch->Length=0x80000000u+(uint32_t)(stream+4);
    ch->CheckSum=(uint32_t)ch->PhysicalAddress ^ TEST_HIBER_PAGE;
    h->Signature=TEST_HIBER_SIG; h->Version=4;
    h->HeaderSize=sizeof(*h); h->MachineType=machine; h->PageSize=TEST_HIBER_PAGE;
    h->PageCountLow=1; h->PayloadSizeLow=(uint32_t)(sizeof(*ch)+stream+4);
    h->Flags|=TEST_HIBER_COMPLETE|TEST_HIBER_CPU|TEST_HIBER_UP;
    h->CpuStateVersion=1; h->ProcessorCount=1;
    h->ResumeFlagPhysicalAddress=0x2000;
    h->CheckSum=0; h->CheckSum=XorWords(h,sizeof(*h));
    ImageSize=sizeof(*h)+sizeof(*ch)+stream+4;
    return 1;
}
static RC95LDR_STATUS OpenFile(void *ctx,const uint16_t *path,RC95LDR_WINLOAD_FILE *file)
{
    size_t i;
    (void)ctx;
    for(i=0;HiberPath[i]&&path[i]==HiberPath[i];++i) {}
    if(HiberPath[i]||path[i]) return Rc95LdrInvalidParameter;
    PathCalls++;
    file->FileContext=Image;file->FileSize=ImageSize;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS ReadFile(void *ctx,const RC95LDR_WINLOAD_FILE *file,
    uint64_t offset,void *buffer,size_t length)
{
    (void)ctx;
    if(offset>file->FileSize||length>file->FileSize-offset)return Rc95LdrOutOfBounds;
    memcpy(buffer,(const uint8_t *)file->FileContext+(size_t)offset,length);
    return Rc95LdrSuccess;
}
static void CloseFile(void *ctx,RC95LDR_WINLOAD_FILE *file)
{(void)ctx;(void)file;CloseCalls++;}
static RC95LDR_STATUS ValidateRange(void *ctx,uint64_t base,uint64_t length)
{(void)ctx;return (base&&length)?Rc95LdrSuccess:Rc95LdrOutOfBounds;}
static RC95LDR_STATUS MapRange(void *ctx,uint64_t base,uint64_t length,void **mapping)
{
    (void)ctx;(void)base;
    if(length!=TEST_HIBER_PAGE)return Rc95LdrOutOfBounds;
    MapCalls++;
    if(FailMapAt && MapCalls==FailMapAt)return Rc95LdrIoError;
    *mapping=Page;return Rc95LdrSuccess;
}
static void UnmapRange(void *ctx,void *mapping,uint64_t length)
{(void)ctx;(void)mapping;(void)length;}
static RC95LDR_STATUS ValidateCpu(void *ctx,uint32_t machine,const void *state,
    uint32_t size,uint32_t flags,uint32_t fxOffset,uint32_t fxSize)
{(void)ctx;(void)machine;(void)state;(void)size;(void)flags;(void)fxOffset;(void)fxSize;return Rc95LdrSuccess;}
static int RunTest(int corrupt)
{
    RC95LDR_WINLOAD_PLATFORM p;
    RC95LDR_WINLOAD_REQUEST q;
    RC95LDR_STATUS s;
    TEST_HIBER_CHUNK *ch;
    TEST_HIBER_HEADER *h;
    static const uint16_t root[]={'C',':','\\','W','i','n','d','o','w','s',0};
    memset(&p,0,sizeof(p));memset(&q,0,sizeof(q));
    BuildImage();
    h=(TEST_HIBER_HEADER *)Image;
    ch=(TEST_HIBER_CHUNK *)(Image+sizeof(*h));
    if(corrupt==-1) {
        /* Exercise the raw (uncompressed) memory-chunk representation. */
        memset(ch+1,0,TEST_HIBER_PAGE);
        ch->Length=TEST_HIBER_PAGE;
        ch->CheckSum=(uint32_t)ch->PhysicalAddress^TEST_HIBER_PAGE;
        h->PayloadSizeLow=(uint32_t)(sizeof(*ch)+TEST_HIBER_PAGE);
        h->CheckSum=0;
        h->CheckSum=XorWords(h,sizeof(*h));
        ImageSize=sizeof(*h)+h->PayloadSizeLow;
    } else if(corrupt>0) {
        if(corrupt == 1) {
            ch->CheckSum^=1u;
        } else if(corrupt==2) {
            /* The first match follows one literal. Force offset 4096 when
               only one output byte exists; the decoder must reject it. */
            ((uint8_t *)(ch+1))[6]=0xFF;
            ((uint8_t *)(ch+1))[7]=0x0F;
        } else if(corrupt==3) {
            uint32_t stored=ch->Length&0x7FFFFFFFu;
            /* Set an unused token bit after the output page is complete. */
            ((uint8_t *)(ch+1))[stored-11u]|=0x80u;
        } else if(corrupt==4) {
            /* Header checksum corruption must be rejected before mapping. */
            h->CheckSum^=1u;
        } else if(corrupt==5) {
            /* One control byte cannot produce a 4 KiB output page. */
            ch->Length=0x80000005u;
        } else {
            uint32_t invalidPlain=TEST_HIBER_PAGE+1u;
            memcpy(ch+1,&invalidPlain,sizeof(invalidPlain));
        }
    }
    p.StructureSize=sizeof(p);p.Version=RC95LDR_WINLOAD_ABI_VERSION;
    p.OpenFile=OpenFile;p.ReadFileAt=ReadFile;p.CloseFile=CloseFile;
    p.ValidateResumeRange=ValidateRange;p.MapPhysicalRange=MapRange;
    p.UnmapPhysicalRange=UnmapRange;p.ValidateResumeProcessorState=ValidateCpu;
    q.StructureSize=sizeof(q);q.Version=RC95LDR_WINLOAD_ABI_VERSION;
    q.OsFamily=Rc95LdrOsRc95;q.ResumePolicy=Rc95LdrResumeInsideWinload;
    q.SystemRoot=root;
    q.HiberFilePath=corrupt>0?NULL:HiberPath;
    MapCalls=RestoreCalls=CloseCalls=PathCalls=0;
    FailMapAt=(corrupt==-2)?1u:0u;
    s=Rc95LdrResumeFromHibernation(&p,&q);
    FailMapAt=0;
    if(corrupt==-2)
        return s==Rc95LdrIoError && MapCalls==1 && RestoreCalls==0 &&
               CloseCalls==1 && PathCalls==1;
    if(corrupt>0)
        return s==Rc95LdrCorruptData && MapCalls==0 && RestoreCalls==0 && CloseCalls==1 && PathCalls==1;
    return s==Rc95LdrUnsupported && MapCalls==2 && RestoreCalls==0 &&
           CloseCalls==1 && PathCalls==1 && Page[0]==1 && Page[1]==0 && Page[2]==0 && Page[3]==0;
}
int main(void)
{
    if(!RunTest(0)){puts("compressed hibernation restore test failed");return 1;}
    if(!RunTest(1)){puts("hibernation checksum rejection test failed");return 1;}
    if(!RunTest(2)){puts("invalid LZ back-reference rejection test failed");return 1;}
    if(!RunTest(3)){puts("unused LZ control-bit rejection test failed");return 1;}
    if(!RunTest(4)){puts("hibernation header checksum rejection test failed");return 1;}
    if(!RunTest(5)){puts("truncated LZ stream rejection test failed");return 1;}
    if(!RunTest(6)){puts("invalid decoded chunk length rejection test failed");return 1;}
    if(!RunTest(-1)){puts("uncompressed hibernation restore test failed");return 1;}
    if(!RunTest(-2)){puts("physical mapping failure propagation test failed");return 1;}
    puts("RC95 hibernation resume tests passed");
    return 0;
}
