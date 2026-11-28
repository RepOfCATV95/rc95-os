# Native RC95LDR installation layout.
#
# This file is included only after the RC95LDR subdirectory has declared its
# final executable targets. The build ARCH selects the matching i386 or
# amd64 WINLOAD image; BIOS and UEFI images remain separate binaries.

if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    if(TARGET rc95ldr_bios)
        add_cd_file(TARGET rc95ldr_bios
            DESTINATION . NO_CAB NAME_ON_CD rc95ldr.sys FOR bootcd regtest)
    endif()

    if(TARGET winload_bios)
        add_cd_file(TARGET winload_bios
            DESTINATION reactos/system32 NO_CAB NAME_ON_CD winload.exe FOR bootcd regtest)
    endif()
endif()

if(DEFINED EFI_PLATFORM_ID AND TARGET rc95ldr_uefi)
    # Install RC95LDR in its private EFI directory and also publish the
    # architecture-standard removable-media bootstrap path.
    add_cd_file(TARGET rc95ldr_uefi
        DESTINATION efi/rc95ldr NO_CAB NAME_ON_CD rc95ldr.efi FOR bootcd regtest)
    add_cd_file(TARGET rc95ldr_uefi
        DESTINATION efi/boot NO_CAB NAME_ON_CD boot${EFI_PLATFORM_ID}.efi FOR bootcd regtest)
endif()

if(DEFINED EFI_PLATFORM_ID AND TARGET winload_uefi)
    add_cd_file(TARGET winload_uefi
        DESTINATION reactos/system32 NO_CAB NAME_ON_CD winload.efi FOR bootcd regtest)
endif()
