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
typedef enum _RC95LDR_PE_FORMAT { Rc95LdrPe32=1, Rc95LdrPe32Plus=2 } RC95LDR_PE_FORMAT;
typedef struct _RC95LDR_PE_INFO {
    RC95LDR_PE_FORMAT Format;
    uint16_t Machine, SectionCount, Characteristics;
    uint32_t EntryPointRva, SizeOfImage, SizeOfHeaders, SectionAlignment, FileAlignment;
    uint64_t PreferredImageBase;
    uint32_t RelocationRva, RelocationSize, ImportRva, ImportSize;
} RC95LDR_PE_INFO;
RC95LDR_STATUS Rc95LdrPeValidate(const void *FileData, size_t FileSize,
    uint16_t ExpectedMachine, RC95LDR_PE_INFO *Info);
/* Maps into a caller-owned, image-sized buffer. DestinationBase is the
   address at which that buffer will execute; it must match its actual address
   in the boot environment. Imports are rejected until a resolver is supplied. */
RC95LDR_STATUS Rc95LdrPeMap(const void *FileData, size_t FileSize,
    const RC95LDR_PE_INFO *Info, void *ImageBuffer, size_t ImageBufferSize,
    uint64_t DestinationBase);
#ifdef __cplusplus
}
#endif
#endif
