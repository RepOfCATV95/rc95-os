/*
 * PROJECT: RC95LDR
 * PURPOSE: Native x86 CPUID and paging-feature discovery for RC95-OS.
 * No FreeLoader interfaces or platform-specific memory services are used.
 */
#include "include/rc95ldrrc95nativeprovider.h"
#include <string.h>
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#define RC95LDR_BUILD_X86 1
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#define RC95LDR_BUILD_X86 1
#else
#define RC95LDR_BUILD_X86 0
#endif
#if defined(_MSC_VER) && defined(_M_IX86)
static int Rc95LdrCpuidAvailable(void) {
    unsigned int Changed;
    __asm {
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 0200000h
        push eax
        popfd
        pushfd
        pop eax
        push ecx
        popfd
        xor eax, ecx
        and eax, 0200000h
        mov Changed, eax
    }
    return Changed != 0;
}
#elif defined(_MSC_VER) && defined(_M_X64)
static int Rc95LdrCpuidAvailable(void) { return 1; }
#elif defined(__i386__)
static int Rc95LdrCpuidAvailable(void) {
    unsigned int Original, Changed;
    __asm__ volatile("pushfl; popl %0" : "=r"(Original));
    Changed = Original ^ 0x00200000u;
    __asm__ volatile("pushl %0; popfl" : : "r"(Changed) : "cc");
    __asm__ volatile("pushfl; popl %0" : "=r"(Changed));
    __asm__ volatile("pushl %0; popfl" : : "r"(Original) : "cc");
    return ((Original ^ Changed) & 0x00200000u) != 0;
}
#elif defined(__x86_64__)
static int Rc95LdrCpuidAvailable(void) { return 1; }
#endif
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
static void Rc95LdrCpuid(uint32_t Leaf, uint32_t Subleaf,
                         uint32_t *A, uint32_t *B, uint32_t *C, uint32_t *D) {
    int R[4];
    __cpuidex(R, (int)Leaf, (int)Subleaf);
    *A=(uint32_t)R[0]; *B=(uint32_t)R[1];
    *C=(uint32_t)R[2]; *D=(uint32_t)R[3];
}
#elif defined(__i386__) || defined(__x86_64__)
static void Rc95LdrCpuid(uint32_t Leaf, uint32_t Subleaf,
                         uint32_t *A, uint32_t *B, uint32_t *C, uint32_t *D) {
#if defined(__i386__) && defined(__PIC__)
    /*
     * EBX is the GOT base in 32-bit PIC code. Exchange it with a compiler-
     * allocated register around CPUID so the instruction's EBX result is
     * captured without violating the platform register convention.
     */
    uint32_t Bx;
    __asm__ volatile(
        "xchgl %%ebx, %1; cpuid; xchgl %%ebx, %1"
        : "=a"(*A), "=&r"(Bx), "=c"(*C), "=d"(*D)
        : "0"(Leaf), "2"(Subleaf));
    *B = Bx;
#else
    __asm__ volatile(
        "cpuid"
        : "=a"(*A), "=b"(*B), "=c"(*C), "=d"(*D)
        : "0"(Leaf), "2"(Subleaf));
#endif
}
#endif
#if RC95LDR_BUILD_X86
static uint32_t Rc95LdrDecodeFeatures(const RC95LDR_RC95_CPU_INFO *I) {
    uint32_t F=RC95LDR_CPU_FEATURE_CPUID, D=I->Leaf1Edx, C=I->Leaf1Ecx;
    uint32_t Xd=I->ExtendedLeaf1Edx, B7=I->Leaf7Ebx;
    if(D&(1u<<0)) F|=RC95LDR_CPU_FEATURE_FPU;
    if(D&(1u<<4)) F|=RC95LDR_CPU_FEATURE_TSC;
    if(D&(1u<<5)) F|=RC95LDR_CPU_FEATURE_MSR;
    if(D&(1u<<6)) F|=RC95LDR_CPU_FEATURE_PAE;
    if(D&(1u<<8)) F|=RC95LDR_CPU_FEATURE_CX8;
    if(D&(1u<<9)) F|=RC95LDR_CPU_FEATURE_APIC;
    if(D&(1u<<11)) F|=RC95LDR_CPU_FEATURE_SEP;
    if(D&(1u<<12)) F|=RC95LDR_CPU_FEATURE_MTRR;
    if(D&(1u<<13)) F|=RC95LDR_CPU_FEATURE_PGE;
    if(D&(1u<<15)) F|=RC95LDR_CPU_FEATURE_CMOV;
    if(D&(1u<<23)) F|=RC95LDR_CPU_FEATURE_MMX;
    if(D&(1u<<24)) F|=RC95LDR_CPU_FEATURE_FXSR;
    if(D&(1u<<25)) F|=RC95LDR_CPU_FEATURE_SSE;
    if(D&(1u<<26)) F|=RC95LDR_CPU_FEATURE_SSE2;
    if(C&(1u<<0)) F|=RC95LDR_CPU_FEATURE_SSE3;
    if(C&(1u<<9)) F|=RC95LDR_CPU_FEATURE_SSSE3;
    if(C&(1u<<19)) F|=RC95LDR_CPU_FEATURE_SSE41;
    if(C&(1u<<20)) F|=RC95LDR_CPU_FEATURE_SSE42;
    if(C&(1u<<13)) F|=RC95LDR_CPU_FEATURE_CX16;
    if(C&(1u<<25)) F|=RC95LDR_CPU_FEATURE_AES;
    if(C&(1u<<26)) F|=RC95LDR_CPU_FEATURE_XSAVE;
    if(C&(1u<<28)) F|=RC95LDR_CPU_FEATURE_AVX;
    if(C&(1u<<30)) F|=RC95LDR_CPU_FEATURE_RDRAND;
    if(B7&(1u<<5)) F|=RC95LDR_CPU_FEATURE_AVX2;
    if(B7&(1u<<10)) F|=RC95LDR_CPU_FEATURE_INVPCID;
    if(B7&(1u<<7)) F|=RC95LDR_CPU_FEATURE_SMEP;
    if(B7&(1u<<20)) F|=RC95LDR_CPU_FEATURE_SMAP;
    if(Xd&(1u<<20)) F|=RC95LDR_CPU_FEATURE_NX;
    if(Xd&(1u<<29)) F|=RC95LDR_CPU_FEATURE_LONG_MODE;
    if(Xd&(1u<<26)) F|=RC95LDR_CPU_FEATURE_PAGE1G;
    if(Xd&(1u<<27)) F|=RC95LDR_CPU_FEATURE_RDTSCP;
    return F;
}
#endif

RC95LDR_STATUS Rc95LdrQueryX86Cpu(void *Context,
    RC95LDR_NT_TARGET_ARCH Architecture, RC95LDR_RC95_CPU_INFO *Information) {
#if !RC95LDR_BUILD_X86
    (void)Context; (void)Architecture; (void)Information;
    return Rc95LdrUnsupported;
#else
    RC95LDR_RC95_CPU_INFO I;
    uint32_t A,B,C,D,MaxBasic,MaxExtended=0;
    uint32_t BaseFamily,BaseModel,ExtFamily,ExtModel;
    (void)Context;
    if(!Information || (Architecture!=Rc95LdrNtArchI386 &&
       Architecture!=Rc95LdrNtArchAmd64)) return Rc95LdrInvalidParameter;
    if(!Rc95LdrCpuidAvailable()) return Rc95LdrUnsupported;
    memset(&I,0,sizeof(I));
    I.StructureSize=sizeof(I); I.Version=RC95LDR_RC95_CPU_INFO_VERSION;
    Rc95LdrCpuid(0,0,&MaxBasic,&B,&C,&D);
    if(!MaxBasic) return Rc95LdrUnsupported;
    I.MaxBasicLeaf=MaxBasic; I.Vendor[0]=B; I.Vendor[1]=D; I.Vendor[2]=C;
    if(MaxBasic>=1u) {
        Rc95LdrCpuid(1,0,&A,&B,&C,&D);
        I.ProcessorSignature=A; I.Leaf1Ecx=C; I.Leaf1Edx=D;
        I.ProcessorId=(B>>24)&0xffu; I.HardwareId=I.ProcessorId;
        BaseFamily=(A>>8)&0xfu; BaseModel=(A>>4)&0xfu;
        ExtFamily=(A>>20)&0xffu; ExtModel=(A>>16)&0xfu;
        if(BaseFamily==0xfu) BaseFamily+=ExtFamily;
        if(BaseFamily==6u || BaseFamily==0xfu) BaseModel|=ExtModel<<4;
        I.ProcessorFamily=BaseFamily; I.ProcessorModel=BaseModel;
        I.ProcessorStepping=A&0xfu;
    }
    if(MaxBasic>=7u) {
        Rc95LdrCpuid(7,0,&A,&B,&C,&D);
        I.Leaf7Ebx=B; I.Leaf7Ecx=C; I.Leaf7Edx=D;
    }
    Rc95LdrCpuid(0x80000000u,0,&MaxExtended,&B,&C,&D);
    if(MaxExtended>=0x80000000u) I.MaxExtendedLeaf=MaxExtended;
    if(MaxExtended>=0x80000001u) {
        Rc95LdrCpuid(0x80000001u,0,&A,&B,&C,&D);
        I.ExtendedLeaf1Ecx=C; I.ExtendedLeaf1Edx=D;
    }
    if(MaxExtended>=0x80000008u) {
        Rc95LdrCpuid(0x80000008u,0,&A,&B,&C,&D);
        I.PhysicalAddressBits=A&0xffu; I.VirtualAddressBits=(A>>8)&0xffu;
    }
    I.FeatureFlags=Rc95LdrDecodeFeatures(&I);
    if(Architecture==Rc95LdrNtArchAmd64) {
        if(!(I.FeatureFlags&RC95LDR_CPU_FEATURE_PAE) ||
           !(I.FeatureFlags&RC95LDR_CPU_FEATURE_CX8) ||
           !(I.FeatureFlags&RC95LDR_CPU_FEATURE_LONG_MODE) ||
           !(I.FeatureFlags&RC95LDR_CPU_FEATURE_FXSR) ||
           !(I.FeatureFlags&RC95LDR_CPU_FEATURE_SSE2)) return Rc95LdrUnsupported;
        I.PagingMode=RC95LDR_CPU_PAGING_AMD64;
        if(!I.PhysicalAddressBits) I.PhysicalAddressBits=36u;
        if(!I.VirtualAddressBits) I.VirtualAddressBits=48u;
        if(I.PhysicalAddressBits<32u || I.PhysicalAddressBits>52u ||
           I.VirtualAddressBits<48u) return Rc95LdrUnsupported;
    } else if(I.FeatureFlags&RC95LDR_CPU_FEATURE_PAE) {
        if(!(I.FeatureFlags&RC95LDR_CPU_FEATURE_CX8)) return Rc95LdrUnsupported;
        I.PagingMode=RC95LDR_CPU_PAGING_I386_PAE;
        I.VirtualAddressBits=32u;
        if(!I.PhysicalAddressBits) I.PhysicalAddressBits=36u;
        if(I.PhysicalAddressBits<32u || I.PhysicalAddressBits>52u)
            return Rc95LdrUnsupported;
    } else {
        I.PagingMode=RC95LDR_CPU_PAGING_I386_NONPAE;
        I.PhysicalAddressBits=32u; I.VirtualAddressBits=32u;
    }
    *Information=I;
    return Rc95LdrSuccess;
#endif
}
