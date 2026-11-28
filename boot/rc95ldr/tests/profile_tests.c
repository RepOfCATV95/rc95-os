#include "rc95ldrprofile.h"
#include <assert.h>
#include <string.h>

static char g_LiberNtErrorText[1024];
static RC95LDR_STATUS CaptureText(void *Context, const char *Text)
{
    size_t Used = strlen(g_LiberNtErrorText);
    size_t Length = strlen(Text);
    (void)Context;
    if (Length >= sizeof(g_LiberNtErrorText) - Used)
        return Rc95LdrBufferTooSmall;
    memcpy(g_LiberNtErrorText + Used, Text, Length + 1);
    return Rc95LdrSuccess;
}

static RC95LDR_OS_IDENTITY Make(RC95LDR_OS_FAMILY Family,
    RC95LDR_BOOT_FIRMWARE Firmware,RC95LDR_BOOT_ENTRY_KIND Entry,
    uint32_t Major,uint32_t Minor,uint32_t Build)
{
    RC95LDR_OS_IDENTITY i;
    memset(&i,0,sizeof(i));i.StructureSize=sizeof(i);
    i.Version=RC95LDR_OS_PROFILE_VERSION;i.OsFamily=Family;
    i.Firmware=Firmware;i.BootEntry=Entry;
    i.OsMajorVersion=Major;i.OsMinorVersion=Minor;i.OsBuildNumber=Build;
    return i;
}
static void TestLegacyNt(void)
{
    RC95LDR_OS_PROFILE p;
    RC95LDR_OS_IDENTITY i=Make(Rc95LdrOsWindowsNtLegacy,Rc95LdrFirmwareBios,
        Rc95LdrEntryNtldr,4,0,1381);
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    assert(p.KernelAbi==Rc95LdrKernelAbiNtLegacy &&
           p.LoaderBlockKind==Rc95LdrLoaderBlockNtLegacy);
    i.OsMajorVersion=5;i.OsMinorVersion=2;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    i.OsMinorVersion=3;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
    i.OsMinorVersion=2;i.BootEntry=Rc95LdrEntryWindowsWinload;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
    i.BootEntry=Rc95LdrEntryNtldr;i.Firmware=Rc95LdrFirmwareUefi;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
}
static void TestModernWindows(void)
{
    RC95LDR_OS_PROFILE p;
    RC95LDR_OS_IDENTITY i=Make(Rc95LdrOsWindowsModern,Rc95LdrFirmwareBios,
        Rc95LdrEntryWindowsWinload,6,0,6000);
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    assert(p.ResumeApplication==Rc95LdrResumeApplicationWinresumeExe);
    i.OsMajorVersion=10;i.OsMinorVersion=0;i.OsBuildNumber=19045;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    i.OsBuildNumber=22000; /* Windows 11 remains NT 10.0. */
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    i.Firmware=Rc95LdrFirmwareUefi;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    assert(p.ResumeApplication==Rc95LdrResumeApplicationWinresumeEfi);
    i.OsBuildNumber=7600;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
}
static void TestLiberNtRejectedForBothFirmwareModes(void)
{
    RC95LDR_OS_PROFILE p;
    RC95LDR_OS_IDENTITY i=Make(Rc95LdrOsLiberNt,Rc95LdrFirmwareBios,
        Rc95LdrEntryWindowsWinload,10,0,0);
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
    i.Firmware=Rc95LdrFirmwareUefi;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
}
static void TestLiberNtBootErrorScreen(void)
{
    RC95LDR_BOOT_CONTEXT boot;
    RC95LDR_TEXTUI ui;
    RC95LDR_WINLOAD_PLATFORM platform;
    RC95LDR_WINLOAD_REQUEST request;
    RC95LDR_WINLOAD_RESULT result;
    unsigned firmware;

    memset(&ui, 0, sizeof(ui));
    ui.StructureSize = sizeof(ui);
    ui.Version = RC95LDR_TEXTUI_VERSION;
    ui.Write = CaptureText;
    memset(&platform, 0, sizeof(platform));
    platform.StructureSize = sizeof(platform);
    platform.Version = RC95LDR_WINLOAD_ABI_VERSION;
    platform.TextUi = &ui;
    memset(&request, 0, sizeof(request));
    request.StructureSize = sizeof(request);
    request.Version = RC95LDR_WINLOAD_ABI_VERSION;
    request.OsFamily = Rc95LdrOsLiberNt;
    memset(&result, 0, sizeof(result));
    result.StructureSize = sizeof(result);
    result.Version = RC95LDR_WINLOAD_ABI_VERSION;

    for (firmware = Rc95LdrFirmwareBios;
         firmware <= Rc95LdrFirmwareUefi; ++firmware) {
        request.Firmware = (RC95LDR_BOOT_FIRMWARE)firmware;
        g_LiberNtErrorText[0] = '\0';
        Rc95LdrInitializeContext(&boot, NULL);
        assert(Rc95LdrLoadAndBootWindows(&boot, &platform, &request, &result) ==
               Rc95LdrUnsupported);
        assert(boot.LastError.Detail == 0x4C4E0001u);
        assert(strstr(g_LiberNtErrorText, "winload.exe (BIOS)") != NULL);
        assert(strstr(g_LiberNtErrorText, "winload.efi (UEFI)") != NULL);
        assert(strstr(g_LiberNtErrorText, "November 11, 2026") != NULL);
    }
}

static void TestReactOsAndRc95(void)
{
    RC95LDR_OS_PROFILE p;
    RC95LDR_OS_IDENTITY i=Make(Rc95LdrOsReactOS,Rc95LdrFirmwareUefi,
        Rc95LdrEntryReactOs,0,0,0);
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    assert(p.KernelAbi==Rc95LdrKernelAbiReactOs);
    i.BootEntry=Rc95LdrEntryNtldr;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
    i=Make(Rc95LdrOsRc95,Rc95LdrFirmwareBios,Rc95LdrEntryRc95Winload,0,0,0);
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    assert(p.ResumeApplication==Rc95LdrResumeApplicationRc95Winload);
    i.Firmware=Rc95LdrFirmwareUefi;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrSuccess);
    assert(p.ResumeApplication==Rc95LdrResumeApplicationRc95Winload);
    i.BootEntry=Rc95LdrEntryWindowsWinload;
    assert(Rc95LdrSelectOsProfile(&i,&p)==Rc95LdrUnsupported);
}
int main(void)
{
    TestLegacyNt();TestModernWindows();
    TestLiberNtRejectedForBothFirmwareModes();
    TestLiberNtBootErrorScreen();
    TestReactOsAndRc95();return 0;
}
