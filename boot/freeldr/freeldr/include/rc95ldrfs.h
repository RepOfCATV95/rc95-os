#pragma once
#include <stddef.h>
#include <rc95bcd.h>

/* Load the BCD store from FreeLoader's selected boot volume.
 * BIOS: NTFS \\Boot\\BCD only.
 * UEFI: NTFS \\Boot\\BCD or FAT32 ESP \\EFI\\Microsoft\\Boot\\BCD.
 * Caller owns Buffer; Hive views point into it. */
RC95LDR_STATUS
Rc95LdrFreeldrLoadBcd(
    void *Buffer,
    size_t BufferCapacity,
    size_t *ImageSize,
    RC95BCD_HIVE *Hive);

/* Mounted-volume adapter backed by FreeLoader's existing FAT32/NTFS ARC
 * filesystem dispatch. ArcDevicePath must name a whole partition device. */
typedef struct _RC95LDR_FREELDR_VOLUME {
    const char *DefaultPath;
    char DevicePath[128];
    uint32_t RawDeviceId;
    uint64_t RawDeviceBytes;
    RC95LDR_FILESYSTEM_KIND RequiredFileSystem;
    RC95LDR_BLOCK_DEVICE BlockDevice;
    RC95LDR_FILESYSTEM FileSystem;
    RC95LDR_VOLUME Volume;
    uint8_t IsOpen;
} RC95LDR_FREELDR_VOLUME;

RC95LDR_STATUS
Rc95LdrFreeldrOpenVolume(
    const char *ArcDevicePath,
    RC95LDR_FREELDR_VOLUME *Adapter,
    RC95LDR_VOLUME **Volume);

void
Rc95LdrFreeldrCloseVolume(RC95LDR_FREELDR_VOLUME *Adapter);
