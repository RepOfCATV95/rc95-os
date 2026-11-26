/*
 * PROJECT:    RC95-OS libwim
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Public file and memory I/O interface for WIM streams.
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95
 */

#ifndef _LIBWIM_WIM_IO_H_
#define _LIBWIM_WIM_IO_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _WIM_IO WIM_IO;

typedef enum _WIM_IO_STATUS {
    WIM_IO_OK = 0,
    WIM_IO_INVALID_ARGUMENT,
    WIM_IO_OUT_OF_MEMORY,
    WIM_IO_OPEN_FAILED,
    WIM_IO_READ_FAILED,
    WIM_IO_WRITE_FAILED,
    WIM_IO_SEEK_FAILED,
    WIM_IO_FLUSH_FAILED,
    WIM_IO_EOF,
    WIM_IO_OUT_OF_RANGE,
    WIM_IO_READ_ONLY
} WIM_IO_STATUS;

typedef enum _WIM_IO_SEEK_ORIGIN {
    WIM_IO_SEEK_BEGIN = 0,
    WIM_IO_SEEK_CURRENT,
    WIM_IO_SEEK_END
} WIM_IO_SEEK_ORIGIN;

typedef enum _WIM_IO_FILE_MODE {
    WIM_IO_FILE_READ = 0,
    WIM_IO_FILE_WRITE,
    WIM_IO_FILE_READ_WRITE,
    WIM_IO_FILE_CREATE
} WIM_IO_FILE_MODE;

/*
 * File and memory streams own only their WIM_IO handle. Memory buffers remain
 * caller-owned and must outlive the stream. Writable memory streams are fixed
 * capacity: writes never grow or reallocate the supplied buffer.
 */
WIM_IO_STATUS WimIoOpenFile(WIM_IO **Io, const char *Path,
                            WIM_IO_FILE_MODE Mode);
WIM_IO_STATUS WimIoOpenMemory(WIM_IO **Io, const void *Buffer, size_t Size);
WIM_IO_STATUS WimIoOpenMemoryWrite(WIM_IO **Io, void *Buffer,
                                   size_t Capacity, size_t InitialSize);
void WimIoClose(WIM_IO *Io);

WIM_IO_STATUS WimIoRead(WIM_IO *Io, void *Buffer, size_t Requested,
                        size_t *ReadCount);
WIM_IO_STATUS WimIoReadExact(WIM_IO *Io, void *Buffer, size_t Requested);
WIM_IO_STATUS WimIoWrite(WIM_IO *Io, const void *Buffer, size_t Requested,
                         size_t *WrittenCount);
WIM_IO_STATUS WimIoWriteExact(WIM_IO *Io, const void *Buffer,
                              size_t Requested);
WIM_IO_STATUS WimIoSeek(WIM_IO *Io, int64_t Offset,
                        WIM_IO_SEEK_ORIGIN Origin);
WIM_IO_STATUS WimIoTell(WIM_IO *Io, uint64_t *Position);
WIM_IO_STATUS WimIoGetSize(WIM_IO *Io, uint64_t *Size);
WIM_IO_STATUS WimIoFlush(WIM_IO *Io);

#ifdef __cplusplus
}
#endif
#endif
