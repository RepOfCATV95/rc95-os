#include "rc95ldrbiosfirmwarestate.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>

static uint16_t Read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t Read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

RC95LDR_STATUS
Rc95LdrBiosCaptureFirmwareState(
    const RC95LDR_BIOS_FIRMWARE_CAPTURE_INPUT *Input,
    RC95LDR_BIOS_FIRMWARE_STATE *State,
    size_t StateCapacity)
{
    const uint8_t *ivt, *bda;
    uint32_t n, j;
    if (!Input || Input->StructureSize < sizeof(*Input) ||
        Input->Version != RC95LDR_BIOS_FIRMWARE_STATE_VERSION ||
        !Input->MapPhysical || !State ||
        StateCapacity < sizeof(*State) ||
        !Input->E820Count || Input->E820Count > RC95LDR_BIOS_E820_MAX ||
        (Input->E820Count && !Input->E820))
        return Rc95LdrInvalidParameter;

    ivt = (const uint8_t *)Input->MapPhysical(Input->Context, 0, 1024);
    bda = (const uint8_t *)Input->MapPhysical(Input->Context, 0x400, 256);
    if (!ivt || !bda) return Rc95LdrOutOfBounds;

    for (n = 0; n < Input->E820Count; ++n) {
        const RC95LDR_BIOS_E820_ENTRY *e = &Input->E820[n];
        if (!e->Length || !e->Type || e->Base > UINT64_MAX - e->Length)
            return Rc95LdrInvalidImage;
        for (j = 0; j < n; ++j) {
            const RC95LDR_BIOS_E820_ENTRY *prior = &Input->E820[j];
            if (e->Base < prior->Base + prior->Length &&
                prior->Base < e->Base + e->Length)
                return Rc95LdrInvalidImage;
        }
    }

    memset(State, 0, sizeof(*State));
    State->Signature = RC95LDR_BIOS_FIRMWARE_STATE_SIGNATURE;
    State->StructureSize = sizeof(*State);
    State->Version = RC95LDR_BIOS_FIRMWARE_STATE_VERSION;
    State->Flags = RC95LDR_BIOS_STATE_IVT_VALID |
        RC95LDR_BIOS_STATE_BDA_VALID | RC95LDR_BIOS_STATE_E820_VALID;
    State->BootDrive = Input->BootDrive;
    memcpy(State->InterruptVectorTable, ivt, sizeof(State->InterruptVectorTable));
    memcpy(State->BiosDataArea, bda, sizeof(State->BiosDataArea));
    State->EquipmentWord = Read16(bda + 0x10);
    State->ConventionalMemoryKilobytes = Read16(bda + 0x13);
    State->VideoMode = bda[0x49];
    State->VideoColumns = (uint8_t)(Read16(bda + 0x4a) & 0xffu);
    State->ActiveVideoPage = bda[0x62];
    State->EbdaSegment = Read16(bda + 0x0e);
    State->BiosTickCount = Read32(bda + 0x6c);
    State->HardDiskCount = bda[0x75];
    State->FloppyDriveCount = (uint8_t)((bda[0x10] & 1u) ? (((bda[0x10] >> 6) & 3u) + 1u) : 0u);
    State->E820Count = Input->E820Count;
    if (Input->E820Count)
        memcpy(State->E820, Input->E820,
            (size_t)Input->E820Count * sizeof(State->E820[0]));
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosAttachFirmwareState(
    const RC95LDR_BIOS_FIRMWARE_CAPTURE_INPUT *Input,
    RC95LDR_BIOS_FIRMWARE_STATE *State,
    size_t StateCapacity,
    RC95LDR_KERNEL_HANDOFF_CONTEXT *Handoff)
{
    RC95LDR_STATUS Status;
    if (!Handoff || Handoff->Firmware != Rc95LdrFirmwareBios ||
        Handoff->Signature != RC95LDR_HANDOFF_SIGNATURE ||
        Handoff->Version != RC95LDR_HANDOFF_VERSION ||
        Handoff->StructureSize < sizeof(*Handoff) || !State)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosCaptureFirmwareState(Input, State, StateCapacity);
    if (Status != Rc95LdrSuccess) return Status;
    Handoff->BiosFirmwareStateAddress = (uint64_t)(uintptr_t)State;
    Handoff->BiosFirmwareStateSize = (uint32_t)sizeof(*State);
    Handoff->BiosFirmwareStateVersion = RC95LDR_BIOS_FIRMWARE_STATE_VERSION;
    Handoff->CompletedRequirements |= RC95LDR_HANDOFF_FIRMWARE_STATE;
    return Rc95LdrSuccess;
}
