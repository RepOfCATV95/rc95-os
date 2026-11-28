#ifndef _RC95LDR_PE_H_
#define _RC95LDR_PE_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_PE_MACHINE_I386 0x014cu
#define RC95LDR_PE_MACHINE_AMD64 0x8664u
#define RC95LDR_PE_DIRECTORY_BASERELOC 5u
#define RC95LDR_PE_DIRECTORY_IMPORT 1u
#define RC95LDR_PE_MAX_SECTIONS 96u
#define RC95LDR_PE_SCN_MEM_EXECUTE 0x20000000u
#define RC95LDR_PE_SCN_MEM_READ    0x40000000u
#define RC95LDR_PE_SCN_MEM_WRITE   0x80000000u
typedef struct _RC95LDR_PE_SECTION_INFO {
    uint32_t VirtualAddress;
    uint32_t VirtualSize;
    uint32_t MappedSize; /* max(VirtualSize, SizeOfRawData), as mapped by PE */
    uint32_t Characteristics;
} RC95LDR_PE_SECTION_INFO;
typedef enum _RC95LDR_PE_FORMAT { Rc95LdrPe32=1, Rc95LdrPe32Plus=2 } RC95LDR_PE_FORMAT;
typedef struct _RC95LDR_PE_INFO {
    RC95LDR_PE_FORMAT Format;
    uint16_t Machine, SectionCount, Characteristics;
    uint32_t EntryPointRva, SizeOfImage, SizeOfHeaders, SectionAlignment, FileAlignment;
    uint64_t PreferredImageBase;
    RC95LDR_PE_SECTION_INFO Sections[RC95LDR_PE_MAX_SECTIONS];
    uint32_t RelocationRva, RelocationSize, ImportRva, ImportSize;
    uint32_t ResourceRva, ResourceSize;
} RC95LDR_PE_INFO;
RC95LDR_STATUS Rc95LdrPeValidate(const void *FileData, size_t FileSize,
    uint16_t ExpectedMachine, RC95LDR_PE_INFO *Info);
/* Resource-only DLLs have no executable entry point and must have no imports. */
RC95LDR_STATUS Rc95LdrPeValidateResource(const void *FileData, size_t FileSize,
    uint16_t ExpectedMachine, RC95LDR_PE_INFO *Info);
/* A resolver must enforce the target OS's module/dependency policy and
   return an address belonging to a validated, loaded dependency image. */
typedef RC95LDR_STATUS (*RC95LDR_PE_IMPORT_RESOLVER)(
    void *Context, const char *ModuleName, const char *SymbolName,
    uint16_t Ordinal, int ByOrdinal, uint64_t *ResolvedAddress);
/* MapEx binds imports only through the caller's explicit resolver. A missing
   resolver, unresolved symbol, or out-of-range address fails closed. */
RC95LDR_STATUS Rc95LdrPeMapEx(const void *FileData, size_t FileSize,
    const RC95LDR_PE_INFO *Info, void *ImageBuffer, size_t ImageBufferSize,
    uint64_t DestinationBase, RC95LDR_PE_IMPORT_RESOLVER Resolver,
    void *ResolverContext);
/* Legacy no-resolver entry point rejects images that contain imports. */
RC95LDR_STATUS Rc95LdrPeMap(const void *FileData, size_t FileSize,
    const RC95LDR_PE_INFO *Info, void *ImageBuffer, size_t ImageBufferSize,
    uint64_t DestinationBase);
RC95LDR_STATUS Rc95LdrPeMapResource(const void *FileData, size_t FileSize,
    const RC95LDR_PE_INFO *Info, void *ImageBuffer, size_t ImageBufferSize,
    uint64_t DestinationBase);
#ifdef __cplusplus
}
#endif
#endif
