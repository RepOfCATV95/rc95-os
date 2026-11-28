#include "rc95ldrbiosntfsboot.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    RC95LDR_BIOS_NTFS_BOOT_CONFIG Config;
    RC95LDR_BIOS_BLOCK_CONTEXT BlockContext;
    RC95LDR_BIOS_DISK Disk;
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;

    memset(&Config, 0, sizeof(Config));
    memset(&BlockContext, 0, sizeof(BlockContext));
    memset(&Disk, 0, sizeof(Disk));
    memset(&Startup, 0, sizeof(Startup));
    memset(&Result, 0, sizeof(Result));

    assert(Rc95LdrBiosStartWindowsFromNtfs(
        NULL, &Disk, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);
    assert(Rc95LdrBiosStartWindowsFromNtfs(
        &BlockContext, NULL, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);
    assert(Rc95LdrBiosStartWindowsFromNtfs(
        &BlockContext, &Disk, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);

    Startup.StructureSize = sizeof(Startup);
    Startup.Version = RC95LDR_STARTUP_ABI_VERSION;
    Startup.Firmware = Rc95LdrFirmwareBios;
    assert(Rc95LdrBiosStartWindowsFromNtfs(
        &BlockContext, &Disk, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);

    puts("RC95LDR BIOS NTFS lifecycle argument tests passed");
    return 0;
}
