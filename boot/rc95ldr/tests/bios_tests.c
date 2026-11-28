#include "rc95ldrbios.h"
#include "rc95ldrbiosstartup.h"
#include "rc95ldrbiosstage.h"
#include "rc95ldrbiosmemory.h"
#include "rc95ldrfirmwaremenu.h"
#include <stdio.h>
#include <string.h>

static uint8_t LowMemory[0x100000];
static int InvalidVbeListPointer, MissingVbeListTerminator;
static uint16_t VbeVersion = 0x0200u;
static uint8_t DiskImage[64 * 512];
static unsigned DiskReads, E820Calls, VideoModeCalls, VideoFontCalls;
static unsigned VideoGraphicsModeCalls, VideoClearCalls;
static uint16_t KeyboardSequence[8];
static unsigned KeyboardCount, KeyboardIndex;
static uint8_t LastVideoClearAttribute;

static RC95LDR_STATUS MapLow(void *Context, uint32_t Physical, size_t Length, void **Mapping)
{
    (void)Context;
    if (Physical >= sizeof(LowMemory) || Length > sizeof(LowMemory) - Physical)
        return Rc95LdrOutOfBounds;
    *Mapping = LowMemory + Physical;
    return Rc95LdrSuccess;
}
static void UnmapLow(void *Context, void *Mapping, size_t Length)
{ (void)Context; (void)Mapping; (void)Length; }

static RC95LDR_STATUS MapLowNull(void *Context, uint32_t Physical,
                                 size_t Length, void **Mapping)
{
    (void)Context; (void)Physical; (void)Length;
    if (!Mapping) return Rc95LdrInvalidParameter;
    *Mapping = NULL;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS Invoke(void *Context, uint8_t Interrupt, RC95LDR_BIOS_REGISTERS *R)
{
    uint8_t Ah = (uint8_t)(R->Eax >> 8);
    (void)Context;
    R->Eflags = 0;
    if (Interrupt == 0x13 && Ah == 0x41) {
        R->Ebx = 0xAA55; R->Ecx = 1; R->Eax = 0x3000;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x13 && Ah == 0x48) {
        uint32_t Address = ((uint32_t)R->Ds << 4) + (uint16_t)R->Esi;
        uint16_t *P = (uint16_t *)(LowMemory + Address);
        uint32_t *C = (uint32_t *)(LowMemory + Address + 4);
        uint64_t *Sectors = (uint64_t *)(LowMemory + Address + 16);
        *P = 30; *C = 1; *Sectors = 64;
        P[12] = 512;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x13 && Ah == 0x42) {
        uint32_t Address = ((uint32_t)R->Ds << 4) + (uint16_t)R->Esi;
        uint8_t *Dap = LowMemory + Address;
        uint16_t Count = *(uint16_t *)(Dap + 2);
        uint16_t Off = *(uint16_t *)(Dap + 4);
        uint16_t Seg = *(uint16_t *)(Dap + 6);
        uint64_t Lba = *(uint64_t *)(Dap + 8);
        uint32_t Target = ((uint32_t)Seg << 4) + Off;
        size_t Bytes = (size_t)Count * 512;
        if (Lba + Count > 64 || Target + Bytes > sizeof(LowMemory))
            return Rc95LdrOutOfBounds;
        memcpy(LowMemory + Target, DiskImage + (size_t)Lba * 512, Bytes);
        DiskReads++;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x15 && (R->Eax & 0xFFFFu) == 0xE820u) {
        uint32_t Address = ((uint32_t)R->Es << 4) + (uint16_t)R->Edi;
        uint8_t *Entry = LowMemory + Address;
        uint64_t Base = E820Calls ? 0x100000u : 0;
        uint64_t Length = E820Calls ? 0x3F00000u : 0x9F000u;
        uint32_t Type = 1;
        memcpy(Entry, &Base, 8); memcpy(Entry + 8, &Length, 8);
        memcpy(Entry + 16, &Type, 4);
        *(uint32_t *)(Entry + 20) = 1;
        R->Eax = RC95LDR_BIOS_E820_SIGNATURE;
        R->Ecx = 24; R->Ebx = E820Calls ? 0 : 1;
        E820Calls++;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x10 && (R->Eax & 0xFFFFu) == 0x4F00u) {
        uint32_t Address = ((uint32_t)R->Es << 4) + (uint16_t)R->Edi;
        uint8_t *Info = LowMemory + Address;
        memcpy(Info, "VESA", 4);
        *(uint16_t *)(Info + 4) = VbeVersion;
        *(uint16_t *)(Info + 14) = InvalidVbeListPointer ? 0xFFF0u : 0u;
        *(uint16_t *)(Info + 16) = InvalidVbeListPointer ? 0xFFFFu : 0x0900u;
        if (MissingVbeListTerminator) {
            unsigned I;
            for (I = 0; I < 128u; ++I)
                *(uint16_t *)(LowMemory + 0x9000u + I * 2u) = 0x0118u;
        } else {
            *(uint16_t *)(LowMemory + 0x9000) = 0x0118u;
            *(uint16_t *)(LowMemory + 0x9002) = 0x0115u;
            *(uint16_t *)(LowMemory + 0x9004) = 0xFFFFu;
        }
        R->Eax = 0x004Fu;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x10 && (R->Eax & 0xFFFFu) == 0x4F01u) {
        uint32_t Address = ((uint32_t)R->Es << 4) + (uint16_t)R->Edi;
        uint8_t *Info = LowMemory + Address;
        memset(Info, 0, 256);
        *(uint16_t *)(Info + 0) = 0x0091u;
        if ((uint16_t)R->Ecx == 0x0115u) {
            *(uint16_t *)(Info + 16) = VbeVersion >= 0x0300u ? 0u : 3200u;
            if (VbeVersion >= 0x0300u) *(uint16_t *)(Info + 50) = 3200u;
            *(uint16_t *)(Info + 18) = 800u;
            *(uint16_t *)(Info + 20) = 600u;
            Info[25] = 32u; Info[27] = 6u;
            /* VBE 2.0 legacy color-mask sizes and positions. */
            Info[31] = 8u; Info[32] = 16u;
            Info[33] = 8u; Info[34] = 8u;
            Info[35] = 8u; Info[36] = 0u;
            Info[37] = 8u; Info[38] = 24u;
            if (VbeVersion >= 0x0300u) {
                Info[54] = 8u; Info[55] = 16u;
                Info[56] = 8u; Info[57] = 8u;
                Info[58] = 8u; Info[59] = 0u;
                Info[60] = 8u; Info[61] = 24u;
            }
            *(uint32_t *)(Info + 40) = 0xD0000000u;
        } else {
            *(uint16_t *)(Info + 16) = VbeVersion >= 0x0300u ? 0u : 2048u;
            if (VbeVersion >= 0x0300u) *(uint16_t *)(Info + 50) = 2048u;
            *(uint16_t *)(Info + 18) = 1024u;
            *(uint16_t *)(Info + 20) = 768u;
            Info[25] = 16u; Info[27] = 6u;
            Info[31] = 5u; Info[32] = 11u;
            Info[33] = 6u; Info[34] = 5u;
            Info[35] = 5u; Info[36] = 0u;
            if (VbeVersion >= 0x0300u) {
                Info[54] = 5u; Info[55] = 11u;
                Info[56] = 6u; Info[57] = 5u;
                Info[58] = 5u; Info[59] = 0u;
            }
            *(uint32_t *)(Info + 40) = 0xE0000000u;
        }
        R->Eax = 0x004Fu;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x10 && (R->Eax & 0xFFFFu) == 0x4F02u) {
        if ((R->Ebx & 0xFFFFu) != 0x4118u &&
            (R->Ebx & 0xFFFFu) != 0x4115u)
            return Rc95LdrUnsupported;
        VideoGraphicsModeCalls++;
        R->Eax = 0x004Fu;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x10 && Ah == 0x06u) {
        VideoClearCalls++;
        LastVideoClearAttribute = (uint8_t)(R->Ebx >> 8);
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x10) {
        if ((R->Eax & 0xFFFFu) == 0x0003u) VideoModeCalls++;
        if ((R->Eax & 0xFFFFu) == 0x1112u && (R->Ebx & 0xFFFFu) == 0)
            VideoFontCalls++;
        return Rc95LdrSuccess;
    }
    if (Interrupt == 0x16) {
        if (KeyboardIndex < KeyboardCount)
            R->Eax = KeyboardSequence[KeyboardIndex++];
        else
            R->Eax = 0x1C0Du;
        return Rc95LdrSuccess;
    }
    return Rc95LdrUnsupported;
}

static RC95LDR_STATUS MenuEnumerate(void *Context,
    RC95LDR_FIRMWARE_VIDEO_MODE *Modes, size_t Capacity, size_t *Count)
{
    (void)Context; (void)Modes; (void)Capacity;
    if (!Count) return Rc95LdrInvalidParameter;
    *Count = 0;
    return Rc95LdrNotFound;
}
static RC95LDR_STATUS MenuSetMode(void *Context, uint32_t Mode)
{ (void)Context; (void)Mode; return Rc95LdrSuccess; }
static RC95LDR_STATUS MenuMapFramebuffer(void *Context, uint64_t Base,
    uint64_t Size, void **Mapping)
{ (void)Context; (void)Base; (void)Size; if (!Mapping) return Rc95LdrInvalidParameter; *Mapping = NULL; return Rc95LdrNotFound; }

int main(void)
{
    /* Keep the assembly/register handoff signature synchronized with the
       public BIOS startup ABI. */
    if (sizeof(RC95LDR_BIOS_STAGE_HANDOFF) != 16 ||
        offsetof(RC95LDR_BIOS_STAGE_HANDOFF, StartupDescriptor) != 4 ||
        offsetof(RC95LDR_BIOS_STAGE_HANDOFF, PlatformProvider) != 8 ||
        offsetof(RC95LDR_BIOS_STAGE_HANDOFF, ProtectedStackTop) != 12) {
        puts("BIOS stage handoff layout mismatch"); return 1;
    }
    if (RC95LDR_BIOS_ENTRY_MAGIC != 0x52353942u) {
        puts("BIOS entry protocol signature mismatch"); return 1;
    }

    if (Rc95LdrBiosStageMain(0x80, NULL, NULL) != Rc95LdrInvalidParameter) {
        puts("BIOS stage provider validation failed"); return 1;
    }

    /* The public startup API rejects an absent boot context before touching
       firmware state, on both native and host-test builds. */
    if (Rc95LdrPrepareNativePlatform(NULL, NULL) != Rc95LdrInvalidParameter) {
        puts("BIOS native startup parameter validation failed"); return 1;
    }
    RC95LDR_BIOS_SERVICES Services, NullMappingServices;
    RC95LDR_BIOS_DISK Disk;
    RC95LDR_BIOS_BLOCK_CONTEXT Block;
    RC95LDR_BLOCK_DEVICE Device;
    RC95LDR_BIOS_TEXT Text;
    RC95LDR_BIOS_VIDEO Video;
    RC95LDR_BGFX_PLATFORM VideoPlatform;
    RC95LDR_BGFX_CONFIG VideoConfig;
    RC95LDR_BGFX_FRAMEBUFFER Framebuffer;
    RC95LDR_TEXTUI Ui;
    RC95LDR_TEXTUI_EXT UiExtension;
    uint32_t Key;
    RC95LDR_FIRMWARE_MEMORY_RANGE Ranges[4];
    RC95LDR_MEMORY_RESERVATION_SET Reservations;
    RC95LDR_BIOS_STARTUP MemoryStartup;
    RC95LDR_BIOS_STARTUP MenuStartup;
    RC95LDR_FIRMWARE_MENU_OPS MenuOps;
    RC95LDR_BIOS_MEMORY_ALLOCATOR Allocator;
    RC95LDR_WINLOAD_PLATFORM MemoryPlatform;
    RC95LDR_FIRMWARE_MEMORY_RANGE AllocatorRanges[2];
    uint64_t AllocationAddress = 0;
    uint8_t Output[8 * 512];
    size_t Count = 0, i;
    memset(&MenuStartup, 0, sizeof(MenuStartup));
    memset(&MenuOps, 0, sizeof(MenuOps));
    MenuStartup.StructureSize = sizeof(MenuStartup);
    MenuStartup.Version = RC95LDR_BIOS_STARTUP_VERSION;
    MenuStartup.GraphicsModeDisabled = 0;
    MenuOps.StructureSize = sizeof(MenuOps);
    MenuOps.Version = RC95LDR_FIRMWARE_MENU_VERSION;
    if (Rc95LdrBiosBindFirmwareMenuOps(&MenuStartup, &MenuOps) != Rc95LdrSuccess ||
        MenuOps.GraphicsModeDisabledOutput != &MenuStartup.GraphicsModeDisabled) {
        puts("BIOS firmware-menu BCD output binding failed"); return 1;
    }
    *MenuOps.GraphicsModeDisabledOutput = 1u;
    if (MenuStartup.GraphicsModeDisabled != 1u ||
        Rc95LdrBiosBindFirmwareMenuOps(NULL, &MenuOps) != Rc95LdrInvalidParameter) {
        puts("BIOS firmware-menu setting did not reach startup"); return 1;
    }
    memset(&Services, 0, sizeof(Services));
    Services.StructureSize = sizeof(Services);
    Services.Version = RC95LDR_BIOS_ABI_VERSION;
    Services.InvokeInterrupt = Invoke;
    Services.MapLowMemory = MapLow;
    Services.UnmapLowMemory = UnmapLow;
    Services.ScratchPhysical = 0x8000;
    Services.ScratchLength = 0x10000;
    Services.BootDrive = 0x80;
    NullMappingServices = Services;
    NullMappingServices.MapLowMemory = MapLowNull;
    /* Exercise the production startup binder, not only the individual
       VBE callbacks: the menu must receive one coherent BIOS context. */
    memset(&MenuStartup,0,sizeof(MenuStartup));
    memset(&MenuOps,0,sizeof(MenuOps));
    MenuStartup.StructureSize=sizeof(MenuStartup);
    MenuStartup.Version=RC95LDR_BIOS_STARTUP_VERSION;
    MenuStartup.BiosServices=&Services;
    MenuOps.StructureSize=sizeof(MenuOps);
    MenuOps.Version=RC95LDR_FIRMWARE_MENU_VERSION;
    {
        RC95LDR_FIRMWARE_VIDEO_MODE MenuModes[1];
        size_t MenuModeCount=0;
        void *MenuFramebuffer=NULL;
        if(Rc95LdrBiosBindFirmwareMenuOps(&MenuStartup,&MenuOps)!=
                Rc95LdrSuccess||
           MenuOps.Context!=&MenuStartup||
           !MenuOps.EnumerateModes||!MenuOps.SetMode||
           !MenuOps.MapFramebuffer||!MenuOps.ReadKey||
           !MenuOps.TextUi||!MenuOps.TextUiExtension||
           MenuOps.GraphicsModeDisabledOutput!=
                &MenuStartup.GraphicsModeDisabled||
           MenuOps.EnumerateModes(MenuOps.Context,MenuModes,1,
                &MenuModeCount)!=Rc95LdrSuccess||MenuModeCount!=1u||
           MenuOps.SetMode(MenuOps.Context,MenuModes[0].ModeId)!=
                Rc95LdrSuccess||
           MenuOps.MapFramebuffer(MenuOps.Context,
                MenuModes[0].FramebufferBase,MenuModes[0].FramebufferSize,
                &MenuFramebuffer)!=Rc95LdrSuccess||
           (uintptr_t)MenuFramebuffer!=0xE0000000u){
            puts("BIOS firmware-menu startup adapter binding failed");
            return 1;
        }
    }
    {
        uint32_t Continuation = 0;
        RC95LDR_FIRMWARE_MEMORY_RANGE Range;
        int Done = 0;
        if (Rc95LdrBiosE820Next(&NullMappingServices, &Continuation,
                &Range, &Done) != Rc95LdrInvalidParameter ||
            Rc95LdrBiosProbeDisk(&NullMappingServices, 0x80, &Disk) !=
                Rc95LdrInvalidParameter) {
            puts("BIOS null low-memory mapping was not rejected");
            return 1;
        }
    }
    for (i = 0; i < sizeof(DiskImage); ++i) DiskImage[i] = (uint8_t)i;

    if (Rc95LdrBiosProbeDisk(&Services, 0x80, &Disk) != Rc95LdrSuccess ||
        Disk.BlockCount != 64 || Disk.BytesPerSector != 512) {
        puts("BIOS EDD disk probe failed"); return 1;
    }
    if (Rc95LdrBiosReadDisk(&NullMappingServices, &Disk, 0, 1,
            Output, 512) != Rc95LdrInvalidParameter ||
        Rc95LdrBiosWriteDisk(&NullMappingServices, &Disk, 0, 1,
            Output, 512) != Rc95LdrInvalidParameter) {
        puts("BIOS EDD data path accepted a null low-memory mapping");
        return 1;
    }
    if (Rc95LdrBiosInitializeBlockDevice(&Services, &Disk, &Block, &Device) != Rc95LdrSuccess ||
        Device.BlockCount != Disk.BlockCount || Device.BlockSize != Disk.BytesPerSector || !Device.Read) {
        puts("BIOS block-device initialization failed"); return 1;
    }
    memset(Output, 0, sizeof(Output));
    if (Device.Read(Device.DeviceContext, 3, 8, Output, sizeof(Output)) != Rc95LdrSuccess ||
        memcmp(Output, DiskImage + 3 * 512, sizeof(Output)) != 0 ||
        DiskReads != 1) {
        puts("BIOS EDD block read failed"); return 1;
    }
    if (Rc95LdrBiosInitializeTextUi(&Services, &Text, &Ui, &UiExtension) != Rc95LdrSuccess ||
        Ui.Write(Ui.Context, "BIOS console test") != Rc95LdrSuccess ||
        Ui.Clear(Ui.Context) != Rc95LdrSuccess ||
        UiExtension.SetMode(UiExtension.Context, 80, 50) != Rc95LdrSuccess ||
        VideoModeCalls != 1 || VideoFontCalls != 1 ||
        UiExtension.ReadKey(UiExtension.Context, &Key) != Rc95LdrSuccess) {
        puts("BIOS text console integration failed"); return 1;
    }
    /* The wrapper binds before menu entry and preserves the exact selected
       candidate's BCD policy in Startup for native video initialization. */
    {
        RC95LDR_BIOS_STARTUP MenuStartup;
        RC95LDR_FIRMWARE_MENU_OPS MenuOps;
        RC95LDR_BCD_BOOT_CANDIDATE Candidates[2];
        const RC95LDR_BCD_BOOT_CANDIDATE *SelectedCandidate = NULL;
        const char *MenuItems[] = { "RC95-OS", "Windows" };
        memset(&MenuStartup, 0, sizeof(MenuStartup));
        memset(&MenuOps, 0, sizeof(MenuOps));
        memset(Candidates, 0, sizeof(Candidates));
        MenuStartup.StructureSize = sizeof(MenuStartup);
        MenuStartup.Version = RC95LDR_BIOS_STARTUP_VERSION;
        MenuOps.StructureSize = sizeof(MenuOps);
        MenuOps.Version = RC95LDR_FIRMWARE_MENU_VERSION;
        MenuOps.Context = &Text;
        MenuOps.EnumerateModes = MenuEnumerate;
        MenuOps.SetMode = MenuSetMode;
        MenuOps.MapFramebuffer = MenuMapFramebuffer;
        MenuOps.ReadKey = Rc95LdrBiosReadKey;
        MenuOps.TextUi = &Ui;
        MenuOps.TextUiExtension = &UiExtension;
        Candidates[0].ObjectCell = 0x100u;
        Candidates[0].GraphicsModeDisabled = 0u;
        Candidates[1].ObjectCell = 0x200u;
        Candidates[1].GraphicsModeDisabled = 1u;
        KeyboardSequence[0] = 0x5000u; /* Down */
        KeyboardSequence[1] = 0x1C0Du; /* Enter */
        KeyboardCount = 2; KeyboardIndex = 0;
        if (Rc95LdrBiosRunCandidateMenu(&MenuStartup, &MenuOps, NULL,
                Candidates, 2, "Select OS", MenuItems,
                &SelectedCandidate) != Rc95LdrSuccess ||
            SelectedCandidate != &Candidates[1] ||
            MenuOps.GraphicsModeDisabledOutput !=
                &MenuStartup.GraphicsModeDisabled ||
            MenuStartup.GraphicsModeDisabled != 1u) {
            puts("BIOS candidate-menu binding/selected policy failed");
            return 1;
        }
        KeyboardSequence[0] = 0x1C0Du;
        KeyboardCount = 1; KeyboardIndex = 0;
        SelectedCandidate = NULL;
        if (Rc95LdrBiosRunCandidateMenu(&MenuStartup, &MenuOps, NULL,
                Candidates, 2, "Select OS", MenuItems,
                &SelectedCandidate) != Rc95LdrSuccess ||
            SelectedCandidate != &Candidates[0] ||
            MenuStartup.GraphicsModeDisabled != 0u) {
            puts("BIOS candidate-menu policy was not refreshed"); return 1;
        }
        if (Rc95LdrBiosRunCandidateMenu(NULL, &MenuOps, NULL, Candidates, 2,
                "Select OS", MenuItems, &SelectedCandidate) !=
                Rc95LdrInvalidParameter ||
            Rc95LdrBiosRunCandidateMenu(&MenuStartup, &MenuOps, NULL,
                NULL, 2, "Select OS", MenuItems, &SelectedCandidate) !=
                Rc95LdrInvalidParameter ||
            Rc95LdrBiosRunCandidateMenu(&MenuStartup, &MenuOps, NULL,
                Candidates, 0, "Select OS", MenuItems, &SelectedCandidate) !=
                Rc95LdrInvalidParameter) {
            puts("BIOS candidate-menu wrapper parameter validation failed");
            return 1;
        }
        /* Exercise the lower-level binder's ABI and pointer validation
           independently of the combined menu wrapper. */
        MenuOps.Version = RC95LDR_FIRMWARE_MENU_VERSION;
        if (Rc95LdrBiosBindFirmwareMenuOps(NULL, &MenuOps) !=
                Rc95LdrInvalidParameter ||
            Rc95LdrBiosBindFirmwareMenuOps(&MenuStartup, NULL) !=
                Rc95LdrInvalidParameter) {
            puts("BIOS menu binder accepted null arguments"); return 1;
        }
        {
            RC95LDR_BIOS_STARTUP BadStartup = MenuStartup;
            RC95LDR_FIRMWARE_MENU_OPS BadOps = MenuOps;
            BadStartup.StructureSize = sizeof(BadStartup) - 1u;
            if (Rc95LdrBiosBindFirmwareMenuOps(&BadStartup, &BadOps) !=
                    Rc95LdrInvalidParameter) {
                puts("BIOS menu binder accepted undersized startup ABI");
                return 1;
            }
            BadStartup = MenuStartup;
            BadStartup.Version++;
            if (Rc95LdrBiosBindFirmwareMenuOps(&BadStartup, &BadOps) !=
                    Rc95LdrInvalidParameter) {
                puts("BIOS menu binder accepted incompatible startup ABI");
                return 1;
            }
            BadOps = MenuOps;
            BadOps.StructureSize = sizeof(BadOps) - 1u;
            if (Rc95LdrBiosBindFirmwareMenuOps(&MenuStartup, &BadOps) !=
                    Rc95LdrInvalidParameter) {
                puts("BIOS menu binder accepted undersized menu ABI");
                return 1;
            }
            BadOps = MenuOps;
            BadOps.Version++;
            if (Rc95LdrBiosBindFirmwareMenuOps(&MenuStartup, &BadOps) !=
                    Rc95LdrInvalidParameter) {
                puts("BIOS menu binder accepted incompatible menu ABI");
                return 1;
            }
        }
        MenuOps.Version++;
        if (Rc95LdrBiosRunCandidateMenu(&MenuStartup, &MenuOps, NULL,
                Candidates, 2, "Select OS", MenuItems,
                &SelectedCandidate) != Rc95LdrInvalidParameter) {
            puts("BIOS candidate-menu wrapper accepted incompatible menu ABI");
            return 1;
        }
    }
    if (Rc95LdrBiosShowErrorScreen(&Text, &Ui,
            "LiberNT is unsupported", "Unsupported operating system.",
            Rc95LdrUnsupported, 0x1234u, "Choose another entry.") !=
            Rc95LdrSuccess || LastVideoClearAttribute != 0x4Fu ||
        Ui.Clear(Ui.Context) != Rc95LdrSuccess ||
        LastVideoClearAttribute != 0x07u) {
        puts("BIOS red error-screen / black normal-screen attributes failed");
        return 1;
    }
    if (Rc95LdrBiosInitializeVideo(&Services, &Video, &VideoPlatform) !=
            Rc95LdrSuccess) {
        puts("BIOS native video adapter initialization failed"); return 1;
    }
    {
        RC95LDR_FIRMWARE_VIDEO_MODE Modes[2];
        size_t ModeCount=0;
        void *Mapped=NULL;
        if(Rc95LdrBiosEnumerateFirmwareModes(&Video,Modes,2,&ModeCount)!=
                Rc95LdrSuccess||ModeCount!=2u||
           Modes[0].ModeId!=0x0118u||Modes[0].Width!=1024u||
           Modes[0].Height!=768u||
           Modes[0].PixelFormat!=RC95LDR_BGFX_PIXEL_RGB565||
           Modes[0].FramebufferBase!=0xE0000000u||
           Modes[1].ModeId!=0x0115u||Modes[1].Width!=800u||
           Modes[1].Height!=600u||
           Modes[1].PixelFormat!=RC95LDR_BGFX_PIXEL_BGRX8888||
           Modes[1].FramebufferBase!=0xD0000000u){
            puts("BIOS VBE firmware-menu mode enumeration failed");return 1;
        }
        if(Rc95LdrBiosSetFirmwareVideoMode(&Video,Modes[0].ModeId)!=
                Rc95LdrSuccess||
           Rc95LdrBiosMapFirmwareFramebuffer(&Video,
                Modes[0].FramebufferBase,Modes[0].FramebufferSize,&Mapped)!=
                Rc95LdrSuccess||(uintptr_t)Mapped!=0xE0000000u||
           Rc95LdrBiosMapFirmwareFramebuffer(&Video,
                Modes[0].FramebufferBase,Modes[0].FramebufferSize+1u,&Mapped)!=
                Rc95LdrNotFound ||
           Rc95LdrBiosSetFirmwareVideoMode(&Video,Modes[1].ModeId)!=
                Rc95LdrSuccess ||
           Video.Framebuffer.Width!=800u || Video.Framebuffer.Height!=600u ||
           Video.Framebuffer.PixelFormat!=RC95LDR_BGFX_PIXEL_BGRX8888 ||
           Rc95LdrBiosSetFirmwareVideoMode(&Video,Modes[0].ModeId)!=
                Rc95LdrSuccess){
            puts("BIOS VBE firmware-menu mode/map callback failed");return 1;
        }
        InvalidVbeListPointer = 1;
        if (Rc95LdrBiosEnumerateFirmwareModes(&Video, Modes, 2,
                &ModeCount) != Rc95LdrCorruptData || ModeCount != 0u) {
            puts("BIOS VBE accepted an out-of-range mode-list pointer");
            return 1;
        }
        InvalidVbeListPointer = 0;
        MissingVbeListTerminator = 1;
        if (Rc95LdrBiosEnumerateFirmwareModes(&Video, Modes, 2,
                &ModeCount) != Rc95LdrCorruptData || ModeCount != 0u) {
            puts("BIOS VBE accepted an unterminated mode list");
            return 1;
        }
        MissingVbeListTerminator = 0;
        VbeVersion = 0x0300u;
        if (Rc95LdrBiosEnumerateFirmwareModes(&Video, Modes, 2,
                &ModeCount) != Rc95LdrSuccess || ModeCount != 2u ||
            Modes[0].PixelsPerScanLine != 1024u ||
            Modes[0].PixelFormat != RC95LDR_BGFX_PIXEL_RGB565 ||
            Modes[1].PixelsPerScanLine != 800u ||
            Modes[1].PixelFormat != RC95LDR_BGFX_PIXEL_BGRX8888 ||
            Rc95LdrBiosSetFirmwareVideoMode(&Video, Modes[1].ModeId) !=
                Rc95LdrSuccess ||
            Video.Framebuffer.Width != 800u ||
            Video.Framebuffer.PixelFormat != RC95LDR_BGFX_PIXEL_BGRX8888) {
            puts("BIOS VBE 3.0 linear pitch/mask decoding failed");
            return 1;
        }
        VbeVersion = 0x0200u;
        if (Rc95LdrBiosSetFirmwareVideoMode(&Video, 0x0118u) !=
                Rc95LdrSuccess) {
            puts("BIOS VBE 2.0 fallback after VBE 3.0 test failed");
            return 1;
        }
    }
    memset(&VideoConfig, 0, sizeof(VideoConfig));
    VideoConfig.StructureSize = sizeof(VideoConfig);
    VideoConfig.Version = RC95LDR_BGFX_VERSION;
    VideoConfig.PreferredWidth = 1024u;
    VideoConfig.PreferredHeight = 768u;
    if (Rc95LdrBgfxInitialize(&VideoPlatform, &VideoConfig,
            RC95LDR_BGFX_FIRMWARE_BIOS, &Framebuffer) != Rc95LdrSuccess ||
        VideoGraphicsModeCalls != 7 || Framebuffer.Width != 1024u ||
        Framebuffer.Height != 768u ||
        Framebuffer.PixelFormat != RC95LDR_BGFX_PIXEL_RGB565 ||
        (uintptr_t)Framebuffer.Base != 0xE0000000u) {
        puts("BIOS VBE 1024x768 linear framebuffer initialization failed");
        return 1;
    }
    /* Earlier text-console/menu tests also exercise BIOS mode changes;
       isolate the graphics-disabled policy's own 80x50 mode transition. */
    VideoModeCalls=0;
    VideoFontCalls=0;
    VideoConfig.GraphicsModeDisabled = 1u;
    if (Rc95LdrBgfxInitialize(&VideoPlatform, &VideoConfig,
            RC95LDR_BGFX_FIRMWARE_BIOS, &Framebuffer) != Rc95LdrUnsupported ||
        VideoModeCalls != 1 || VideoFontCalls != 1) {
        puts("BIOS graphicsmodedisabled 80x50 override failed");
        return 1;
    }

    memset(&Reservations, 0, sizeof(Reservations));
    if (Rc95LdrBiosAddMemoryReservations(&Services, &Reservations) != Rc95LdrSuccess ||
        Reservations.Count != 2 || Reservations.Entries[0].Base != 0x5000u ||
        Reservations.Entries[1].Base != Services.ScratchPhysical) {
        puts("BIOS gateway memory reservations failed"); return 1;
    }
    E820Calls = 0;
    if (Rc95LdrBiosCollectMemoryMap(&Services, Ranges, 4, &Count) != Rc95LdrSuccess ||
        Count != 2 || Ranges[0].Kind != Rc95LdrFirmwareMemoryAvailable ||
        Ranges[1].Base != 0x100000u) {
        puts("BIOS E820 memory map failed"); return 1;
    }

    /* The native allocator must use only available E820 ranges, skip firmware
       reservations, return identity addresses, and attest only live extents. */
    memset(&MemoryStartup, 0, sizeof(MemoryStartup));
    memset(&Reservations, 0, sizeof(Reservations));
    memset(AllocatorRanges, 0, sizeof(AllocatorRanges));
    MemoryStartup.StructureSize = sizeof(MemoryStartup);
    MemoryStartup.Version = RC95LDR_BIOS_STARTUP_VERSION;
    MemoryStartup.MemoryReservations = &Reservations;
    MemoryStartup.FirmwareMemoryRanges = AllocatorRanges;
    MemoryStartup.FirmwareMemoryCapacity = 2;
    MemoryStartup.FirmwareMemoryCount = 2;
    AllocatorRanges[0].Base = 0x100000u;
    AllocatorRanges[0].Length = 0x2000000u;
    AllocatorRanges[0].Kind = Rc95LdrFirmwareMemoryAvailable;
    AllocatorRanges[1].Base = 0;
    AllocatorRanges[1].Length = 0xA0000u;
    AllocatorRanges[1].Kind = Rc95LdrFirmwareMemoryReserved;
    if (Rc95LdrReserveMemoryRange(&Reservations, 0x100000u, 0x100000u,
                                  LoaderMemoryData) != Rc95LdrSuccess ||
        Rc95LdrBiosInitializeMemoryAllocator(&Allocator, &MemoryStartup) != Rc95LdrSuccess ||
        Rc95LdrBiosAllocatePages(&Allocator, 8192u, 4096u,
                                 &AllocationAddress) != Rc95LdrSuccess ||
        AllocationAddress < 0x200000u ||
        Rc95LdrBiosValidateAllocationExtent(&Allocator, AllocationAddress,
            AllocationAddress, 8192u, 8192u) != Rc95LdrSuccess) {
        puts("BIOS E820 physical allocator failed"); return 1;
    }
    memset(&MemoryPlatform, 0, sizeof(MemoryPlatform));
    MemoryPlatform.StructureSize = sizeof(MemoryPlatform);
    MemoryPlatform.Version = RC95LDR_WINLOAD_ABI_VERSION;
    if (Rc95LdrBiosInstallMemoryCallbacks(&Allocator, &MemoryPlatform) != Rc95LdrSuccess ||
        MemoryPlatform.Context != &Allocator || !MemoryPlatform.AllocatePages ||
        !MemoryPlatform.FreePages || !MemoryPlatform.GetMemoryMap ||
        !MemoryPlatform.ValidateAllocationExtent) {
        puts("BIOS memory callback installation failed"); return 1;
    }
    Rc95LdrBiosFreePages(&Allocator, AllocationAddress, 8192u);
    if (Rc95LdrBiosValidateAllocationExtent(&Allocator, AllocationAddress,
            AllocationAddress, 8192u, 8192u) != Rc95LdrOutOfBounds) {
        puts("BIOS allocator release validation failed"); return 1;
    }

    puts("RC95LDR BIOS service tests passed");
    return 0;
}
