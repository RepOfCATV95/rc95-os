#include <stdlib.h>
#include <string.h>
#include <libwim/wim_codec.h>

#define LZX_WINDOW_SIZE 32768u
#define LZX_NUM_CHARS 256u
#define LZX_NUM_PRIMARY_LENS 7u
#define LZX_LEN_SLOTS 8u
#define LZX_LENGTH_SYMBOLS 249u
#define LZX_PRETREE_SYMBOLS 20u
#define LZX_ALIGNED_SYMBOLS 8u
#define LZX_MAX_CODE_BITS 16u
#define LZX_MAX_SLOTS 66u
#define LZX_E8_FILE_SIZE 12000000

typedef struct _WIM_LZX_STATE
{
    WIM_CODEC_ALLOCATOR Allocator;
    uint64_t ExpectedOutputSize;
    uint64_t MaximumOutputSize;
    int Completed;
} WIM_LZX_STATE;

typedef struct _WIM_LZX_BITS
{
    const uint8_t *Input;
    size_t InputSize;
    size_t Position;
    uint64_t Buffer;
    unsigned Count;
} WIM_LZX_BITS;

typedef struct _WIM_LZX_HUFFMAN
{
    uint32_t FirstCode[LZX_MAX_CODE_BITS + 1];
    uint16_t FirstIndex[LZX_MAX_CODE_BITS + 1];
    uint16_t Count[LZX_MAX_CODE_BITS + 1];
    uint16_t Sorted[512];
    unsigned SymbolCount;
} WIM_LZX_HUFFMAN;

typedef struct _WIM_LZX_SLOTS
{
    uint32_t Base[LZX_MAX_SLOTS];
    uint8_t Extra[LZX_MAX_SLOTS];
    unsigned Count;
} WIM_LZX_SLOTS;

static int
LzxReadBits(WIM_LZX_BITS *Bits, unsigned Count, uint32_t *Value)
{
    uint32_t Mask;
    if (Count > 16 || Value == NULL)
        return 0;
    while (Bits->Count < Count)
    {
        uint16_t Word;
        if (Bits->Position > Bits->InputSize ||
            Bits->InputSize - Bits->Position < 2)
            return 0;
        Word = (uint16_t)(Bits->Input[Bits->Position] |
                         ((uint16_t)Bits->Input[Bits->Position + 1] << 8));
        Bits->Position += 2;
        Bits->Buffer = (Bits->Buffer << 16) | Word;
        Bits->Count += 16;
    }
    if (Count == 0)
    {
        *Value = 0;
        return 1;
    }
    Mask = ((uint32_t)1 << Count) - 1;
    *Value = (uint32_t)((Bits->Buffer >> (Bits->Count - Count)) & Mask);
    Bits->Count -= Count;
    if (Bits->Count == 0)
        Bits->Buffer = 0;
    else
        Bits->Buffer &= (((uint64_t)1 << Bits->Count) - 1);
    return 1;
}

static int
LzxPeekBits(WIM_LZX_BITS *Bits, unsigned Count, uint32_t *Value)
{
    if (Count == 0 || Count > LZX_MAX_CODE_BITS || Value == NULL)
        return 0;
    while (Bits->Count < Count)
    {
        uint16_t Word;
        if (Bits->Position > Bits->InputSize ||
            Bits->InputSize - Bits->Position < 2)
            return 0;
        Word = (uint16_t)(Bits->Input[Bits->Position] |
                         ((uint16_t)Bits->Input[Bits->Position + 1] << 8));
        Bits->Position += 2;
        Bits->Buffer = (Bits->Buffer << 16) | Word;
        Bits->Count += 16;
    }
    *Value = (uint32_t)((Bits->Buffer >> (Bits->Count - Count)) &
                        (((uint32_t)1 << Count) - 1));
    return 1;
}

static void
LzxDropBits(WIM_LZX_BITS *Bits, unsigned Count)
{
    Bits->Count -= Count;
    if (Bits->Count == 0)
        Bits->Buffer = 0;
    else
        Bits->Buffer &= (((uint64_t)1 << Bits->Count) - 1);
}

/* The LZX uncompressed-block alignment consumes a word even when already
 * aligned; this matches the WIM LZX bitstream convention used by wimlib. */
static int
LzxAlignToWord(WIM_LZX_BITS *Bits)
{
    if (Bits->Count == 0)
    {
        uint32_t Discard;
        if (!LzxReadBits(Bits, 1, &Discard))
            return 0;
    }
    Bits->Buffer = 0;
    Bits->Count = 0;
    return 1;
}

static int
LzxReadRaw(WIM_LZX_BITS *Bits, uint8_t *Output, size_t Size)
{
    if (Bits->Count != 0 || (Bits->Position & 1) != 0 ||
        Bits->Position > Bits->InputSize ||
        Size > Bits->InputSize - Bits->Position)
        return 0;
    memcpy(Output, Bits->Input + Bits->Position, Size);
    Bits->Position += Size;
    return 1;
}

static int
LzxReadRaw32(WIM_LZX_BITS *Bits, uint32_t *Value)
{
    const uint8_t *P;
    if (Bits->Count != 0 || (Bits->Position & 1) != 0 ||
        Bits->Position > Bits->InputSize ||
        Bits->InputSize - Bits->Position < 4)
        return 0;
    P = Bits->Input + Bits->Position;
    *Value = (uint32_t)P[0] | ((uint32_t)P[1] << 8) |
             ((uint32_t)P[2] << 16) | ((uint32_t)P[3] << 24);
    Bits->Position += 4;
    return 1;
}

static int
LzxBuildHuffman(
    WIM_LZX_HUFFMAN *Table,
    const uint8_t *Lengths,
    unsigned SymbolCount)
{
    uint16_t Counts[LZX_MAX_CODE_BITS + 1] = {0};
    uint16_t Next[LZX_MAX_CODE_BITS + 1] = {0};
    uint32_t Code = 0;
    uint32_t Used = 0;
    int32_t Remainder = 1;
    unsigned I, Length;

    if (SymbolCount == 0 || SymbolCount > 512)
        return 0;
    memset(Table, 0, sizeof(*Table));
    for (I = 0; I < SymbolCount; ++I)
    {
        if (Lengths[I] > LZX_MAX_CODE_BITS)
            return 0;
        if (Lengths[I] != 0)
            ++Counts[Lengths[I]];
    }
    for (Length = 1; Length <= LZX_MAX_CODE_BITS; ++Length)
    {
        Code = (Code + Counts[Length - 1]) << 1;
        Remainder = (Remainder << 1) - Counts[Length];
        if (Remainder < 0)
            return 0;
        Table->FirstCode[Length] = Code;
        Table->FirstIndex[Length] = (uint16_t)Used;
        Table->Count[Length] = Counts[Length];
        Used += Counts[Length];
    }
    /* LZX permits an empty tree when that tree is not used by this block,
       but every non-empty canonical tree must fill its code space. */
    if (Used == 0)
        return 1;
    if (Remainder != 0)
        return 0;
    memcpy(Next, Table->FirstIndex, sizeof(Next));
    for (I = 0; I < SymbolCount; ++I)
    {
        Length = Lengths[I];
        if (Length != 0)
            Table->Sorted[Next[Length]++] = (uint16_t)I;
    }
    Table->SymbolCount = SymbolCount;
    return 1;
}

static int
LzxDecodeSymbol(
    const WIM_LZX_HUFFMAN *Table,
    WIM_LZX_BITS *Bits,
    unsigned *Symbol)
{
    unsigned Length;
    for (Length = 1; Length <= LZX_MAX_CODE_BITS; ++Length)
    {
        uint32_t Code, First, Delta;
        if (!LzxPeekBits(Bits, Length, &Code))
            return 0;
        First = Table->FirstCode[Length];
        if (Code >= First)
        {
            Delta = Code - First;
            if (Delta < Table->Count[Length])
            {
                *Symbol = Table->Sorted[Table->FirstIndex[Length] + Delta];
                LzxDropBits(Bits, Length);
                return 1;
            }
        }
    }
    return 0;
}

static int
LzxReadCodeLengths(
    WIM_LZX_BITS *Bits,
    uint8_t *Lengths,
    unsigned First,
    unsigned Last)
{
    uint8_t PretreeLengths[LZX_PRETREE_SYMBOLS];
    WIM_LZX_HUFFMAN Pretree;
    unsigned I, Symbol;

    for (I = 0; I < LZX_PRETREE_SYMBOLS; ++I)
    {
        uint32_t V;
        if (!LzxReadBits(Bits, 4, &V))
            return 0;
        PretreeLengths[I] = (uint8_t)V;
    }
    if (!LzxBuildHuffman(&Pretree, PretreeLengths, LZX_PRETREE_SYMBOLS))
        return 0;

    I = First;
    while (I < Last)
    {
        unsigned Run, Delta;
        uint8_t NewLength;
        if (!LzxDecodeSymbol(&Pretree, Bits, &Symbol))
            return 0;
        if (Symbol <= 16)
        {
            NewLength = (uint8_t)((Lengths[I] + 17 - Symbol) % 17);
            Lengths[I++] = NewLength;
            continue;
        }
        if (Symbol == 17 || Symbol == 18)
        {
            uint32_t V;
            unsigned Extra = Symbol == 17 ? 4 : 5;
            unsigned Base = Symbol == 17 ? 4 : 20;
            if (!LzxReadBits(Bits, Extra, &V))
                return 0;
            Run = Base + V;
            NewLength = 0;
        }
        else if (Symbol == 19)
        {
            uint32_t V;
            if (!LzxReadBits(Bits, 1, &V))
                return 0;
            Run = 4 + V;
            if (!LzxDecodeSymbol(&Pretree, Bits, &Delta) || Delta > 16)
                return 0;
            NewLength = (uint8_t)((Lengths[I] + 17 - Delta) % 17);
        }
        else
            return 0;

        if (Run > Last - I)
            return 0;
        while (Run-- != 0)
            Lengths[I++] = NewLength;
    }
    return 1;
}

static int
LzxBuildSlots(WIM_LZX_SLOTS *Slots)
{
    uint32_t Base = 0;
    unsigned Slot;
    memset(Slots, 0, sizeof(*Slots));
    for (Slot = 0; Slot < LZX_MAX_SLOTS; ++Slot)
    {
        unsigned Extra = Slot < 4 ? 0 : (Slot >> 1) - 1;
        if (Extra > 17)
            Extra = 17;
        if (Base >= LZX_WINDOW_SIZE)
            break;
        Slots->Base[Slot] = Base;
        Slots->Extra[Slot] = (uint8_t)Extra;
        Base += (uint32_t)1 << Extra;
        ++Slots->Count;
    }
    return Slots->Count != 0 && LZX_NUM_CHARS + Slots->Count * LZX_LEN_SLOTS <= 512;
}

static void
LzxUndoE8(uint8_t *Output, size_t Size)
{
    size_t I, End;
    if (Size <= 10)
        return;
    End = Size - 10;
    I = 0;
    while (I < End)
    {
        if (Output[I] == 0xE8)
        {
            int32_t SignedTarget = (int32_t)(
                (uint32_t)Output[I + 1] |
                ((uint32_t)Output[I + 2] << 8) |
                ((uint32_t)Output[I + 3] << 16) |
                ((uint32_t)Output[I + 4] << 24));
            int64_t Target = SignedTarget;
            if (Target >= -(int64_t)I && Target < LZX_E8_FILE_SIZE)
            {
                int64_t Relative = Target >= 0
                    ? Target - (int64_t)I
                    : Target + LZX_E8_FILE_SIZE;
                uint32_t V = (uint32_t)(int32_t)Relative;
                Output[I + 1] = (uint8_t)V;
                Output[I + 2] = (uint8_t)(V >> 8);
                Output[I + 3] = (uint8_t)(V >> 16);
                Output[I + 4] = (uint8_t)(V >> 24);
            }
            I += 5;
        }
        else
            ++I;
    }
}

static int
LzxDecodeChunk(
    const uint8_t *Input,
    size_t InputSize,
    uint8_t *Output,
    size_t OutputSize)
{
    WIM_LZX_BITS Bits;
    WIM_LZX_SLOTS Slots;
    WIM_LZX_HUFFMAN MainTable, LengthTable, AlignedTable;
    uint8_t MainLengths[512] = {0};
    uint8_t LengthLengths[LZX_LENGTH_SYMBOLS] = {0};
    uint8_t AlignedLengths[LZX_ALIGNED_SYMBOLS];
    uint32_t Recent[3] = {1, 1, 1};
    size_t OutPos = 0;
    unsigned MainSymbols;

    if (Input == NULL || Output == NULL || OutputSize == 0 ||
        OutputSize > LZX_WINDOW_SIZE || !LzxBuildSlots(&Slots))
        return 0;
    MainSymbols = LZX_NUM_CHARS + Slots.Count * LZX_LEN_SLOTS;
    Bits.Input = Input;
    Bits.InputSize = InputSize;
    Bits.Position = 0;
    Bits.Buffer = 0;
    Bits.Count = 0;

    while (OutPos < OutputSize)
    {
        uint32_t V;
        unsigned BlockType;
        size_t BlockSize, BlockEnd;

        if (!LzxReadBits(&Bits, 3, &V))
            return 0;
        BlockType = V;
        if (!LzxReadBits(&Bits, 1, &V))
            return 0;
        if (V != 0)
            BlockSize = LZX_WINDOW_SIZE;
        else
        {
            if (!LzxReadBits(&Bits, 16, &V))
                return 0;
            BlockSize = V;
        }
        if (BlockSize == 0 || BlockSize > OutputSize - OutPos)
            return 0;
        BlockEnd = OutPos + BlockSize;

        if (BlockType == 3)
        {
            unsigned I;
            if (!LzxAlignToWord(&Bits))
                return 0;
            for (I = 0; I < 3; ++I)
            {
                if (!LzxReadRaw32(&Bits, &Recent[I]) ||
                    Recent[I] == 0 || Recent[I] > LZX_WINDOW_SIZE)
                    return 0;
            }
            if (!LzxReadRaw(&Bits, Output + OutPos, BlockSize))
                return 0;
            OutPos = BlockEnd;
            if ((BlockSize & 1) != 0)
            {
                if (Bits.Position >= Bits.InputSize)
                    return 0;
                ++Bits.Position;
            }
            continue;
        }
        if (BlockType != 1 && BlockType != 2)
            return 0;

        if (BlockType == 2)
        {
            unsigned I;
            for (I = 0; I < LZX_ALIGNED_SYMBOLS; ++I)
            {
                if (!LzxReadBits(&Bits, 3, &V))
                    return 0;
                AlignedLengths[I] = (uint8_t)V;
            }
            if (!LzxBuildHuffman(&AlignedTable, AlignedLengths,
                                 LZX_ALIGNED_SYMBOLS))
                return 0;
        }

        if (!LzxReadCodeLengths(&Bits, MainLengths, 0, LZX_NUM_CHARS) ||
            !LzxReadCodeLengths(&Bits, MainLengths, LZX_NUM_CHARS,
                                MainSymbols) ||
            !LzxReadCodeLengths(&Bits, LengthLengths, 0,
                                LZX_LENGTH_SYMBOLS) ||
            !LzxBuildHuffman(&MainTable, MainLengths, MainSymbols) ||
            !LzxBuildHuffman(&LengthTable, LengthLengths,
                             LZX_LENGTH_SYMBOLS))
            return 0;

        while (OutPos < BlockEnd)
        {
            unsigned Symbol, LengthHeader, Slot;
            size_t MatchLength, Offset;
            uint32_t Low = 0;

            if (!LzxDecodeSymbol(&MainTable, &Bits, &Symbol))
                return 0;
            if (Symbol < LZX_NUM_CHARS)
            {
                Output[OutPos++] = (uint8_t)Symbol;
                continue;
            }

            Symbol -= LZX_NUM_CHARS;
            LengthHeader = Symbol % LZX_LEN_SLOTS;
            Slot = Symbol / LZX_LEN_SLOTS;
            if (Slot >= Slots.Count)
                return 0;
            MatchLength = LengthHeader;
            if (LengthHeader == LZX_NUM_PRIMARY_LENS)
            {
                unsigned LengthSymbol;
                if (!LzxDecodeSymbol(&LengthTable, &Bits, &LengthSymbol) ||
                    LengthSymbol >= LZX_LENGTH_SYMBOLS)
                    return 0;
                MatchLength += LengthSymbol;
            }
            MatchLength += 2;
            if (MatchLength > BlockEnd - OutPos)
                return 0;

            if (Slot < 3)
            {
                Offset = Recent[Slot];
                Recent[Slot] = Recent[0];
            }
            else
            {
                unsigned Extra = Slots.Extra[Slot];
                if (BlockType == 2 && Extra >= 3)
                {
                    uint32_t High;
                    unsigned Aligned;
                    if (!LzxReadBits(&Bits, Extra - 3, &High) ||
                        !LzxDecodeSymbol(&AlignedTable, &Bits, &Aligned))
                        return 0;
                    Low = (High << 3) | Aligned;
                }
                else if (!LzxReadBits(&Bits, Extra, &Low))
                    return 0;

                if ((uint64_t)Slots.Base[Slot] + Low < 2)
                    return 0;
                Offset = (size_t)(Slots.Base[Slot] + Low - 2);
                Recent[2] = Recent[1];
                Recent[1] = Recent[0];
            }
            Recent[0] = (uint32_t)Offset;
            if (Offset == 0 || Offset > OutPos ||
                Offset > LZX_WINDOW_SIZE)
                return 0;
            while (MatchLength-- != 0)
            {
                Output[OutPos] = Output[OutPos - Offset];
                ++OutPos;
            }
        }
    }

    LzxUndoE8(Output, OutputSize);
    return 1;
}

static WIM_CODEC_STATUS
WimLzxCreate(
    const WIM_CODEC_OPTIONS *Options,
    WIM_CODEC_DIRECTION Direction,
    void **State)
{
    WIM_LZX_STATE *NewState;
    if (State == NULL || Options == NULL ||
        Direction != WIM_CODEC_DECODE ||
        Options->ExpectedOutputSize == 0 ||
        Options->ExpectedOutputSize > LZX_WINDOW_SIZE ||
        Options->ExpectedOutputSize > (uint64_t)(size_t)-1 ||
        (Options->MaximumOutputSize != 0 &&
         Options->ExpectedOutputSize > Options->MaximumOutputSize))
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;

    NewState = (WIM_LZX_STATE *)Options->Allocator.Allocate(
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
WimLzxDestroy(void *State)
{
    WIM_LZX_STATE *CodecState = (WIM_LZX_STATE *)State;
    if (CodecState != NULL)
        CodecState->Allocator.Free(CodecState->Allocator.Context, CodecState);
}

static WIM_CODEC_STATUS
WimLzxProcess(
    void *State,
    const uint8_t *Input,
    size_t InputSize,
    size_t *InputUsed,
    uint8_t *Output,
    size_t OutputSize,
    size_t *OutputUsed,
    uint32_t ProcessFlags)
{
    WIM_LZX_STATE *CodecState = (WIM_LZX_STATE *)State;
    size_t Expected;
    if (InputUsed != NULL) *InputUsed = 0;
    if (OutputUsed != NULL) *OutputUsed = 0;
    if (CodecState == NULL || InputUsed == NULL || OutputUsed == NULL ||
        Input == NULL || Output == NULL ||
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
    if (!LzxDecodeChunk(Input, InputSize, Output, Expected))
        return WIM_CODEC_STATUS_CORRUPT_DATA;

    /* WIM's resource layer treats the compressed chunk as one independent
       stream; all bytes belong to this chunk, including word padding. */
    *InputUsed = InputSize;
    *OutputUsed = Expected;
    CodecState->Completed = 1;
    return WIM_CODEC_STATUS_STREAM_END;
}

static const WIM_CODEC_PROVIDER WimLzxProvider = {
    WIM_CODEC_ABI_VERSION,
    sizeof(WIM_CODEC_PROVIDER),
    WIM_CODEC_LZX,
    WIM_CODEC_CAP_DECODE | WIM_CODEC_CAP_WIM_CHUNKS,
    "WIM LZX (32 KiB independent chunk decoder)",
    WimLzxCreate,
    WimLzxDestroy,
    WimLzxProcess,
    NULL,
    NULL,
    NULL
};

WIM_CODEC_STATUS
WimCodecRegisterLzxProvider(void)
{
    return WimCodecRegisterProvider(&WimLzxProvider);
}
