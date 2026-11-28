#include "rc95ldrbiosfirmwarestate.h"
#include <assert.h>
#include <string.h>

static uint8_t Memory[0x2000];
static const void *Map(void *Context,uint64_t Address,size_t Length)
{
    (void)Context;
    if(Address>sizeof(Memory)||Length>sizeof(Memory)-(size_t)Address)return 0;
    return Memory+(size_t)Address;
}
static void Put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void Put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}

int main(void)
{
    RC95LDR_BIOS_FIRMWARE_CAPTURE_INPUT in;
    RC95LDR_BIOS_FIRMWARE_STATE state;
    RC95LDR_BIOS_E820_ENTRY e820[2];
    RC95LDR_KERNEL_HANDOFF_CONTEXT handoff;
    memset(Memory,0,sizeof(Memory));
    Memory[0x20]=0x34;Memory[0x21]=0x12; /* INT 8 vector offset */
    Put16(Memory+0x400+0x0e,0x9fc0);
    Put16(Memory+0x400+0x10,0x0021);
    Put16(Memory+0x400+0x13,640);
    Memory[0x400+0x49]=3;Put16(Memory+0x400+0x4a,80);
    Memory[0x400+0x62]=1;Put32(Memory+0x400+0x6c,123456);
    memset(&in,0,sizeof(in));in.StructureSize=sizeof(in);
    in.Version=RC95LDR_BIOS_FIRMWARE_STATE_VERSION;in.MapPhysical=Map;
    in.BootDrive=0x80;in.E820=e820;in.E820Count=2;
    e820[0].Base=0;e820[0].Length=0x9f000;e820[0].Type=1;
    e820[1].Base=0x100000;e820[1].Length=0x3f00000;e820[1].Type=1;
    assert(Rc95LdrBiosCaptureFirmwareState(&in,&state,sizeof(state))==Rc95LdrSuccess);
    assert(state.Signature==RC95LDR_BIOS_FIRMWARE_STATE_SIGNATURE);
    assert(state.BootDrive==0x80&&state.EbdaSegment==0x9fc0);
    assert(state.ConventionalMemoryKilobytes==640&&state.EquipmentWord==0x21);
    assert(state.VideoMode==3&&state.VideoColumns==80&&state.ActiveVideoPage==1);
    assert(state.BiosTickCount==123456&&state.E820Count==2);
    assert(state.InterruptVectorTable[0x20]==0x34);
    memset(&handoff,0,sizeof(handoff));handoff.Signature=RC95LDR_HANDOFF_SIGNATURE;
    handoff.StructureSize=sizeof(handoff);handoff.Version=RC95LDR_HANDOFF_VERSION;
    handoff.Firmware=Rc95LdrFirmwareBios;
    assert(Rc95LdrBiosAttachFirmwareState(&in,&state,sizeof(state),&handoff)==Rc95LdrSuccess);
    assert(handoff.BiosFirmwareStateAddress==(uint64_t)(uintptr_t)&state);
    assert(handoff.BiosFirmwareStateSize==sizeof(state));
    assert(handoff.BiosFirmwareStateVersion==RC95LDR_BIOS_FIRMWARE_STATE_VERSION);
    assert(handoff.CompletedRequirements&RC95LDR_HANDOFF_FIRMWARE_STATE);
    e820[1].Base=0x80000;
    assert(Rc95LdrBiosCaptureFirmwareState(&in,&state,sizeof(state))==Rc95LdrInvalidImage);
    return 0;
}
