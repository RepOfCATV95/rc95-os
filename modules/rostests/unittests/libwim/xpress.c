/*
 * PROJECT:    RC95-OS libwim tests
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Exercise bounded XPRESS LZ77 encoding and decoding.
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libwim/wim_codec.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "libwim XPRESS test failed at line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

static void *
TestAllocate(void *Context, size_t Size)
{
    (void)Context;
    return malloc(Size);
}

static void
TestFree(void *Context, void *Allocation)
{
    (void)Context;
    free(Allocation);
}

static int
Decode(const uint8_t *Input, size_t InputSize, size_t ExpectedSize,
       uint8_t *Output, size_t OutputSize, WIM_CODEC_STATUS ExpectedStatus,
       size_t ExpectedInputUsed, size_t ExpectedOutputUsed)
{
    WIM_CODEC_OPTIONS Options;
    WIM_CODEC_INSTANCE *Instance = NULL;
    WIM_CODEC_STATUS Status;
    size_t InputUsed = 0, OutputUsed = 0;

    memset(&Options, 0, sizeof(Options));
    Options.StructureSize = sizeof(Options);
    Options.ExpectedOutputSize = ExpectedSize;
    Options.MaximumOutputSize = ExpectedSize;
    Options.Allocator.Allocate = TestAllocate;
    Options.Allocator.Free = TestFree;

    Status = WimCodecOpen(&Instance, WIM_CODEC_XPRESS, WIM_CODEC_DECODE,
                          &Options);
    if (Status != WIM_CODEC_STATUS_OK)
        return 0;

    Status = WimCodecProcess(Instance, Input, InputSize, &InputUsed,
                             Output, OutputSize, &OutputUsed,
                             WIM_CODEC_PROCESS_FINAL);
    WimCodecClose(Instance);

    return Status == ExpectedStatus &&
           InputUsed == ExpectedInputUsed &&
           OutputUsed == ExpectedOutputUsed;
}

static int
Encode(const uint8_t *Input, size_t InputSize, uint8_t *Output,
       size_t OutputSize, size_t *EncodedSize)
{
    WIM_CODEC_OPTIONS Options;
    WIM_CODEC_INSTANCE *Instance = NULL;
    WIM_CODEC_STATUS Status;
    size_t InputUsed = 0, OutputUsed = 0;

    memset(&Options, 0, sizeof(Options));
    Options.StructureSize = sizeof(Options);
    Options.MaximumOutputSize = OutputSize;
    Options.Allocator.Allocate = TestAllocate;
    Options.Allocator.Free = TestFree;

    Status = WimCodecOpen(&Instance, WIM_CODEC_XPRESS, WIM_CODEC_ENCODE,
                          &Options);
    if (Status != WIM_CODEC_STATUS_OK)
        return 0;
    Status = WimCodecProcess(Instance, Input, InputSize, &InputUsed,
                             Output, OutputSize, &OutputUsed,
                             WIM_CODEC_PROCESS_FINAL);
    WimCodecClose(Instance);
    *EncodedSize = OutputUsed;
    return Status == WIM_CODEC_STATUS_STREAM_END &&
           InputUsed == InputSize;
}

int
main(void)
{
    /* Three literals followed by a distance-three, length-three match. */
    static const uint8_t MatchVector[] = {
        0x08, 0x00, 0x00, 0x00, 'A', 'B', 'C', 0x10, 0x00
    };
    static const uint8_t LiteralVector[] = {
        0x00, 0x00, 0x00, 0x00, 'W', 'I', 'M'
    };
    static const uint8_t BadDistanceVector[] = {
        0x01, 0x00, 0x00, 0x00, 0x10, 0x00
    };
    static const uint8_t Extended16Vector[] = {
        0x02, 0x00, 0x00, 0x00, 'Z', 0x07, 0x00, 0xFF, 0x02, 0x00
    };
    static const uint8_t Extended32Vector[] = {
        0x02, 0x00, 0x00, 0x00, 'Z', 0x07, 0x00,
        0xFF, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00
    };
    static const uint8_t TruncatedVector[] = {
        0x01, 0x00, 0x00, 0x00, 0x07, 0x00
    };
    uint8_t Output[64] = {0};
    uint8_t Encoded[64];
    size_t EncodedSize = 0;

    CHECK((WimCodecGetCapabilities(WIM_CODEC_XPRESS) &
           (WIM_CODEC_CAP_DECODE | WIM_CODEC_CAP_ENCODE)) ==
          (WIM_CODEC_CAP_DECODE | WIM_CODEC_CAP_ENCODE));

    CHECK(Decode(MatchVector, sizeof(MatchVector), 6, Output,
                 sizeof(Output), WIM_CODEC_STATUS_STREAM_END,
                 sizeof(MatchVector), 6));
    CHECK(memcmp(Output, "ABCABC", 6) == 0);

    memset(Output, 0, sizeof(Output));
    CHECK(Decode(LiteralVector, sizeof(LiteralVector), 3, Output,
                 sizeof(Output), WIM_CODEC_STATUS_STREAM_END,
                 sizeof(LiteralVector), 3));
    CHECK(memcmp(Output, "WIM", 3) == 0);

    memset(Output, 0, sizeof(Output));
    CHECK(Decode(Extended16Vector, sizeof(Extended16Vector), 13, Output,
                 sizeof(Output), WIM_CODEC_STATUS_STREAM_END,
                 sizeof(Extended16Vector), 13));
    CHECK(memcmp(Output, "ZZZZZZZZZZZZZ", 13) == 0);

    memset(Output, 0, sizeof(Output));
    CHECK(Decode(Extended32Vector, sizeof(Extended32Vector), 13, Output,
                 sizeof(Output), WIM_CODEC_STATUS_STREAM_END,
                 sizeof(Extended32Vector), 13));
    CHECK(memcmp(Output, "ZZZZZZZZZZZZZ", 13) == 0);

    CHECK(WimCodecMaximumCompressedSize(WIM_CODEC_XPRESS, 18, 32768) >= 18);
    CHECK(Encode((const uint8_t *)"ABCABCABCABCABCABC", 18,
                 Encoded, sizeof(Encoded), &EncodedSize));
    CHECK(EncodedSize <=
          WimCodecMaximumCompressedSize(WIM_CODEC_XPRESS, 18, 32768));
    memset(Output, 0, sizeof(Output));
    CHECK(Decode(Encoded, EncodedSize, 18, Output, sizeof(Output),
                 WIM_CODEC_STATUS_STREAM_END, EncodedSize, 18));
    CHECK(memcmp(Output, "ABCABCABCABCABCABC", 18) == 0);

    CHECK(Decode(BadDistanceVector, sizeof(BadDistanceVector), 3, Output,
                 sizeof(Output), WIM_CODEC_STATUS_CORRUPT_DATA, 0, 0));
    CHECK(Decode(TruncatedVector, sizeof(TruncatedVector), 10, Output,
                 sizeof(Output), WIM_CODEC_STATUS_CORRUPT_DATA, 0, 0));

    puts("libwim XPRESS tests passed");
    return 0;
}
