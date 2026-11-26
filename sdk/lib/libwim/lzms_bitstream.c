/*
 * PROJECT:    RC95-OS LZMS codec
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Bounded bitstream reader and writer for LZMS primitives
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#include <string.h>
#include <libwim/lzms_bitstream.h>

static LZMS_BITSTREAM_STATUS
LzmsBitstreamValidateBuffer(
    const void *Buffer,
    size_t BufferSize,
    LZMS_BIT_ORDER BitOrder)
{
    if ((Buffer == NULL && BufferSize != 0) ||
        (BitOrder != LZMS_BIT_ORDER_MSB_FIRST &&
         BitOrder != LZMS_BIT_ORDER_LSB_FIRST))
    {
        return LZMS_BITSTREAM_INVALID_ARGUMENT;
    }

    if (BufferSize > UINT64_MAX / 8)
        return LZMS_BITSTREAM_POSITION_OVERFLOW;

    return LZMS_BITSTREAM_OK;
}

static uint64_t
LzmsBitstreamCapacityBits(size_t BufferSize)
{
    return (uint64_t)BufferSize * 8;
}

LZMS_BITSTREAM_STATUS
LzmsBitReaderInitialize(
    LZMS_BIT_READER *Reader,
    const uint8_t *Buffer,
    size_t BufferSize,
    LZMS_BIT_ORDER BitOrder)
{
    LZMS_BITSTREAM_STATUS Status;

    if (Reader == NULL)
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    Status = LzmsBitstreamValidateBuffer(Buffer, BufferSize, BitOrder);
    Reader->Buffer = NULL;
    Reader->BufferSize = 0;
    Reader->BitPosition = 0;
    Reader->BitOrder = BitOrder;
    Reader->Status = Status;

    if (Status != LZMS_BITSTREAM_OK)
        return Status;

    Reader->Buffer = Buffer;
    Reader->BufferSize = BufferSize;
    return LZMS_BITSTREAM_OK;
}

LZMS_BITSTREAM_STATUS
LzmsBitReaderReadBits(
    LZMS_BIT_READER *Reader,
    uint32_t BitCount,
    uint32_t *Value)
{
    uint32_t Result = 0;
    uint32_t Index;
    uint64_t Capacity;

    if (Reader == NULL || Value == NULL)
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    *Value = 0;
    if (Reader->Status != LZMS_BITSTREAM_OK)
        return Reader->Status;

    if (BitCount > 32)
    {
        Reader->Status = LZMS_BITSTREAM_INVALID_ARGUMENT;
        return Reader->Status;
    }

    Capacity = LzmsBitstreamCapacityBits(Reader->BufferSize);
    if ((uint64_t)BitCount > Capacity - Reader->BitPosition)
    {
        Reader->Status = LZMS_BITSTREAM_END_OF_INPUT;
        return Reader->Status;
    }

    for (Index = 0; Index < BitCount; ++Index)
    {
        uint64_t Position = Reader->BitPosition + Index;
        uint8_t Byte = Reader->Buffer[Position >> 3];
        uint32_t Shift = (Reader->BitOrder == LZMS_BIT_ORDER_MSB_FIRST)
                       ? 7u - (uint32_t)(Position & 7u)
                       : (uint32_t)(Position & 7u);
        uint32_t Bit = (Byte >> Shift) & 1u;

        if (Reader->BitOrder == LZMS_BIT_ORDER_MSB_FIRST)
            Result = (Result << 1) | Bit;
        else
            Result |= Bit << Index;
    }

    Reader->BitPosition += BitCount;
    *Value = Result;
    return LZMS_BITSTREAM_OK;
}

LZMS_BITSTREAM_STATUS
LzmsBitReaderAlign(
    LZMS_BIT_READER *Reader,
    int RequireZeroPadding)
{
    uint32_t PaddingBits;
    uint32_t PaddingValue;
    LZMS_BITSTREAM_STATUS Status;

    if (Reader == NULL)
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    if (Reader->Status != LZMS_BITSTREAM_OK)
        return Reader->Status;

    PaddingBits = (uint32_t)((8 - (Reader->BitPosition & 7u)) & 7u);
    if (PaddingBits == 0)
        return LZMS_BITSTREAM_OK;

    Status = LzmsBitReaderReadBits(Reader, PaddingBits, &PaddingValue);
    if (Status != LZMS_BITSTREAM_OK)
        return Status;

    if (RequireZeroPadding && PaddingValue != 0)
    {
        Reader->Status = LZMS_BITSTREAM_INVALID_PADDING;
        return Reader->Status;
    }

    return LZMS_BITSTREAM_OK;
}

LZMS_BITSTREAM_STATUS
LzmsBitReaderReadBytes(
    LZMS_BIT_READER *Reader,
    uint8_t *Destination,
    size_t ByteCount)
{
    uint64_t RemainingBytes;

    if (Reader == NULL || (Destination == NULL && ByteCount != 0))
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    if (Reader->Status != LZMS_BITSTREAM_OK)
        return Reader->Status;

    if ((Reader->BitPosition & 7u) != 0)
    {
        Reader->Status = LZMS_BITSTREAM_INVALID_ARGUMENT;
        return Reader->Status;
    }

    RemainingBytes = (LzmsBitstreamCapacityBits(Reader->BufferSize) -
                      Reader->BitPosition) / 8;
    if ((uint64_t)ByteCount > RemainingBytes)
    {
        Reader->Status = LZMS_BITSTREAM_END_OF_INPUT;
        return Reader->Status;
    }

    if (ByteCount != 0)
    {
        memmove(Destination,
                Reader->Buffer + (size_t)(Reader->BitPosition >> 3),
                ByteCount);
        Reader->BitPosition += (uint64_t)ByteCount * 8;
    }

    return LZMS_BITSTREAM_OK;
}

uint64_t
LzmsBitReaderGetPosition(const LZMS_BIT_READER *Reader)
{
    return (Reader != NULL) ? Reader->BitPosition : 0;
}

LZMS_BITSTREAM_STATUS
LzmsBitWriterInitialize(
    LZMS_BIT_WRITER *Writer,
    uint8_t *Buffer,
    size_t BufferSize,
    LZMS_BIT_ORDER BitOrder)
{
    LZMS_BITSTREAM_STATUS Status;

    if (Writer == NULL)
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    Status = LzmsBitstreamValidateBuffer(Buffer, BufferSize, BitOrder);
    Writer->Buffer = NULL;
    Writer->BufferSize = 0;
    Writer->BitPosition = 0;
    Writer->BitOrder = BitOrder;
    Writer->Status = Status;

    if (Status != LZMS_BITSTREAM_OK)
        return Status;

    Writer->Buffer = Buffer;
    Writer->BufferSize = BufferSize;
    if (BufferSize != 0)
        memset(Buffer, 0, BufferSize);

    return LZMS_BITSTREAM_OK;
}

LZMS_BITSTREAM_STATUS
LzmsBitWriterWriteBits(
    LZMS_BIT_WRITER *Writer,
    uint32_t BitCount,
    uint32_t Value)
{
    uint32_t Index;
    uint64_t Capacity;

    if (Writer == NULL)
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    if (Writer->Status != LZMS_BITSTREAM_OK)
        return Writer->Status;

    if (BitCount > 32)
    {
        Writer->Status = LZMS_BITSTREAM_INVALID_ARGUMENT;
        return Writer->Status;
    }

    Capacity = LzmsBitstreamCapacityBits(Writer->BufferSize);
    if ((uint64_t)BitCount > Capacity - Writer->BitPosition)
    {
        Writer->Status = LZMS_BITSTREAM_OUTPUT_FULL;
        return Writer->Status;
    }

    for (Index = 0; Index < BitCount; ++Index)
    {
        uint64_t Position = Writer->BitPosition + Index;
        uint32_t SourceShift = (Writer->BitOrder == LZMS_BIT_ORDER_MSB_FIRST)
                             ? BitCount - Index - 1
                             : Index;
        uint32_t Bit = (Value >> SourceShift) & 1u;
        uint32_t DestinationShift =
            (Writer->BitOrder == LZMS_BIT_ORDER_MSB_FIRST)
            ? 7u - (uint32_t)(Position & 7u)
            : (uint32_t)(Position & 7u);

        if (Bit != 0)
            Writer->Buffer[Position >> 3] |= (uint8_t)(1u << DestinationShift);
    }

    Writer->BitPosition += BitCount;
    return LZMS_BITSTREAM_OK;
}

LZMS_BITSTREAM_STATUS
LzmsBitWriterAlign(LZMS_BIT_WRITER *Writer)
{
    uint32_t PaddingBits;

    if (Writer == NULL)
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    if (Writer->Status != LZMS_BITSTREAM_OK)
        return Writer->Status;

    PaddingBits = (uint32_t)((8 - (Writer->BitPosition & 7u)) & 7u);
    return LzmsBitWriterWriteBits(Writer, PaddingBits, 0);
}

LZMS_BITSTREAM_STATUS
LzmsBitWriterWriteBytes(
    LZMS_BIT_WRITER *Writer,
    const uint8_t *Source,
    size_t ByteCount)
{
    uint64_t RemainingBytes;

    if (Writer == NULL || (Source == NULL && ByteCount != 0))
        return LZMS_BITSTREAM_INVALID_ARGUMENT;

    if (Writer->Status != LZMS_BITSTREAM_OK)
        return Writer->Status;

    if ((Writer->BitPosition & 7u) != 0)
    {
        Writer->Status = LZMS_BITSTREAM_INVALID_ARGUMENT;
        return Writer->Status;
    }

    RemainingBytes = (LzmsBitstreamCapacityBits(Writer->BufferSize) -
                      Writer->BitPosition) / 8;
    if ((uint64_t)ByteCount > RemainingBytes)
    {
        Writer->Status = LZMS_BITSTREAM_OUTPUT_FULL;
        return Writer->Status;
    }

    if (ByteCount != 0)
    {
        memmove(Writer->Buffer + (size_t)(Writer->BitPosition >> 3),
                Source,
                ByteCount);
        Writer->BitPosition += (uint64_t)ByteCount * 8;
    }

    return LZMS_BITSTREAM_OK;
}

size_t
LzmsBitWriterGetBytesWritten(const LZMS_BIT_WRITER *Writer)
{
    if (Writer == NULL)
        return 0;

    return (size_t)((Writer->BitPosition + 7) / 8);
}

uint64_t
LzmsBitWriterGetPosition(const LZMS_BIT_WRITER *Writer)
{
    return (Writer != NULL) ? Writer->BitPosition : 0;
}

/*
 * LZMS input bitstream.  Words are fetched from the end of the payload
 * toward the beginning; bytes within each word are little-endian, while
 * bits within a fetched word are consumed most-significant first.
 */
LZMS_BITSTREAM_STATUS
LzmsReverse16ReaderInitialize(LZMS_REVERSE16_READER *Reader,
                              const uint8_t *Buffer, size_t BufferSize)
{
    if (Reader == NULL || (Buffer == NULL && BufferSize != 0))
        return LZMS_BITSTREAM_INVALID_ARGUMENT;
    memset(Reader, 0, sizeof(*Reader));
    Reader->Buffer = Buffer;
    Reader->BufferSize = BufferSize;
    Reader->NextWordOffset = BufferSize;
    Reader->Status = LZMS_BITSTREAM_OK;
    if ((BufferSize & 1u) != 0) {
        Reader->Status = LZMS_BITSTREAM_INVALID_ARGUMENT;
        return Reader->Status;
    }
    return LZMS_BITSTREAM_OK;
}

LZMS_BITSTREAM_STATUS
LzmsReverse16ReaderReadBits(LZMS_REVERSE16_READER *Reader,
                            uint32_t BitCount, uint32_t *Value)
{
    uint32_t Result = 0, i;
    if (Reader == NULL || Value == NULL || BitCount > 32)
        return LZMS_BITSTREAM_INVALID_ARGUMENT;
    *Value = 0;
    if (Reader->Status != LZMS_BITSTREAM_OK)
        return Reader->Status;
    for (i = 0; i < BitCount; ++i) {
        uint32_t Bit;
        if (Reader->BitsRemaining == 0) {
            if (Reader->NextWordOffset >= 2) {
                Reader->NextWordOffset -= 2;
                Reader->Word = (uint16_t)Reader->Buffer[Reader->NextWordOffset] |
                    ((uint16_t)Reader->Buffer[Reader->NextWordOffset + 1] << 8);
            } else {
                /*
                 * LZMS's backwards 16-bit bitstream is zero-extended once
                 * all complete words have been consumed.  This matches the
                 * format's bounded bit-buffer refill behavior; the decoder
                 * still rejects streams when the forward range stream
                 * overlaps this reader or a decoded symbol violates bounds.
                 */
                Reader->Word = 0;
            }
            Reader->BitsRemaining = 16;
        }
        Bit = (Reader->Word >> 15) & 1u;
        Reader->Word <<= 1;
        Reader->BitsRemaining--;
        Result = (Result << 1) | Bit;
    }
    *Value = Result;
    return LZMS_BITSTREAM_OK;
}

size_t
LzmsReverse16ReaderBytesRead(const LZMS_REVERSE16_READER *Reader)
{
    return Reader != NULL ? Reader->BufferSize - Reader->NextWordOffset : 0;
}
