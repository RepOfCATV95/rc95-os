#ifndef _RC95LDR_PLATFORM_INVENTORY_H_
#define _RC95LDR_PLATFORM_INVENTORY_H_
#include "rc95ldrloaderstate.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_PLATFORM_INVENTORY_VERSION 1u
#define RC95LDR_ACPI_TABLE_MAX 32u
#define RC95LDR_PCI_FUNCTION_MAX 64u
#define RC95LDR_PLATFORM_COMPONENT_MAX RC95LDR_CONFIG_COMPONENT_MAX
typedef const void *(*RC95LDR_MAP_PHYSICAL)(void *Context,uint64_t PhysicalAddress,size_t Length);
typedef RC95LDR_STATUS (*RC95LDR_READ_PCI_CONFIG)(void *Context,uint16_t Segment,uint8_t Bus,uint8_t Device,uint8_t Function,uint16_t Offset,uint32_t *Value);
typedef struct _RC95LDR_ACPI_ROOT {
    uint8_t Revision;
    uint8_t Reserved[7];
    uint32_t RsdtAddress;
    uint64_t XsdtAddress;
} RC95LDR_ACPI_ROOT;
typedef struct _RC95LDR_PCI_FUNCTION {
    uint16_t Segment;
    uint8_t Bus,Device,Function;
    uint16_t VendorId,DeviceId;
    uint8_t ClassCode,SubClass,ProgIf,HeaderType;
    uint8_t Reserved[4];
} RC95LDR_PCI_FUNCTION;
typedef struct _RC95LDR_PLATFORM_INVENTORY_INPUT {
    uint32_t StructureSize,Version;
    RC95LDR_ACPI_ROOT AcpiRoot;
    uint64_t RsdpPhysicalAddress;
    RC95LDR_MAP_PHYSICAL MapPhysical;
    RC95LDR_READ_PCI_CONFIG ReadPciConfig;
    void *FirmwareContext;
    uint32_t PciSegment;
} RC95LDR_PLATFORM_INVENTORY_INPUT;
typedef struct _RC95LDR_PLATFORM_INVENTORY_RESULT {
    RC95LDR_CONFIG_COMPONENT_INPUT Components[RC95LDR_PLATFORM_COMPONENT_MAX];
    RC95LDR_PCI_FUNCTION PciFunctions[RC95LDR_PCI_FUNCTION_MAX];
    char Identifiers[RC95LDR_PLATFORM_COMPONENT_MAX][48];
    uint8_t ConfigurationData[RC95LDR_PLATFORM_COMPONENT_MAX][32];
    uint32_t ComponentCount,PciFunctionCount,AcpiTableCount;
} RC95LDR_PLATFORM_INVENTORY_RESULT;
RC95LDR_STATUS Rc95LdrBuildPlatformInventory(const RC95LDR_PLATFORM_INVENTORY_INPUT*,RC95LDR_PLATFORM_INVENTORY_RESULT*);
#ifdef __cplusplus
}
#endif
#endif
