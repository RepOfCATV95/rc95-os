/*
 * PROJECT:   CATV 95 Kernel
 * LICENSE:   GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:   Header for Hibernation support
 * COPYRIGHT: Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#ifndef _RC95_HIBERFIL_H_
#define _RC95_HIBERFIL_H_

#include <ntdef.h>
#include <ndk/mmtypes.h>

#define HIBERFILE_NAME L"\\hiberfil.sys"
#define HIBERFILE_SIGNATURE 0x72626968UL /* "hibr" */
#define HIBERFILE_VERSION 4
#define HIBERFILE_CPU_STATE_VERSION 1
#define HIBERFILE_CPU_STATE_MAX_SIZE 2048

#define HIBERFILE_FLAG_HAS_RTC_TIME       0x00000001
#define HIBERFILE_FLAG_IMAGE_COMPLETE     0x00000002
#define HIBERFILE_FLAG_HAS_CPU_STATE      0x00000004
#define HIBERFILE_FLAG_UNIPROCESSOR       0x00000008
#define HIBERFILE_FLAG_HAS_FX_STATE       0x00000010
#define HIBERFILE_FLAG_HAS_FPU_STATE      0x00000020

#if defined(_M_IX86) || defined(_X86_)
#define HIBERFILE_MACHINE_TYPE 0x014C
#elif defined(_M_AMD64) || defined(_AMD64_)
#define HIBERFILE_MACHINE_TYPE 0x8664
#else
#define HIBERFILE_MACHINE_TYPE 0
#endif

typedef struct _PO_HIBER_RTC_TIME
{
    ULONG Year;
    ULONG Month;
    ULONG Day;
    ULONG Weekday;
    ULONG Hour;
    ULONG Minute;
    ULONG Second;
    ULONG Milliseconds;
} PO_HIBER_RTC_TIME, *PPO_HIBER_RTC_TIME;

typedef struct _PO_HIBER_IMAGE_HEADER
{
    ULONG Signature;
    ULONG Version;
    ULONG HeaderSize;
    ULONG MachineType;
    ULONG PageSize;
    ULONG PageCountLow;
    ULONG PageCountHigh;
    ULONG PayloadSizeLow;
    ULONG PayloadSizeHigh;
    ULONG Flags;
    ULONG CheckSum;
    PO_HIBER_RTC_TIME RtcTime;
    ULONG CpuStateVersion;
    ULONG CpuStateSize;
    ULONG ProcessorCount;
    ULONG FxStateOffset;
    ULONG FxStateSize;
    ULONG Reserved;
    ULONGLONG ResumeFlagPhysicalAddress;
    UCHAR CpuState[HIBERFILE_CPU_STATE_MAX_SIZE];
} PO_HIBER_IMAGE_HEADER, *PPO_HIBER_IMAGE_HEADER;

/* Each chunk header is followed by Length bytes of physical memory data. */
typedef struct _PO_HIBER_MEMORY_CHUNK
{
    ULONGLONG PhysicalAddress;
    ULONG Length;
    ULONG CheckSum;
} PO_HIBER_MEMORY_CHUNK, *PPO_HIBER_MEMORY_CHUNK;

typedef struct _PO_HIBER_PERF
{
    ULONGLONG IoTicks;
    ULONGLONG InitTicks;
    ULONGLONG CopyTicks;
    ULONGLONG StartCount;
    ULONG ElapsedTime;
    ULONG IoTime;
    ULONG CopyTime;
    ULONG InitTime;
    ULONG PagesWritten;
    ULONG PagesProcessed;
    ULONG BytesCopied;
    ULONG DumpCount;
    ULONG FileRuns;
} PO_HIBER_PERF, *PPO_HIBER_PERF;

typedef struct _PO_MEMORY_IMAGE
{
    ULONG Signature;
    ULONG Version;
    ULONG CheckSum;
    ULONG LengthSelf;
    PFN_NUMBER PageSelf;
    ULONG PageSize;
    ULONG ImageType;
    LARGE_INTEGER SystemTime;
    ULONGLONG InterruptTime;
    ULONG FeatureFlags;
    UCHAR HiberFlags;
    UCHAR spare[3];
    ULONG NoHiberPtes;
    ULONG_PTR HiberVa;
    PHYSICAL_ADDRESS HiberPte;
    ULONG NoFreePages;
    ULONG FreeMapCheck;
    ULONG WakeCheck;
    PFN_NUMBER TotalPages;
    PFN_NUMBER FirstTablePage;
    PFN_NUMBER LastFilePage;
    PO_HIBER_PERF PerfInfo;
} PO_MEMORY_IMAGE, *PPO_MEMORY_IMAGE;

typedef struct _PO_MEMORY_RANGE_ARRAY_RANGE
{
    PFN_NUMBER PageNo;
    PFN_NUMBER StartPage;
    PFN_NUMBER EndPage;
    ULONG CheckSum;
} PO_MEMORY_RANGE_ARRAY_RANGE;

typedef struct _PO_MEMORY_RANGE_ARRAY_LINK
{
    struct _PO_MEMORY_RANGE_ARRAY *Next;
    PFN_NUMBER NextTable;
    ULONG CheckSum;
    ULONG EntryCount;
} PO_MEMORY_RANGE_ARRAY_LINK;

typedef struct _PO_MEMORY_RANGE_ARRAY
{
    union
    {
        PO_MEMORY_RANGE_ARRAY_RANGE Range;
        PO_MEMORY_RANGE_ARRAY_LINK Link;
    };
} PO_MEMORY_RANGE_ARRAY, *PPO_MEMORY_RANGE_ARRAY;

#endif /* _RC95_HIBERFIL_H_ */
