#ifndef _RC95LDR_PLATFORM_INVENTORY_H_
#define _RC95LDR_PLATFORM_INVENTORY_H_
#include "rc95ldrloaderstate.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_PLATFORM_INVENTORY_VERSION 5u
#define RC95LDR_PCI_SEGMENT_MAX 8u
#define RC95LDR_PCI_BAR_MAX 6u
#define RC95LDR_SMBIOS_STRING_MAX 64u
#define RC95LDR_PLATFORM_CONFIG_DATA_MAX 256u
#define RC95LDR_APIC_PROCESSOR_MAX 32u
#define RC95LDR_APIC_IO_CONTROLLER_MAX 8u
#define RC95LDR_APIC_IRQ_OVERRIDE_MAX 32u
#define RC95LDR_ACPI_TABLE_MAX 32u
#define RC95LDR_PCI_FUNCTION_MAX 64u
#define RC95LDR_PLATFORM_COMPONENT_MAX RC95LDR_CONFIG_COMPONENT_MAX
typedef const void *(*RC95LDR_MAP_PHYSICAL)(void *Context,uint64_t PhysicalAddress,size_t Length);
typedef RC95LDR_STATUS (*RC95LDR_READ_PCI_CONFIG)(void *Context,uint16_t Segment,uint8_t Bus,uint8_t Device,uint8_t Function,uint16_t Offset,uint32_t *Value);
/* Width is 2 for command-register writes and 4 for BAR writes. The adapter
 * must issue exactly that width to avoid clearing write-one-to-clear status bits. */
typedef RC95LDR_STATUS (*RC95LDR_WRITE_PCI_CONFIG)(void *Context,uint16_t Segment,uint8_t Bus,uint8_t Device,uint8_t Function,uint16_t Offset,uint8_t Width,uint32_t Value);
/* Optional trusted resource-size provider. When absent or unsupported, the
 * generic reader may size assigned BARs through WritePciConfig if supplied. */
typedef RC95LDR_STATUS (*RC95LDR_QUERY_PCI_BAR_SIZE)(
    void *Context, uint16_t Segment, uint8_t Bus, uint8_t Device,
    uint8_t Function, uint8_t BarIndex, uint64_t *Length);
typedef struct _RC95LDR_ACPI_ROOT {
    uint8_t Revision;
    uint8_t Reserved[7];
    uint32_t RsdtAddress;
    uint64_t XsdtAddress;
} RC95LDR_ACPI_ROOT;
typedef struct _RC95LDR_PCI_SEGMENT {
    uint64_t EcAMBase;
    uint16_t Segment;
    uint8_t StartBus, EndBus;
    uint32_t Reserved;
} RC95LDR_PCI_SEGMENT;
#define RC95LDR_PCI_RESOURCE_IO       0x00000001u
#define RC95LDR_PCI_RESOURCE_MEMORY   0x00000002u
#define RC95LDR_PCI_RESOURCE_PREFETCH 0x00000004u
#define RC95LDR_PCI_RESOURCE_64BIT    0x00000008u
#define RC95LDR_PCI_RESOURCE_ASSIGNED 0x00000010u
#define RC95LDR_PCI_RESOURCE_SIZE_UNKNOWN 0x00000020u
typedef struct _RC95LDR_PCI_RESOURCE {
    uint64_t Base;
    uint64_t Length; /* Zero with SIZE_UNKNOWN when read-only config cannot size a BAR. */
    uint32_t Flags;
    uint32_t RegisterOffset;
} RC95LDR_PCI_RESOURCE;
typedef struct _RC95LDR_PCI_BRIDGE {
    uint16_t Segment;
    uint8_t Bus, Device, Function;
    uint8_t PrimaryBus, SecondaryBus, SubordinateBus, Reserved;
    uint64_t IoBase, IoLimit, MemoryBase, MemoryLimit;
    uint64_t PrefetchBase, PrefetchLimit;
} RC95LDR_PCI_BRIDGE;
typedef struct _RC95LDR_SMBIOS_INFO {
    uint8_t MajorVersion, MinorVersion, DocRevision, EntryPointRevision;
    uint32_t TableLength, StructureCount;
    uint64_t EntryPointAddress, TableAddress;
    char Manufacturer[RC95LDR_SMBIOS_STRING_MAX];
    char ProductName[RC95LDR_SMBIOS_STRING_MAX];
    char Version[RC95LDR_SMBIOS_STRING_MAX];
    char SerialNumber[RC95LDR_SMBIOS_STRING_MAX];
    char Uuid[40];
} RC95LDR_SMBIOS_INFO;
typedef struct _RC95LDR_PCI_FUNCTION {
    uint16_t Segment;
    uint8_t Bus,Device,Function;
    uint16_t VendorId,DeviceId;
    uint8_t ClassCode,SubClass,ProgIf,HeaderType;
    uint8_t Reserved[4];
    uint32_t ResourceFirst, ResourceCount;
    uint32_t BridgeIndex;
} RC95LDR_PCI_FUNCTION;
/* Serialized as one ARC configuration-data payload per PCI function:
 * function header, six bounded BAR records, then the optional bridge record. */
typedef struct _RC95LDR_PCI_CONFIGURATION_DATA {
    RC95LDR_PCI_FUNCTION Function;
    RC95LDR_PCI_RESOURCE Resources[RC95LDR_PCI_BAR_MAX];
    RC95LDR_PCI_BRIDGE Bridge;
} RC95LDR_PCI_CONFIGURATION_DATA;
typedef struct _RC95LDR_APIC_PROCESSOR {
    uint32_t ProcessorId;
    uint32_t ApicId;
    uint32_t Flags;
    uint32_t IsX2Apic;
} RC95LDR_APIC_PROCESSOR;
typedef struct _RC95LDR_APIC_IO_CONTROLLER {
    uint32_t Id;
    uint32_t Address;
    uint32_t GlobalSystemInterruptBase;
} RC95LDR_APIC_IO_CONTROLLER;
typedef struct _RC95LDR_APIC_IRQ_OVERRIDE {
    uint32_t Bus;
    uint32_t SourceIrq;
    uint32_t GlobalSystemInterrupt; /* MPS stores the destination input pin here. */
    uint32_t Flags;
    uint32_t InterruptType;
    uint32_t DestinationApic;
} RC95LDR_APIC_IRQ_OVERRIDE;
typedef struct _RC95LDR_FIRMWARE_TOPOLOGY {
    uint32_t AcpiPresent, MadtPresent, MpsPresent, Source;
    uint64_t LocalApicAddress;
    uint32_t ProcessorCount, IoControllerCount, IrqOverrideCount, ImcrPresent;
    uint32_t MadtFlags;
    uint32_t MpsSpecificationRevision; /* MP floating-pointer spec revision: 1, 2, or 4. */
    RC95LDR_APIC_PROCESSOR Processors[RC95LDR_APIC_PROCESSOR_MAX];
    RC95LDR_APIC_IO_CONTROLLER IoControllers[RC95LDR_APIC_IO_CONTROLLER_MAX];
    RC95LDR_APIC_IRQ_OVERRIDE IrqOverrides[RC95LDR_APIC_IRQ_OVERRIDE_MAX];
} RC95LDR_FIRMWARE_TOPOLOGY;
typedef struct _RC95LDR_PLATFORM_INVENTORY_INPUT {
    uint32_t StructureSize,Version;
    RC95LDR_ACPI_ROOT AcpiRoot;
    uint64_t RsdpPhysicalAddress;
    RC95LDR_MAP_PHYSICAL MapPhysical;
    RC95LDR_READ_PCI_CONFIG ReadPciConfig;
    RC95LDR_WRITE_PCI_CONFIG WritePciConfig;
    void *FirmwareContext;
    uint32_t PciSegment;
    uint64_t MpsFloatingPointerAddress;
    uint32_t ScanLegacyFirmwareTables;
    uint32_t Reserved;
    uint64_t SmbiosEntryPointAddress;
    RC95LDR_QUERY_PCI_BAR_SIZE QueryPciBarSize;
} RC95LDR_PLATFORM_INVENTORY_INPUT;
typedef struct _RC95LDR_PLATFORM_INVENTORY_RESULT {
    RC95LDR_CONFIG_COMPONENT_INPUT Components[RC95LDR_PLATFORM_COMPONENT_MAX];
    RC95LDR_PCI_FUNCTION PciFunctions[RC95LDR_PCI_FUNCTION_MAX];
    RC95LDR_PCI_SEGMENT PciSegments[RC95LDR_PCI_SEGMENT_MAX];
    RC95LDR_PCI_RESOURCE PciResources[RC95LDR_PCI_FUNCTION_MAX * RC95LDR_PCI_BAR_MAX];
    RC95LDR_PCI_BRIDGE PciBridges[RC95LDR_PCI_FUNCTION_MAX];
    RC95LDR_SMBIOS_INFO Smbios;
    char Identifiers[RC95LDR_PLATFORM_COMPONENT_MAX][48];
    uint8_t ConfigurationData[RC95LDR_PLATFORM_COMPONENT_MAX][RC95LDR_PLATFORM_CONFIG_DATA_MAX];
    uint32_t ComponentCount,PciFunctionCount,AcpiTableCount;
    uint32_t PciSegmentCount,PciResourceCount,PciBridgeCount;
    RC95LDR_FIRMWARE_TOPOLOGY Topology;
} RC95LDR_PLATFORM_INVENTORY_RESULT;
RC95LDR_STATUS Rc95LdrBuildPlatformInventory(const RC95LDR_PLATFORM_INVENTORY_INPUT*,RC95LDR_PLATFORM_INVENTORY_RESULT*);
#ifdef __cplusplus
}
#endif
#endif
