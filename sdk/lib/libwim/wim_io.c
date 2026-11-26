/*
 * PROJECT:    RC95-OS libwim
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Public file and memory I/O interface for WIM streams.
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <libwim/wim_io.h>

#if defined(_WIN32) && defined(__REACTOS__)
/*
 * The in-tree ReactOS CRT does not currently export the Microsoft 64-bit
 * stdio seek entry points. Keep target-side file streams linkable; the
 * memory-backed reader remains fully size_t-bounded. Refuse offsets that
 * cannot be represented by the target CRT's long-based fseek interface.
 */
#define WIM_FSEEK fseek
#define WIM_FTELL ftell
#elif defined(_WIN32)
#include <io.h>
#define WIM_FSEEK _fseeki64
#define WIM_FTELL _ftelli64
#else
#include <sys/types.h>
#define WIM_FSEEK fseeko
#define WIM_FTELL ftello
#endif

typedef enum _WIM_IO_KIND {
    WIM_IO_KIND_FILE,
    WIM_IO_KIND_MEMORY_READ,
    WIM_IO_KIND_MEMORY_WRITE
} WIM_IO_KIND;

struct _WIM_IO {
    WIM_IO_KIND Kind;
    FILE *File;
    uint8_t *Memory;
    uint64_t Size;
    uint64_t Capacity;
    uint64_t Position;
};

static WIM_IO_STATUS
WimIoAllocate(WIM_IO **Io)
{
    WIM_IO *NewIo;
    if (!Io)
        return WIM_IO_INVALID_ARGUMENT;
    *Io = NULL;
    NewIo = (WIM_IO *)calloc(1, sizeof(*NewIo));
    if (!NewIo)
        return WIM_IO_OUT_OF_MEMORY;
    *Io = NewIo;
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoOpenFile(WIM_IO **Io, const char *Path, WIM_IO_FILE_MODE Mode)
{
    const char *FileMode;
    WIM_IO_STATUS Status;
    WIM_IO *NewIo;

    if (!Io || !Path || !Path[0])
        return WIM_IO_INVALID_ARGUMENT;
    switch (Mode) {
    case WIM_IO_FILE_READ: FileMode = "rb"; break;
    case WIM_IO_FILE_WRITE: FileMode = "w+b"; break;
    case WIM_IO_FILE_READ_WRITE: FileMode = "r+b"; break;
    case WIM_IO_FILE_CREATE: FileMode = "w+b"; break;
    default: return WIM_IO_INVALID_ARGUMENT;
    }

    Status = WimIoAllocate(&NewIo);
    if (Status != WIM_IO_OK)
        return Status;
    NewIo->File = fopen(Path, FileMode);
    if (!NewIo->File) {
        free(NewIo);
        *Io = NULL;
        return WIM_IO_OPEN_FAILED;
    }
    NewIo->Kind = WIM_IO_KIND_FILE;
    *Io = NewIo;
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoOpenMemory(WIM_IO **Io, const void *Buffer, size_t Size)
{
    WIM_IO_STATUS Status;
    WIM_IO *NewIo;
    if (!Io || (!Buffer && Size))
        return WIM_IO_INVALID_ARGUMENT;
    Status = WimIoAllocate(&NewIo);
    if (Status != WIM_IO_OK)
        return Status;
    NewIo->Kind = WIM_IO_KIND_MEMORY_READ;
    NewIo->Memory = (uint8_t *)(uintptr_t)Buffer;
    NewIo->Size = Size;
    NewIo->Capacity = Size;
    *Io = NewIo;
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoOpenMemoryWrite(WIM_IO **Io, void *Buffer, size_t Capacity,
                     size_t InitialSize)
{
    WIM_IO_STATUS Status;
    WIM_IO *NewIo;
    if (!Io || (!Buffer && Capacity) || InitialSize > Capacity)
        return WIM_IO_INVALID_ARGUMENT;
    Status = WimIoAllocate(&NewIo);
    if (Status != WIM_IO_OK)
        return Status;
    NewIo->Kind = WIM_IO_KIND_MEMORY_WRITE;
    NewIo->Memory = (uint8_t *)Buffer;
    NewIo->Size = InitialSize;
    NewIo->Capacity = Capacity;
    *Io = NewIo;
    return WIM_IO_OK;
}

void
WimIoClose(WIM_IO *Io)
{
    if (!Io)
        return;
    if (Io->File)
        fclose(Io->File);
    memset(Io, 0, sizeof(*Io));
    free(Io);
}

WIM_IO_STATUS
WimIoRead(WIM_IO *Io, void *Buffer, size_t Requested, size_t *ReadCount)
{
    size_t Count;
    if (ReadCount)
        *ReadCount = 0;
    if (!Io || !ReadCount || (!Buffer && Requested))
        return WIM_IO_INVALID_ARGUMENT;
    if (Io->Kind == WIM_IO_KIND_MEMORY_WRITE)
        return WIM_IO_READ_FAILED;
    if (Io->Kind == WIM_IO_KIND_FILE) {
        Count = Requested ? fread(Buffer, 1, Requested, Io->File) : 0;
        *ReadCount = Count;
        if (Count < Requested && ferror(Io->File))
            return WIM_IO_READ_FAILED;
        return WIM_IO_OK;
    }
    if (Io->Position >= Io->Size || Requested == 0)
        return WIM_IO_OK;
    if ((uint64_t)Requested > Io->Size - Io->Position)
        Requested = (size_t)(Io->Size - Io->Position);
    memcpy(Buffer, Io->Memory + (size_t)Io->Position, Requested);
    Io->Position += Requested;
    *ReadCount = Requested;
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoReadExact(WIM_IO *Io, void *Buffer, size_t Requested)
{
    size_t Total = 0, Count;
    WIM_IO_STATUS Status;
    if (!Io || (!Buffer && Requested))
        return WIM_IO_INVALID_ARGUMENT;
    while (Total < Requested) {
        Status = WimIoRead(Io, (uint8_t *)Buffer + Total,
                           Requested - Total, &Count);
        if (Status != WIM_IO_OK)
            return Status;
        if (!Count)
            return WIM_IO_EOF;
        Total += Count;
    }
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoWrite(WIM_IO *Io, const void *Buffer, size_t Requested,
           size_t *WrittenCount)
{
    size_t Count;
    if (WrittenCount)
        *WrittenCount = 0;
    if (!Io || !WrittenCount || (!Buffer && Requested))
        return WIM_IO_INVALID_ARGUMENT;
    if (Io->Kind == WIM_IO_KIND_MEMORY_READ)
        return WIM_IO_READ_ONLY;
    if (Io->Kind == WIM_IO_KIND_FILE) {
        Count = Requested ? fwrite(Buffer, 1, Requested, Io->File) : 0;
        *WrittenCount = Count;
        return Count == Requested ? WIM_IO_OK : WIM_IO_WRITE_FAILED;
    }
    if (Io->Position > Io->Capacity ||
        (uint64_t)Requested > Io->Capacity - Io->Position)
        return WIM_IO_OUT_OF_RANGE;
    if (Requested)
        memcpy(Io->Memory + (size_t)Io->Position, Buffer, Requested);
    Io->Position += Requested;
    if (Io->Position > Io->Size)
        Io->Size = Io->Position;
    *WrittenCount = Requested;
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoWriteExact(WIM_IO *Io, const void *Buffer, size_t Requested)
{
    size_t Total = 0, Count;
    WIM_IO_STATUS Status;
    if (!Io || (!Buffer && Requested))
        return WIM_IO_INVALID_ARGUMENT;
    while (Total < Requested) {
        Status = WimIoWrite(Io, (const uint8_t *)Buffer + Total,
                            Requested - Total, &Count);
        Total += Count;
        if (Status != WIM_IO_OK)
            return Status;
        if (!Count)
            return WIM_IO_WRITE_FAILED;
    }
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoSeek(WIM_IO *Io, int64_t Offset, WIM_IO_SEEK_ORIGIN Origin)
{
    int Whence;
    uint64_t Base, Target;
    if (!Io)
        return WIM_IO_INVALID_ARGUMENT;
    switch (Origin) {
    case WIM_IO_SEEK_BEGIN: Whence = SEEK_SET; Base = 0; break;
    case WIM_IO_SEEK_CURRENT: Whence = SEEK_CUR; Base = 0; break;
    case WIM_IO_SEEK_END: Whence = SEEK_END; Base = 0; break;
    default: return WIM_IO_INVALID_ARGUMENT;
    }
    if (Io->Kind == WIM_IO_KIND_FILE) {
#if defined(_WIN32) && defined(__REACTOS__)
        if (Offset > LONG_MAX || Offset < LONG_MIN)
            return WIM_IO_OUT_OF_RANGE;
#endif
#if defined(_WIN32) && defined(__REACTOS__)
        if (WIM_FSEEK(Io->File, (long)Offset, Whence) != 0)
#else
        if (WIM_FSEEK(Io->File, (int64_t)Offset, Whence) != 0)
#endif
            return WIM_IO_SEEK_FAILED;
        return WIM_IO_OK;
    }
    if (Origin == WIM_IO_SEEK_CURRENT)
        Base = Io->Position;
    else if (Origin == WIM_IO_SEEK_END)
        Base = Io->Size;
    if (Offset < 0) {
        uint64_t Magnitude = (uint64_t)(-(Offset + 1)) + 1;
        if (Magnitude > Base)
            return WIM_IO_OUT_OF_RANGE;
        Target = Base - Magnitude;
    } else {
        if ((uint64_t)Offset > UINT64_MAX - Base)
            return WIM_IO_OUT_OF_RANGE;
        Target = Base + (uint64_t)Offset;
    }
    if (Target > (Io->Kind == WIM_IO_KIND_MEMORY_WRITE
                      ? Io->Capacity : Io->Size))
        return WIM_IO_OUT_OF_RANGE;
    Io->Position = Target;
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoTell(WIM_IO *Io, uint64_t *Position)
{
    if (!Io || !Position)
        return WIM_IO_INVALID_ARGUMENT;
    if (Io->Kind == WIM_IO_KIND_FILE) {
        int64_t Value = (int64_t)WIM_FTELL(Io->File);
        if (Value < 0)
            return WIM_IO_SEEK_FAILED;
        *Position = (uint64_t)Value;
    } else {
        *Position = Io->Position;
    }
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoGetSize(WIM_IO *Io, uint64_t *Size)
{
    if (!Io || !Size)
        return WIM_IO_INVALID_ARGUMENT;
    if (Io->Kind != WIM_IO_KIND_FILE) {
        *Size = Io->Size;
        return WIM_IO_OK;
    }
    {
        int64_t Current = (int64_t)WIM_FTELL(Io->File);
        int64_t End;
        if (Current < 0 || WIM_FSEEK(Io->File, 0, SEEK_END) != 0)
            return WIM_IO_SEEK_FAILED;
        End = (int64_t)WIM_FTELL(Io->File);
        if (End < 0 || WIM_FSEEK(Io->File, Current, SEEK_SET) != 0)
            return WIM_IO_SEEK_FAILED;
        *Size = (uint64_t)End;
    }
    return WIM_IO_OK;
}

WIM_IO_STATUS
WimIoFlush(WIM_IO *Io)
{
    if (!Io)
        return WIM_IO_INVALID_ARGUMENT;
    if (Io->Kind != WIM_IO_KIND_FILE)
        return WIM_IO_OK;
    return fflush(Io->File) == 0 ? WIM_IO_OK : WIM_IO_FLUSH_FAILED;
}
