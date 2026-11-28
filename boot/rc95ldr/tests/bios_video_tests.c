#include "rc95ldrbios.h"
#include <assert.h>
#include <string.h>

typedef struct _VBE_TEST_CONTEXT {
    uint8_t Scratch[512];
    uint8_t ModeList[256];
    uint32_t LastMappedAddress;
} VBE_TEST_CONTEXT;

static RC95LDR_STATUS
VbeTestMap(void *Context, uint32_t PhysicalAddress, size_t Length,
           void **Mapping)
{
    VBE_TEST_CONTEXT *Test = (VBE_TEST_CONTEXT *)Context;
    if (!Mapping) return Rc95LdrInvalidParameter;
    *Mapping = NULL;
    if (PhysicalAddress == 0x7000u && Length <= sizeof(Test->Scratch)) {
        *Mapping = Test->Scratch;
        return Rc95LdrSuccess;
    }
    if (PhysicalAddress == 0x90020u && Length <= sizeof(Test->ModeList)) {
        Test->LastMappedAddress = PhysicalAddress;
        *Mapping = Test->ModeList;
        return Rc95LdrSuccess;
    }
    return Rc95LdrNotFound;
}

static void
VbeTestUnmap(void *Context, void *Mapping, size_t Length)
{
    (void)Context;
    (void)Mapping;
    (void)Length;
}

static RC95LDR_STATUS
VbeTestInterrupt(void *Context, uint8_t Interrupt,
                 RC95LDR_BIOS_REGISTERS *Registers)
{
    VBE_TEST_CONTEXT *Test = (VBE_TEST_CONTEXT *)Context;
    uint8_t *Info = Test->Scratch;
    if (Interrupt != 0x10u) return Rc95LdrUnsupported;
    if ((uint16_t)Registers->Eax == 0x4F00u) {
        memcpy(Info, "VESA", 4);
        Info[4] = 0;
        Info[5] = 2;
        /* Deliberately use an unnormalised far pointer whose segment base
           and offset overlap: 9001:0010 linearises to 0x90020, not 0x90010. */
        Info[14] = 0x10;
        Info[15] = 0x00;
        Info[16] = 0x01;
        Info[17] = 0x90;
        Test->ModeList[0] = 0x18;
        Test->ModeList[1] = 0x01;
        Test->ModeList[2] = 0xFF;
        Test->ModeList[3] = 0xFF;
        Registers->Eax = 0x004F;
        return Rc95LdrSuccess;
    }
    if ((uint16_t)Registers->Eax == 0x4F01u) {
        memset(Info, 0, 256);
        Info[0] = 0x80; Info[1] = 0x00; /* supported, graphics, LFB */
        Info[16] = 0x00; Info[17] = 0x0A; /* 2560-byte pitch */
        Info[18] = 0x80; Info[19] = 0x02; /* 640 pixels */
        Info[20] = 0x90; Info[21] = 0x01; /* 400 lines */
        Info[25] = 32;
        Info[27] = 6;
        Info[31] = 8; Info[32] = 16;
        Info[33] = 8; Info[34] = 8;
        Info[35] = 8; Info[36] = 0;
        Info[37] = 8; Info[38] = 24;
        Info[40] = 0x00; Info[41] = 0x00;
        Info[42] = 0x00; Info[43] = 0xE0; /* 0xE0000000 */
        Registers->Eax = 0x004F;
        return Rc95LdrSuccess;
    }
    return Rc95LdrUnsupported;
}


int main(void)
{
    RC95LDR_BIOS_VIDEO Video;
    RC95LDR_BGFX_FRAMEBUFFER View;
    uint8_t Pixels[4u * 4u * 4u];
    void *Mapping = (void *)(uintptr_t)1u;

    memset(&Video, 0, sizeof(Video));
    memset(&View, 0, sizeof(View));
    memset(Pixels, 0, sizeof(Pixels));

    /* A valid active BIOS framebuffer can be acquired and identity-mapped. */
    Video.Framebuffer.StructureSize = sizeof(Video.Framebuffer);
    Video.Framebuffer.Version = RC95LDR_BGFX_VERSION;
    Video.Framebuffer.Base = Pixels;
    Video.Framebuffer.Size = sizeof(Pixels);
    Video.Framebuffer.Width = 4u;
    Video.Framebuffer.Height = 4u;
    Video.Framebuffer.PixelsPerScanLine = 4u;
    Video.Framebuffer.PixelFormat = RC95LDR_BGFX_PIXEL_RGBX8888;

    assert(Rc95LdrBiosAcquireFramebuffer(&Video, &View) == Rc95LdrSuccess);
    assert(View.Base == Pixels);
    assert(Rc95LdrBiosMapFirmwareFramebuffer(&Video,
        (uint64_t)(uintptr_t)Pixels, sizeof(Pixels), &Mapping) == Rc95LdrSuccess);
    assert(Mapping == Pixels);

    /* Corrupt a live descriptor: neither API may expose it, and the stale
       descriptor must be invalidated so later calls cannot reuse it. */
    Video.Framebuffer.Size = 1u;
    assert(Rc95LdrBiosAcquireFramebuffer(&Video, &View) == Rc95LdrOutOfBounds);
    assert(Video.Framebuffer.Base == NULL);
    Mapping = (void *)(uintptr_t)1u;
    assert(Rc95LdrBiosMapFirmwareFramebuffer(&Video,
        (uint64_t)(uintptr_t)Pixels, sizeof(Pixels), &Mapping) == Rc95LdrNotFound);
    assert(Mapping == NULL);

    /* Mapping clears its output before reporting a mismatched active range. */
    Video.Framebuffer.StructureSize = sizeof(Video.Framebuffer);
    Video.Framebuffer.Version = RC95LDR_BGFX_VERSION;
    Video.Framebuffer.Base = Pixels;
    Video.Framebuffer.Size = sizeof(Pixels);
    Video.Framebuffer.Width = 4u;
    Video.Framebuffer.Height = 4u;
    Video.Framebuffer.PixelsPerScanLine = 4u;
    Video.Framebuffer.PixelFormat = RC95LDR_BGFX_PIXEL_RGBX8888;
    Mapping = (void *)(uintptr_t)1u;
    assert(Rc95LdrBiosMapFirmwareFramebuffer(&Video,
        (uint64_t)(uintptr_t)Pixels, sizeof(Pixels) - 1u, &Mapping)
        == Rc95LdrNotFound);
    assert(Mapping == NULL);

    /* VBE mode-list pointers are real-mode segment:offset addresses. Exercise
       a non-normalised pointer where addition differs from bitwise OR. */
    {
        VBE_TEST_CONTEXT Firmware;
        RC95LDR_BIOS_SERVICES Services;
        RC95LDR_BIOS_VIDEO BiosVideo;
        RC95LDR_FIRMWARE_VIDEO_MODE Modes[4];
        size_t ModeCount = 0;
        memset(&Firmware, 0, sizeof(Firmware));
        memset(&Services, 0, sizeof(Services));
        memset(&BiosVideo, 0, sizeof(BiosVideo));
        Services.StructureSize = sizeof(Services);
        Services.Version = RC95LDR_BIOS_ABI_VERSION;
        Services.Context = &Firmware;
        Services.InvokeInterrupt = VbeTestInterrupt;
        Services.MapLowMemory = VbeTestMap;
        Services.UnmapLowMemory = VbeTestUnmap;
        Services.ScratchPhysical = 0x7000u;
        Services.ScratchLength = sizeof(Firmware.Scratch);
        BiosVideo.Text.Services = &Services;
        assert(Rc95LdrBiosEnumerateFirmwareModes(&BiosVideo, Modes, 4,
            &ModeCount) == Rc95LdrSuccess);
        assert(Firmware.LastMappedAddress == 0x90020u);
        assert(ModeCount == 1u);
        assert(Modes[0].ModeId == 0x0118u);
        assert(Modes[0].Width == 640u && Modes[0].Height == 400u);
    }
    return 0;
}
