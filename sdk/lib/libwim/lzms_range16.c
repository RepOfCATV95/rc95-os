/*
 * PROJECT:    RC95-OS LZMS codec
 * LICENSE:    GPL-3.0-or-later
 * PURPOSE:    Bounded 16-bit-word LZMS range decoder
 *
 * LZMS uses a 6-bit zero probability.  Before each decision the range is
 * normalized to at least 0x10000, then split at (range >> 6) * probability.
 * Words are consumed little-endian and placed into the low 16 bits.
 */
#include <string.h>
#include <libwim/lzms_range16.h>

static LZMS_RANGE16_STATUS
LzmsRange16ReadWord(LZMS_RANGE16_DECODER *Decoder, uint32_t *Word)
{
    if (Decoder->InputPosition > Decoder->InputSize ||
        Decoder->InputSize - Decoder->InputPosition < 2) {
        Decoder->Status = LZMS_RANGE16_INPUT_EXHAUSTED;
        return Decoder->Status;
    }
    *Word = (uint32_t)Decoder->Input[Decoder->InputPosition] |
            ((uint32_t)Decoder->Input[Decoder->InputPosition + 1] << 8);
    Decoder->InputPosition += 2;
    return LZMS_RANGE16_OK;
}

LZMS_RANGE16_STATUS
LzmsRange16DecoderInitialize(LZMS_RANGE16_DECODER *Decoder,
                             const uint8_t *Input, size_t InputSize)
{
    uint32_t FirstWord, SecondWord;
    if (Decoder == NULL || (Input == NULL && InputSize != 0))
        return LZMS_RANGE16_INVALID_ARGUMENT;
    memset(Decoder, 0, sizeof(*Decoder));
    Decoder->Input = Input;
    Decoder->InputSize = InputSize;
    Decoder->Range = UINT32_MAX;
    if (LzmsRange16ReadWord(Decoder, &FirstWord) != LZMS_RANGE16_OK ||
        LzmsRange16ReadWord(Decoder, &SecondWord) != LZMS_RANGE16_OK)
        return Decoder->Status;
    /* The first word is the high half of the initial code register. */
    Decoder->Code = (FirstWord << 16) | SecondWord;
    return LZMS_RANGE16_OK;
}

LZMS_RANGE16_STATUS
LzmsRange16DecodeBit(LZMS_RANGE16_DECODER *Decoder,
                     uint32_t ZeroProbability, uint32_t *Bit)
{
    LZMS_RANGE16_DECODER Saved;
    uint32_t Word, Bound, DecodedBit;
    uint64_t Product;

    if (Decoder == NULL || Bit == NULL || ZeroProbability == 0 ||
        ZeroProbability >= LZMS_RANGE16_PROBABILITY_SCALE)
        return LZMS_RANGE16_INVALID_ARGUMENT;
    if (Decoder->Status != LZMS_RANGE16_OK)
        return Decoder->Status;

    Saved = *Decoder;
    /* LZMS normalizes before the decision, not after it.  Its 16-bit
       arithmetic stream is implicitly zero-extended at the end: once the
       last complete word is consumed, normalization still shifts range and
       code but contributes a zero word. */
    if ((Decoder->Range & 0xFFFF0000u) == 0) {
        Decoder->Range <<= 16;
        Decoder->Code <<= 16;
        if (Decoder->InputPosition <= Decoder->InputSize &&
            Decoder->InputSize - Decoder->InputPosition >= 2) {
            if (LzmsRange16ReadWord(Decoder, &Word) != LZMS_RANGE16_OK) {
                *Decoder = Saved;
                return Decoder->Status;
            }
            Decoder->Code |= Word;
        }
    }

    Product = (uint64_t)(Decoder->Range >> 6) * ZeroProbability;
    if (Product == 0 || Product >= Decoder->Range) {
        *Decoder = Saved;
        Decoder->Status = LZMS_RANGE16_CORRUPT_DATA;
        return Decoder->Status;
    }
    Bound = (uint32_t)Product;

    if (Decoder->Code < Bound) {
        DecodedBit = 0;
        Decoder->Range = Bound;
    } else {
        DecodedBit = 1;
        Decoder->Code -= Bound;
        Decoder->Range -= Bound;
    }
    *Bit = DecodedBit;
    return LZMS_RANGE16_OK;
}

LZMS_RANGE16_STATUS
LzmsRange16DecodeModelBit(LZMS_RANGE16_DECODER *Decoder,
                          LZMS_PROBABILITY_MODEL *Model, uint32_t *Bit)
{
    uint32_t Probability, DecodedBit;
    LZMS_RANGE16_STATUS Status;
    if (Decoder == NULL || Model == NULL || Bit == NULL)
        return LZMS_RANGE16_INVALID_ARGUMENT;
    Probability = LzmsProbabilityModelGetZeroCount(Model);
    Status = LzmsRange16DecodeBit(Decoder, Probability, &DecodedBit);
    if (Status != LZMS_RANGE16_OK)
        return Status;
    if (LzmsProbabilityModelUpdate(Model, DecodedBit) != LZMS_PROBABILITY_OK)
        return LZMS_RANGE16_INVALID_ARGUMENT;
    *Bit = DecodedBit;
    return LZMS_RANGE16_OK;
}

LZMS_RANGE16_STATUS
LzmsRange16DecodeBits(LZMS_RANGE16_DECODER *Decoder,
                      uint32_t BitCount, uint32_t *Value)
{
    uint32_t Index, Bit, Result = 0;
    LZMS_RANGE16_STATUS Status;
    if (Decoder == NULL || Value == NULL || BitCount > 32)
        return LZMS_RANGE16_INVALID_ARGUMENT;
    for (Index = 0; Index < BitCount; ++Index) {
        Status = LzmsRange16DecodeBit(Decoder, 32, &Bit);
        if (Status != LZMS_RANGE16_OK)
            return Status;
        Result = (Result << 1) | Bit;
    }
    *Value = Result;
    return LZMS_RANGE16_OK;
}

size_t
LzmsRange16DecoderBytesRead(const LZMS_RANGE16_DECODER *Decoder)
{
    return Decoder != NULL ? Decoder->InputPosition : 0;
}
