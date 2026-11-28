#ifndef RC95LDR_HOST_NTDEF_H
#define RC95LDR_HOST_NTDEF_H

/*
 * Minimal host-test shim for the fixed-width primitive and layout types
 * consumed by sdk/include/reactos/arc/arc.h. This file is visible only to
 * the standalone RC95LDR host-test build. Production builds use the
 * generated SDK ntdef.h and never include this shim.
 */
#include <stddef.h>
#include <stdint.h>

typedef uint8_t BOOLEAN;
typedef char CHAR;
typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint16_t WCHAR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int32_t NTSTATUS;
typedef uint64_t ULONG64;
typedef uint64_t ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef void VOID;
typedef void *PVOID;
typedef CHAR *PCHAR;
typedef CHAR *PSTR;
typedef UCHAR *PUCHAR;

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    WCHAR *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;

typedef union _LARGE_INTEGER {
    struct {
        uint32_t LowPart;
        int32_t HighPart;
    } Parts;
    int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _DEBUG_DEVICE_DESCRIPTOR DEBUG_DEVICE_DESCRIPTOR;

#ifndef _Field_size_bytes_
#define _Field_size_bytes_(size)
#endif
#ifndef ANYSIZE_ARRAY
#define ANYSIZE_ARRAY 1
#endif

/*
 * EDK2's Uefi.h is layered on the NT/ReactOS base definitions in this tree.
 * The standalone host build has no generated SDK ntdef.h, so provide the
 * annotation and inline spellings consumed by the checked-in EDK2 headers.
 */
#ifndef IN
#define IN
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef _In_
#define _In_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif
#ifndef _In_reads_
#define _In_reads_(count)
#endif
#ifndef _Out_writes_
#define _Out_writes_(count)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(count)
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef FORCEINLINE
#define FORCEINLINE static inline
#endif
#ifndef TRUE
#define TRUE ((BOOLEAN)1)
#endif
#ifndef FALSE
#define FALSE ((BOOLEAN)0)
#endif

/* Keep optional post-NT5 ARC extension fields out of this narrow shim. */
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x05010000
#endif
#define NTDDI_WINXPSP1 0x05010200
#define NTDDI_WS03 0x05020000
#define NTDDI_WS03SP1 0x05020100
#define NTDDI_LONGHORN 0x06000000
#define NTDDI_WIN7 0x06010000
#define NTDDI_WIN8 0x06020000
#define NTDDI_WINBLUE 0x06030000
#define NTDDI_WIN10 0x0A000000
#ifndef OSVER
#define OSVER(version) ((version) & 0xffff0000)
#endif

#endif
