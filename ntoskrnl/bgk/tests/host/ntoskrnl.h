#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>

typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef int32_t LONG;
typedef uint32_t NTSTATUS;
typedef size_t SIZE_T;
typedef ULONG *PULONG;
typedef UCHAR *PUCHAR;
typedef void *PVOID;
typedef const wchar_t *PCWSTR;
typedef void VOID;
typedef int BOOLEAN;
typedef int INTERFACE_TYPE;
typedef INTERFACE_TYPE *PINTERFACE_TYPE;
typedef struct _CM_MONITOR_DEVICE_DATA { ULONG Reserved; } CM_MONITOR_DEVICE_DATA, *PCM_MONITOR_DEVICE_DATA;
typedef union _PHYSICAL_ADDRESS { int64_t QuadPart; } PHYSICAL_ADDRESS;
typedef PHYSICAL_ADDRESS *PPHYSICAL_ADDRESS;

#define NTAPI
#define _In_
#define _In_opt_
#define _In_reads_bytes_(x)
#define _In_reads_(x)
#define _Out_
#define _Inout_
#define MINLONG INT32_MIN
#define MAXULONG UINT32_MAX
#define TRUE 1
#define FALSE 0
#define NonPagedPool 0
#define PAGE_SIZE 4096u
#define MmWriteCombined 1
#define MmNonCached 0
#define STATUS_SUCCESS ((NTSTATUS)0x00000000u)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000Du)
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xC000007Bu)
#define STATUS_RESOURCE_DATA_NOT_FOUND ((NTSTATUS)0xC000008Bu)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3u)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023u)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001u)
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xC0000182u)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBu)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009Au)
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) < 0x80000000u)
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define ExAllocatePoolWithTag(Pool, Bytes, Tag) malloc((size_t)(Bytes))
#define ExFreePoolWithTag(Address, Tag) free((Address))

PVOID MmMapIoSpace(PHYSICAL_ADDRESS Address, SIZE_T NumberOfBytes, int CacheType);
VOID MmUnmapIoSpace(PVOID BaseAddress, SIZE_T NumberOfBytes);

typedef union _LARGE_INTEGER { int64_t QuadPart; } LARGE_INTEGER;
typedef struct _KDPC KDPC, *PKDPC;
typedef VOID (NTAPI *PKDEFERRED_ROUTINE)(PKDPC, PVOID, PVOID, PVOID);
struct _KDPC { PKDEFERRED_ROUTINE DeferredRoutine; PVOID DeferredContext; };
typedef struct _KTIMER { BOOLEAN Active; } KTIMER, *PKTIMER;
#define PASSIVE_LEVEL 0
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define ASSERT(x) assert(x)
#define DPRINT1(...) ((void)0)
VOID KeInitializeTimer(PKTIMER Timer);
VOID KeInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE Routine, PVOID Context);
BOOLEAN KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime, LONG Period, PKDPC Dpc);
BOOLEAN KeCancelTimer(PKTIMER Timer);
VOID KeFlushQueuedDpcs(VOID);
VOID KeStallExecutionProcessor(ULONG MicroSeconds);
ULONGLONG KeQueryInterruptTime(VOID);
ULONG KeGetCurrentIrql(VOID);
LONG InterlockedCompareExchange(volatile LONG *Destination, LONG Exchange, LONG Comparand);
LONG InterlockedExchange(volatile LONG *Destination, LONG Value);
