/*
 * PROJECT:    RC95-OS libwim tests
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Verify raw WIM writer output with the independent container reader.
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libwim/wim_read.h>
#include <libwim/wim_write.h>

static void *TestAllocate(void *Context, size_t Size)
{
    (void)Context;
    return malloc(Size);
}

static void TestFree(void *Context, void *Allocation)
{
    (void)Context;
    free(Allocation);
}

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "libwim writer test failed at line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

int main(void)
{
    uint8_t Image[2048];
    const char Metadata[] = "metadata-resource";
    const char Xml[] = "<WIM><IMAGE INDEX=\"1\"/></WIM>";
    char Decoded[sizeof(Metadata)];
    WIM_WRITE_BLOB Blob;
    WIM_WRITER_OPTIONS WriterOptions;
    WIM_READER_OPTIONS ReaderOptions;
    WIM_IO *Output = NULL;
    WIM_READER *Reader = NULL;
    WIM_HEADER_INFO Header;
    WIM_IO_STATUS IoStatus;
    WIM_WRITER_STATUS WriterStatus;
    WIM_READER_STATUS ReaderStatus;
    size_t LookupCount = 0, Used = 0;

    memset(Image, 0, sizeof(Image));
    Blob.Data = Metadata;
    Blob.Size = sizeof(Metadata);
    Blob.ReferenceCount = 1;
    Blob.Flags = WIM_RESOURCE_FLAG_METADATA;

    memset(&WriterOptions, 0, sizeof(WriterOptions));
    WriterOptions.StructureSize = sizeof(WriterOptions);
    WriterOptions.ImageCount = 1;
    WriterOptions.Blobs = &Blob;
    WriterOptions.BlobCount = 1;
    WriterOptions.XmlData = Xml;
    WriterOptions.XmlSize = sizeof(Xml);

    IoStatus = WimIoOpenMemoryWrite(&Output, Image, sizeof(Image), 0);
    CHECK(IoStatus == WIM_IO_OK);
    WriterStatus = WimWriterWrite(Output, &WriterOptions);
    CHECK(WriterStatus == WIM_WRITER_OK);
    WimIoClose(Output);

    memset(&ReaderOptions, 0, sizeof(ReaderOptions));
    ReaderOptions.StructureSize = sizeof(ReaderOptions);
    ReaderOptions.Allocator.Allocate = TestAllocate;
    ReaderOptions.Allocator.Free = TestFree;
    ReaderStatus = WimReaderOpenMemory(&Reader, Image, sizeof(Image),
                                       &ReaderOptions);
    CHECK(ReaderStatus == WIM_READER_OK);
    CHECK(WimReaderGetHeaderInfo(Reader, &Header) == WIM_READER_OK);
    CHECK(Header.ImageCount == 1 && Header.PartNumber == 1 &&
          Header.TotalParts == 1);
    CHECK(WimReaderGetLookupEntryCount(Reader, &LookupCount) == WIM_READER_OK);
    CHECK(LookupCount == 1);
    ReaderStatus = WimReaderReadLookupResource(Reader, 0,
        (uint8_t *)Decoded, sizeof(Decoded), &Used);
    CHECK(ReaderStatus == WIM_READER_OK && Used == sizeof(Metadata));
    CHECK(memcmp(Decoded, Metadata, sizeof(Metadata)) == 0);
    WimReaderClose(Reader);
    puts("libwim writer tests passed");
    return 0;
}
