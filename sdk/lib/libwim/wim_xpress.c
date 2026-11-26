/*
 * PROJECT:    RC95-OS libwim
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    XPRESS LZ77 decoder provider.
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95
 */
#include <string.h>
#include <libwim/wim_codec.h>

typedef struct _WIM_XPRESS_STATE
{
    WIM_CODEC_ALLOCATOR Allocator;
    uint64_t ExpectedOutputSize;
    uint64_t MaximumOutputSize;
    WIM_CODEC_DIRECTION Direction;
    uint8_t Completed;
} WIM_XPRESS_STATE;

static int
WimXpressReadByte(const uint8_t *Input, size_t InputSize,
                  size_t *Position, uint8_t *Value)
{
    if (*Position >= InputSize)
        return 0;
    *Value = Input[(*Position)++];
    return 1;
}

static int
WimXpressRead16(const uint8_t *Input, size_t InputSize,
                size_t *Position, uint16_t *Value)
{
    if (InputSize - *Position < 2)
        return 0;
    *Value = (uint16_t)(Input[*Position] |
                        ((uint16_t)Input[*Position + 1] << 8));
    *Position += 2;
    return 1;
}

static int
WimXpressRead32(const uint8_t *Input, size_t InputSize,
                size_t *Position, uint32_t *Value)
{
    if (InputSize - *Position < 4)
        return 0;
    *Value = (uint32_t)Input[*Position] |
             ((uint32_t)Input[*Position + 1] << 8) |
             ((uint32_t)Input[*Position + 2] << 16) |
             ((uint32_t)Input[*Position + 3] << 24);
    *Position += 4;
    return 1;
}

/*
 * Decode one complete XPRESS LZ77 chunk.  XPRESS stores a little-endian
 * 32-bit indicator word followed by up to 32 items, least-significant bit
 * first.  A zero bit is a literal; a one bit is a 16-bit (13-bit distance,
 * 3-bit length) match.  The distance field is biased by one and the length
 * has the format's byte/word/dword extension chain.
 *
 * This provider deliberately exposes whole-chunk decoding only.  WIM chunk
 * framing already supplies the exact uncompressed size, so accepting a
 * partial final token would only hide truncated or malformed resources.
 */
static WIM_CODEC_STATUS
WimXpressDecode(const uint8_t *Input, size_t InputSize,
                uint8_t *Output, size_t OutputSize,
                size_t ExpectedOutputSize, size_t *InputUsed,
                size_t *OutputUsed)
{
    size_t InPos = 0, OutPos = 0;
    uint32_t Indicators = 0;
    unsigned IndicatorBits = 0;

    while (OutPos < ExpectedOutputSize)
    {
        if (IndicatorBits == 0)
        {
            if (!WimXpressRead32(Input, InputSize, &InPos, &Indicators))
                return WIM_CODEC_STATUS_CORRUPT_DATA;
            IndicatorBits = 32;
        }

        if ((Indicators & 1u) == 0)
        {
            uint8_t Literal;
            if (!WimXpressReadByte(Input, InputSize, &InPos, &Literal))
                return WIM_CODEC_STATUS_CORRUPT_DATA;
            Output[OutPos++] = Literal;
        }
        else
        {
            uint16_t Match;
            uint64_t Length;
            size_t Distance;
            uint8_t Extension;

            if (!WimXpressRead16(Input, InputSize, &InPos, &Match))
                return WIM_CODEC_STATUS_CORRUPT_DATA;

            Distance = (size_t)(Match >> 3) + 1;
            Length = (uint64_t)(Match & 7u) + 3;

            if ((Match & 7u) == 7u)
            {
                if (!WimXpressReadByte(Input, InputSize, &InPos, &Extension))
                    return WIM_CODEC_STATUS_CORRUPT_DATA;
                if (Extension != 255u)
                {
                    Length += Extension;
                }
                else
                {
                    uint16_t Extended16;
                    if (!WimXpressRead16(Input, InputSize, &InPos,
                                         &Extended16))
                        return WIM_CODEC_STATUS_CORRUPT_DATA;

                    if (Extended16 != 0)
                    {
                        Length += Extended16;
                    }
                    else
                    {
                        uint32_t Extended32;
                        if (!WimXpressRead32(Input, InputSize, &InPos,
                                             &Extended32))
                            return WIM_CODEC_STATUS_CORRUPT_DATA;
                        Length += Extended32;
                    }
                }
            }

            if (Distance > OutPos || Length > ExpectedOutputSize - OutPos)
                return WIM_CODEC_STATUS_CORRUPT_DATA;

            while (Length-- != 0)
            {
                Output[OutPos] = Output[OutPos - Distance];
                ++OutPos;
            }
        }

        Indicators >>= 1;
        --IndicatorBits;
    }

    if (OutPos != ExpectedOutputSize || OutPos > OutputSize)
        return WIM_CODEC_STATUS_CORRUPT_DATA;

    *InputUsed = InPos;
    *OutputUsed = OutPos;
    return WIM_CODEC_STATUS_STREAM_END;
}

static uint32_t
WimXpressHash3(const uint8_t *P)
{
    return (((uint32_t)P[0] * 251u + P[1]) * 251u + P[2]) & 0xFFFFu;
}

static int
WimXpressPutByte(uint8_t *Output, size_t Capacity, size_t *Position,
                 uint8_t Value)
{
    if (*Position >= Capacity)
        return 0;
    Output[(*Position)++] = Value;
    return 1;
}

static int
WimXpressPut16(uint8_t *Output, size_t Capacity, size_t *Position,
               uint16_t Value)
{
    return WimXpressPutByte(Output, Capacity, Position, (uint8_t)Value) &&
           WimXpressPutByte(Output, Capacity, Position, (uint8_t)(Value >> 8));
}

static int
WimXpressPut32(uint8_t *Output, size_t Capacity, size_t *Position,
               uint32_t Value)
{
    return WimXpressPut16(Output, Capacity, Position, (uint16_t)Value) &&
           WimXpressPut16(Output, Capacity, Position, (uint16_t)(Value >> 16));
}

/*
 * Greedy XPRESS encoder. A 16-bit hash table and per-position predecessor
 * chain bound the search to 256 candidates per token and the format's 8192
 * byte window. It emits independent 32-token indicator groups.
 */
static WIM_CODEC_STATUS
WimXpressEncode(const uint8_t *Input, size_t InputSize,
                uint8_t *Output, size_t OutputSize,
                size_t *InputUsed, size_t *OutputUsed,
                const WIM_CODEC_ALLOCATOR *Allocator)
{
    int32_t *Heads = NULL, *Previous = NULL;
    size_t InPos = 0, OutPos = 0;
    WIM_CODEC_STATUS Status = WIM_CODEC_STATUS_NEED_OUTPUT;

    *InputUsed = 0;
    *OutputUsed = 0;
    if (InputSize > (size_t)INT32_MAX ||
        InputSize > (size_t)-1 / sizeof(*Previous))
        return WIM_CODEC_STATUS_BUFFER_ERROR;

    Heads = (int32_t *)Allocator->Allocate(Allocator->Context,
                                           65536u * sizeof(*Heads));
    if (Heads == NULL)
        return WIM_CODEC_STATUS_OUT_OF_MEMORY;
    Previous = (int32_t *)Allocator->Allocate(
        Allocator->Context, (InputSize ? InputSize : 1) * sizeof(*Previous));
    if (Previous == NULL)
    {
        Allocator->Free(Allocator->Context, Heads);
        return WIM_CODEC_STATUS_OUT_OF_MEMORY;
    }
    memset(Heads, 0xFF, 65536u * sizeof(*Heads));

    while (InPos < InputSize)
    {
        size_t IndicatorPosition = OutPos;
        uint32_t Indicators = 0;
        unsigned Bit;

        if (OutputSize - OutPos < 4)
            goto Cleanup;
        OutPos += 4;

        for (Bit = 0; Bit < 32 && InPos < InputSize; ++Bit)
        {
            size_t BestLength = 0, BestDistance = 0;
            int32_t Candidate = -1;
            uint32_t Hash = 0;
            unsigned Attempts = 0;

            if (InputSize - InPos >= 3)
            {
                Hash = WimXpressHash3(Input + InPos);
                Candidate = Heads[Hash];
                while (Candidate >= 0 && Attempts++ < 256)
                {
                    size_t CandidatePos = (size_t)Candidate;
                    size_t Distance = InPos - CandidatePos;
                    size_t Length = 0;

                    if (Distance > 8192)
                        break;
                    while (Length < InputSize - InPos &&
                           Input[CandidatePos + Length] == Input[InPos + Length])
                        ++Length;
                    if (Length > BestLength && Length >= 3)
                    {
                        BestLength = Length;
                        BestDistance = Distance;
                        if (Length == InputSize - InPos)
                            break;
                    }
                    Candidate = Previous[CandidatePos];
                }
            }

            if (BestLength >= 3)
            {
                size_t Extra = 0;
                uint16_t Token = (uint16_t)(((BestDistance - 1) << 3) |
                                            (BestLength >= 10 ? 7 :
                                             BestLength - 3));
                if (!WimXpressPut16(Output, OutputSize, &OutPos, Token))
                    goto Cleanup;
                if (BestLength >= 10)
                {
                    Extra = BestLength - 10;
                    if (Extra < 255)
                    {
                        if (!WimXpressPutByte(Output, OutputSize, &OutPos,
                                              (uint8_t)Extra))
                            goto Cleanup;
                    }
                    else
                    {
                        if (!WimXpressPutByte(Output, OutputSize, &OutPos, 255))
                            goto Cleanup;
                        if (Extra <= 65535)
                        {
                            if (!WimXpressPut16(Output, OutputSize, &OutPos,
                                                (uint16_t)Extra))
                                goto Cleanup;
                        }
                        else
                        {
                            if (!WimXpressPut16(Output, OutputSize, &OutPos, 0) ||
                                !WimXpressPut32(Output, OutputSize, &OutPos,
                                                (uint32_t)Extra))
                                goto Cleanup;
                        }
                    }
                }

                while (BestLength-- != 0)
                {
                    if (InputSize - InPos >= 3)
                    {
                        uint32_t InsertHash = WimXpressHash3(Input + InPos);
                        Previous[InPos] = Heads[InsertHash];
                        Heads[InsertHash] = (int32_t)InPos;
                    }
                    ++InPos;
                }
                Indicators |= (uint32_t)1u << Bit;
            }
            else
            {
                if (!WimXpressPutByte(Output, OutputSize, &OutPos,
                                      Input[InPos]))
                    goto Cleanup;
                if (InputSize - InPos >= 3)
                {
                    Previous[InPos] = Heads[Hash];
                    Heads[Hash] = (int32_t)InPos;
                }
                ++InPos;
            }
        }

        Output[IndicatorPosition] = (uint8_t)Indicators;
        Output[IndicatorPosition + 1] = (uint8_t)(Indicators >> 8);
        Output[IndicatorPosition + 2] = (uint8_t)(Indicators >> 16);
        Output[IndicatorPosition + 3] = (uint8_t)(Indicators >> 24);
    }

    *InputUsed = InputSize;
    *OutputUsed = OutPos;
    Status = WIM_CODEC_STATUS_STREAM_END;

Cleanup:
    Allocator->Free(Allocator->Context, Previous);
    Allocator->Free(Allocator->Context, Heads);
    return Status;
}

static size_t
WimXpressMaximumCompressedSize(size_t InputSize, uint32_t ChunkSize)
{
    size_t Groups = InputSize / 32 + ((InputSize % 32) != 0);
    (void)ChunkSize;
    if (Groups > ((size_t)-1 - InputSize) / 4)
        return 0;
    return InputSize + Groups * 4;
}

static WIM_CODEC_STATUS
WimXpressCreate(const WIM_CODEC_OPTIONS *Options,
                WIM_CODEC_DIRECTION Direction, void **State)
{
    WIM_XPRESS_STATE *S;

    if (State == NULL)
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    *State = NULL;

    if (Options == NULL ||
        (Direction != WIM_CODEC_DECODE && Direction != WIM_CODEC_ENCODE) ||
        (Direction == WIM_CODEC_DECODE &&
         (Options->ExpectedOutputSize == 0 ||
          Options->ExpectedOutputSize > (uint64_t)((size_t)-1) ||
          (Options->MaximumOutputSize != 0 &&
           Options->ExpectedOutputSize > Options->MaximumOutputSize))))
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;

    S = (WIM_XPRESS_STATE *)Options->Allocator.Allocate(
        Options->Allocator.Context, sizeof(*S));
    if (S == NULL)
        return WIM_CODEC_STATUS_OUT_OF_MEMORY;

    memset(S, 0, sizeof(*S));
    S->Allocator = Options->Allocator;
    S->ExpectedOutputSize = Options->ExpectedOutputSize;
    S->MaximumOutputSize = Options->MaximumOutputSize;
    S->Direction = Direction;
    *State = S;
    return WIM_CODEC_STATUS_OK;
}

static void
WimXpressDestroy(void *State)
{
    WIM_XPRESS_STATE *S = (WIM_XPRESS_STATE *)State;
    WIM_CODEC_ALLOCATOR Allocator;

    if (S == NULL)
        return;
    Allocator = S->Allocator;
    memset(S, 0, sizeof(*S));
    Allocator.Free(Allocator.Context, S);
}

static WIM_CODEC_STATUS
WimXpressReset(void *State)
{
    WIM_XPRESS_STATE *S = (WIM_XPRESS_STATE *)State;

    if (S == NULL)
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    S->Completed = 0;
    return WIM_CODEC_STATUS_OK;
}

static WIM_CODEC_STATUS
WimXpressProcess(void *State, const uint8_t *Input, size_t InputSize,
                 size_t *InputUsed, uint8_t *Output, size_t OutputSize,
                 size_t *OutputUsed, uint32_t ProcessFlags)
{
    WIM_XPRESS_STATE *S = (WIM_XPRESS_STATE *)State;
    size_t Consumed = 0, Produced = 0;
    WIM_CODEC_STATUS Status;

    if (InputUsed != NULL)
        *InputUsed = 0;
    if (OutputUsed != NULL)
        *OutputUsed = 0;

    if (S == NULL || InputUsed == NULL || OutputUsed == NULL ||
        (InputSize != 0 && Input == NULL) ||
        (OutputSize != 0 && Output == NULL) ||
        (ProcessFlags & ~WIM_CODEC_PROCESS_FINAL) != 0)
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;

    if (S->Completed)
        return WIM_CODEC_STATUS_STREAM_END;
    if ((ProcessFlags & WIM_CODEC_PROCESS_FINAL) == 0)
        return WIM_CODEC_STATUS_NEED_INPUT;
    if (S->Direction == WIM_CODEC_DECODE &&
        S->ExpectedOutputSize > OutputSize)
        return WIM_CODEC_STATUS_NEED_OUTPUT;
    if (S->Direction == WIM_CODEC_DECODE &&
        S->MaximumOutputSize != 0 &&
        S->ExpectedOutputSize > S->MaximumOutputSize)
        return WIM_CODEC_STATUS_BUFFER_ERROR;

    if (S->Direction == WIM_CODEC_ENCODE)
    {
        size_t EncodeCapacity = OutputSize;
        if (S->MaximumOutputSize != 0)
        {
            if (S->MaximumOutputSize > (uint64_t)((size_t)-1))
                return WIM_CODEC_STATUS_BUFFER_ERROR;
            if ((size_t)S->MaximumOutputSize < EncodeCapacity)
                EncodeCapacity = (size_t)S->MaximumOutputSize;
        }
        Status = WimXpressEncode(Input, InputSize, Output, EncodeCapacity,
                                 &Consumed, &Produced, &S->Allocator);
    }
    else
    {
        Status = WimXpressDecode(Input, InputSize, Output, OutputSize,
                                 (size_t)S->ExpectedOutputSize,
                                 &Consumed, &Produced);
    }
    if (Status == WIM_CODEC_STATUS_STREAM_END)
    {
        *InputUsed = Consumed;
        *OutputUsed = Produced;
        S->Completed = 1;
    }
    return Status;
}

static const WIM_CODEC_PROVIDER WimXpressProvider = {
    WIM_CODEC_ABI_VERSION,
    sizeof(WIM_CODEC_PROVIDER),
    WIM_CODEC_XPRESS,
    WIM_CODEC_CAP_DECODE | WIM_CODEC_CAP_ENCODE | WIM_CODEC_CAP_WIM_CHUNKS,
    "XPRESS LZ77 encoder/decoder",
    WimXpressCreate,
    WimXpressDestroy,
    WimXpressProcess,
    WimXpressReset,
    WimXpressMaximumCompressedSize,
    NULL
};

WIM_CODEC_STATUS
WimCodecRegisterXpressProvider(void)
{
    return WimCodecRegisterProvider(&WimXpressProvider);
}
