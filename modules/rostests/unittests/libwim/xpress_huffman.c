/*
 * PROJECT:    RC95-OS libwim tests
 * LICENSE:    GPL-3.0-or-later
 * PURPOSE:    Validate XPRESS-Huffman against an independent compressed vector.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libwim/wim_codec.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "libwim XPRESS-Huffman test failed at line %d: %s\n", __LINE__, #x); \
    return 1; \
} } while (0)

static const uint8_t SmallVector[] = {
    0x07,     0x78,     0x08,     0x00,     0x08,     0x80,     0x87,     0x00,     0x00,     0x00,     0x88,     0x00,
    0x08,     0x07,     0x08,     0x87,     0x80,     0x00,     0x70,     0x00,     0x80,     0x80,     0x00,     0x88,
    0x00,     0x70,     0x00,     0x00,     0x88,     0x80,     0x00,     0x00,     0x00,     0x00,     0x88,     0x00,
    0x88,     0x08,     0x00,     0x00,     0x08,     0x08,     0x08,     0x77,     0x70,     0x77,     0x76,     0x70,
    0x60,     0x70,     0x46,     0x00,     0x57,     0x67,     0x75,     0x55,     0x07,     0x56,     0x76,     0x77,
    0x77,     0x07,     0x70,     0x70,     0x00,     0x07,     0x07,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x70,     0x70,     0x77,     0x70,     0x00,     0x07,     0x00,     0x00,     0x00,     0x00,     0x07,
    0x00,     0x60,     0x00,     0x77,     0x70,     0x70,     0x00,     0x70,     0x00,     0x00,     0x00,     0x00,
    0x77,     0x07,     0x70,     0x00,     0x00,     0x07,     0x70,     0x00,     0x00,     0x00,     0x07,     0x70,
    0x77,     0x07,     0x00,     0x00,     0x07,     0x07,     0x00,     0x00,     0x00,     0x77,     0x00,     0x07,
    0x00,     0x70,     0x77,     0x07,     0x00,     0x00,     0x70,     0x60,     0x07,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x50,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x70,
    0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x70,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x70,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x70,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,     0x00,
    0x00,     0x00,     0x00,     0x00,     0x6a,     0xf1,     0xf0,     0xc0,     0x2b,     0x94,     0xff,     0x9b,
    0x01,     0x31,     0xe0,     0xc9,     0xb7,     0xf8,     0xd0,     0x00,     0x82,     0x08,     0xff,     0x8f,
    0x02,     0x08,     0x7c,     0x22,     0x7d,     0x49,     0xa2,     0xc9,     0x08,     0x28,     0xb7,     0xc1,
    0xe1,     0xc8,     0x06,     0x11,     0x38,     0x2c,     0x12,     0x11,     0x53,     0x99,     0x52,     0x08,
    0x35,     0x30,     0x3f,     0x18,     0x8c,     0x52,     0xa4,     0xcb,     0x80,     0x9f,     0xaf,     0xbb,
    0xd7,     0xde,     0x66,     0x2f,     0x9f,     0x2b,     0x77,     0xb9,     0x4b,     0xfd,     0xcd,     0xbd,
    0x8e,     0xab,     0xde,     0xd8,     0x57,     0xec,     0xbc,     0xe9,     0xce,     0xeb,     0xfa,     0x71,
    0xf8,     0xfc,     0x80,     0x00,     0x33,     0x8c,     0xff,     0xfe,     0x01,     0xfc,     0xf1,     0x0d,
    0xcc,     0xde,     0x9c,     0xc2,     0xb4,     0x97,     0xcf,     0x19,     0x62,     0xfb,     0x5a,     0xef,
    0xad,     0x1d,     0x14,     0xe2,     0xaa,     0x68,     0x89,     0x21,     0xeb,     0x58,     0x98,     0xae,
    0x40,     0x28,     0xf4,     0x1b,     0x90,     0x48,     0x6b,     0xc2,     0x3e,     0x2c,     0x32,     0xd8,
    0x67,     0x58,     0xb2,     0x52,     0x2d,     0xad,     0xa4,     0xf1,     0xed,     0xff,     0x00,     0x35,
    0x35,     0xff,     0x7e,     0x01,     0x57,     0x6d,     0xd3,     0x2a,     0x7e,     0x2d,     0x95,     0xe6,
    0xf3,     0x47,     0x4f,     0x80,     0xca,     0xa6,     0x00,     0xff,     0x9a,     0x02,     0x9d,     0x7e,
    0xb4,     0xf3,     0xd1,     0x2d,     0xcf,     0xbe,     0x00,     0xb8,     0x00,     0x36,     0x00,     0x00

};

/* Windows 10 Prefetch MAM page captured by the independent fvol test corpus. */
static const char RealPrefetchBase64[] =
    "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"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
    ;

static int DecodeBase64(const char *Text, uint8_t *Output,
                        size_t Capacity, size_t *OutputSize)
{
    uint32_t Accumulator = 0;
    unsigned Bits = 0;
    size_t Used = 0;
    const char *P;

    for (P = Text; *P != 0; ++P)
    {
        unsigned Value;
        if (*P >= 'A' && *P <= 'Z') Value = (unsigned)(*P - 'A');
        else if (*P >= 'a' && *P <= 'z') Value = (unsigned)(*P - 'a') + 26;
        else if (*P >= '0' && *P <= '9') Value = (unsigned)(*P - '0') + 52;
        else if (*P == '+') Value = 62;
        else if (*P == '/') Value = 63;
        else if (*P == '=') break;
        else continue;

        Accumulator = (Accumulator << 6) | Value;
        Bits += 6;
        if (Bits >= 8)
        {
            Bits -= 8;
            if (Used == Capacity) return 0;
            Output[Used++] = (uint8_t)(Accumulator >> Bits);
            Accumulator &= Bits != 0 ? (((uint32_t)1 << Bits) - 1) : 0;
        }
    }
    *OutputSize = Used;
    return 1;
}

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

static uint64_t NextRandom(uint64_t *State)
{
    uint64_t X = *State;
    X ^= X << 13;
    X ^= X >> 7;
    X ^= X << 17;
    *State = X;
    return X;
}

static size_t GenerateSmall(uint8_t *Output, size_t Capacity)
{
    static const uint8_t Word0[] = "volatility";
    static const uint8_t Word1[] = "memory";
    static const uint8_t Word2[] = "kernel";
    static const uint8_t Word3[] = {0,0,0,0,0,0,0,0};
    static const uint8_t Word4[] = "process";
    static const uint8_t Word5[] = "handle";
    static const uint8_t Word6[] = "\\Device\\HarddiskVolume3\\Windows";
    static const uint8_t Word7[] = "ntoskrnl.exe";
    static const uint8_t Word8[] = {0xff,0xff};
    static const uint8_t *Words[] = {
        Word0,Word1,Word2,Word3,Word4,Word5,Word6,Word7,Word8
    };
    static const size_t WordSizes[] = {
        sizeof(Word0)-1,sizeof(Word1)-1,sizeof(Word2)-1,sizeof(Word3),
        sizeof(Word4)-1,sizeof(Word5)-1,sizeof(Word6)-1,sizeof(Word7)-1,
        sizeof(Word8)
    };
    uint64_t State = 1;
    size_t Used = 0;

    while (Used < 3000)
    {
        uint64_t R = NextRandom(&State) % 100;
        if (R < 30)
        {
            size_t I = (size_t)(NextRandom(&State) % 9);
            memcpy(Output + Used, Words[I], WordSizes[I]);
            Used += WordSizes[I];
        }
        else if (R < 40)
        {
            size_t N = (size_t)(NextRandom(&State) % 700);
            memset(Output + Used, 0, N);
            Used += N;
        }
        else if (R < 55 && Used != 0)
        {
            size_t Start = (size_t)(NextRandom(&State) % Used);
            size_t Length = (size_t)(NextRandom(&State) % 300);
            size_t End = Start + Length;
            if (End > Used) End = Used;
            Length = End - Start;
            memmove(Output + Used, Output + Start, Length);
            Used += Length;
        }
        else
        {
            size_t I, N = 1 + (size_t)(NextRandom(&State) % 16);
            for (I = 0; I < N; ++I)
                Output[Used++] = (uint8_t)NextRandom(&State);
        }
        if (Used > Capacity)
            return 0;
    }
    return 3000;
}

static WIM_CODEC_STATUS Decode(
    const uint8_t *Input, size_t InputSize, size_t ExpectedSize,
    uint8_t *Output, size_t OutputSize, size_t *InputUsed, size_t *OutputUsed)
{
    WIM_CODEC_OPTIONS Options;
    WIM_CODEC_INSTANCE *Instance = NULL;
    WIM_CODEC_STATUS Status;

    memset(&Options, 0, sizeof(Options));
    Options.StructureSize = sizeof(Options);
    Options.ExpectedOutputSize = ExpectedSize;
    Options.MaximumOutputSize = ExpectedSize;
    Options.Allocator.Allocate = TestAllocate;
    Options.Allocator.Free = TestFree;

    Status = WimCodecOpen(&Instance, WIM_CODEC_XPRESS_HUFFMAN,
                          WIM_CODEC_DECODE, &Options);
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
    uint8_t Expected[4096], Actual[3000];
    uint8_t BadTable[300] = {0};
    uint8_t RealCompressed[4096], RealOutput[0x2400];
    size_t RealCompressedSize = 0;
    size_t InputUsed = 0, OutputUsed = 0, ExpectedSize;

    CHECK((WimCodecGetCapabilities(WIM_CODEC_XPRESS_HUFFMAN) &
           WIM_CODEC_CAP_DECODE) == WIM_CODEC_CAP_DECODE);
    CHECK((WimCodecGetCapabilities(WIM_CODEC_XPRESS_HUFFMAN) &
           (WIM_CODEC_CAP_ENCODE | WIM_CODEC_CAP_STREAMING)) == 0);

    ExpectedSize = GenerateSmall(Expected, sizeof(Expected));
    CHECK(ExpectedSize == sizeof(Actual));
    memset(Actual, 0, sizeof(Actual));
    CHECK(Decode(SmallVector, sizeof(SmallVector), ExpectedSize,
                 Actual, sizeof(Actual), &InputUsed, &OutputUsed) ==
          WIM_CODEC_STATUS_STREAM_END);
    CHECK(OutputUsed == ExpectedSize);
    CHECK(InputUsed <= sizeof(SmallVector));
    CHECK(memcmp(Actual, Expected, ExpectedSize) == 0);

    InputUsed = OutputUsed = 0;
    CHECK(Decode(BadTable, sizeof(BadTable), 1, Actual, sizeof(Actual),
                 &InputUsed, &OutputUsed) == WIM_CODEC_STATUS_CORRUPT_DATA);
    CHECK(InputUsed == 0 && OutputUsed == 0);

    InputUsed = OutputUsed = 0;
    CHECK(Decode(SmallVector, 255, ExpectedSize, Actual, sizeof(Actual),
                 &InputUsed, &OutputUsed) == WIM_CODEC_STATUS_CORRUPT_DATA);
    CHECK(InputUsed == 0 && OutputUsed == 0);

    CHECK(DecodeBase64(RealPrefetchBase64, RealCompressed,
                       sizeof(RealCompressed), &RealCompressedSize));
    CHECK(RealCompressedSize == 4088);
    InputUsed = OutputUsed = 0;
    CHECK(Decode(RealCompressed, RealCompressedSize, sizeof(RealOutput),
                 RealOutput, sizeof(RealOutput), &InputUsed, &OutputUsed) ==
          WIM_CODEC_STATUS_STREAM_END);
    CHECK(OutputUsed == sizeof(RealOutput));
    CHECK(InputUsed <= RealCompressedSize);

    puts("libwim XPRESS-Huffman tests passed");
    return 0;
}
