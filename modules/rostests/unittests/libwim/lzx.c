/*
 * PROJECT:    RC95-OS libwim tests
 * LICENSE:    GPL-3.0-or-later
 * PURPOSE:    Validate the WIM-specific LZX chunk decoder.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libwim/wim_codec.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "libwim WIM-LZX test failed at line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

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

/*
 * One WIM uncompressed LZX block:
 *   type=3, size-selector=0, block-size=4
 *   recent offsets={1,1,1}
 *   payload="LZX!"
 * The first two words contain the 20-bit block header.
 */
static const uint8_t UncompressedChunk[] = {
    0x00, 0x60, 0x00, 0x40,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    'L', 'Z', 'X', '!'
};

/* Verbatim WIM LZX block with a complete two-symbol literal tree ("ABAB"). */
static const uint8_t VerbatimChunk[] = {
    0x00, 0x20, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x01,
    0xf5, 0x0f, 0xff, 0x1f, 0xe0, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x41, 0x24, 0xff, 0xff, 0xfc, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x88, 0x04, 0xff, 0x0f, 0xff, 0xff, 0xa0, 0xee
};

/* Two uncompressed blocks in one WIM chunk; chunk state must continue. */
static const uint8_t MultiBlockChunk[] = {
    0x00, 0x60, 0x00, 0x20,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    'A', 'B',
    0x00, 0x60, 0x00, 0x20,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    'C', 'D'
};

/* E8 at output offset 2, absolute target 20: undo maps it to 20 - 2 = 18. */
static const uint8_t E8Chunk[] = {
    0x00, 0x60, 0x00, 0x10,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    'x', 'x', 0xE8, 0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static WIM_CODEC_STATUS
Decode(
    const uint8_t *Input,
    size_t InputSize,
    uint8_t *Output,
    size_t OutputSize,
    size_t *InputUsed,
    size_t *OutputUsed)
{
    WIM_CODEC_OPTIONS Options;
    WIM_CODEC_INSTANCE *Instance = NULL;
    WIM_CODEC_STATUS Status;

    memset(&Options, 0, sizeof(Options));
    Options.StructureSize = sizeof(Options);
    Options.ExpectedOutputSize = OutputSize;
    Options.MaximumOutputSize = OutputSize;
    Options.ChunkSize = 32768;
    Options.Allocator.Allocate = TestAllocate;
    Options.Allocator.Free = TestFree;

    Status = WimCodecOpen(&Instance, WIM_CODEC_LZX, WIM_CODEC_DECODE,
                          &Options);
    if (Status != WIM_CODEC_STATUS_OK)
        return Status;
    Status = WimCodecProcess(Instance, Input, InputSize, InputUsed,
                             Output, OutputSize, OutputUsed,
                             WIM_CODEC_PROCESS_FINAL);
    WimCodecClose(Instance);
    return Status;
}

int main(void)
{
    uint8_t Output[16] = {0};
    size_t InputUsed = 0, OutputUsed = 0;
    uint32_t Capabilities = WimCodecGetCapabilities(WIM_CODEC_LZX);

    CHECK((Capabilities & (WIM_CODEC_CAP_DECODE | WIM_CODEC_CAP_WIM_CHUNKS)) ==
          (WIM_CODEC_CAP_DECODE | WIM_CODEC_CAP_WIM_CHUNKS));
    CHECK((Capabilities & (WIM_CODEC_CAP_ENCODE | WIM_CODEC_CAP_STREAMING)) == 0);

    CHECK(Decode(UncompressedChunk, sizeof(UncompressedChunk),
                 Output, 4, &InputUsed, &OutputUsed) ==
          WIM_CODEC_STATUS_STREAM_END);
    CHECK(InputUsed == sizeof(UncompressedChunk));
    CHECK(OutputUsed == 4);
    CHECK(memcmp(Output, "LZX!", 4) == 0);

    InputUsed = OutputUsed = 0;
    CHECK(Decode(UncompressedChunk, sizeof(UncompressedChunk) - 1,
                 Output, 4, &InputUsed, &OutputUsed) ==
          WIM_CODEC_STATUS_CORRUPT_DATA);
    CHECK(InputUsed == 0 && OutputUsed == 0);

    InputUsed = OutputUsed = 0;
    memset(Output, 0, sizeof(Output));
    CHECK(Decode(VerbatimChunk, sizeof(VerbatimChunk),
                 Output, 4, &InputUsed, &OutputUsed) ==
          WIM_CODEC_STATUS_STREAM_END);
    CHECK(InputUsed == sizeof(VerbatimChunk));
    CHECK(OutputUsed == 4);
    CHECK(memcmp(Output, "ABAB", 4) == 0);

    InputUsed = OutputUsed = 0;
    memset(Output, 0, sizeof(Output));
    CHECK(Decode(MultiBlockChunk, sizeof(MultiBlockChunk),
                 Output, 4, &InputUsed, &OutputUsed) ==
          WIM_CODEC_STATUS_STREAM_END);
    CHECK(InputUsed == sizeof(MultiBlockChunk));
    CHECK(OutputUsed == 4);
    CHECK(memcmp(Output, "ABCD", 4) == 0);

    InputUsed = OutputUsed = 0;
    memset(Output, 0, sizeof(Output));
    CHECK(Decode(E8Chunk, sizeof(E8Chunk),
                 Output, 16, &InputUsed, &OutputUsed) ==
          WIM_CODEC_STATUS_STREAM_END);
    CHECK(InputUsed == sizeof(E8Chunk));
    CHECK(OutputUsed == 16);
    CHECK(Output[2] == 0xE8);
    CHECK(Output[3] == 18 && Output[4] == 0 &&
          Output[5] == 0 && Output[6] == 0);

    puts("libwim WIM-LZX tests passed");
    return 0;
}
