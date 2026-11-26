/*
 * PROJECT:    RC95-OS libwim tests
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Exercise bounded memory stream I/O and exact-read behavior.
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95
 */

#include <stdio.h>
#include <string.h>
#include <libwim/wim_io.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "libwim I/O test failed at line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

int main(void)
{
    uint8_t Buffer[8] = {0};
    const char Payload[] = "WIMio";
    char ReadBack[sizeof(Payload)] = {0};
    WIM_IO *Io = NULL;
    size_t Count = 0;
    uint64_t Position = 0, Size = 0;

    CHECK(WimIoOpenMemoryWrite(&Io, Buffer, sizeof(Buffer), 0) == WIM_IO_OK);
    CHECK(WimIoWriteExact(Io, Payload, sizeof(Payload)) == WIM_IO_OK);
    CHECK(WimIoTell(Io, &Position) == WIM_IO_OK && Position == sizeof(Payload));
    CHECK(WimIoGetSize(Io, &Size) == WIM_IO_OK && Size == sizeof(Payload));
    CHECK(WimIoSeek(Io, 0, WIM_IO_SEEK_BEGIN) == WIM_IO_OK);
    CHECK(WimIoSeek(Io, 3, WIM_IO_SEEK_BEGIN) == WIM_IO_OK);
    CHECK(WimIoWrite(Io, Payload, sizeof(Payload), &Count) == WIM_IO_OUT_OF_RANGE);
    WimIoClose(Io);

    Io = NULL;
    CHECK(WimIoOpenMemory(&Io, Buffer, sizeof(Payload)) == WIM_IO_OK);
    CHECK(WimIoReadExact(Io, ReadBack, sizeof(ReadBack)) == WIM_IO_OK);
    CHECK(memcmp(ReadBack, Payload, sizeof(Payload)) == 0);
    CHECK(WimIoRead(Io, ReadBack, 1, &Count) == WIM_IO_OK && Count == 0);
    CHECK(WimIoReadExact(Io, ReadBack, 1) == WIM_IO_EOF);
    CHECK(WimIoSeek(Io, -1, WIM_IO_SEEK_BEGIN) == WIM_IO_OUT_OF_RANGE);
    WimIoClose(Io);

    {
        const char *Path = "libwim-io-test.tmp";
        Io = NULL;
        memset(ReadBack, 0, sizeof(ReadBack));
        CHECK(WimIoOpenFile(&Io, Path, WIM_IO_FILE_CREATE) == WIM_IO_OK);
        CHECK(WimIoWriteExact(Io, Payload, sizeof(Payload)) == WIM_IO_OK);
        CHECK(WimIoFlush(Io) == WIM_IO_OK);
        CHECK(WimIoSeek(Io, 0, WIM_IO_SEEK_BEGIN) == WIM_IO_OK);
        CHECK(WimIoReadExact(Io, ReadBack, sizeof(ReadBack)) == WIM_IO_OK);
        CHECK(memcmp(ReadBack, Payload, sizeof(Payload)) == 0);
        CHECK(WimIoGetSize(Io, &Size) == WIM_IO_OK && Size == sizeof(Payload));
        WimIoClose(Io);
        CHECK(remove(Path) == 0);
    }
    puts("libwim I/O tests passed");
    return 0;
}
