
include_directories(include ${REACTOS_SOURCE_DIR}/drivers/bus/acpi/acpica/include)

list(APPEND HAL_ACPI_SOURCE
    acpi/halacpi.c
    acpi/sleep.c
    acpi/halpnpdd.c
    acpi/busemul.c
    acpi/madt.c
    legacy/bus/pcibus.c)

# Waking up from sleep states: the real-mode stub and its low memory setup
if(ARCH STREQUAL "i386")
    list(APPEND HAL_ACPI_SOURCE smp/i386/lowstub.c)
    list(APPEND HAL_ACPI_ASM_SOURCE smp/i386/wakestub.S)
elseif(ARCH STREQUAL "amd64")
    list(APPEND HAL_ACPI_SOURCE smp/amd64/lowstub.c)
    list(APPEND HAL_ACPI_ASM_SOURCE smp/amd64/wakestub.S)
endif()

# Needed to compile while using ACPICA
if(ARCH STREQUAL "amd64")
    add_definitions(-DWIN64)
endif()

add_asm_files(lib_hal_acpi_asm ${HAL_ACPI_ASM_SOURCE})
add_library(lib_hal_acpi OBJECT ${HAL_ACPI_SOURCE} ${lib_hal_acpi_asm})
add_pch(lib_hal_acpi include/hal.h ${HAL_ACPI_SOURCE})
add_dependencies(lib_hal_acpi bugcodes asm xdk)
