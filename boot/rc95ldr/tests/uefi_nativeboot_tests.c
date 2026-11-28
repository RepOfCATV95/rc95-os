#include "rc95ldruefinativeboot.h"
#include <stdio.h>
#include <string.h>

static int Check(RC95LDR_STATUS Actual, RC95LDR_STATUS Expected,
                 const char *Name)
{
    if (Actual == Expected) return 0;
    fprintf(stderr, "%s: expected %d, got %d\n",
            Name, (int)Expected, (int)Actual);
    return 1;
}

int main(void)
{
    RC95LDR_UEFI_NATIVE_DISCOVERY_CONTEXT Context;
    RC95LDR_UEFI_DISCOVERY_OPS Operations;
    RC95LDR_UEFI_IMAGE_CONTEXT Image;
    RC95LDR_PARTITION_INFO Partitions[1];
    RC95LDR_BCD_BOOT_CANDIDATE Candidates[1];
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;
    RC95LDR_WINDOWS_INSTALLATION Installation;
    size_t CandidateCount = 0, EspIndex = 0;
    unsigned Failures = 0;

    memset(&Context, 0, sizeof(Context));
    memset(&Operations, 0xa5, sizeof(Operations));
    memset(&Image, 0, sizeof(Image));
    memset(Partitions, 0, sizeof(Partitions));
    memset(Candidates, 0, sizeof(Candidates));
    memset(&Startup, 0, sizeof(Startup));
    memset(&Result, 0, sizeof(Result));
    memset(&Installation, 0, sizeof(Installation));

    Failures += Check(Rc95LdrUefiGetNativeDiscoveryOps(
        &Context, &Operations), Rc95LdrInvalidParameter,
        "uninitialized discovery ops");
    Rc95LdrUefiDestroyNativeDiscovery(&Context);
    Rc95LdrUefiDestroyNativeDiscovery(&Context);

#define EXPECT_INVALID(label, context, image, partitions, partition_capacity, \
                       candidates, candidate_capacity, count, hive, hive_size, \
                       startup, result, installation, esp_index)             \
    do {                                                                       \
        Failures += Check(Rc95LdrUefiNativeDiscoverAndStartWindows(            \
            context, image, 0, partitions, partition_capacity, candidates,     \
            candidate_capacity, count, hive, hive_size, startup, result,      \
            installation, esp_index, NULL), Rc95LdrInvalidParameter, label);  \
    } while (0)

    EXPECT_INVALID("null context", NULL, &Image, Partitions, 1,
        Candidates, 1, &CandidateCount, &Result, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("null image", &Context, NULL, Partitions, 1,
        Candidates, 1, &CandidateCount, &Result, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("null partition array", &Context, &Image, NULL, 1,
        Candidates, 1, &CandidateCount, &Result, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("zero partition capacity", &Context, &Image, Partitions, 0,
        Candidates, 1, &CandidateCount, &Result, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("null candidate array", &Context, &Image, Partitions, 1,
        NULL, 1, &CandidateCount, &Result, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("zero candidate capacity", &Context, &Image, Partitions, 1,
        Candidates, 0, &CandidateCount, &Result, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("null candidate count", &Context, &Image, Partitions, 1,
        Candidates, 1, NULL, &Result, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("null hive buffer", &Context, &Image, Partitions, 1,
        Candidates, 1, &CandidateCount, NULL, sizeof(Result), &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("zero hive capacity", &Context, &Image, Partitions, 1,
        Candidates, 1, &CandidateCount, &Result, 0, &Startup,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("null startup", &Context, &Image, Partitions, 1,
        Candidates, 1, &CandidateCount, &Result, sizeof(Result), NULL,
        &Result, &Installation, &EspIndex);
    EXPECT_INVALID("null result", &Context, &Image, Partitions, 1,
        Candidates, 1, &CandidateCount, &Result, sizeof(Result), &Startup,
        NULL, &Installation, &EspIndex);
    EXPECT_INVALID("null selected installation", &Context, &Image,
        Partitions, 1, Candidates, 1, &CandidateCount, &Result,
        sizeof(Result), &Startup, &Result, NULL, &EspIndex);
    EXPECT_INVALID("null ESP index", &Context, &Image, Partitions, 1,
        Candidates, 1, &CandidateCount, &Result, sizeof(Result), &Startup,
        &Result, &Installation, NULL);

#undef EXPECT_INVALID


    {
        RC95LDR_WINDOWS_INSTALLATION Bcd;
        char WimPath[128],GuidText[39];
        static const uint16_t Options[] =
            {'r','a','m','d','i','s','k','=','[','b','o','o','t',']',
             '\\','s','o','u','r','c','e','s','\\','b','o','o','t',
             '.', 'w','i','m',',','{','1','2','3','4','5','6','7','8','-','1','2','3','4','-','4','2','3','4','-','8','9','a','b','-','1','2','3','4','5','6','7','8','9','0','a','b','c','d','e','f','}',' ',0};
        memset(&Bcd,0,sizeof(Bcd));
        memcpy(Bcd.BootOptionsUtf16,Options,sizeof(Options));
        Bcd.BootOptionsCharacters=(uint32_t)(sizeof(Options)/sizeof(Options[0])-1);
        if (Rc95LdrUefiParseBcdRamdiskTarget(&Bcd,WimPath,sizeof(WimPath),GuidText)!=Rc95LdrSuccess ||
            strcmp(WimPath,"sources\\boot.wim")!=0 ||
            strcmp(GuidText,"{12345678-1234-4234-89ab-1234567890abcdef}")!=0) {
            fprintf(stderr,"BCD ramdisk WIM path decoding failed: %s\\n",WimPath);
            ++Failures;
        }
        memset(&Bcd,0,sizeof(Bcd));
        if (Rc95LdrUefiParseBcdRamdiskTarget(&Bcd,WimPath,sizeof(WimPath),GuidText)!=Rc95LdrNotFound)
            ++Failures;
        Bcd.BootOptionsCharacters=3;
        Bcd.BootOptionsUtf16[0]='b'; Bcd.BootOptionsUtf16[1]='a';
        Bcd.BootOptionsUtf16[2]='d';
        if (Rc95LdrUefiParseBcdWimPath(&Bcd,WimPath,sizeof(WimPath))!=Rc95LdrNotFound)
            ++Failures;
        {
            static const uint16_t BadGuid[] =
                {'r','a','m','d','i','s','k','=','[','b','o','o','t',']',
                 '\\','b','o','o','t','.','w','i','m',',','{','1','2','3','4','5','6','7','8',
                 '-','1','2','3','4','-','4','2','3','4','-','8','9','a','b','-',
                 '1','2','3','4','5','6','7','8','9','0','a','b','c','d','e','g','}'};
            memset(&Bcd,0,sizeof(Bcd));
            memcpy(Bcd.BootOptionsUtf16,BadGuid,sizeof(BadGuid));
            Bcd.BootOptionsCharacters=(uint32_t)(sizeof(BadGuid)/sizeof(BadGuid[0]));
            if (Rc95LdrUefiParseBcdRamdiskTarget(&Bcd,WimPath,sizeof(WimPath),GuidText)!=Rc95LdrCorruptData)
                ++Failures;
        }
        {
            static const uint16_t Traversal[] =
                {'r','a','m','d','i','s','k','=','[','b','o','o','t',']',
                 '\\','.','.','\\','e','v','i','l','.','w','i','m',',','{','x','}'};
            memset(&Bcd,0,sizeof(Bcd));
            memcpy(Bcd.BootOptionsUtf16,Traversal,sizeof(Traversal));
            Bcd.BootOptionsCharacters=(uint32_t)(sizeof(Traversal)/sizeof(Traversal[0]));
            if (Rc95LdrUefiParseBcdRamdiskTarget(&Bcd,WimPath,sizeof(WimPath),GuidText)!=Rc95LdrCorruptData)
                ++Failures;
        }
    }
\n    {
        uint8_t SdiImage[12288];
        RC95LDR_SDI_IMAGE_INFO SdiInfo;
        memset(SdiImage,0,sizeof(SdiImage));
        memcpy(SdiImage,"$SDI0001",8);
        SdiImage[8]=1; /* volatile memory image */
        SdiImage[112]=1; /* default page alignment */
        /* TOC[0] = PART at 4096, TOC[1] = LOAD at 8192,
           TOC[2] = BOOT at 10240. All offsets are file-relative. */
        SdiImage[1024]='P'; SdiImage[1025]='A'; SdiImage[1026]='R'; SdiImage[1027]='T';
        SdiImage[1041]=0x10; SdiImage[1049]=0x10;
        SdiImage[1088]='L'; SdiImage[1089]='O'; SdiImage[1090]='A'; SdiImage[1091]='D';
        SdiImage[1104]=0x00; SdiImage[1105]=0x20;
        SdiImage[1112]=0x00; SdiImage[1113]=0x08;
        SdiImage[1152]='B'; SdiImage[1153]='O'; SdiImage[1154]='O'; SdiImage[1155]='T';
        SdiImage[1168]=0x00; SdiImage[1169]=0x28;
        SdiImage[1176]=0x00; SdiImage[1177]=0x08;
        SdiImage[16]=0x00; SdiImage[17]=0x28;
        SdiImage[24]=0x00; SdiImage[25]=0x08;
        Failures += Check(Rc95LdrUefiParseSdiImage(SdiImage,sizeof(SdiImage),&SdiInfo),
            Rc95LdrSuccess,"valid SDI TOC and partition layout");
        if (SdiInfo.SectionCount!=3 || SdiInfo.RamdiskKind!=Rc95LdrSdiRamdiskPartition ||
            SdiInfo.RamdiskOffset!=4096 || SdiInfo.RamdiskSize!=4096 ||
            SdiInfo.Sections[0].Type!=0x54524150u ||
            SdiInfo.Sections[1].Type!=0x44414f4cu ||
            SdiInfo.Sections[1].Offset!=8192 || SdiInfo.Sections[1].Size!=2048 ||
            SdiInfo.BootCodeOffset!=10240 || SdiInfo.BootCodeSize!=2048) {
            fprintf(stderr,"SDI section/ramdisk layout decode failed\\n"); ++Failures;
        }
        Failures += Check(Rc95LdrUefiValidateSdiImage(SdiImage,sizeof(SdiImage)),
            Rc95LdrSuccess,"valid SDI image");
        Failures += Check(Rc95LdrUefiValidateSdiImage(SdiImage,4095),
            Rc95LdrCorruptData,"truncated SDI header");
        SdiImage[0]='X';
        Failures += Check(Rc95LdrUefiValidateSdiImage(SdiImage,sizeof(SdiImage)),
            Rc95LdrInvalidImage,"invalid SDI signature");
        SdiImage[0]='

    if (Failures) return 1;
    puts("RC95LDR native UEFI boot contract tests passed");
    return 0;
}
;
        /* Force LOAD to overlap PART: the parser must reject the layout. */
        SdiImage[1104]=0x00; SdiImage[1105]=0x10;
        Failures += Check(Rc95LdrUefiValidateSdiImage(SdiImage,sizeof(SdiImage)),
            Rc95LdrCorruptData,"SDI overlapping section extents");
        Failures += Check(Rc95LdrUefiValidateSdiImage(SdiImage,
            (size_t)64u*1024u*1024u+1u),
            Rc95LdrCorruptData,"SDI over strict size limit");
        Failures += Check(Rc95LdrUefiValidateSdiImage(NULL,sizeof(SdiImage)),
            Rc95LdrInvalidParameter,"null SDI image");
    }

    if (Failures) return 1;
    puts("RC95LDR native UEFI boot contract tests passed");
    return 0;
}
