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
    if(n>=RC95LDR_PLATFORM_COMPONENT_MAX||length>32)return Rc95LdrBufferTooSmall;
    c=&r->Components[n];memset(c,0,sizeof(*c));c->Class=cls;c->Type=type;c->ParentIndex=parent;
    (void)snprintf(r->Identifiers[n],sizeof(r->Identifiers[n]),"%s",name);
    c->Identifier=r->Identifiers[n];
    if(length){memcpy(r->ConfigurationData[n],data,length);c->ConfigurationData=r->ConfigurationData[n];c->ConfigurationDataLength=length;}
    r->ComponentCount++;return Rc95LdrSuccess;
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
    r->AcpiTableCount++;return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrBuildPlatformInventory(const RC95LDR_PLATFORM_INVENTORY_INPUT*i,RC95LDR_PLATFORM_INVENTORY_RESULT*r)
{
    uint64_t root=0;const ACPI_SDT_HEADER*h;uint32_t j,entrySize,count;
    RC95LDR_STATUS s;uint8_t systemInfo[16]={0};
    if(!i||!r||i->StructureSize<sizeof(*i)||i->Version!=RC95LDR_PLATFORM_INVENTORY_VERSION||
       !i->MapPhysical||!i->ReadPciConfig||i->PciSegment>UINT16_MAX)return Rc95LdrInvalidParameter;
    memset(r,0,sizeof(*r));
    memcpy(systemInfo,"RC95",4);
    s=AddComponent(r,0,0,"System",systemInfo,sizeof(systemInfo),-1);if(s!=Rc95LdrSuccess)return s;
    if(i->RsdpPhysicalAddress){
        const ACPI_RSDP20 *rsdp=(const ACPI_RSDP20*)i->MapPhysical(i->FirmwareContext,i->RsdpPhysicalAddress,20);
        if(!rsdp||memcmp(rsdp->Signature,"RSD PTR ",8)!=0||!Checksum((const uint8_t*)rsdp,20))
            return Rc95LdrInvalidImage;
        if(rsdp->Revision>=2){
            rsdp=(const ACPI_RSDP20*)i->MapPhysical(i->FirmwareContext,i->RsdpPhysicalAddress,sizeof(*rsdp));
            if(!rsdp||rsdp->Length<sizeof(*rsdp)||rsdp->Length>4096u)return Rc95LdrInvalidImage;
            rsdp=(const ACPI_RSDP20*)i->MapPhysical(i->FirmwareContext,i->RsdpPhysicalAddress,rsdp->Length);
            if(!rsdp||!Checksum((const uint8_t*)rsdp,rsdp->Length))return Rc95LdrInvalidImage;
            root=rsdp->XsdtAddress?rsdp->XsdtAddress:rsdp->RsdtAddress;
        }else root=rsdp->RsdtAddress;
    }else if(i->AcpiRoot.Revision>0&&i->AcpiRoot.XsdtAddress)root=i->AcpiRoot.XsdtAddress;
    else root=i->AcpiRoot.RsdtAddress;
    if(root){
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
    /* Enumerate all conventional PCI functions through the firmware's
       config-space accessor. Function zero determines multifunction status. */
    for(j=0;j<256;j++){
        uint32_t dev,fn;
        for(dev=0;dev<32;dev++){
            uint32_t id,head;
            s=i->ReadPciConfig(i->FirmwareContext,(uint16_t)i->PciSegment,(uint8_t)j,(uint8_t)dev,0,0,&id);
            if(s!=Rc95LdrSuccess||((id&0xffffu)==0xffffu))continue;
            s=i->ReadPciConfig(i->FirmwareContext,(uint16_t)i->PciSegment,(uint8_t)j,(uint8_t)dev,0,0x0c,&head);
            if(s!=Rc95LdrSuccess)return s;
            for(fn=0;fn<(((head>>16)&0x80u)?8u:1u);fn++){
                uint32_t vid,classreg,ht;RC95LDR_PCI_FUNCTION*p;uint8_t blob[sizeof(RC95LDR_PCI_FUNCTION)]={0};char name[48];
                s=i->ReadPciConfig(i->FirmwareContext,(uint16_t)i->PciSegment,(uint8_t)j,(uint8_t)dev,(uint8_t)fn,0,&vid);
                if(s!=Rc95LdrSuccess||((vid&0xffffu)==0xffffu))continue;
                s=i->ReadPciConfig(i->FirmwareContext,(uint16_t)i->PciSegment,(uint8_t)j,(uint8_t)dev,(uint8_t)fn,8,&classreg);if(s!=Rc95LdrSuccess)return s;
                s=i->ReadPciConfig(i->FirmwareContext,(uint16_t)i->PciSegment,(uint8_t)j,(uint8_t)dev,(uint8_t)fn,0x0c,&ht);if(s!=Rc95LdrSuccess)return s;
                if(r->PciFunctionCount>=RC95LDR_PCI_FUNCTION_MAX)return Rc95LdrBufferTooSmall;
                p=&r->PciFunctions[r->PciFunctionCount++];memset(p,0,sizeof(*p));
                p->Segment=(uint16_t)i->PciSegment;p->Bus=(uint8_t)j;p->Device=(uint8_t)dev;p->Function=(uint8_t)fn;
                p->VendorId=(uint16_t)vid;p->DeviceId=(uint16_t)(vid>>16);
                p->ProgIf=(uint8_t)(classreg>>8);p->SubClass=(uint8_t)(classreg>>16);p->ClassCode=(uint8_t)(classreg>>24);p->HeaderType=(uint8_t)(ht>>16);
                memcpy(blob,p,sizeof(*p));(void)snprintf(name,sizeof(name),"PCI %02X:%02X.%u %04X:%04X",p->Bus,p->Device,p->Function,p->VendorId,p->DeviceId);
                s=AddComponent(r,2,3,name,blob,sizeof(*p),0);if(s!=Rc95LdrSuccess)return s;
            }
        }
    }
    return Rc95LdrSuccess;
}
