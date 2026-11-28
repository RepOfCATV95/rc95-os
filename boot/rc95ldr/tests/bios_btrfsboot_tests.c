#include "rc95ldrbiosbtrfsboot.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    RC95LDR_BIOS_BTRFS_BOOT_CONFIG Config;
    RC95LDR_BIOS_BLOCK_CONTEXT BlockContext;
    RC95LDR_BIOS_DISK Disk;
    RC95LDR_WINLOAD_STARTUP Startup;
    RC95LDR_WINLOAD_RESULT Result;

    memset(&Config, 0, sizeof(Config));
    memset(&BlockContext, 0, sizeof(BlockContext));
    memset(&Disk, 0, sizeof(Disk));
    memset(&Startup, 0, sizeof(Startup));
    memset(&Result, 0, sizeof(Result));

    assert(Rc95LdrBiosStartWindowsFromBtrfs(
        NULL, &Disk, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);
    assert(Rc95LdrBiosStartWindowsFromBtrfs(
        &BlockContext, NULL, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);
    assert(Rc95LdrBiosStartWindowsFromBtrfs(
        &BlockContext, &Disk, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);

    Startup.StructureSize = sizeof(Startup);
    Startup.Version = RC95LDR_STARTUP_ABI_VERSION;
    Startup.Firmware = Rc95LdrFirmwareBios;
    assert(Rc95LdrBiosStartWindowsFromBtrfs(
        &BlockContext, &Disk, NULL, &Startup, &Config, &Result) ==
        Rc95LdrInvalidParameter);

    puts("RC95LDR BIOS Btrfs WINLOAD lifecycle argument tests passed");
    return 0;
}
