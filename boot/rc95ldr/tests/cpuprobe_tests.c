#include "rc95ldrrc95nativeprovider.h"
#include <assert.h>
#include <string.h>
int main(void) {
    RC95LDR_RC95_CPU_INFO Cpu;
    RC95LDR_STATUS Status;
    memset(&Cpu,0,sizeof(Cpu));
    Status=Rc95LdrQueryX86Cpu(NULL,Rc95LdrNtArchAmd64,&Cpu);
    if(Status==Rc95LdrUnsupported) return 0;
    assert(Status==Rc95LdrSuccess);
    assert(Cpu.StructureSize==sizeof(Cpu));
    assert(Cpu.Version==RC95LDR_RC95_CPU_INFO_VERSION);
    assert(Cpu.FeatureFlags&RC95LDR_CPU_FEATURE_CPUID);
    assert(Cpu.FeatureFlags&RC95LDR_CPU_FEATURE_CX8);
    assert(Cpu.FeatureFlags&RC95LDR_CPU_FEATURE_PAE);
    assert(Cpu.FeatureFlags&RC95LDR_CPU_FEATURE_LONG_MODE);
    assert(Cpu.PagingMode==RC95LDR_CPU_PAGING_AMD64);
    assert(Cpu.PhysicalAddressBits>=32u && Cpu.PhysicalAddressBits<=52u);
    {
        RC95LDR_RC95_CPU_INFO I386;
        RC95LDR_STATUS I386Status;
        memset(&I386,0,sizeof(I386));
        I386Status=Rc95LdrQueryX86Cpu(NULL,Rc95LdrNtArchI386,&I386);
        if(I386Status==Rc95LdrSuccess) {
            assert(I386.FeatureFlags&RC95LDR_CPU_FEATURE_CPUID);
            assert(I386.FeatureFlags&RC95LDR_CPU_FEATURE_CX8);
            if(I386.PagingMode==RC95LDR_CPU_PAGING_I386_NONPAE)
                assert(I386.PhysicalAddressBits==32u);
            else {
                assert(I386.PagingMode==RC95LDR_CPU_PAGING_I386_PAE);
                assert(I386.FeatureFlags&RC95LDR_CPU_FEATURE_PAE);
                assert(I386.VirtualAddressBits==32u);
            }
        } else assert(I386Status==Rc95LdrUnsupported);
    }
    return 0;
}
