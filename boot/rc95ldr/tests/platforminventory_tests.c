#include "rc95ldrplatforminventory.h"
#include <assert.h>
#include <string.h>

static uint8_t Memory[0x200000];
static int BarProbeActive;
static int BarProbeSawRestore;
static uint16_t LastCommandWrite;
static const void *MapPhysical(void *Context,uint64_t Address,size_t Length)
{
    (void)Context;
    if(Address>sizeof(Memory)||Length>sizeof(Memory)-(size_t)Address)return NULL;
    return Memory+(size_t)Address;
}
static RC95LDR_STATUS ReadPci(void *Context,uint16_t Segment,uint8_t Bus,
    uint8_t Device,uint8_t Function,uint16_t Offset,uint32_t *Value)
{
    (void)Context;
    *Value=0xffffffffu;
    if(Segment==0 && Bus==0 && Device==1 && Function==0) {
        if(Offset==0x00)*Value=0x56781234u;
        else if(Offset==0x08)*Value=0x06040000u;
        else if(Offset==0x0c)*Value=0x00010000u;
        else if(Offset==0x10)*Value=0x80000000u;
        else if(Offset==0x18)*Value=0x00202000u;
        else if(Offset==0x1c)*Value=0x00001010u;
        else if(Offset==0x20)*Value=0x00001000u;
        else if(Offset==0x24)*Value=0x00001000u;
        else if(Offset==0x28)*Value=0x00000001u;
        else if(Offset==0x2c)*Value=0x00000002u;
        else if(Offset==0x30)*Value=0x00000000u;
    } else if(Segment==2 && Bus==32 && Device==0 && Function==0) {
        if(Offset==0x00)*Value=0x22221111u;
        else if(Offset==0x08)*Value=0x03000000u;
        else if(Offset==0x04)*Value=0x00000007u;
        else if(Offset==0x0c)*Value=0;
        else if(Offset==0x10)*Value=BarProbeActive?0xfffff000u:0x90000000u;
    }
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS WritePci(void *Context,uint16_t Segment,uint8_t Bus,
    uint8_t Device,uint8_t Function,uint16_t Offset,uint8_t Width,uint32_t Value)
{
    (void)Context;
    if(Segment!=2||Bus!=32||Device!=0||Function!=0)return Rc95LdrUnsupported;
    if(Offset==4&&Width==2) { LastCommandWrite=(uint16_t)Value; return Rc95LdrSuccess; }
    if(Offset==0x10&&Width==4) {
        if(Value==UINT32_MAX)BarProbeActive=1;
        else { BarProbeActive=0; if(Value==0x90000000u)BarProbeSawRestore=1; }
        return Rc95LdrSuccess;
    }
    return Rc95LdrUnsupported;
}
static RC95LDR_STATUS QueryBarSize(void *Context,uint16_t Segment,
    uint8_t Bus,uint8_t Device,uint8_t Function,uint8_t BarIndex,uint64_t *Length)
{
    (void)Context;(void)Segment;(void)Bus;(void)Device;(void)Function;
    if(!Length||BarIndex!=0)return Rc95LdrNotFound;
    *Length=0x1000;
    return Rc95LdrSuccess;
}
static void SetChecksum(uint8_t *Data,size_t Length,size_t Offset)
{
    uint8_t sum=0;size_t i;
    Data[Offset]=0;
    for(i=0;i<Length;i++)sum=(uint8_t)(sum+Data[i]);
    Data[Offset]=(uint8_t)(0u-sum);
}
static void Put16(uint8_t *p,uint16_t v){memcpy(p,&v,2);}
static void Put32(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static void TestAcpiMadt(void)
{
    RC95LDR_PLATFORM_INVENTORY_INPUT in;
    RC95LDR_PLATFORM_INVENTORY_RESULT out;
    uint8_t *rsdp=Memory+0x1000,*rsdt=Memory+0x2000,*madt=Memory+0x3000;
    memset(Memory,0,sizeof(Memory));
    memcpy(rsdp,"RSD PTR ",8);rsdp[15]=0;Put32(rsdp+16,0x2000);SetChecksum(rsdp,20,8);
    memcpy(rsdt,"RSDT",4);Put32(rsdt+4,40);Put32(rsdt+36,0x3000);SetChecksum(rsdt,40,9);
    memcpy(madt,"APIC",4);Put32(madt+4,74);Put32(madt+36,0xfee00000u);
    madt[44]=0;madt[45]=8;madt[46]=0;madt[47]=2;Put32(madt+48,1);
    madt[52]=1;madt[53]=12;madt[54]=2;Put32(madt+56,0xfec00000u);Put32(madt+60,0);
    madt[64]=2;madt[65]=10;madt[66]=0;madt[67]=0;Put32(madt+68,2);madt[72]=0;madt[73]=0;
    SetChecksum(madt,74,9);
    memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);
    in.Version=RC95LDR_PLATFORM_INVENTORY_VERSION;in.RsdpPhysicalAddress=0x1000;
    in.MapPhysical=MapPhysical;in.ReadPciConfig=ReadPci;
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.Topology.AcpiPresent&&out.Topology.MadtPresent);
    assert(out.Topology.ProcessorCount==1&&out.Topology.Processors[0].ApicId==2);
    assert(out.Topology.IoControllerCount==1&&out.Topology.IoControllers[0].Address==0xfec00000u);
    assert(out.Topology.IrqOverrideCount==1&&out.Topology.IrqOverrides[0].GlobalSystemInterrupt==2);
}
static void TestMps(void)
{
    RC95LDR_PLATFORM_INVENTORY_INPUT in;
    RC95LDR_PLATFORM_INVENTORY_RESULT out;
    uint8_t *fp=Memory+0x4000,*table=Memory+0x5000;
    memset(Memory,0,sizeof(Memory));
    memcpy(fp,"_MP_",4);Put32(fp+4,0x5000);fp[8]=1;fp[9]=4;SetChecksum(fp,16,10);
    memcpy(table,"PCMP",4);table[6]=4;Put32(table+36,0xfee00000u);
    table[44]=0;table[45]=0;table[46]=0x14;table[47]=3;
    table[64]=1;table[65]=0;memcpy(table+66,"ISA   ",6);
    table[72]=2;table[73]=2;table[74]=0x11;table[75]=1;Put32(table+76,0xfec00000u);
    table[80]=3;table[81]=0;table[82]=0;table[83]=0;table[84]=0;table[85]=0;table[86]=2;table[87]=2;
    table[4]=88;table[5]=0;table[34]=4;table[35]=0;SetChecksum(table,88,7);
    memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);
    in.Version=RC95LDR_PLATFORM_INVENTORY_VERSION;in.MpsFloatingPointerAddress=0x4000;
    in.MapPhysical=MapPhysical;in.ReadPciConfig=ReadPci;
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.Topology.MpsPresent&&out.Topology.Source==2);
    assert(out.Topology.ProcessorCount==1&&out.Topology.Processors[0].ApicId==0);
    assert(out.Topology.IoControllerCount==1&&out.Topology.IoControllers[0].Id==2);
    assert(out.Topology.IrqOverrideCount==1&&out.Topology.IrqOverrides[0].GlobalSystemInterrupt==2);
    assert(out.Topology.MpsSpecificationRevision==4u);

    /* Revision 2 is retained explicitly so native preparation can reject
       HALMPS.DLL with the user-facing MPS 1.2 compatibility diagnostic. */
    fp[9]=2;SetChecksum(fp,16,10);
    table[6]=2;SetChecksum(table,88,7);
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.Topology.MpsPresent&&out.Topology.MpsSpecificationRevision==2u);
}

static void TestMcfgPciAndSmbios(void)
{
    RC95LDR_PLATFORM_INVENTORY_INPUT in;
    RC95LDR_PLATFORM_INVENTORY_RESULT out;
    uint8_t *xsdt=Memory+0x2000,*mcfg=Memory+0x3000,*ep=Memory+0x4000,*table=Memory+0x5000;
    uint32_t off=0;
    memset(Memory,0,sizeof(Memory));
    memcpy(xsdt,"XSDT",4);Put32(xsdt+4,44);Put32(xsdt+36,0x3000);SetChecksum(xsdt,44,9);
    memcpy(mcfg,"MCFG",4);Put32(mcfg+4,76);
    /* ECAM windows are 1 MiB aligned, and each segment has its own bus range. */
    {uint64_t base=0xe0000000ull;memcpy(mcfg+44,&base,8);}
    {uint16_t seg=0;memcpy(mcfg+52,&seg,2);mcfg[54]=0;mcfg[55]=0;}
    {uint64_t base=0xe1000000ull;memcpy(mcfg+60,&base,8);}
    {uint16_t seg=2;memcpy(mcfg+68,&seg,2);mcfg[70]=32;mcfg[71]=32;}
    SetChecksum(mcfg,76,9);

    /* SMBIOS 2.x entry point plus a small type-1 system record and type-127 end. */
    memcpy(ep,"_SM_",4);ep[5]=31;ep[6]=2;ep[7]=8;ep[10]=0;
    memcpy(ep+16,"_DMI_",5);Put16(ep+22,60);Put32(ep+24,0x5000);Put16(ep+28,2);
    ep[30]=0x28;SetChecksum(ep+16,15,5);SetChecksum(ep,31,4);
    table[off]=1;table[off+1]=0x19;Put16(table+off+2,1);
    table[off+4]=1;table[off+5]=2;table[off+6]=3;table[off+7]=4;
    memset(table+off+8,0x11,16);off+=0x19;
    memcpy(table+off,"ACME",5);off+=5;memcpy(table+off,"RC95 Board",11);off+=11;
    memcpy(table+off,"Rev A",6);off+=6;memcpy(table+off,"SN123",6);off+=6;
    table[off++]=0; /* second string-set terminator */
    table[off]=127;table[off+1]=4;Put16(table+off+2,0xffff);table[off+4]=0;table[off+5]=0;
    memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);
    in.Version=RC95LDR_PLATFORM_INVENTORY_VERSION;in.AcpiRoot.Revision=2;
    in.AcpiRoot.XsdtAddress=0x2000;in.MapPhysical=MapPhysical;
    in.ReadPciConfig=ReadPci;in.SmbiosEntryPointAddress=0x4000;
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.PciSegmentCount==2);
    assert(out.PciSegments[0].Segment==0&&out.PciSegments[0].StartBus==0);
    assert(out.PciSegments[1].Segment==2&&out.PciSegments[1].StartBus==32);
    assert(out.PciFunctionCount==2&&out.PciResourceCount==2);
    assert(out.PciBridgeCount==1);
    assert(out.PciFunctions[0].BridgeIndex==0);
    assert(out.PciBridges[0].SecondaryBus==32&&out.PciBridges[0].SubordinateBus==32);
    assert(out.PciResources[0].Base==0x80000000ull);
    assert(out.PciResources[1].Base==0x90000000ull);
    assert(out.PciResources[0].Length==0 &&
        (out.PciResources[0].Flags&RC95LDR_PCI_RESOURCE_SIZE_UNKNOWN));
    /* A firmware resource provider can supply a trusted size without the
       generic read-only PCI accessor writing to BAR registers. */
    in.QueryPciBarSize=QueryBarSize;
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.PciResources[0].Length==0x1000 &&
        !(out.PciResources[0].Flags&RC95LDR_PCI_RESOURCE_SIZE_UNKNOWN));
    assert(out.PciResources[1].Length==0x1000);
    /* Without the trusted size callback, the generic writer probes an assigned
       BAR after disabling decode/bus mastering and restores BAR + command. */
    in.QueryPciBarSize=NULL;in.WritePciConfig=WritePci;
    BarProbeActive=0;BarProbeSawRestore=0;LastCommandWrite=0xffffu;
    assert(Rc95LdrBuildPlatformInventory(&in,&out)==Rc95LdrSuccess);
    assert(out.PciResources[1].Length==0x1000);
    assert(!(out.PciResources[1].Flags&RC95LDR_PCI_RESOURCE_SIZE_UNKNOWN));
    assert(!BarProbeActive&&BarProbeSawRestore&&LastCommandWrite==0x0007u);
    assert(out.Smbios.MajorVersion==2&&out.Smbios.MinorVersion==8);
    assert(out.Smbios.StructureCount==2);
    assert(strcmp(out.Smbios.Manufacturer,"ACME")==0);
    assert(strcmp(out.Smbios.ProductName,"RC95 Board")==0);
    assert(strcmp(out.Smbios.SerialNumber,"SN123")==0);
    {
        uint32_t k,foundPci=0,foundSmbios=0;
        for(k=0;k<out.ComponentCount;k++) {
            const RC95LDR_CONFIG_COMPONENT_INPUT *component=&out.Components[k];
            if(component->Class==2 && component->Type==3) {
                const RC95LDR_PCI_CONFIGURATION_DATA *data=
                    (const RC95LDR_PCI_CONFIGURATION_DATA*)component->ConfigurationData;
                assert(component->ConfigurationDataLength==sizeof(*data));
                if(data->Function.Segment==0 && data->Function.Device==1) {
                    assert(data->Function.ResourceCount==1);
                    assert(data->Resources[0].Base==0x80000000ull);
                    assert(data->Bridge.SecondaryBus==32);
                    foundPci=1;
                }
            }
            if(component->Class==1 && component->Type==2)foundSmbios=1;
        }
        assert(foundPci && foundSmbios);
    }
}

int main(void)
{
    TestAcpiMadt();
    TestMps();
    TestMcfgPciAndSmbios();
    return 0;
}
