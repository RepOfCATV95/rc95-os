/*
 * PROJECT:    RC95-OS LZMS codec
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Bounded bitstream primitives for the clean-room LZMS backend
 * COPYRIGHT:  Copyright 1997-2028 Republic of CATV 95 <catv95republic@proton.me>
 */

#ifndef _LIBWIM_LZMS_BITSTREAM_H_
#define _LIBWIM_LZMS_BITSTREAM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _LZMS_BITSTREAM_STATUS
{
    LZMS_BITSTREAM_OK = 0,
    LZMS_BITSTREAM_INVALID_ARGUMENT,
    LZMS_BITSTREAM_END_OF_INPUT,
    LZMS_BITSTREAM_OUTPUT_FULL,
    LZMS_BITSTREAM_INVALID_PADDING,
    LZMS_BITSTREAM_POSITION_OVERFLOW,
    LZMS_BITSTREAM_INVALID_STATE
} LZMS_BITSTREAM_STATUS;

typedef enum _LZMS_BIT_ORDER
{
    LZMS_BIT_ORDER_MSB_FIRST = 0,
    LZMS_BIT_ORDER_LSB_FIRST = 1
} LZMS_BIT_ORDER;

typedef struct _LZMS_BIT_READER
{
    const uint8_t *Buffer;
    size_t BufferSize;
    uint64_t BitPosition;
    LZMS_BIT_ORDER BitOrder;
    LZMS_BITSTREAM_STATUS Status;
} LZMS_BIT_READER;


/* LZMS Huffman/verbatim stream: little-endian 16-bit words consumed
 * backwards from the end of the compressed payload, MSB first per word. */
typedef struct _LZMS_REVERSE16_READER
{
    const uint8_t *Buffer;
    size_t BufferSize;
    size_t NextWordOffset;
    uint16_t Word;
    uint8_t BitsRemaining;
    LZMS_BITSTREAM_STATUS Status;
} LZMS_REVERSE16_READER;

LZMS_BITSTREAM_STATUS
LzmsReverse16ReaderInitialize(LZMS_REVERSE16_READER *Reader,
                              const uint8_t *Buffer, size_t BufferSize);
LZMS_BITSTREAM_STATUS
LzmsReverse16ReaderReadBits(LZMS_REVERSE16_READER *Reader,
                            uint32_t BitCount, uint32_t *Value);
size_t LzmsReverse16ReaderBytesRead(const LZMS_REVERSE16_READER *Reader);

typedef struct _LZMS_BIT_WRITER
{
    uint8_t *Buffer;
    size_t BufferSize;
    uint64_t BitPosition;
    LZMS_BIT_ORDER BitOrder;
    LZMS_BITSTREAM_STATUS Status;
} LZMS_BIT_WRITER;

LZMS_BITSTREAM_STATUS
LzmsBitReaderInitialize(
    LZMS_BIT_READER *Reader,
    const uint8_t *Buffer,
    size_t BufferSize,
    LZMS_BIT_ORDER BitOrder);

LZMS_BITSTREAM_STATUS
LzmsBitReaderReadBits(
    LZMS_BIT_READER *Reader,
    uint32_t BitCount,
    uint32_t *Value);

LZMS_BITSTREAM_STATUS
LzmsBitReaderAlign(
    LZMS_BIT_READER *Reader,
    int RequireZeroPadding);

LZMS_BITSTREAM_STATUS
LzmsBitReaderReadBytes(
    LZMS_BIT_READER *Reader,
    uint8_t *Destination,
    size_t ByteCount);

uint64_t
LzmsBitReaderGetPosition(const LZMS_BIT_READER *Reader);

LZMS_BITSTREAM_STATUS
LzmsBitWriterInitialize(
    LZMS_BIT_WRITER *Writer,
    uint8_t *Buffer,
    size_t BufferSize,
    LZMS_BIT_ORDER BitOrder);

LZMS_BITSTREAM_STATUS
LzmsBitWriterWriteBits(
    LZMS_BIT_WRITER *Writer,
    uint32_t BitCount,
    uint32_t Value);

LZMS_BITSTREAM_STATUS
LzmsBitWriterAlign(LZMS_BIT_WRITER *Writer);

LZMS_BITSTREAM_STATUS
LzmsBitWriterWriteBytes(
    LZMS_BIT_WRITER *Writer,
    const uint8_t *Source,
    size_t ByteCount);

size_t
LzmsBitWriterGetBytesWritten(const LZMS_BIT_WRITER *Writer);

uint64_t
LzmsBitWriterGetPosition(const LZMS_BIT_WRITER *Writer);

#ifdef __cplusplus
}
#endif

#endif
