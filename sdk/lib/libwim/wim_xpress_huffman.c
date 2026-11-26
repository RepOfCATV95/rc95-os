#include <stdlib.h>
#include <string.h>
#include <libwim/wim_codec.h>

#define XPRESS_HUFF_SYMBOLS 512u
#define XPRESS_HUFF_MAX_BITS 15u
#define XPRESS_HUFF_BLOCK_SIZE 65536u

typedef struct _XPRESS_HUFF_STATE
{
    WIM_CODEC_ALLOCATOR Allocator;
    uint64_t ExpectedOutputSize;
    uint64_t MaximumOutputSize;
    int Completed;
} XPRESS_HUFF_STATE;

typedef struct _XPRESS_HUFF_TABLE
{
    uint32_t FirstCode[XPRESS_HUFF_MAX_BITS + 1];
    uint16_t FirstIndex[XPRESS_HUFF_MAX_BITS + 1];
    uint16_t Count[XPRESS_HUFF_MAX_BITS + 1];
    uint16_t Sorted[XPRESS_HUFF_SYMBOLS];
} XPRESS_HUFF_TABLE;

typedef struct _XPRESS_HUFF_BITS
{
    const uint8_t *Input;
    size_t InputSize;
    size_t Base;
    size_t Position;
    uint64_t Buffer;
    unsigned Count;
} XPRESS_HUFF_BITS;

static int
XpressHuffBuildTable(
    XPRESS_HUFF_TABLE *Table,
    const uint8_t *Lengths)
{
    uint16_t Counts[XPRESS_HUFF_MAX_BITS + 1] = {0};
    uint16_t Next[XPRESS_HUFF_MAX_BITS + 1] = {0};
    uint32_t Code = 0;
    uint16_t Offset = 0;
    unsigned I, Length;

    memset(Table, 0, sizeof(*Table));
    for (I = 0; I < 256; ++I)
    {
        ++Counts[Lengths[I] & 0x0f];
        ++Counts[Lengths[I] >> 4];
    }
    Counts[0] = 0;

    for (Length = 1; Length <= XPRESS_HUFF_MAX_BITS; ++Length)
    {
        Table->FirstCode[Length] = Code;
        Table->FirstIndex[Length] = Offset;
        Table->Count[Length] = Counts[Length];
        if (Code + Counts[Length] > (1u << Length))
            return 0;
        Code = (Code + Counts[Length]) << 1;
        Offset = (uint16_t)(Offset + Counts[Length]);
    }

    /* XPRESS-Huffman requires a complete 15-bit canonical code. */
    if (Code != (1u << (XPRESS_HUFF_MAX_BITS + 1)))
        return 0;

    memcpy(Next, Table->FirstIndex, sizeof(Next));
    for (I = 0; I < 256; ++I)
    {
        unsigned Symbols[2] = { I * 2, I * 2 + 1 };
        unsigned Lens[2] = { Lengths[I] & 0x0f, Lengths[I] >> 4 };
        unsigned J;
        for (J = 0; J < 2; ++J)
        {
            Length = Lens[J];
            if (Length != 0)
                Table->Sorted[Next[Length]++] = (uint16_t)Symbols[J];
        }
    }
    return 1;
}

static int
XpressHuffReadWord(XPRESS_HUFF_BITS *Bits)
{
    uint16_t Word;
    if (Bits->Position > Bits->InputSize ||
        Bits->InputSize - Bits->Position < 2)
        return 0;

    Word = (uint16_t)(Bits->Input[Bits->Position] |
                     ((uint16_t)Bits->Input[Bits->Position + 1] << 8));
    Bits->Position += 2;
    Bits->Buffer |= (uint64_t)Word << (48 - Bits->Count);
    Bits->Count += 16;
    return 1;
}

static int
XpressHuffRefill(XPRESS_HUFF_BITS *Bits)
{
    while (Bits->Count < 48)
        if (!XpressHuffReadWord(Bits))
            return 0;
    return 1;
}

static void
XpressHuffConsume(XPRESS_HUFF_BITS *Bits, unsigned Count)
{
    Bits->Buffer <<= Count;
    Bits->Count -= Count;
}

static size_t
XpressHuffSpecWords(const XPRESS_HUFF_BITS *Bits)
{
    size_t Consumed = 16 * ((Bits->Position - Bits->Base) / 2) -
                      Bits->Count;
    size_t Words = (Consumed + 15) / 16 + 1;
    return Words < 2 ? 2 : Words;
}

static size_t
XpressHuffSpecPosition(const XPRESS_HUFF_BITS *Bits)
{
    return Bits->Base + 2 * XpressHuffSpecWords(Bits);
}

static int
XpressHuffResync(
    XPRESS_HUFF_BITS *Bits,
    size_t NewPosition)
{
    size_t Words = XpressHuffSpecWords(Bits);
    size_t Consumed = 16 * ((Bits->Position - Bits->Base) / 2) -
                      Bits->Count;
    size_t Keep = 16 * Words - Consumed;

    if (NewPosition < Bits->Base || NewPosition > Bits->InputSize ||
        Keep < 16 || Keep > 31)
        return 0;

    Bits->Buffer &= UINT64_MAX << (64 - Keep);
    Bits->Count = (unsigned)Keep;
    Bits->Position = NewPosition;
    Bits->Base = NewPosition - 2 * Words;
    return 1;
}

static int
XpressHuffReadByte(
    const XPRESS_HUFF_BITS *Bits,
    size_t Position,
    uint8_t *Value)
{
    if (Position >= Bits->InputSize)
        return 0;
    *Value = Bits->Input[Position];
    return 1;
}

static int
XpressHuffDecodeSymbol(
    const XPRESS_HUFF_TABLE *Table,
    XPRESS_HUFF_BITS *Bits,
    unsigned *Symbol)
{
    unsigned Length;
    uint32_t Lookahead = (uint32_t)(Bits->Buffer >> 49);

    for (Length = 1; Length <= XPRESS_HUFF_MAX_BITS; ++Length)
    {
        uint32_t Code = Lookahead >> (15 - Length);
        uint32_t First = Table->FirstCode[Length];
        uint32_t Delta = Code - First;
        if (Code >= First && Delta < Table->Count[Length])
        {
            *Symbol = Table->Sorted[Table->FirstIndex[Length] + Delta];
            XpressHuffConsume(Bits, Length);
            return 1;
        }
    }
    return 0;
}

static int
XpressHuffCopyMatch(
    uint8_t *Output,
    size_t OutputSize,
    size_t *OutputPosition,
    size_t Offset,
    size_t Length)
{
    size_t I;
    if (Offset == 0 || Offset > *OutputPosition)
        return 0;
    if (Length > OutputSize - *OutputPosition)
        Length = OutputSize - *OutputPosition;

    for (I = 0; I < Length; ++I)
        Output[*OutputPosition + I] =
            Output[*OutputPosition + I - Offset];
    *OutputPosition += Length;
    return 1;
}

static int
XpressHuffDecode(
    const uint8_t *Input,
    size_t InputSize,
    uint8_t *Output,
    size_t OutputSize,
    size_t *InputUsed)
{
    XPRESS_HUFF_TABLE Table;
    XPRESS_HUFF_BITS Bits;
    size_t InputPosition = 0;
    size_t OutputPosition = 0;

    if (Input == NULL || Output == NULL || InputUsed == NULL)
        return 0;

    while (OutputPosition < OutputSize)
    {
        size_t BlockEnd;
        size_t BlockStart = OutputPosition;
        unsigned Symbol;

        if (InputPosition > InputSize ||
            InputSize - InputPosition < 256)
            return 0;
        if (!XpressHuffBuildTable(&Table, Input + InputPosition))
            return 0;

        Bits.Input = Input;
        Bits.InputSize = InputSize;
        Bits.Base = InputPosition + 256;
        Bits.Position = Bits.Base;
        Bits.Buffer = 0;
        Bits.Count = 0;
        BlockEnd = OutputPosition +
            ((OutputSize - OutputPosition) > XPRESS_HUFF_BLOCK_SIZE
             ? XPRESS_HUFF_BLOCK_SIZE : OutputSize - OutputPosition);

        while (OutputPosition < BlockEnd)
        {
            unsigned LengthCode, OffsetBits;
            uint64_t MatchLength64;
            size_t MatchLength, Offset;

            if (!XpressHuffRefill(&Bits) ||
                !XpressHuffDecodeSymbol(&Table, &Bits, &Symbol))
                return 0;

            if (Symbol < 256)
            {
                Output[OutputPosition++] = (uint8_t)Symbol;
                continue;
            }

            Symbol -= 256;
            LengthCode = Symbol & 15;
            OffsetBits = Symbol >> 4;
            MatchLength64 = LengthCode;

            if (LengthCode == 15)
            {
                size_t P = XpressHuffSpecPosition(&Bits);
                uint8_t B;
                if (!XpressHuffReadByte(&Bits, P++, &B))
                    return 0;
                MatchLength64 = B;
                if (B == 255)
                {
                    uint8_t Lo, Hi;
                    if (!XpressHuffReadByte(&Bits, P++, &Lo) ||
                        !XpressHuffReadByte(&Bits, P++, &Hi))
                        return 0;
                    MatchLength64 = (uint64_t)Lo | ((uint64_t)Hi << 8);
                    if (MatchLength64 == 0)
                    {
                        uint8_t B0, B1, B2, B3;
                        if (!XpressHuffReadByte(&Bits, P++, &B0) ||
                            !XpressHuffReadByte(&Bits, P++, &B1) ||
                            !XpressHuffReadByte(&Bits, P++, &B2) ||
                            !XpressHuffReadByte(&Bits, P++, &B3))
                            return 0;
                        MatchLength64 = (uint64_t)B0 |
                            ((uint64_t)B1 << 8) |
                            ((uint64_t)B2 << 16) |
                            ((uint64_t)B3 << 24);
                    }
                    if (MatchLength64 < 15)
                        return 0;
                    MatchLength64 -= 15;
                }
                MatchLength64 += 15;
                if (!XpressHuffResync(&Bits, P))
                    return 0;
            }

            MatchLength64 += 3;
            if (MatchLength64 > (uint64_t)(OutputSize - OutputPosition))
                MatchLength = OutputSize - OutputPosition;
            else
                MatchLength = (size_t)MatchLength64;
            if (OffsetBits != 0)
                Offset = (size_t)(Bits.Buffer >> (64 - OffsetBits)) +
                         ((size_t)1 << OffsetBits);
            else
                Offset = 1;
            XpressHuffConsume(&Bits, OffsetBits);

            if (!XpressHuffCopyMatch(Output, OutputSize, &OutputPosition,
                                     Offset, MatchLength))
                return 0;
        }

        if (OutputPosition <= BlockStart)
            return 0;
        InputPosition = XpressHuffSpecPosition(&Bits);
        if (InputPosition > InputSize)
            return 0;
    }

    *InputUsed = InputPosition;
    return 1;
}

static WIM_CODEC_STATUS
WimXpressHuffmanCreate(
    const WIM_CODEC_OPTIONS *Options,
    WIM_CODEC_DIRECTION Direction,
    void **State)
{
    XPRESS_HUFF_STATE *NewState;

    if (State == NULL || Options == NULL ||
        Direction != WIM_CODEC_DECODE ||
        Options->ExpectedOutputSize == 0 ||
        Options->ExpectedOutputSize > (uint64_t)(size_t)-1 ||
        (Options->MaximumOutputSize != 0 &&
         Options->ExpectedOutputSize > Options->MaximumOutputSize))
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;

    NewState = (XPRESS_HUFF_STATE *)Options->Allocator.Allocate(
        Options->Allocator.Context, sizeof(*NewState));
    if (NewState == NULL)
        return WIM_CODEC_STATUS_OUT_OF_MEMORY;

    memset(NewState, 0, sizeof(*NewState));
    NewState->Allocator = Options->Allocator;
    NewState->ExpectedOutputSize = Options->ExpectedOutputSize;
    NewState->MaximumOutputSize = Options->MaximumOutputSize;
    *State = NewState;
    return WIM_CODEC_STATUS_OK;
}

static void
WimXpressHuffmanDestroy(void *State)
{
    XPRESS_HUFF_STATE *CodecState = (XPRESS_HUFF_STATE *)State;
    if (CodecState != NULL)
        CodecState->Allocator.Free(CodecState->Allocator.Context,
                                   CodecState);
}

static WIM_CODEC_STATUS
WimXpressHuffmanProcess(
    void *State,
    const uint8_t *Input,
    size_t InputSize,
    size_t *InputUsed,
    uint8_t *Output,
    size_t OutputSize,
    size_t *OutputUsed,
    uint32_t ProcessFlags)
{
    XPRESS_HUFF_STATE *CodecState = (XPRESS_HUFF_STATE *)State;
    size_t Consumed = 0;
    size_t Expected;

    if (InputUsed != NULL) *InputUsed = 0;
    if (OutputUsed != NULL) *OutputUsed = 0;
    if (CodecState == NULL || InputUsed == NULL || OutputUsed == NULL ||
        (InputSize != 0 && Input == NULL) ||
        (OutputSize != 0 && Output == NULL) ||
        ProcessFlags != WIM_CODEC_PROCESS_FINAL)
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    if (CodecState->Completed)
        return WIM_CODEC_STATUS_STREAM_END;

    Expected = (size_t)CodecState->ExpectedOutputSize;
    if (OutputSize < Expected)
        return WIM_CODEC_STATUS_NEED_OUTPUT;
    if (CodecState->MaximumOutputSize != 0 &&
        CodecState->ExpectedOutputSize > CodecState->MaximumOutputSize)
        return WIM_CODEC_STATUS_CORRUPT_DATA;

    if (!XpressHuffDecode(Input, InputSize, Output, Expected, &Consumed))
        return WIM_CODEC_STATUS_CORRUPT_DATA;

    *InputUsed = Consumed;
    *OutputUsed = Expected;
    CodecState->Completed = 1;
    return WIM_CODEC_STATUS_STREAM_END;
}

static const WIM_CODEC_PROVIDER WimXpressHuffmanProvider = {
    WIM_CODEC_ABI_VERSION,
    sizeof(WIM_CODEC_PROVIDER),
    WIM_CODEC_XPRESS_HUFFMAN,
    WIM_CODEC_CAP_DECODE,
    "XPRESS-Huffman (bounded block decoder)",
    WimXpressHuffmanCreate,
    WimXpressHuffmanDestroy,
    WimXpressHuffmanProcess,
    NULL,
    NULL,
    NULL
};

WIM_CODEC_STATUS
WimCodecRegisterXpressHuffmanProvider(void)
{
    return WimCodecRegisterProvider(&WimXpressHuffmanProvider);
}
