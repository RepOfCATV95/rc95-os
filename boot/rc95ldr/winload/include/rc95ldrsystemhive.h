#ifndef _RC95LDR_SYSTEM_HIVE_H_
#define _RC95LDR_SYSTEM_HIVE_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_HIVE_MAX_NAME_CHARS 256u
typedef struct _RC95LDR_SYSTEM_HIVE {
    const uint8_t *Bytes;
    size_t Size;
    uint32_t DataLength;
    uint32_t RootCell;
} RC95LDR_SYSTEM_HIVE;
typedef struct _RC95LDR_HIVE_KEY {
    uint32_t Cell;
    uint32_t SubkeyCount;
    uint32_t SubkeyList;
    uint32_t VolatileSubkeyCount;
    uint32_t VolatileSubkeyList;
    uint32_t ValueCount;
    uint32_t ValueList;
    uint16_t NameLength;
    uint16_t Flags;
    char Name[RC95LDR_HIVE_MAX_NAME_CHARS + 1];
} RC95LDR_HIVE_KEY;
typedef struct _RC95LDR_HIVE_VALUE {
    uint32_t Cell;
    uint32_t Type;
    uint32_t DataLength;
    uint32_t DataCell;
    uint32_t DataInline;
    uint16_t NameLength;
    char Name[RC95LDR_HIVE_MAX_NAME_CHARS + 1];
} RC95LDR_HIVE_VALUE;
RC95LDR_STATUS Rc95LdrOpenSystemHive(const void *Bytes, size_t Size,
    RC95LDR_SYSTEM_HIVE *Hive);
RC95LDR_STATUS Rc95LdrHiveOpenKey(const RC95LDR_SYSTEM_HIVE *Hive,
    uint32_t ParentCell, const char *Name, RC95LDR_HIVE_KEY *Key);
RC95LDR_STATUS Rc95LdrHiveOpenPath(const RC95LDR_SYSTEM_HIVE *Hive,
    const char *Path, RC95LDR_HIVE_KEY *Key);
RC95LDR_STATUS Rc95LdrHiveGetValue(const RC95LDR_SYSTEM_HIVE *Hive,
    const RC95LDR_HIVE_KEY *Key, const char *Name, RC95LDR_HIVE_VALUE *Value);
RC95LDR_STATUS Rc95LdrHiveReadValue(const RC95LDR_SYSTEM_HIVE *Hive,
    const RC95LDR_HIVE_VALUE *Value, void *Buffer, size_t Capacity,
    size_t *BytesRead);
RC95LDR_STATUS Rc95LdrHiveGetDword(const RC95LDR_SYSTEM_HIVE *Hive,
    const RC95LDR_HIVE_KEY *Key, const char *Name, uint32_t *Data);
typedef RC95LDR_STATUS (*RC95LDR_HIVE_SUBKEY_CALLBACK)(void *Context, const RC95LDR_HIVE_KEY *Key);
typedef struct _RC95LDR_HIVE_BOOT_DRIVER {
    char ServiceName[RC95LDR_HIVE_MAX_NAME_CHARS + 1];
    char ImagePath[RC95LDR_HIVE_MAX_NAME_CHARS + 1];
    char Group[RC95LDR_HIVE_MAX_NAME_CHARS + 1];
    uint32_t Start, Type, ErrorControl, Tag;
} RC95LDR_HIVE_BOOT_DRIVER;
typedef RC95LDR_STATUS (*RC95LDR_HIVE_BOOT_DRIVER_CALLBACK)(void *Context, const RC95LDR_HIVE_BOOT_DRIVER *Driver);
RC95LDR_STATUS Rc95LdrHiveEnumerateSubkeys(const RC95LDR_SYSTEM_HIVE *Hive, const RC95LDR_HIVE_KEY *Parent, RC95LDR_HIVE_SUBKEY_CALLBACK Callback, void *Context);
RC95LDR_STATUS Rc95LdrHiveEnumerateBootDrivers(const RC95LDR_SYSTEM_HIVE *Hive, RC95LDR_HIVE_BOOT_DRIVER_CALLBACK Callback, void *Context);
RC95LDR_STATUS Rc95LdrHiveResolveCurrentControlSet(
    const RC95LDR_SYSTEM_HIVE *Hive, RC95LDR_HIVE_KEY *ControlSet);
#ifdef __cplusplus
}
#endif
#endif
