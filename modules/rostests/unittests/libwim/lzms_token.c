#include <stdio.h>
#include <stdlib.h>
#include <libwim/wim_read.h>
#include <libwim/wim_image.h>
#include <libwim/sha1.h>
#include <string.h>
#include <libwim/lzms_token.h>
#include <libwim/lzms_huffman.h>
#include <libwim/lzms_probability.h>
#include <libwim/lzms_range16.h>
#include <libwim/lzms_block.h>
#include <libwim/lzms_context.h>
#include <libwim/lzms_slots.h>

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static int
TestTokenHistoryAndDelta(void)
{
    uint8_t window[128], out[64];
    LZMS_DICTIONARY dict;
    LZMS_TOKEN_DECODER d;
    LZMS_TOKEN t;
    size_t n;

    CHECK(LzmsDictionaryInitialize(&dict, window, sizeof(window)) == LZMS_DICTIONARY_OK);
    CHECK(LzmsTokenDecoderInitialize(&d, &dict, 0, 0) == LZMS_TOKEN_OK);

    /* The four-slot queues start at {1,2,3,4}; only ranks 0..2 are selectable. */
    CHECK(d.RepDistances[0] == 1 && d.RepDistances[1] == 2);
    CHECK(d.RepDistances[2] == 3 && d.RepDistances[3] == 4);
    CHECK(d.DeltaRepDistances[0] == 1 && d.DeltaRepDistances[3] == 4);
    CHECK(d.DeltaRepPowers[0] == 0 && d.DeltaRepPowers[3] == 0);

    memset(&t, 0, sizeof(t));
    t.Kind = LZMS_TOKEN_LITERAL;
    t.Literal = 'A';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Literal = 'B';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Literal = 'C';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Literal = 'D';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Literal = 'E';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Literal = 'F';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);

    /* Explicit LZ offsets shift the overflow slot; the next repeat is delayed. */
    t.Kind = LZMS_TOKEN_MATCH; t.Length = 1; t.Distance = 3;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.RepDistances[0] == 3 && d.RepDistances[1] == 1 &&
          d.RepDistances[2] == 2 && d.RepDistances[3] == 3);
    t.Distance = 2;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.RepDistances[0] == 2 && d.RepDistances[1] == 3 &&
          d.RepDistances[2] == 1 && d.RepDistances[3] == 2);

    /* Previous item is LZ: rank 0 reads slot 1, not the just-inserted slot 0. */
    t.Kind = LZMS_TOKEN_REP_MATCH; t.Length = 1; t.RepIndex = 0;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.RepDistances[0] == 3 && d.RepDistances[1] == 2);
    CHECK(d.PreviousItemType == 1);

    /* Delta source is a (power, raw-offset) pair, with its own delayed queue. */
    t.Kind = LZMS_TOKEN_DELTA_MATCH; t.Length = 3;
    t.Distance = 2; t.DeltaPower = 1; /* span=2, effective offset=4 */
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(n == 3 && memcmp(out, "HEH", 3) == 0);
    CHECK(d.DeltaRepDistances[0] == 2 && d.DeltaRepPowers[0] == 1);
    CHECK(d.PreviousItemType == 2);

    /* Immediately after delta, rank 0 sees the older slot-1 pair. */
    t.Kind = LZMS_TOKEN_REP_DELTA_MATCH; t.Length = 1; t.RepIndex = 0;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.DeltaRepDistances[0] == 1 && d.DeltaRepPowers[0] == 0);
    CHECK(d.PreviousItemType == 2);

    /* Insert the non-default pair again, then a literal ends the delay window. */
    t.Kind = LZMS_TOKEN_DELTA_MATCH; t.Length = 1;
    t.Distance = 2; t.DeltaPower = 1;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Kind = LZMS_TOKEN_LITERAL; t.Literal = '!';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Kind = LZMS_TOKEN_REP_DELTA_MATCH; t.Length = 1; t.RepIndex = 0;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.DeltaRepDistances[0] == 2 && d.DeltaRepPowers[0] == 1);
    CHECK(d.PreviousItemType == 2);

    /* Invalid references must not mutate repeat history or produce bytes. */
    {
        uint32_t old0 = d.DeltaRepDistances[0];
        uint32_t old1 = d.DeltaRepDistances[1];
        t.Kind = LZMS_TOKEN_DELTA_MATCH;
        t.Distance = UINT32_MAX;
        t.DeltaPower = 7;
        CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_BAD_DISTANCE);
        CHECK(n == 0 && d.DeltaRepDistances[0] == old0 &&
              d.DeltaRepDistances[1] == old1);
    }
    return 0;
}

static int
TestTokenBoundsAndOverlap(void)
{
    uint8_t window[8], out[16];
    LZMS_DICTIONARY dict;
    LZMS_TOKEN_DECODER d;
    LZMS_TOKEN t;
    size_t n;

    CHECK(LzmsDictionaryInitialize(&dict, window, sizeof(window)) == LZMS_DICTIONARY_OK);
    CHECK(LzmsTokenDecoderInitialize(&d, &dict, 5, 8) == LZMS_TOKEN_OK);
    memset(&t, 0, sizeof(t));
    t.Kind = LZMS_TOKEN_LITERAL; t.Literal = 'x';
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Kind = LZMS_TOKEN_MATCH; t.Distance = 1; t.Length = 4;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(n == 4 && memcmp(out, "xxxx", 4) == 0);
    t.Length = 1;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OUTPUT_LIMIT);
    CHECK(n == 0);
    CHECK(LzmsTokenDecode(&d, &t, out, 0, &n) == LZMS_TOKEN_OUTPUT_FULL);
    return 0;
}

static int
TestCanonicalHuffman(void)
{
    uint8_t lengths[2] = { 1, 1 };
    uint8_t encoded = 0x40; /* MSB-first codes 0, then 1. */
    LZMS_HUFFMAN_ALPHABET alphabet;
    LZMS_BIT_READER reader;
    uint16_t symbol;

    CHECK(LzmsHuffmanBuildCanonical(&alphabet, lengths, 2, 1, 0) == LZMS_HUFFMAN_OK);
    CHECK(LzmsBitReaderInitialize(&reader, &encoded, 1, LZMS_BIT_ORDER_MSB_FIRST) == LZMS_BITSTREAM_OK);
    CHECK(LzmsHuffmanDecode(&alphabet, &reader, &symbol) == LZMS_HUFFMAN_OK && symbol == 0);
    CHECK(LzmsHuffmanDecode(&alphabet, &reader, &symbol) == LZMS_HUFFMAN_OK && symbol == 1);
    lengths[0] = 2; lengths[1] = 2;
    CHECK(LzmsHuffmanBuildCanonical(&alphabet, lengths, 2, 2, 0) == LZMS_HUFFMAN_INCOMPLETE);
    {
        LZMS_ADAPTIVE_HUFFMAN adaptive, singleton;
        uint32_t a=1,b=1,kraft=0;
        unsigned i;
        CHECK(LzmsAdaptiveHuffmanInitialize(&singleton,1,8,15)==
              LZMS_HUFFMAN_OK);
        CHECK(singleton.Code.Lengths[0]==1);
        CHECK(LzmsAdaptiveHuffmanInitialize(&adaptive,20,8,5)==
              LZMS_HUFFMAN_OK);
        adaptive.Frequencies[0]=a;
        adaptive.Frequencies[1]=b;
        for(i=2;i<20;i++) {
            uint32_t next=a+b;
            adaptive.Frequencies[i]=next;
            a=b;b=next;
        }
        CHECK(LzmsAdaptiveHuffmanRebuild(&adaptive)==LZMS_HUFFMAN_OK);
        for(i=0;i<20;i++) {
            CHECK(adaptive.Code.Lengths[i]>=1 &&
                  adaptive.Code.Lengths[i]<=5);
            kraft += 1u << (5-adaptive.Code.Lengths[i]);
        }
        CHECK(kraft==32);
    }
    return 0;
}

static int
TestRange16WordOrderAndProbability(void)
{
    const uint8_t input[] = { 0x34, 0x12, 0x78, 0x56, 0, 0, 0, 0 };
    LZMS_RANGE16_DECODER range;
    LZMS_PROBABILITY_MODEL model;
    uint32_t bit;

    CHECK(LzmsRange16DecoderInitialize(&range, input, sizeof(input)) == LZMS_RANGE16_OK);
    CHECK(range.Code == UINT32_C(0x12345678));
    CHECK(range.InputPosition == 4);
    CHECK(LzmsRange16DecodeBit(&range, 32, &bit) == LZMS_RANGE16_OK);
    CHECK(bit == 0);

    LzmsProbabilityModelInitialize(&model);
    CHECK(LzmsProbabilityModelGetZeroCount(&model) == 48);
    CHECK(LzmsProbabilityModelUpdate(&model, 1) == LZMS_PROBABILITY_OK);
    CHECK(LzmsProbabilityModelGetZeroCount(&model) == 47);
    CHECK(LzmsProbabilityModelUpdate(&model, 2) == LZMS_PROBABILITY_INVALID_ARGUMENT);
    return 0;
}


static int
TestRange16RefillAndModel(void)
{
    const uint8_t input[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const uint8_t shortInput[] = { 0, 0, 0, 0 };
    LZMS_RANGE16_DECODER range, shortRange;
    LZMS_PROBABILITY_MODEL model;
    uint32_t value = UINT32_MAX, bit = 7;
    unsigned i;

    CHECK(LzmsRange16DecoderInitialize(&range, input, sizeof(input)) ==
          LZMS_RANGE16_OK);
    CHECK(LzmsRange16DecoderBytesRead(&range) == 4);
    for (i = 0; i < 16; ++i) {
        CHECK(LzmsRange16DecodeBit(&range, 32, &bit) == LZMS_RANGE16_OK);
        CHECK(bit == 0);
    }
    CHECK(LzmsRange16DecoderBytesRead(&range) == 4);
    CHECK(LzmsRange16DecodeBit(&range, 32, &bit) == LZMS_RANGE16_OK);
    CHECK(bit == 0);
    CHECK(LzmsRange16DecoderBytesRead(&range) == 6);

    CHECK(LzmsRange16DecoderInitialize(&shortRange, shortInput,
                                       sizeof(shortInput)) == LZMS_RANGE16_OK);
    for (i = 0; i < 16; ++i)
        CHECK(LzmsRange16DecodeBit(&shortRange, 32, &bit) ==
              LZMS_RANGE16_OK);
    value = 0xA5A5A5A5;
    CHECK(LzmsRange16DecodeBit(&shortRange, 32, &value) ==
          LZMS_RANGE16_OK);
    CHECK(value == 0);
    CHECK(shortRange.InputPosition == 4);
    CHECK(shortRange.Range != 65535);

    CHECK(LzmsRange16DecoderInitialize(&range, input, sizeof(input)) ==
          LZMS_RANGE16_OK);
    LzmsProbabilityModelInitialize(&model);
    CHECK(LzmsRange16DecodeModelBit(&range, &model, &bit) ==
          LZMS_RANGE16_OK);
    CHECK(bit == 0);
    CHECK(LzmsProbabilityModelGetZeroCount(&model) == 48);

    value = 0xFFFFFFFF;
    CHECK(LzmsRange16DecodeBits(&range, 0, &value) == LZMS_RANGE16_OK);
    CHECK(value == 0);
    CHECK(LzmsRange16DecodeBits(&range, 33, &value) ==
          LZMS_RANGE16_INVALID_ARGUMENT);
    return 0;
}



static int
TestReverse16AndAdaptiveAlphabets(void)
{
    const uint8_t words[] = { 0x34, 0x12, 0x78, 0x56 };
    LZMS_REVERSE16_READER reader;
    LZMS_ADAPTIVE_HUFFMAN alphabets[LZMS_HUFFMAN_ALPHABET_COUNT];
    uint32_t value;
    uint16_t symbol;
    unsigned i;

    CHECK(LzmsReverse16ReaderInitialize(&reader, words, sizeof(words)) ==
          LZMS_BITSTREAM_OK);
    CHECK(LzmsReverse16ReaderReadBits(&reader, 16, &value) ==
          LZMS_BITSTREAM_OK);
    CHECK(value == 0x5678);
    CHECK(LzmsReverse16ReaderReadBits(&reader, 16, &value) ==
          LZMS_BITSTREAM_OK);
    CHECK(value == 0x1234);
    CHECK(LzmsReverse16ReaderReadBits(&reader, 1, &value) ==
          LZMS_BITSTREAM_OK);
    CHECK(value == 0);
    CHECK(reader.NextWordOffset == 0);

    CHECK(LzmsAdaptiveHuffmanInitializeLzmsSet(alphabets, 799) ==
          LZMS_HUFFMAN_OK);
    CHECK(alphabets[LZMS_HUFFMAN_LITERAL].SymbolCount == 256);
    CHECK(alphabets[LZMS_HUFFMAN_LZ_OFFSET].SymbolCount == 799);
    CHECK(alphabets[LZMS_HUFFMAN_LENGTH].SymbolCount == 54);
    CHECK(alphabets[LZMS_HUFFMAN_DELTA_OFFSET].SymbolCount == 799);
    CHECK(alphabets[LZMS_HUFFMAN_DELTA_POWER].SymbolCount == 8);
    for (i = 0; i < LZMS_HUFFMAN_ALPHABET_COUNT; ++i) {
        CHECK(alphabets[i].SymbolsUntilRebuild ==
              alphabets[i].RebuildFrequency);
        CHECK(alphabets[i].Frequencies[0] == 1);
    }

    /* Rebuild and dilution are exercised independently for all five
       alphabets.  The all-zero stream repeatedly selects symbol zero. */
    {
        uint8_t zeroWords[2048] = { 0 };
        for (i = 0; i < LZMS_HUFFMAN_ALPHABET_COUNT; ++i) {
            uint32_t j;
            int HasUpdatedFrequency = 0;
            LZMS_ADAPTIVE_HUFFMAN expected;
            CHECK(LzmsReverse16ReaderInitialize(&reader, zeroWords,
                                                 sizeof(zeroWords)) ==
                  LZMS_BITSTREAM_OK);
            for (j = 0; j < alphabets[i].RebuildFrequency; ++j) {
                CHECK(LzmsAdaptiveHuffmanDecode(&alphabets[i], &reader,
                                                &symbol) == LZMS_HUFFMAN_OK);
                CHECK(symbol == 0);
            }
            for (j = 0; j < alphabets[i].SymbolCount; ++j)
                if (alphabets[i].Frequencies[j] > 1)
                    HasUpdatedFrequency = 1;
            CHECK(HasUpdatedFrequency);
            CHECK(alphabets[i].SymbolsUntilRebuild ==
                  alphabets[i].RebuildFrequency);

            /*
             * LZMS's rebuild table uses the pre-dilution counts (N+1 for
             * symbol zero, 1 for all others).  The live counters are then
             * diluted independently for the next interval.
             */
            expected = alphabets[i];
            for (j = 0; j < expected.SymbolCount; ++j)
                expected.Frequencies[j] = 1;
            expected.Frequencies[0] =
                (uint32_t)expected.RebuildFrequency + 1;
            CHECK(LzmsAdaptiveHuffmanRebuild(&expected) == LZMS_HUFFMAN_OK);
            CHECK(memcmp(alphabets[i].Code.Lengths, expected.Code.Lengths,
                         alphabets[i].SymbolCount) == 0);
            CHECK(alphabets[i].Frequencies[0] ==
                  ((uint32_t)alphabets[i].RebuildFrequency >> 1) + 1);
        }
    }
    CHECK(LzmsAdaptiveHuffmanInitializeLzmsSet(alphabets, 0) ==
          LZMS_HUFFMAN_OK);
    CHECK(alphabets[LZMS_HUFFMAN_LZ_OFFSET].SymbolCount == 1);
    CHECK(alphabets[LZMS_HUFFMAN_DELTA_OFFSET].SymbolCount == 1);
    return 0;
}

static int
TestAllRepeatRanks(void)
{
    uint8_t window[128], out[32];
    LZMS_DICTIONARY dict;
    LZMS_TOKEN_DECODER d;
    LZMS_TOKEN t;
    size_t n;
    unsigned i;

    CHECK(LzmsDictionaryInitialize(&dict, window, sizeof(window)) == LZMS_DICTIONARY_OK);
    CHECK(LzmsTokenDecoderInitialize(&d, &dict, 0, 0) == LZMS_TOKEN_OK);
    memset(&t, 0, sizeof(t));
    t.Kind = LZMS_TOKEN_LITERAL;
    for (i = 0; i < 8; ++i) {
        t.Literal = (uint8_t)('a' + i);
        CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    }

    t.Kind = LZMS_TOKEN_MATCH; t.Length = 1;
    t.Distance = 1;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Distance = 2;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    t.Distance = 3;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.RepDistances[0] == 3 && d.RepDistances[1] == 2 &&
          d.RepDistances[2] == 1 && d.RepDistances[3] == 1);

    t.Kind = LZMS_TOKEN_REP_MATCH; t.RepIndex = 0;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.RepDistances[0] == 2);
    t.RepIndex = 1;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.RepDistances[0] == 2);
    t.RepIndex = 2;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.RepDistances[0] == 1);

    /* Exercise all three delta ranks while the preceding item is delta. */
    CHECK(LzmsDictionaryInitialize(&dict, window, sizeof(window)) == LZMS_DICTIONARY_OK);
    CHECK(LzmsTokenDecoderInitialize(&d, &dict, 0, 0) == LZMS_TOKEN_OK);
    t.Kind = LZMS_TOKEN_LITERAL;
    for (i = 0; i < 8; ++i) {
        t.Literal = (uint8_t)('A' + i);
        CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    }
    t.Kind = LZMS_TOKEN_DELTA_MATCH; t.Length = 1;
    t.Distance = 2; t.DeltaPower = 0;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.DeltaRepDistances[0] == 2);
    t.Kind = LZMS_TOKEN_REP_DELTA_MATCH; t.RepIndex = 0;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.DeltaRepDistances[0] == 1);
    t.RepIndex = 1;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.DeltaRepDistances[0] == 1);
    t.RepIndex = 2;
    CHECK(LzmsTokenDecode(&d, &t, out, sizeof(out), &n) == LZMS_TOKEN_OK);
    CHECK(d.DeltaRepDistances[0] == 1);
    return 0;
}


static int
TestRangeDecodedContexts(void)
{
    const uint8_t input[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    LZMS_RANGE16_DECODER range;
    LZMS_CONTEXT_SET contexts;
    LZMS_ITEM_CLASS item;
    uint32_t repeat;
    uint8_t rank;
    CHECK(LzmsContextSetInitialize(&contexts)==LZMS_CONTEXT_OK);
    CHECK(contexts.Main.StateCount==16 && contexts.Match.StateCount==32);
    CHECK(contexts.Lz.StateCount==64 && contexts.Delta.StateCount==64);
    CHECK(contexts.LzRepeat[0].StateCount==64 &&
          contexts.DeltaRepeat[1].StateCount==64);
    CHECK(LzmsRange16DecoderInitialize(&range,input,sizeof(input))==LZMS_RANGE16_OK);
    CHECK(LzmsContextDecodeItemClass(&contexts,&range,&item)==LZMS_CONTEXT_OK);
    CHECK(item==LZMS_ITEM_LITERAL);
    CHECK(contexts.Main.State==0);
    CHECK(LzmsContextDecodeOffsetMode(&contexts,&range,LZMS_ITEM_LZ_MATCH,&repeat)==LZMS_CONTEXT_OK);
    CHECK(repeat==0);
    CHECK(LzmsContextDecodeRepeatRank(&contexts,&range,LZMS_ITEM_LZ_MATCH,&rank)==LZMS_CONTEXT_OK);
    CHECK(rank<=2);
    return 0;
}


static int
TestRawBlockFramingAndSlots(void)
{
    const uint8_t shortBlock[4]={0,0,0,0};
    const uint8_t oddBlock[5]={0,0,0,0,0};
    uint8_t output[16];
    uint8_t aliased[8]={0,0,0,0,0,0,0,0};
    size_t used=99;
    CHECK(LzmsGetOffsetSlotCount(1)==0);
    CHECK(LzmsGetOffsetSlotCount(2)==1);
    CHECK(LzmsGetOffsetSlotCount(10)==9);
    CHECK(LzmsGetOffsetSlotCount(14)==10);
    CHECK(LzmsDecodeRawBlock(oddBlock,sizeof(oddBlock),output,
                             sizeof(output),&used)==LZMS_BLOCK_INVALID_ARGUMENT);
    CHECK(used==0);
    CHECK(LzmsDecodeRawBlock(shortBlock,2,output,sizeof(output),&used)==
          LZMS_BLOCK_INVALID_ARGUMENT);
    CHECK(LzmsDecodeRawBlock(shortBlock,sizeof(shortBlock),output,1,&used)==
          LZMS_BLOCK_BAD_FRAMING);
    used=99;
    CHECK(LzmsDecodeRawBlock(aliased,4,aliased,4,&used)==
          LZMS_BLOCK_INPUT_OVERLAP);
    CHECK(used==0);
    return 0;
}

static void *TestWimAllocate(void *Context, size_t Size)
{
    (void)Context;
    return malloc(Size);
}

static void TestWimFree(void *Context, void *Allocation)
{
    (void)Context;
    free(Allocation);
}

static void TestWriteLe32(uint8_t *P, uint32_t V)
{
    P[0]=(uint8_t)V; P[1]=(uint8_t)(V>>8);
    P[2]=(uint8_t)(V>>16); P[3]=(uint8_t)(V>>24);
}

static void TestWriteLe64(uint8_t *P, uint64_t V)
{
    TestWriteLe32(P,(uint32_t)V);
    TestWriteLe32(P+4,(uint32_t)(V>>32));
}

static uint32_t TestCrc32(const uint8_t *Data, size_t Size)
{
    uint32_t Crc=0xFFFFFFFFu;
    size_t I;
    unsigned Bit;
    for(I=0;I<Size;I++) {
        Crc^=Data[I];
        for(Bit=0;Bit<8;Bit++)
            Crc=(Crc>>1)^(0xEDB88320u&(0u-(Crc&1u)));
    }
    return ~Crc;
}

static int
TestWimHeaderAndRawResource(void)
{
    uint8_t image[212], output[4];
    WIM_READER_OPTIONS options;
    WIM_READER *reader=NULL;
    WIM_HEADER_INFO header;
    WIM_RESOURCE_DESCRIPTOR resource;
    size_t used=0;
    uint8_t digest[20];
    static const uint8_t abcSha1[20] = {
        0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,
        0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,
        0x9c,0xd0,0xd8,0x9d
    };

    WimSha1("abc",3,digest);
    CHECK(memcmp(digest,abcSha1,20)==0);
    memset(image,0,sizeof(image));
    memcpy(image,WIM_HEADER_SIGNATURE,8);
    TestWriteLe32(image+8,WIM_HEADER_MIN_SIZE);
    TestWriteLe32(image+12,0x00010D00);
    TestWriteLe32(image+20,32768);
    image[40]=1; image[42]=1;
    TestWriteLe32(image+44,1);
    TestWriteLe32(image+120,1);
    /* XML resource descriptor: 4 stored bytes at file offset 208. */
    TestWriteLe64(image+72,4);
    TestWriteLe64(image+80,208);
    TestWriteLe64(image+88,4);
    memcpy(image+208,"WIM!",4);
    TestWriteLe32(image+204,TestCrc32(image,204));

    memset(&options,0,sizeof(options));
    options.StructureSize=sizeof(options);
    options.MaximumResourceSize=1024;
    options.Allocator.Allocate=TestWimAllocate;
    options.Allocator.Free=TestWimFree;
    CHECK(WimReaderOpenMemory(&reader,image,sizeof(image),&options)==
          WIM_READER_OK);
    CHECK(WimReaderGetHeaderInfo(reader,&header)==WIM_READER_OK);
    CHECK(header.ImageCount==1 && header.PartNumber==1 &&
          header.TotalParts==1 && header.ChunkSize==32768 &&
          header.BootIndex==1);
    CHECK(WimReaderGetResource(reader,WIM_RESOURCE_XML_DATA,&resource)==
          WIM_READER_OK);
    CHECK(resource.Offset==208 && resource.OriginalSize==4 &&
          resource.CompressedSize==4);
    CHECK(WimReaderReadResource(reader,&resource,output,sizeof(output),&used)==
          WIM_READER_OK);
    CHECK(used==4 && memcmp(output,"WIM!",4)==0);
    resource.Offset=UINT64_MAX-1;
    used=7;
    CHECK(WimReaderReadResource(reader,&resource,output,sizeof(output),&used)==
          WIM_READER_BAD_RESOURCE);
    CHECK(used==0);
    WimReaderClose(reader);
    image[24]^=1;
    reader=NULL;
    CHECK(WimReaderOpenMemory(&reader,image,sizeof(image),&options)==
          WIM_READER_BAD_HEADER);
    image[24]^=1;

    /*
     * Original-format WIM chunk table: one 32-bit entry gives the second
     * chunk's offset relative to the end of the table. Both chunks are raw
     * because their stored sizes equal their uncompressed sizes.
     */
    {
        uint8_t chunked[218], chunkOutput[6];
        memset(chunked,0,sizeof(chunked));
        memcpy(chunked,WIM_HEADER_SIGNATURE,8);
        TestWriteLe32(chunked+8,WIM_HEADER_MIN_SIZE);
        TestWriteLe32(chunked+12,0x00010D00);
        TestWriteLe32(chunked+16,WIM_HEADER_FLAG_COMPRESS_LZMS);
        TestWriteLe32(chunked+20,4);
        chunked[40]=1; chunked[42]=1;
        TestWriteLe32(chunked+44,1);
        TestWriteLe64(chunked+72,
            ((uint64_t)WIM_RESOURCE_FLAG_COMPRESSED<<56)|10);
        TestWriteLe64(chunked+80,208);
        TestWriteLe64(chunked+88,6);
        TestWriteLe32(chunked+208,4);
        memcpy(chunked+212,"ABCDEF",6);
        TestWriteLe32(chunked+204,TestCrc32(chunked,204));
        reader=NULL;
        CHECK(WimReaderOpenMemory(&reader,chunked,sizeof(chunked),&options)==
              WIM_READER_OK);
        CHECK(WimReaderGetResource(reader,WIM_RESOURCE_XML_DATA,&resource)==
              WIM_READER_OK);
        used=0;
        CHECK(WimReaderReadResource(reader,&resource,chunkOutput,
                                    sizeof(chunkOutput),&used)==WIM_READER_OK);
        CHECK(used==6 && memcmp(chunkOutput,"ABCDEF",6)==0);
        WimReaderClose(reader);
    }

    /* Lookup-table entry -> resource descriptor -> resource bytes. */
    {
        uint8_t lookupImage[262], blob[4];
        size_t entryCount=0;
        WIM_LOOKUP_ENTRY entry;
        memset(lookupImage,0,sizeof(lookupImage));
        memcpy(lookupImage,WIM_HEADER_SIGNATURE,8);
        TestWriteLe32(lookupImage+8,WIM_HEADER_MIN_SIZE);
        TestWriteLe32(lookupImage+12,0x00010D00);
        TestWriteLe32(lookupImage+20,32768);
        lookupImage[40]=1; lookupImage[42]=1;
        TestWriteLe32(lookupImage+44,1);
        TestWriteLe64(lookupImage+48,50);
        TestWriteLe64(lookupImage+56,208);
        TestWriteLe64(lookupImage+64,50);
        TestWriteLe64(lookupImage+208,
            ((uint64_t)WIM_RESOURCE_FLAG_METADATA<<56)|4);
        TestWriteLe64(lookupImage+216,258);
        TestWriteLe64(lookupImage+224,4);
        lookupImage[232]=1;
        TestWriteLe32(lookupImage+234,1);
        memcpy(lookupImage+258,"DATA",4);
        reader=NULL;
        CHECK(WimReaderOpenMemory(&reader,lookupImage,sizeof(lookupImage),
                                  &options)==WIM_READER_OK);
        CHECK(WimReaderGetLookupEntryCount(reader,&entryCount)==WIM_READER_OK);
        CHECK(entryCount==1);
        CHECK(WimReaderGetLookupEntry(reader,0,&entry)==WIM_READER_OK);
        CHECK(entry.PartNumber==1 && entry.ReferenceCount==1 &&
              entry.Resource.Offset==258 && entry.Resource.OriginalSize==4 &&
              (entry.Resource.Flags & WIM_RESOURCE_FLAG_METADATA));
        used=0;
        CHECK(WimReaderReadLookupResource(reader,0,blob,sizeof(blob),&used)==
              WIM_READER_OK);
        CHECK(used==4 && memcmp(blob,"DATA",4)==0);
        WimReaderClose(reader);
    }
    return 0;
}


static int
TestWimImageCatalogueAndDirectoryTree(void)
{
    static const char xml[]="<WIM><IMAGE INDEX=\"1\"><NAME>Test Image</NAME><DESCRIPTION>Fixture</DESCRIPTION><DIRCOUNT>1</DIRCOUNT><FILECOUNT>1</FILECOUNT><TOTALBYTES>4</TOTALBYTES></IMAGE></WIM>";
    static const uint8_t dataSha1[20]={0x58,0x03,0x93,0xf5,0xa9,0x4f,0xb4,0x69,0x58,0x5f,0x5d,0xd2,0xa6,0x85,0x9a,0x4a,0xab,0x89,0x9f,0x37};
    static const uint8_t sideSha1[20]={0xBA,0xB9,0xFD,0xD5,0xC9,0x02,0x27,0x98,0x10,0xF6,0x76,0xCA,0x74,0xC6,0x63,0x15,0xA7,0x47,0x99,0x5B};
    uint8_t image[358 + sizeof(xml) - 1 + 272 + 8], *lookup, *meta, *root, *file;
    const uint64_t metadataOffset=358 + sizeof(xml) - 1;
    const uint64_t blobOffset=metadataOffset+272;
    const uint64_t sideOffset=blobOffset+4;
    WIM_READER_OPTIONS options;
    WIM_READER *reader=NULL;
    WIM_IMAGE_INFO info;
    WIM_DIRECTORY_NODE *tree=NULL;
    size_t count=0, streamUsed=0;
    uint8_t streamData[4];
    static const uint8_t abcSha1[20]={0xA9,0x99,0x3E,0x36,0x47,0x06,0x81,0x6A,0xBA,0x3E,0x25,0x71,0x78,0x50,0xC2,0x6C,0x9C,0xD0,0xD8,0x9D};
    uint8_t digest[20];
    WimSha1("abc",3,digest);
    CHECK(memcmp(digest,abcSha1,20)==0);
    memset(image,0,sizeof(image));
    memcpy(image,WIM_HEADER_SIGNATURE,8);
    TestWriteLe32(image+8,WIM_HEADER_MIN_SIZE);
    TestWriteLe32(image+12,0x00010D00);
    TestWriteLe32(image+20,32768);
    image[40]=1; image[42]=1; TestWriteLe32(image+44,1);
    TestWriteLe64(image+48,150); TestWriteLe64(image+56,208); TestWriteLe64(image+64,150);
    TestWriteLe64(image+72,sizeof(xml)-1); TestWriteLe64(image+80,358); TestWriteLe64(image+88,sizeof(xml)-1);
    lookup=image+208;
    TestWriteLe64(lookup,((uint64_t)WIM_RESOURCE_FLAG_METADATA<<56)|272);
    TestWriteLe64(lookup+8,metadataOffset); TestWriteLe64(lookup+16,272);
    lookup[24]=1; TestWriteLe32(lookup+26,1);
    TestWriteLe64(lookup+50,4); TestWriteLe64(lookup+58,blobOffset);
    TestWriteLe64(lookup+66,4); lookup[74]=1; TestWriteLe32(lookup+76,1);
    memcpy(lookup+80,dataSha1,20);
    TestWriteLe64(lookup+100,4); TestWriteLe64(lookup+108,sideOffset);
    TestWriteLe64(lookup+116,4); lookup[124]=1; TestWriteLe32(lookup+126,1);
    memcpy(lookup+130,sideSha1,20);
    memcpy(image+358,xml,sizeof(xml)-1);
    meta=image+(size_t)metadataOffset;
    TestWriteLe32(meta,8); TestWriteLe32(meta+4,0);
    root=meta+8;
    TestWriteLe64(root,104); TestWriteLe32(root+8,0x10);
    TestWriteLe32(root+12,UINT32_MAX); TestWriteLe64(root+16,120);
    file=meta+120;
    TestWriteLe64(file,112); TestWriteLe32(file+8,0x80);
    TestWriteLe32(file+12,UINT32_MAX); file[96]=1; file[100]=2; file[102]='a'; file[103]=0;
    memcpy(file+64,dataSha1,20); TestWriteLe64(file+112,40); memcpy(file+120,sideSha1,20);
    TestWriteLe64(file+140,4); file[148]=2; file[150]='s'; file[151]=0;
    memcpy(image+(size_t)blobOffset,"DATA",4); memcpy(image+(size_t)sideOffset,"SIDE",4);
    WimSha1(meta,272,lookup+30);
    memset(&options,0,sizeof(options)); options.StructureSize=sizeof(options);
    options.MaximumResourceSize=4096; options.Allocator.Allocate=TestWimAllocate;
    options.Allocator.Free=TestWimFree;
    CHECK(WimReaderOpenMemory(&reader,image,sizeof(image),&options)==WIM_READER_OK);
    CHECK(WimReaderGetImageCount(reader,&count)==WIM_READER_OK && count==1);
    CHECK(WimReaderGetImageInfo(reader,0,&info)==WIM_READER_OK);
    CHECK(info.Index==1 && info.Name && strcmp(info.Name,"Test Image")==0);
    WimImageInfoFree(reader,&info);
    CHECK(WimReaderLoadImageDirectoryTree(reader,0,&tree)==WIM_READER_OK);
    CHECK(tree && tree->IsDirectory && tree->ChildCount==1);
    CHECK(tree->Children[0]->Name && strcmp(tree->Children[0]->Name,"a")==0);
    CHECK(!tree->Children[0]->IsDirectory);
    CHECK(tree->Children[0]->HasMainStream && tree->Children[0]->MainStreamSize==4);
    CHECK(tree->Children[0]->MainStreamLookupIndex==1);
    CHECK(WimImageReadMainStream(reader,tree->Children[0],streamData,sizeof(streamData),&streamUsed)==WIM_READER_OK);
    CHECK(streamUsed==4 && memcmp(streamData,"DATA",4)==0);
    CHECK(tree->Children[0]->NamedStreamCount==1);
    CHECK(strcmp(tree->Children[0]->NamedStreams[0].Name,"s")==0);
    CHECK(tree->Children[0]->NamedStreams[0].HasLookup);
    streamUsed=0;
    CHECK(WimImageReadNamedStream(reader,tree->Children[0],0,streamData,sizeof(streamData),&streamUsed)==WIM_READER_OK);
    CHECK(streamUsed==4 && memcmp(streamData,"SIDE",4)==0);
    WimImageDirectoryTreeFree(reader,tree);
    WimReaderClose(reader);
    return 0;
}

int
main(void)
{
    int status;
    status = TestTokenHistoryAndDelta(); if (status) return status;
    status = TestAllRepeatRanks(); if (status) return status;
    status = TestTokenBoundsAndOverlap(); if (status) return status;
    status = TestCanonicalHuffman(); if (status) return status;
    status = TestRange16WordOrderAndProbability(); if (status) return status;
    status = TestRange16RefillAndModel(); if (status) return status;
    status = TestReverse16AndAdaptiveAlphabets(); if (status) return status;
    status = TestRangeDecodedContexts(); if (status) return status;
    status = TestRawBlockFramingAndSlots(); if (status) return status;
    status = TestWimHeaderAndRawResource(); if (status) return status;
    status = TestWimImageCatalogueAndDirectoryTree(); if (status) return status;
    return 0;
}
