#include "rc95ldrplatforminventory.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#pragma pack(push,1)
typedef struct _ACPI_SDT_HEADER {char Signature[4];uint32_t Length;uint8_t Revision,Checksum;char OemId[6],OemTableId[8];uint32_t OemRevision,CreatorId,CreatorRevision;} ACPI_SDT_HEADER;
typedef struct _ACPI_RSDP20 {char Signature[8];uint8_t Checksum;char OemId[6];uint8_t Revision;uint32_t RsdtAddress;uint32_t Length;uint64_t XsdtAddress;uint8_t ExtendedChecksum;uint8_t Reserved[3];} ACPI_RSDP20;
#pragma pack(pop)
static int Checksum(const uint8_t*p,size_t n){uint8_t s=0;size_t i;for(i=0;i<n;i++)s=(uint8_t)(s+p[i]);return s==0;}
static RC95LDR_STATUS AddComponent(RC95LDR_PLATFORM_INVENTORY_RESULT*r,uint32_t cls,uint32_t type,const char*name,const void*data,uint32_t length,int parent)
{
    RC95LDR_CONFIG_COMPONENT_INPUT*c;uint32_t n=r->ComponentCount;
    if(n>=RC95LDR_PLATFORM_COMPONENT_MAX||length>RC95LDR_PLATFORM_CONFIG_DATA_MAX)return Rc95LdrBufferTooSmall;
    c=&r->Components[n];memset(c,0,sizeof(*c));c->Class=cls;c->Type=type;c->ParentIndex=parent;
    (void)snprintf(r->Identifiers[n],sizeof(r->Identifiers[n]),"%s",name);
    c->Identifier=r->Identifiers[n];
    if(length){memcpy(r->ConfigurationData[n],data,length);c->ConfigurationData=r->ConfigurationData[n];c->ConfigurationDataLength=length;}
    r->ComponentCount++;return Rc95LdrSuccess;
}

static uint16_t Read16(const uint8_t *p){uint16_t v;memcpy(&v,p,2);return v;}
static uint32_t Read32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t Read64(const uint8_t *p){uint64_t v;memcpy(&v,p,8);return v;}
static int ScanSignature(const RC95LDR_PLATFORM_INVENTORY_INPUT*i,uint64_t base,uint32_t length,const char*sig,uint32_t siglen,uint64_t*out)
{
    uint32_t off;
    if(!base||length<(siglen==8?20u:16u))return 0;
    for(off=0;off<=length-(siglen==8?20u:16u);off+=16){
        const uint8_t*p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,base+off,siglen==8?20:16);
        if(p&&memcmp(p,sig,siglen)==0&&Checksum(p,siglen==8?20:16)){*out=base+off;return 1;}
    }
    return 0;
}
static uint64_t FindRsdp(const RC95LDR_PLATFORM_INVENTORY_INPUT*i)
{
    const uint8_t*p;uint16_t ebda=0,kb=0;uint64_t found=0;
    p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,0x40e,2);if(p)ebda=Read16(p);
    p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,0x413,2);if(p)kb=Read16(p);
    if(ebda&&ScanSignature(i,(uint64_t)ebda<<4,1024,"RSD PTR ",8,&found))return found;
    if(kb&&kb<=640&&ScanSignature(i,(uint64_t)kb*1024-1024,1024,"RSD PTR ",8,&found))return found;
    if(ScanSignature(i,0xe0000,0x20000,"RSD PTR ",8,&found))return found;
    return 0;
}
static RC95LDR_STATUS ParseMadt(const RC95LDR_PLATFORM_INVENTORY_INPUT*i,RC95LDR_PLATFORM_INVENTORY_RESULT*r,uint64_t address,const ACPI_SDT_HEADER*h)
{
    const uint8_t*p=(const uint8_t*)h;uint32_t off;
    if(h->Length<44)return Rc95LdrInvalidImage;
    r->Topology.AcpiPresent=1;r->Topology.MadtPresent=1;r->Topology.Source=1;
    r->Topology.LocalApicAddress=Read32(p+36);
    r->Topology.MadtFlags=Read32(p+40);
    off=44;
    while(off<h->Length){
        uint8_t type,len;const uint8_t*e;
        if(h->Length-off<2)return Rc95LdrInvalidImage;
        e=p+off;type=e[0];len=e[1];
        if(len<2||len>h->Length-off)return Rc95LdrInvalidImage;
        if(type==0){
            if(len<8)return Rc95LdrInvalidImage;
            if(Read32(e+4)&3u){
                uint32_t n=r->Topology.ProcessorCount,j;
                if(n>=RC95LDR_APIC_PROCESSOR_MAX)return Rc95LdrBufferTooSmall;
                for(j=0;j<n;j++)if(r->Topology.Processors[j].ApicId==e[3])return Rc95LdrInvalidImage;
                r->Topology.Processors[n].ProcessorId=e[2];r->Topology.Processors[n].ApicId=e[3];
                r->Topology.Processors[n].Flags=Read32(e+4);r->Topology.ProcessorCount++;
            }
        }else if(type==1){
            uint32_t n;
            if(len<12)return Rc95LdrInvalidImage;
            n=r->Topology.IoControllerCount;if(n>=RC95LDR_APIC_IO_CONTROLLER_MAX)return Rc95LdrBufferTooSmall;
            r->Topology.IoControllers[n].Id=e[2];r->Topology.IoControllers[n].Address=Read32(e+4);
            r->Topology.IoControllers[n].GlobalSystemInterruptBase=Read32(e+8);
            if(!r->Topology.IoControllers[n].Address)return Rc95LdrInvalidImage;
            r->Topology.IoControllerCount++;
        }else if(type==2){
            uint32_t n;
            if(len<10)return Rc95LdrInvalidImage;
            n=r->Topology.IrqOverrideCount;if(n>=RC95LDR_APIC_IRQ_OVERRIDE_MAX)return Rc95LdrBufferTooSmall;
            r->Topology.IrqOverrides[n].Bus=e[2];r->Topology.IrqOverrides[n].SourceIrq=e[3];
            r->Topology.IrqOverrides[n].GlobalSystemInterrupt=Read32(e+4);
            r->Topology.IrqOverrides[n].Flags=Read16(e+8);
            r->Topology.IrqOverrides[n].InterruptType=0;r->Topology.IrqOverrides[n].DestinationApic=UINT32_MAX;
            r->Topology.IrqOverrideCount++;
        }else if(type==3){
            uint32_t n;
            if(len<8)return Rc95LdrInvalidImage;
            n=r->Topology.IrqOverrideCount;if(n>=RC95LDR_APIC_IRQ_OVERRIDE_MAX)return Rc95LdrBufferTooSmall;
            r->Topology.IrqOverrides[n].GlobalSystemInterrupt=Read32(e+4);
            r->Topology.IrqOverrides[n].Flags=Read16(e+2);
            r->Topology.IrqOverrides[n].InterruptType=1;
            r->Topology.IrqOverrides[n].DestinationApic=UINT32_MAX;
            r->Topology.IrqOverrideCount++;
        }else if(type==4){
            uint32_t n;
            if(len<6)return Rc95LdrInvalidImage;
            n=r->Topology.IrqOverrideCount;if(n>=RC95LDR_APIC_IRQ_OVERRIDE_MAX)return Rc95LdrBufferTooSmall;
            r->Topology.IrqOverrides[n].SourceIrq=e[2];
            r->Topology.IrqOverrides[n].Flags=Read16(e+3);
            r->Topology.IrqOverrides[n].GlobalSystemInterrupt=e[5];
            r->Topology.IrqOverrides[n].InterruptType=2;
            r->Topology.IrqOverrides[n].DestinationApic=UINT32_MAX;
            r->Topology.IrqOverrideCount++;
        }else if(type==5){
            if(len<12)return Rc95LdrInvalidImage;
            r->Topology.LocalApicAddress=Read64(e+4);
        }else if(type==9){
            uint32_t n,j,apicId;
            if(len<16)return Rc95LdrInvalidImage;
            if(Read32(e+8)&3u){
                apicId=Read32(e+4);n=r->Topology.ProcessorCount;
                if(n>=RC95LDR_APIC_PROCESSOR_MAX)return Rc95LdrBufferTooSmall;
                for(j=0;j<n;j++)if(r->Topology.Processors[j].ApicId==apicId)return Rc95LdrInvalidImage;
                r->Topology.Processors[n].ProcessorId=Read32(e+12);r->Topology.Processors[n].ApicId=apicId;
                r->Topology.Processors[n].Flags=Read32(e+8);r->Topology.Processors[n].IsX2Apic=1;
                r->Topology.ProcessorCount++;
            }
        }else if(type==10){
            uint32_t n;
            if(len<12)return Rc95LdrInvalidImage;
            n=r->Topology.IrqOverrideCount;if(n>=RC95LDR_APIC_IRQ_OVERRIDE_MAX)return Rc95LdrBufferTooSmall;
            r->Topology.IrqOverrides[n].SourceIrq=Read32(e+4);
            r->Topology.IrqOverrides[n].Flags=Read16(e+2);
            r->Topology.IrqOverrides[n].GlobalSystemInterrupt=e[8];
            r->Topology.IrqOverrides[n].InterruptType=2;
            r->Topology.IrqOverrides[n].DestinationApic=UINT32_MAX;
            r->Topology.IrqOverrideCount++;
        }
        off+=len;
    }
    (void)i;(void)address;
    return Rc95LdrSuccess;
}
static int FindMpsFp(const RC95LDR_PLATFORM_INVENTORY_INPUT*i,uint64_t*out)
{
    const uint8_t*p;uint16_t ebda=0,kb=0;
    if(i->MpsFloatingPointerAddress){*out=i->MpsFloatingPointerAddress;return 1;}
    if(!i->ScanLegacyFirmwareTables)return 0;
    p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,0x40e,2);if(p)ebda=Read16(p);
    p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,0x413,2);if(p)kb=Read16(p);
    if(ebda&&ScanSignature(i,(uint64_t)ebda<<4,1024,"_MP_",4,out))return 1;
    if(kb&&kb<=640&&ScanSignature(i,(uint64_t)kb*1024-1024,1024,"_MP_",4,out))return 1;
    return ScanSignature(i,0xf0000,0x10000,"_MP_",4,out);
}
static RC95LDR_STATUS ParseMps(const RC95LDR_PLATFORM_INVENTORY_INPUT*i,RC95LDR_PLATFORM_INVENTORY_RESULT*r)
{
    uint64_t fpAddress=0;const uint8_t*fp,*table;uint32_t tableAddress,off,index;
    uint16_t length,entryCount,extLength;uint8_t rev;
    if(!FindMpsFp(i,&fpAddress))return Rc95LdrSuccess;
    fp=(const uint8_t*)i->MapPhysical(i->FirmwareContext,fpAddress,16);
    if(!fp||memcmp(fp,"_MP_",4)||fp[8]!=1||fp[9]==0||!Checksum(fp,16))return Rc95LdrInvalidImage;
    tableAddress=Read32(fp+4);
    if(!tableAddress){
        uint8_t configType=fp[11];uint32_t cpuCount,k;
        if(configType<1||configType>7)return Rc95LdrInvalidImage;
        r->Topology.MpsPresent=1;r->Topology.MpsSpecificationRevision=fp[9];
        r->Topology.ImcrPresent=(fp[12]&0x80u)?1u:0u;
        if(fp[9]!=4u||r->Topology.MadtPresent)return Rc95LdrSuccess;
        r->Topology.Source=2;
        r->Topology.LocalApicAddress=UINT64_C(0xFEE00000);
        cpuCount=(configType==6||configType==7)?2u:1u;
        for(k=0;k<cpuCount;k++){
            r->Topology.Processors[k].ProcessorId=k;
            r->Topology.Processors[k].ApicId=k;
            r->Topology.Processors[k].Flags=(k==0)?3u:1u;
        }
        r->Topology.ProcessorCount=cpuCount;
        r->Topology.IoControllers[0].Id=2;
        r->Topology.IoControllers[0].Address=0xFEC00000u;
        r->Topology.IoControllerCount=1;
        return Rc95LdrSuccess;
    }
    table=(const uint8_t*)i->MapPhysical(i->FirmwareContext,tableAddress,44);
    if(!table||memcmp(table,"PCMP",4))return Rc95LdrInvalidImage;
    length=Read16(table+4);rev=table[6];entryCount=Read16(table+34);extLength=Read16(table+40);
    if(length<44||!rev||rev!=fp[9])return Rc95LdrInvalidImage;
    table=(const uint8_t*)i->MapPhysical(i->FirmwareContext,tableAddress,(size_t)length+extLength);
    if(!table||!Checksum(table,length)||table[43])return Rc95LdrInvalidImage;
    if(!extLength&&table[42])return Rc95LdrInvalidImage;
    if(extLength){
        uint8_t sum=table[42];uint32_t extOff=length,extEnd=(uint32_t)length+extLength;
        for(index=0;index<extLength;index++)sum=(uint8_t)(sum+table[length+index]);
        if(sum||table[43])return Rc95LdrInvalidImage;
        while(extOff<extEnd){
            uint32_t extSize;
            switch(table[extOff]){
            case 128:extSize=20;break;
            case 129:case 130:extSize=8;break;
            default:return Rc95LdrInvalidImage;
            }
            if(extSize>extEnd-extOff)return Rc95LdrInvalidImage;
            extOff+=extSize;
        }
    }
    r->Topology.MpsPresent=1;r->Topology.MpsSpecificationRevision=fp[9];
    r->Topology.ImcrPresent=(fp[12]&0x80u)?1u:0u;
    if(fp[9]!=4u||r->Topology.MadtPresent)return Rc95LdrSuccess;
    r->Topology.Source=2;r->Topology.LocalApicAddress=Read32(table+36);
    off=44;
    for(index=0;index<entryCount;index++){
        const uint8_t*e;uint32_t size;
        if (off >= length)
            return Rc95LdrInvalidImage;
        e = table + off;
        switch(e[0]){case 0:size=20;break;case 1:size=8;break;case 2:size=8;break;case 3:size=8;break;case 4:size=8;break;default:return Rc95LdrInvalidImage;}
        if(size>length-off)return Rc95LdrInvalidImage;
        if(e[0]==0&&(e[3]&1)){
            uint32_t n=r->Topology.ProcessorCount,j;
            if(n>=RC95LDR_APIC_PROCESSOR_MAX)return Rc95LdrBufferTooSmall;
            for(j=0;j<n;j++)if(r->Topology.Processors[j].ApicId==e[1])return Rc95LdrInvalidImage;
            r->Topology.Processors[n].ProcessorId=e[1];r->Topology.Processors[n].ApicId=e[1];
            r->Topology.Processors[n].Flags=e[3];r->Topology.ProcessorCount++;
        }else if(e[0]==2&&(e[3]&1)){
            uint32_t n=r->Topology.IoControllerCount;if(n>=RC95LDR_APIC_IO_CONTROLLER_MAX)return Rc95LdrBufferTooSmall;
            r->Topology.IoControllers[n].Id=e[1];r->Topology.IoControllers[n].Address=Read32(e+4);
            if(!r->Topology.IoControllers[n].Address)return Rc95LdrInvalidImage;
            r->Topology.IoControllerCount++;
        }else if(e[0]==3||e[0]==4){
            uint32_t n=r->Topology.IrqOverrideCount;if(n>=RC95LDR_APIC_IRQ_OVERRIDE_MAX)return Rc95LdrBufferTooSmall;
            r->Topology.IrqOverrides[n].Bus=e[4];r->Topology.IrqOverrides[n].SourceIrq=e[5];
            r->Topology.IrqOverrides[n].GlobalSystemInterrupt=e[7];r->Topology.IrqOverrides[n].Flags=Read16(e+2);
            r->Topology.IrqOverrides[n].InterruptType=e[1];r->Topology.IrqOverrides[n].DestinationApic=e[6];
            r->Topology.IrqOverrideCount++;
        }
        off+=size;
    }
    if(off!=length)return Rc95LdrInvalidImage;
    {
        uint32_t k,bspIndex=0,bspCount=0;
        for(k=0;k<r->Topology.ProcessorCount;k++){
            if(r->Topology.Processors[k].Flags&2u){bspIndex=k;bspCount++;}
        }
        if(bspCount!=1)return Rc95LdrInvalidImage;
        if(bspIndex){
            RC95LDR_APIC_PROCESSOR bsp=r->Topology.Processors[bspIndex];
            memmove(&r->Topology.Processors[1],&r->Topology.Processors[0],
                bspIndex*sizeof(r->Topology.Processors[0]));
            r->Topology.Processors[0]=bsp;
        }
    }
    return Rc95LdrSuccess;
}


static RC95LDR_STATUS ParseMcfg(const ACPI_SDT_HEADER *h,
    RC95LDR_PLATFORM_INVENTORY_RESULT *r)
{
    const uint8_t *p=(const uint8_t*)h;
    uint32_t off;
    if(h->Length<44 || ((h->Length-44u)%16u)!=0) return Rc95LdrInvalidImage;
    for(off=44;off<h->Length;off+=16) {
        RC95LDR_PCI_SEGMENT *s;
        uint64_t base=Read64(p+off);
        uint16_t segment=Read16(p+off+8);
        uint8_t first=p[off+10],last=p[off+11];
        uint32_t j;
        if(!base || (base&0xfffffu) || first>last ||
           Read32(p+off+12)!=0 ||
           base>UINT64_MAX-(((uint64_t)last-first+1u)<<20))
            return Rc95LdrInvalidImage;
        for(j=0;j<r->PciSegmentCount;j++)
            if(r->PciSegments[j].Segment==segment &&
               !(last<r->PciSegments[j].StartBus || first>r->PciSegments[j].EndBus))
                return Rc95LdrInvalidImage;
        if(r->PciSegmentCount>=RC95LDR_PCI_SEGMENT_MAX) return Rc95LdrBufferTooSmall;
        s=&r->PciSegments[r->PciSegmentCount++];
        memset(s,0,sizeof(*s));s->EcAMBase=base;s->Segment=segment;
        s->StartBus=first;s->EndBus=last;
    }
    return Rc95LdrSuccess;
}
static void CopySmbiosString(char *out,size_t capacity,
    const uint8_t *strings,const uint8_t *end,uint8_t index)
{
    const uint8_t *p=strings;uint8_t current=1;size_t n;
    if(!capacity||!index)return;
    while(p<end && *p) {
        const uint8_t *z=p;
        while(z<end && *z)z++;
        if(z==end)return;
        if(current==index) {
            n=(size_t)(z-p);if(n>=capacity)n=capacity-1;
            memcpy(out,p,n);out[n]=0;return;
        }
        p=z+1;current++;
    }
}
static RC95LDR_STATUS ParseSmbiosTable(const RC95LDR_PLATFORM_INVENTORY_INPUT *i,
    RC95LDR_PLATFORM_INVENTORY_RESULT *r,uint64_t entry,uint64_t table,
    uint32_t length,uint32_t expectedCount,uint8_t major,uint8_t minor,
    uint8_t docrev,uint8_t entryrev)
{
    const uint8_t *p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,table,length);
    uint32_t off=0,count=0;
    if(!p||!length||length>16u*1024u*1024u)return Rc95LdrInvalidImage;
    memset(&r->Smbios,0,sizeof(r->Smbios));
    r->Smbios.MajorVersion=major;r->Smbios.MinorVersion=minor;
    r->Smbios.DocRevision=docrev;r->Smbios.EntryPointRevision=entryrev;
    r->Smbios.EntryPointAddress=entry;r->Smbios.TableAddress=table;
    r->Smbios.TableLength=length;
    while(off<length) {
        uint8_t type,formatted;
        uint32_t end;
        const uint8_t *strings;
        if(length-off<4)return Rc95LdrInvalidImage;
        type=p[off];formatted=p[off+1];
        if(formatted<4||formatted>length-off)return Rc95LdrInvalidImage;
        end=off+formatted;
        strings=p+end;
        while(end+1<length && !(p[end]==0&&p[end+1]==0))end++;
        if(end+1>=length)return Rc95LdrInvalidImage;
        if(type==0&&formatted>5) {
            CopySmbiosString(r->Smbios.Version,sizeof(r->Smbios.Version),
                strings,p+end,p[off+5]);
        } else if(type==1&&formatted>=8) {
            CopySmbiosString(r->Smbios.Manufacturer,sizeof(r->Smbios.Manufacturer),strings,p+end,p[off+4]);
            CopySmbiosString(r->Smbios.ProductName,sizeof(r->Smbios.ProductName),strings,p+end,p[off+5]);
            CopySmbiosString(r->Smbios.Version,sizeof(r->Smbios.Version),strings,p+end,p[off+6]);
            CopySmbiosString(r->Smbios.SerialNumber,sizeof(r->Smbios.SerialNumber),strings,p+end,p[off+7]);
            if(formatted>=24) {
                static const char hex[]="0123456789abcdef";
                uint32_t k;char *u=r->Smbios.Uuid;
                for(k=0;k<16;k++){u[k*2]=hex[p[off+8+k]>>4];u[k*2+1]=hex[p[off+8+k]&15];}
                u[32]=0;
            }
        }
        count++;
        off=end+2;
        if(type==127)break;
    }
    if(expectedCount && count!=expectedCount)return Rc95LdrInvalidImage;
    r->Smbios.StructureCount=count;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS FindSmbios(const RC95LDR_PLATFORM_INVENTORY_INPUT *i,
    RC95LDR_PLATFORM_INVENTORY_RESULT *r)
{
    uint64_t address=i->SmbiosEntryPointAddress;
    const uint8_t *p;
    uint32_t tableLength,count;
    uint64_t tableAddress;
    uint8_t major,minor,docrev=0,entryrev=0;
    if(!address && i->ScanLegacyFirmwareTables) {
        uint32_t off;
        for(off=0xe0000;off<0x100000;off+=16) {
            p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,off,32);
            if(p && (!memcmp(p,"_SM3_",5)||!memcmp(p,"_SM_",4))) {address=off;break;}
        }
    }
    if(!address)return Rc95LdrSuccess;
    p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,address,32);
    if(!p)return Rc95LdrOutOfBounds;
    if(!memcmp(p,"_SM3_",5)) {
        uint8_t len=p[6];
        if(len<24||len>32)return Rc95LdrInvalidImage;
        p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,address,len);
        if(!p||!Checksum(p,len))return Rc95LdrInvalidImage;
        major=p[7];minor=p[8];docrev=p[9];entryrev=p[10];
        tableLength=Read32(p+12);tableAddress=Read64(p+16);count=0;
    } else if(!memcmp(p,"_SM_",4)) {
        uint8_t len=p[5];
        if(len<31||len>32)return Rc95LdrInvalidImage;
        p=(const uint8_t*)i->MapPhysical(i->FirmwareContext,address,len);
        if(!p||!Checksum(p,len)||memcmp(p+16,"_DMI_",5)||!Checksum(p+16,15))
            return Rc95LdrInvalidImage;
        major=p[6];minor=p[7];entryrev=p[10];
        tableLength=Read16(p+22);tableAddress=Read32(p+24);count=Read16(p+28);
    } else return Rc95LdrInvalidImage;
    if(!tableAddress||!tableLength||tableAddress>UINT64_MAX-tableLength)return Rc95LdrInvalidImage;
    return ParseSmbiosTable(i,r,address,tableAddress,tableLength,count,
        major,minor,docrev,entryrev);
}
static RC95LDR_STATUS ReadPci(RC95LDR_READ_PCI_CONFIG read,void *ctx,
    uint16_t seg,uint8_t bus,uint8_t dev,uint8_t fn,uint16_t off,uint32_t *v)
{
    RC95LDR_STATUS s=read(ctx,seg,bus,dev,fn,off,v);
    if(s!=Rc95LdrSuccess)return s;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS ProbePciBarSize(const RC95LDR_PLATFORM_INVENTORY_INPUT *i,
    uint16_t seg,uint8_t bus,uint8_t dev,uint8_t fn,uint16_t offset,int is64,
    uint32_t originalLow,uint32_t originalHigh,uint64_t *length)
{
    uint32_t commandDword,maskLow=0,maskHigh=0;
    uint16_t command;
    RC95LDR_STATUS status,restoreStatus;
    uint64_t mask;
    if(!i->WritePciConfig||!length)return Rc95LdrUnsupported;
    *length=0;
    status=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,4,&commandDword);
    if(status!=Rc95LdrSuccess)return status;
    command=(uint16_t)commandDword;
    /* Disable I/O, memory and bus-master decoding before probing. */
    status=i->WritePciConfig(i->FirmwareContext,seg,bus,dev,fn,4,2,
        (uint32_t)(command&~7u));
    if(status!=Rc95LdrSuccess)return status;
    status=i->WritePciConfig(i->FirmwareContext,seg,bus,dev,fn,offset,4,UINT32_MAX);
    if(status==Rc95LdrSuccess && is64)
        status=i->WritePciConfig(i->FirmwareContext,seg,bus,dev,fn,
            (uint16_t)(offset+4),4,UINT32_MAX);
    if(status==Rc95LdrSuccess)
        status=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,offset,&maskLow);
    if(status==Rc95LdrSuccess && is64)
        status=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,
            (uint16_t)(offset+4),&maskHigh);
    /* Restore the original BAR pair and command register on every path. */
    if(is64) {
        restoreStatus=i->WritePciConfig(i->FirmwareContext,seg,bus,dev,fn,
            (uint16_t)(offset+4),4,originalHigh);
        if(status==Rc95LdrSuccess && restoreStatus!=Rc95LdrSuccess)status=restoreStatus;
    }
    restoreStatus=i->WritePciConfig(i->FirmwareContext,seg,bus,dev,fn,offset,4,originalLow);
    if(status==Rc95LdrSuccess && restoreStatus!=Rc95LdrSuccess)status=restoreStatus;
    restoreStatus=i->WritePciConfig(i->FirmwareContext,seg,bus,dev,fn,4,2,command);
    if(status==Rc95LdrSuccess && restoreStatus!=Rc95LdrSuccess)status=restoreStatus;
    if(status!=Rc95LdrSuccess)return status;
    if(is64) {
        mask=((uint64_t)maskHigh<<32)|(maskLow&~0xfull);
        if(!mask)return Rc95LdrNotFound;
        *length=(~mask)+1u;
    } else {
        uint32_t mask32=maskLow&((maskLow&1u)?~3u:~0x0fu);
        if(!mask32)return Rc95LdrNotFound;
        *length=(uint32_t)(~mask32+1u);
    }
    if(!*length||(*length&(*length-1u)))return Rc95LdrInvalidImage;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS AddPciFunction(const RC95LDR_PLATFORM_INVENTORY_INPUT *i,
    RC95LDR_PLATFORM_INVENTORY_RESULT *r,uint16_t seg,uint8_t bus,
    uint8_t dev,uint8_t fn)
{
    uint32_t id,classreg,head,k,barCount,bridgeIndex=UINT32_MAX;
    RC95LDR_PCI_FUNCTION *p;
    RC95LDR_PCI_CONFIGURATION_DATA blob;char name[48];RC95LDR_STATUS s;
    s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0,&id);
    if(s!=Rc95LdrSuccess)return s;
    if((id&0xffffu)==0xffffu)return Rc95LdrSuccess;
    if(r->PciFunctionCount>=RC95LDR_PCI_FUNCTION_MAX)return Rc95LdrBufferTooSmall;
    s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,8,&classreg);if(s!=Rc95LdrSuccess)return s;
    s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x0c,&head);if(s!=Rc95LdrSuccess)return s;
    p=&r->PciFunctions[r->PciFunctionCount++];memset(p,0,sizeof(*p));
    p->Segment=seg;p->Bus=bus;p->Device=dev;p->Function=fn;
    p->VendorId=(uint16_t)id;p->DeviceId=(uint16_t)(id>>16);
    p->ProgIf=(uint8_t)(classreg>>8);p->SubClass=(uint8_t)(classreg>>16);
    p->ClassCode=(uint8_t)(classreg>>24);p->HeaderType=(uint8_t)(head>>16);
    p->ResourceFirst=r->PciResourceCount;p->BridgeIndex=UINT32_MAX;
    barCount=((p->HeaderType&0x7fu)==1u)?2u:((p->HeaderType&0x7fu)==0u?6u:0u);
    for(k=0;k<barCount;k++) {
        uint32_t barIndex=k;uint16_t off=(uint16_t)(0x10u+k*4u);uint32_t lo,hi=0,flags=0;uint64_t base;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,off,&lo);if(s!=Rc95LdrSuccess)return s;
        if(lo==0 || lo==0xffffffffu)continue;
        if(lo&1u){flags=RC95LDR_PCI_RESOURCE_IO;base=lo&~3u;}
        else {
            flags=RC95LDR_PCI_RESOURCE_MEMORY;
            if(lo&8u)flags|=RC95LDR_PCI_RESOURCE_PREFETCH;
            if(((lo>>1)&3u)==2u) {
                if(k+1>=barCount)return Rc95LdrInvalidImage;
                s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,(uint16_t)(off+4),&hi);if(s!=Rc95LdrSuccess)return s;
                base=((uint64_t)hi<<32)|(lo&~0xfull);flags|=RC95LDR_PCI_RESOURCE_64BIT;k++;
            } else base=lo&~0xfull;
        }
        if(base)flags|=RC95LDR_PCI_RESOURCE_ASSIGNED;
        if(r->PciResourceCount>=RC95LDR_PCI_FUNCTION_MAX*RC95LDR_PCI_BAR_MAX)return Rc95LdrBufferTooSmall;
        {
            uint64_t length=0;
            uint32_t resourceFlags=flags|RC95LDR_PCI_RESOURCE_SIZE_UNKNOWN;
            {
                RC95LDR_STATUS qs=Rc95LdrNotFound;
                if(i->QueryPciBarSize)
                    qs=i->QueryPciBarSize(i->FirmwareContext,seg,bus,dev,fn,
                        (uint8_t)barIndex,&length);
                if((qs==Rc95LdrUnsupported||qs==Rc95LdrNotFound)&&i->WritePciConfig)
                    qs=ProbePciBarSize(i,seg,bus,dev,fn,off,
                        (flags&RC95LDR_PCI_RESOURCE_64BIT)!=0,lo,hi,&length);
                if(qs==Rc95LdrSuccess) {
                    if(!length || (length&(length-1u)) ||
                       (base && (base&(length-1u))) || base>UINT64_MAX-length)
                        return Rc95LdrInvalidImage;
                    resourceFlags&=~RC95LDR_PCI_RESOURCE_SIZE_UNKNOWN;
                } else if(qs!=Rc95LdrUnsupported && qs!=Rc95LdrNotFound) {
                    return qs;
                } else length=0;
            }
            r->PciResources[r->PciResourceCount].Base=base;
            r->PciResources[r->PciResourceCount].Length=length;
            r->PciResources[r->PciResourceCount].Flags=resourceFlags;
            r->PciResources[r->PciResourceCount].RegisterOffset=off;
        }
        r->PciResourceCount++;p->ResourceCount++;
    }
    if(p->ClassCode==6u && p->SubClass==4u && (p->HeaderType&0x7fu)==1u) {
        uint32_t buses,io,mem,pref,iohi=0,prehi=0,prelimitHi=0;
        RC95LDR_PCI_BRIDGE *b;
        if(r->PciBridgeCount>=RC95LDR_PCI_FUNCTION_MAX)return Rc95LdrBufferTooSmall;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x18,&buses);if(s!=Rc95LdrSuccess)return s;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x1c,&io);if(s!=Rc95LdrSuccess)return s;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x20,&mem);if(s!=Rc95LdrSuccess)return s;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x24,&pref);if(s!=Rc95LdrSuccess)return s;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x30,&iohi);if(s!=Rc95LdrSuccess)return s;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x28,&prehi);if(s!=Rc95LdrSuccess)return s;
        s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg,bus,dev,fn,0x2c,&prelimitHi);if(s!=Rc95LdrSuccess)return s;
        b=&r->PciBridges[r->PciBridgeCount];memset(b,0,sizeof(*b));
        b->Segment=seg;b->Bus=bus;b->Device=dev;b->Function=fn;
        b->PrimaryBus=(uint8_t)buses;b->SecondaryBus=(uint8_t)(buses>>8);b->SubordinateBus=(uint8_t)(buses>>16);
        if (b->PrimaryBus != bus ||
            (b->SecondaryBus && b->SubordinateBus < b->SecondaryBus))
            return Rc95LdrInvalidImage;
        if (!(io & 1u)) iohi=0;
        if (!(pref & 1u)) { prehi=0; prelimitHi=0; }
        b->IoBase=((uint64_t)((io&0xf0u)<<8))|((uint64_t)(iohi&0xffffu)<<16);
        b->IoLimit=((uint64_t)((io&0xf000u)<<8))|((uint64_t)(iohi>>16)<<16)|0xfffu;
        b->MemoryBase=(uint64_t)(mem&0xfff0u)<<16;b->MemoryLimit=((uint64_t)(mem&0xfff00000u))|0xfffffu;
        b->PrefetchBase=((uint64_t)(pref&0xfff0u)<<16)|((uint64_t)prehi<<32);
        b->PrefetchLimit=((uint64_t)(pref&0xfff00000u))|((uint64_t)prelimitHi<<32)|0xfffffu;
        bridgeIndex=r->PciBridgeCount++;p->BridgeIndex=bridgeIndex;
    }
    p->ResourceCount=r->PciResourceCount-p->ResourceFirst;
    memset(&blob,0,sizeof(blob));
    blob.Function=*p;
    if(p->ResourceCount>RC95LDR_PCI_BAR_MAX)return Rc95LdrInvalidImage;
    if(p->ResourceCount)
        memcpy(blob.Resources,&r->PciResources[p->ResourceFirst],
            p->ResourceCount*sizeof(blob.Resources[0]));
    if(p->BridgeIndex!=UINT32_MAX)
        blob.Bridge=r->PciBridges[p->BridgeIndex];
    (void)snprintf(name,sizeof(name),"PCI %04X:%02X:%02X.%u %04X:%04X",seg,bus,dev,fn,p->VendorId,p->DeviceId);
    s=AddComponent(r,2,3,name,&blob,sizeof(blob),0);
    return s;
}
static RC95LDR_STATUS AddAcpiTable(const RC95LDR_PLATFORM_INVENTORY_INPUT*i,RC95LDR_PLATFORM_INVENTORY_RESULT*r,uint64_t address)
{
    const ACPI_SDT_HEADER*h;uint8_t first[sizeof(ACPI_SDT_HEADER)];uint32_t tableLength;
    h=(const ACPI_SDT_HEADER*)i->MapPhysical(i->FirmwareContext,address,sizeof(first));
    if(!h)return Rc95LdrOutOfBounds;
    memcpy(first,h,sizeof(first));memcpy(&tableLength,first+4,sizeof(tableLength));
    if(tableLength<sizeof(*h)||tableLength>1024u*1024u)return Rc95LdrInvalidImage;
    h=(const ACPI_SDT_HEADER*)i->MapPhysical(i->FirmwareContext,address,tableLength);
    if(!h||h->Length!=tableLength||!Checksum((const uint8_t*)h,h->Length))return Rc95LdrInvalidImage;
    {char id[48];uint8_t info[16]={0};memcpy(info,h->Signature,4);memcpy(info+4,&h->Length,4);memcpy(info+8,&address,8);
     (void)snprintf(id,sizeof(id),"ACPI %.4s",h->Signature);
     if(AddComponent(r,1,1,id,info,sizeof(info),0)!=Rc95LdrSuccess)return Rc95LdrBufferTooSmall;}
    r->AcpiTableCount++;
    if(memcmp(h->Signature,"APIC",4)==0){RC95LDR_STATUS ts=ParseMadt(i,r,address,h);if(ts!=Rc95LdrSuccess)return ts;}
    if(memcmp(h->Signature,"MCFG",4)==0){RC95LDR_STATUS ts=ParseMcfg(h,r);if(ts!=Rc95LdrSuccess)return ts;}
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrBuildPlatformInventory(const RC95LDR_PLATFORM_INVENTORY_INPUT*i,RC95LDR_PLATFORM_INVENTORY_RESULT*r)
{
    uint64_t root=0,rsdpAddress=0;const ACPI_SDT_HEADER*h;uint32_t j,entrySize,count;
    RC95LDR_STATUS s;uint8_t systemInfo[16]={0};
    if(!i||!r||i->StructureSize<sizeof(*i)||i->Version!=RC95LDR_PLATFORM_INVENTORY_VERSION||
       !i->MapPhysical||!i->ReadPciConfig||i->PciSegment>UINT16_MAX||i->ScanLegacyFirmwareTables>1)return Rc95LdrInvalidParameter;
    memset(r,0,sizeof(*r));
    memcpy(systemInfo,"RC95",4);
    s=AddComponent(r,0,0,"System",systemInfo,sizeof(systemInfo),-1);if(s!=Rc95LdrSuccess)return s;
    rsdpAddress=i->RsdpPhysicalAddress;
    if(!rsdpAddress&&i->ScanLegacyFirmwareTables)rsdpAddress=FindRsdp(i);
    if(rsdpAddress){
        const ACPI_RSDP20 *rsdp=(const ACPI_RSDP20*)i->MapPhysical(i->FirmwareContext,rsdpAddress,20);
        if(!rsdp||memcmp(rsdp->Signature,"RSD PTR ",8)!=0||!Checksum((const uint8_t*)rsdp,20))
            return Rc95LdrInvalidImage;
        if(rsdp->Revision>=2){
            rsdp=(const ACPI_RSDP20*)i->MapPhysical(i->FirmwareContext,rsdpAddress,sizeof(*rsdp));
            if(!rsdp||rsdp->Length<sizeof(*rsdp)||rsdp->Length>4096u)return Rc95LdrInvalidImage;
            rsdp=(const ACPI_RSDP20*)i->MapPhysical(i->FirmwareContext,rsdpAddress,rsdp->Length);
            if(!rsdp||!Checksum((const uint8_t*)rsdp,rsdp->Length))return Rc95LdrInvalidImage;
            root=rsdp->XsdtAddress?rsdp->XsdtAddress:rsdp->RsdtAddress;
        }else root=rsdp->RsdtAddress;
    }else if(i->AcpiRoot.Revision>0&&i->AcpiRoot.XsdtAddress)root=i->AcpiRoot.XsdtAddress;
    else root=i->AcpiRoot.RsdtAddress;
    if(root){
        r->Topology.AcpiPresent=1;
        h=(const ACPI_SDT_HEADER*)i->MapPhysical(i->FirmwareContext,root,sizeof(*h));
        if(!h||h->Length<sizeof(*h)||h->Length>65536u)return Rc95LdrInvalidImage;
        h=(const ACPI_SDT_HEADER*)i->MapPhysical(i->FirmwareContext,root,h->Length);
        if(!h||!Checksum((const uint8_t*)h,h->Length))return Rc95LdrInvalidImage;
        if(memcmp(h->Signature,"XSDT",4)==0)entrySize=8;
        else if(memcmp(h->Signature,"RSDT",4)==0)entrySize=4;
        else return Rc95LdrInvalidImage;
        if((h->Length-sizeof(*h))%entrySize)return Rc95LdrInvalidImage;
        count=(h->Length-(uint32_t)sizeof(*h))/entrySize;
        if(count>RC95LDR_ACPI_TABLE_MAX)return Rc95LdrBufferTooSmall;
        for(j=0;j<count;j++){
            uint64_t address=0;
            if(entrySize==8)memcpy(&address,(const uint8_t*)h+sizeof(*h)+(size_t)j*8,8);
            else {uint32_t a;memcpy(&a,(const uint8_t*)h+sizeof(*h)+(size_t)j*4,4);address=a;}
            if(!address)continue;
            s=AddAcpiTable(i,r,address);if(s!=Rc95LdrSuccess)return s;
        }
    }
    s=ParseMps(i,r);if(s!=Rc95LdrSuccess)return s;
    if(r->Topology.MadtPresent||r->Topology.MpsPresent){
        uint32_t k;
        for(k=0;k<r->Topology.ProcessorCount;k++){
            uint8_t info[16]={0};char name[48];
            memcpy(info,&r->Topology.Processors[k],sizeof(RC95LDR_APIC_PROCESSOR));
            (void)snprintf(name,sizeof(name),"APIC processor %u",r->Topology.Processors[k].ApicId);
            s=AddComponent(r,1,0,name,info,sizeof(info),0);if(s!=Rc95LdrSuccess)return s;
        }
        for(k=0;k<r->Topology.IoControllerCount;k++){
            uint8_t info[12]={0};char name[48];
            memcpy(info,&r->Topology.IoControllers[k],sizeof(RC95LDR_APIC_IO_CONTROLLER));
            (void)snprintf(name,sizeof(name),"I/O APIC %u",r->Topology.IoControllers[k].Id);
            s=AddComponent(r,4,0,name,info,sizeof(info),0);if(s!=Rc95LdrSuccess)return s;
        }
    }
    s=FindSmbios(i,r);if(s!=Rc95LdrSuccess)return s;
    if(r->Smbios.TableAddress) {
        uint8_t info[32]={0};
        memcpy(info,&r->Smbios.MajorVersion,4);
        memcpy(info+4,&r->Smbios.TableLength,4);
        memcpy(info+8,&r->Smbios.EntryPointAddress,8);
        memcpy(info+16,&r->Smbios.TableAddress,8);
        memcpy(info+24,&r->Smbios.StructureCount,4);
        s=AddComponent(r,1,2,"SMBIOS",info,sizeof(info),0);
        if(s!=Rc95LdrSuccess)return s;
    }
    if(!r->PciSegmentCount) {
        r->PciSegments[0].Segment=(uint16_t)i->PciSegment;
        r->PciSegments[0].StartBus=0;r->PciSegments[0].EndBus=255;
        r->PciSegmentCount=1;
    }
    for(j=0;j<r->PciSegmentCount;j++) {
        char name[48];
        (void)snprintf(name,sizeof(name),"PCI segment %u",r->PciSegments[j].Segment);
        s=AddComponent(r,2,2,name,&r->PciSegments[j],
            sizeof(r->PciSegments[j]),0);
        if(s!=Rc95LdrSuccess)return s;
    }
    /* Enumerate every bus covered by each validated MCFG allocation. The
       config-space callback owns ECAM/legacy access and must not probe outside
       the segment's declared bus window. */
    for(j=0;j<r->PciSegmentCount;j++) {
        RC95LDR_PCI_SEGMENT *seg=&r->PciSegments[j];
        uint32_t bus,dev,fn;
        for(bus=seg->StartBus;bus<=(uint32_t)seg->EndBus;bus++) {
            for(dev=0;dev<32;dev++) {
                uint32_t id0,head0;
                s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg->Segment,
                    (uint8_t)bus,(uint8_t)dev,0,0,&id0);
                if(s!=Rc95LdrSuccess)return s;
                if((id0&0xffffu)==0xffffu)continue;
                s=ReadPci(i->ReadPciConfig,i->FirmwareContext,seg->Segment,
                    (uint8_t)bus,(uint8_t)dev,0,0x0c,&head0);
                if(s!=Rc95LdrSuccess)return s;
                for(fn=0;fn<(((head0>>16)&0x80u)?8u:1u);fn++) {
                    s=AddPciFunction(i,r,seg->Segment,(uint8_t)bus,
                        (uint8_t)dev,(uint8_t)fn);
                    if(s!=Rc95LdrSuccess)return s;
                }
            }
        }
    }
    return Rc95LdrSuccess;
}
