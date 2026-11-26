#include <string.h>
#include <libwim/wim_read.h>
#include <libwim/sha1.h>

#define WIM_RESOURCE_DESCRIPTOR_SIZE 24u
#define WIM_OFFSET_LOOKUP_TABLE 48u
#define WIM_OFFSET_XML_DATA 72u
#define WIM_OFFSET_BOOT_METADATA 96u
#define WIM_OFFSET_INTEGRITY_TABLE 124u



static uint16_t WimReadLe16(const uint8_t *P)
{
    return (uint16_t)(P[0] | ((uint16_t)P[1] << 8));
}

static uint32_t WimReadLe32(const uint8_t *P)
{
    return (uint32_t)P[0] | ((uint32_t)P[1] << 8) |
           ((uint32_t)P[2] << 16) | ((uint32_t)P[3] << 24);
}

static uint64_t WimReadLe64(const uint8_t *P)
{
    return (uint64_t)WimReadLe32(P) |
           ((uint64_t)WimReadLe32(P + 4) << 32);
}

static uint32_t WimReadCrc32(const uint8_t *Data, size_t Size)
{
    uint32_t Crc = 0xFFFFFFFFu;
    size_t I;
    unsigned Bit;
    for (I = 0; I < Size; ++I) {
        Crc ^= Data[I];
        for (Bit = 0; Bit < 8; ++Bit)
            Crc = (Crc >> 1) ^ (0xEDB88320u & (0u - (Crc & 1u)));
    }
    return ~Crc;
}

static int WimRangeIsValid(uint64_t Offset, uint64_t Length, size_t Total)
{
    return Offset <= (uint64_t)Total &&
           Length <= (uint64_t)Total - Offset;
}

static int WimIsPowerOfTwo(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1)) == 0;
}

static WIM_READER_STATUS
WimParseResource(const uint8_t *Image, size_t ImageSize, size_t HeaderOffset,
                 WIM_RESOURCE_DESCRIPTOR *D)
{
    uint64_t EncodedSize;
    if (HeaderOffset > ImageSize ||
        WIM_RESOURCE_DESCRIPTOR_SIZE > ImageSize - HeaderOffset)
        return WIM_READER_BAD_HEADER;

    EncodedSize = WimReadLe64(Image + HeaderOffset);
    D->Flags = (uint8_t)(EncodedSize >> 56);
    D->CompressedSize = EncodedSize & WIM_RESOURCE_SIZE_MASK;
    D->Offset = WimReadLe64(Image + HeaderOffset + 8);
    D->OriginalSize = WimReadLe64(Image + HeaderOffset + 16);

    if (D->CompressedSize == 0 && D->OriginalSize == 0)
        return WIM_READER_OK;
    if (D->CompressedSize == 0 || D->OriginalSize == 0 ||
        D->Offset < WIM_HEADER_MIN_SIZE ||
        !WimRangeIsValid(D->Offset, D->CompressedSize, ImageSize))
        return WIM_READER_BAD_RESOURCE;
    if (D->Flags & (WIM_RESOURCE_FLAG_SPANNED | WIM_RESOURCE_FLAG_SOLID))
        return WIM_READER_UNSUPPORTED;
    return WIM_READER_OK;
}

WIM_READER_STATUS
WimReaderOpenMemory(WIM_READER **Reader, const void *ImageBytes,
                    size_t ImageSize, const WIM_READER_OPTIONS *Options)
{
    const uint8_t *Image = (const uint8_t *)ImageBytes;
    static const size_t ResourceOffsets[WIM_RESOURCE_KIND_COUNT] = {
        WIM_OFFSET_LOOKUP_TABLE, WIM_OFFSET_XML_DATA,
        WIM_OFFSET_BOOT_METADATA, WIM_OFFSET_INTEGRITY_TABLE
    };
    WIM_READER *R;
    WIM_READER_STATUS Status;
    uint32_t HeaderSize, Compression;
    unsigned I;

    if (!Reader)
        return WIM_READER_INVALID_ARGUMENT;
    *Reader = NULL;
    if (!Image || !Options || Options->StructureSize < sizeof(*Options) ||
        !Options->Allocator.Allocate || !Options->Allocator.Free)
        return WIM_READER_INVALID_ARGUMENT;
    if (ImageSize < WIM_HEADER_MIN_SIZE)
        return WIM_READER_BAD_HEADER;
    if (memcmp(Image, WIM_HEADER_SIGNATURE, 8) != 0)
        return WIM_READER_BAD_SIGNATURE;

    HeaderSize = WimReadLe32(Image + 8);
    if (HeaderSize < WIM_HEADER_MIN_SIZE || HeaderSize > ImageSize)
        return WIM_READER_BAD_HEADER;
    if (WimReadLe32(Image + 204) != WimReadCrc32(Image, 204))
        return WIM_READER_BAD_HEADER;

    R = (WIM_READER *)Options->Allocator.Allocate(
        Options->Allocator.Context, sizeof(*R));
    if (!R)
        return WIM_READER_OUT_OF_MEMORY;
    memset(R, 0, sizeof(*R));
    R->Image = Image;
    R->ImageSize = ImageSize;
    R->Options = *Options;

    R->Header.HeaderSize = HeaderSize;
    R->Header.Version = WimReadLe32(Image + 12);
    R->Header.Flags = WimReadLe32(Image + 16);
    R->Header.ChunkSize = WimReadLe32(Image + 20);
    R->Header.PartNumber = WimReadLe16(Image + 40);
    R->Header.TotalParts = WimReadLe16(Image + 42);
    R->Header.ImageCount = WimReadLe32(Image + 44);
    R->Header.BootIndex = WimReadLe32(Image + 120);

    if (!R->Header.PartNumber || !R->Header.TotalParts ||
        R->Header.PartNumber > R->Header.TotalParts ||
        ((R->Header.Flags & WIM_HEADER_FLAG_COMPRESSION_MASK) &&
         !WimIsPowerOfTwo(R->Header.ChunkSize)) ||
        R->Header.ChunkSize > 16u * 1024u * 1024u) {
        Status = WIM_READER_BAD_HEADER;
        goto Fail;
    }

    Compression = R->Header.Flags & WIM_HEADER_FLAG_COMPRESSION_MASK;
    if (Compression &&
        Compression != WIM_HEADER_FLAG_COMPRESS_XPRESS &&
        Compression != WIM_HEADER_FLAG_COMPRESS_XPRESS_2 &&
        Compression != WIM_HEADER_FLAG_COMPRESS_LZX &&
        Compression != WIM_HEADER_FLAG_COMPRESS_LZMS) {
        Status = WIM_READER_BAD_HEADER;
        goto Fail;
    }

    for (I = 0; I < WIM_RESOURCE_KIND_COUNT; ++I) {
        Status = WimParseResource(Image, ImageSize, ResourceOffsets[I],
                                  &R->Resources[I]);
        if (Status != WIM_READER_OK)
            goto Fail;
        if (Options->MaximumResourceSize &&
            R->Resources[I].OriginalSize > Options->MaximumResourceSize) {
            Status = WIM_READER_BAD_RESOURCE;
            goto Fail;
        }
    }

    *Reader = R;
    return WIM_READER_OK;

Fail:
    memset(R, 0, sizeof(*R));
    Options->Allocator.Free(Options->Allocator.Context, R);
    return Status;
}

void WimReaderClose(WIM_READER *Reader)
{
    WIM_CODEC_ALLOCATOR A;
    if (!Reader)
        return;
    A = Reader->Options.Allocator;
    if (Reader->LookupEntries)
        A.Free(A.Context, Reader->LookupEntries);
    if (Reader->Catalogue) {
        extern void WimReaderFreeImageCatalogueInternal(WIM_READER *);
        WimReaderFreeImageCatalogueInternal(Reader);
    }
    memset(Reader, 0, sizeof(*Reader));
    A.Free(A.Context, Reader);
}

WIM_READER_STATUS
WimReaderGetHeaderInfo(const WIM_READER *Reader, WIM_HEADER_INFO *Info)
{
    if (!Reader || !Info)
        return WIM_READER_INVALID_ARGUMENT;
    *Info = Reader->Header;
    return WIM_READER_OK;
}

WIM_READER_STATUS
WimReaderGetResource(const WIM_READER *Reader, WIM_RESOURCE_KIND Kind,
                     WIM_RESOURCE_DESCRIPTOR *Descriptor)
{
    if (!Reader || !Descriptor || (unsigned)Kind >= WIM_RESOURCE_KIND_COUNT)
        return WIM_READER_INVALID_ARGUMENT;
    *Descriptor = Reader->Resources[Kind];
    return WIM_READER_OK;
}

#define WIM_LOOKUP_ENTRY_DISK_SIZE 50u

WIM_READER_STATUS
WimReaderLoadLookupTable(WIM_READER *Reader)
{
    WIM_RESOURCE_DESCRIPTOR *Lookup;
    uint8_t *Raw;
    size_t RawSize, Used = 0, Count, I;
    WIM_LOOKUP_ENTRY *Entries;
    WIM_READER_STATUS Status;

    if (!Reader)
        return WIM_READER_INVALID_ARGUMENT;
    if (Reader->LookupEntries)
        return WIM_READER_OK;
    Lookup = &Reader->Resources[WIM_RESOURCE_LOOKUP_TABLE];
    if (!Lookup->OriginalSize ||
        Lookup->OriginalSize > (uint64_t)(size_t)-1 ||
        (Reader->Options.MaximumResourceSize &&
         Lookup->OriginalSize > Reader->Options.MaximumResourceSize) ||
        (size_t)Lookup->OriginalSize % WIM_LOOKUP_ENTRY_DISK_SIZE != 0)
        return WIM_READER_BAD_RESOURCE;
    RawSize = (size_t)Lookup->OriginalSize;
    Count = RawSize / WIM_LOOKUP_ENTRY_DISK_SIZE;
    if (Count > (size_t)-1 / sizeof(*Entries))
        return WIM_READER_BAD_RESOURCE;

    Raw = (uint8_t *)Reader->Options.Allocator.Allocate(
        Reader->Options.Allocator.Context, RawSize);
    if (!Raw)
        return WIM_READER_OUT_OF_MEMORY;
    Status = WimReaderReadResource(Reader, Lookup, Raw, RawSize, &Used);
    if (Status != WIM_READER_OK || Used != RawSize) {
        Reader->Options.Allocator.Free(Reader->Options.Allocator.Context, Raw);
        return Status == WIM_READER_OK ? WIM_READER_CORRUPT_DATA : Status;
    }

    Entries = (WIM_LOOKUP_ENTRY *)Reader->Options.Allocator.Allocate(
        Reader->Options.Allocator.Context, Count * sizeof(*Entries));
    if (!Entries) {
        Reader->Options.Allocator.Free(Reader->Options.Allocator.Context, Raw);
        return WIM_READER_OUT_OF_MEMORY;
    }
    memset(Entries, 0, Count * sizeof(*Entries));
    for (I = 0; I < Count; ++I) {
        const uint8_t *P = Raw + I * WIM_LOOKUP_ENTRY_DISK_SIZE;
        uint64_t EncodedSize = WimReadLe64(P);
        Entries[I].Resource.Flags = (uint8_t)(EncodedSize >> 56);
        Entries[I].Resource.CompressedSize = EncodedSize & WIM_RESOURCE_SIZE_MASK;
        Entries[I].Resource.Offset = WimReadLe64(P + 8);
        Entries[I].Resource.OriginalSize = WimReadLe64(P + 16);
        Entries[I].PartNumber = WimReadLe16(P + 24);
        Entries[I].ReferenceCount = WimReadLe32(P + 26);
        memcpy(Entries[I].Sha1, P + 30, sizeof(Entries[I].Sha1));

        if (Entries[I].Resource.Flags &
            (WIM_RESOURCE_FLAG_SPANNED | WIM_RESOURCE_FLAG_SOLID)) {
            Reader->Options.Allocator.Free(
                Reader->Options.Allocator.Context, Entries);
            Reader->Options.Allocator.Free(
                Reader->Options.Allocator.Context, Raw);
            return WIM_READER_UNSUPPORTED;
        }
        if (!Entries[I].PartNumber ||
            Entries[I].PartNumber > Reader->Header.TotalParts ||
            ((Entries[I].Resource.CompressedSize == 0) !=
             (Entries[I].Resource.OriginalSize == 0)) ||
            (Reader->Options.MaximumResourceSize &&
             Entries[I].Resource.OriginalSize >
                 Reader->Options.MaximumResourceSize) ||
            (Entries[I].Resource.OriginalSize != 0 &&
             Entries[I].PartNumber == Reader->Header.PartNumber &&
             (Entries[I].Resource.Offset < Reader->Header.HeaderSize ||
              !WimRangeIsValid(Entries[I].Resource.Offset,
                               Entries[I].Resource.CompressedSize,
                               Reader->ImageSize)))) {
            Reader->Options.Allocator.Free(
                Reader->Options.Allocator.Context, Entries);
            Reader->Options.Allocator.Free(
                Reader->Options.Allocator.Context, Raw);
            return WIM_READER_BAD_RESOURCE;
        }
    }
    Reader->Options.Allocator.Free(Reader->Options.Allocator.Context, Raw);
    Reader->LookupEntries = Entries;
    Reader->LookupEntryCount = Count;
    return WIM_READER_OK;
}

WIM_READER_STATUS
WimReaderGetLookupEntryCount(WIM_READER *Reader, size_t *EntryCount)
{
    WIM_READER_STATUS Status;
    if (!Reader || !EntryCount)
        return WIM_READER_INVALID_ARGUMENT;
    *EntryCount = 0;
    Status = WimReaderLoadLookupTable(Reader);
    if (Status != WIM_READER_OK)
        return Status;
    *EntryCount = Reader->LookupEntryCount;
    return WIM_READER_OK;
}

WIM_READER_STATUS
WimReaderGetLookupEntry(WIM_READER *Reader, size_t Index,
                        WIM_LOOKUP_ENTRY *Entry)
{
    WIM_READER_STATUS Status;
    if (!Reader || !Entry)
        return WIM_READER_INVALID_ARGUMENT;
    Status = WimReaderLoadLookupTable(Reader);
    if (Status != WIM_READER_OK)
        return Status;
    if (Index >= Reader->LookupEntryCount)
        return WIM_READER_INVALID_ARGUMENT;
    *Entry = Reader->LookupEntries[Index];
    return WIM_READER_OK;
}

WIM_READER_STATUS
WimReaderReadLookupResource(WIM_READER *Reader, size_t Index,
                            uint8_t *Output, size_t OutputCapacity,
                            size_t *OutputUsed)
{
    WIM_LOOKUP_ENTRY Entry;
    WIM_READER_STATUS Status;
    size_t Actual=0,i;
    uint8_t Digest[20];
    int HasDigest=0;
    if (!Reader || !OutputUsed || (!Output && OutputCapacity))
        return WIM_READER_INVALID_ARGUMENT;
    *OutputUsed=0;
    Status=WimReaderGetLookupEntry(Reader,Index,&Entry);
    if(Status!=WIM_READER_OK) return Status;
    if(Entry.PartNumber!=Reader->Header.PartNumber) return WIM_READER_UNSUPPORTED;
    Status=WimReaderReadResource(Reader,&Entry.Resource,Output,OutputCapacity,&Actual);
    if(Status!=WIM_READER_OK) return Status;
    for(i=0;i<sizeof(Entry.Sha1);i++) if(Entry.Sha1[i]) { HasDigest=1; break; }
    if(HasDigest) {
        WimSha1(Output,Actual,Digest);
        if(memcmp(Digest,Entry.Sha1,sizeof(Digest))!=0)
            return WIM_READER_CORRUPT_DATA;
    }
    *OutputUsed=Actual;
    return WIM_READER_OK;
}

static WIM_CODEC_ALGORITHM
WimReaderCompressionAlgorithm(uint32_t Flags)
{
    switch (Flags & WIM_HEADER_FLAG_COMPRESSION_MASK) {
    case WIM_HEADER_FLAG_COMPRESS_XPRESS:
    case WIM_HEADER_FLAG_COMPRESS_XPRESS_2:
        return WIM_CODEC_XPRESS;
    case WIM_HEADER_FLAG_COMPRESS_LZX:
        return WIM_CODEC_LZX;
    case WIM_HEADER_FLAG_COMPRESS_LZMS:
        return WIM_CODEC_LZMS;
    default:
        return (WIM_CODEC_ALGORITHM)0;
    }
}

/*
 * Original WIM chunk table: the first chunk starts at offset zero immediately
 * after the table. Each subsequent table entry gives that chunk's offset
 * relative to the end of the table. Entries are 32-bit below 4 GiB
 * uncompressed resource size and 64-bit otherwise.
 */
static uint64_t
WimGetChunkOffset(const uint8_t *Table, uint64_t Index, unsigned EntrySize)
{
    const uint8_t *P = Table + (size_t)(Index * EntrySize);
    return EntrySize == 4 ? WimReadLe32(P) : WimReadLe64(P);
}

WIM_READER_STATUS
WimReaderReadResource(const WIM_READER *Reader,
                      const WIM_RESOURCE_DESCRIPTOR *Descriptor,
                      uint8_t *Output, size_t OutputCapacity,
                      size_t *OutputUsed)
{
    uint64_t OutputSize64, NumChunks, TableEntries, TableSize64;
    uint64_t PreviousOffset = 0;
    uint64_t PayloadOffset, PayloadSize, OutOffset;
    size_t OutputSize;
    unsigned EntrySize;
    WIM_CODEC_ALGORITHM Algorithm;

    if (OutputUsed)
        *OutputUsed = 0;
    if (!Reader || !Descriptor || !OutputUsed ||
        (OutputCapacity && !Output))
        return WIM_READER_INVALID_ARGUMENT;
    OutputSize64 = Descriptor->OriginalSize;
    if (OutputSize64 > (uint64_t)(size_t)-1 ||
        OutputSize64 > OutputCapacity)
        return WIM_READER_BUFFER_TOO_SMALL;
    if (!OutputSize64)
        return Descriptor->CompressedSize == 0
            ? WIM_READER_OK : WIM_READER_BAD_RESOURCE;
    if (!Descriptor->CompressedSize ||
        (Descriptor->Flags & (WIM_RESOURCE_FLAG_SPANNED |
                              WIM_RESOURCE_FLAG_SOLID)) ||
        Descriptor->Offset < Reader->Header.HeaderSize ||
        !WimRangeIsValid(Descriptor->Offset, Descriptor->CompressedSize,
                         Reader->ImageSize))
        return WIM_READER_BAD_RESOURCE;

    OutputSize = (size_t)OutputSize64;
    if (!(Descriptor->Flags & WIM_RESOURCE_FLAG_COMPRESSED)) {
        if (Descriptor->CompressedSize != OutputSize64)
            return WIM_READER_BAD_RESOURCE;
        memcpy(Output, Reader->Image + (size_t)Descriptor->Offset, OutputSize);
        *OutputUsed = OutputSize;
        return WIM_READER_OK;
    }

    Algorithm = WimReaderCompressionAlgorithm(Reader->Header.Flags);
    if (!WimIsPowerOfTwo(Reader->Header.ChunkSize))
        return WIM_READER_BAD_HEADER;

    NumChunks = ((OutputSize64 - 1) / Reader->Header.ChunkSize) + 1;
    TableEntries = NumChunks - 1;
    EntrySize = OutputSize64 <= UINT32_MAX ? 4 : 8;
    if (TableEntries > UINT64_MAX / EntrySize)
        return WIM_READER_BAD_RESOURCE;
    TableSize64 = TableEntries * EntrySize;
    if (TableSize64 > Descriptor->CompressedSize)
        return WIM_READER_CORRUPT_DATA;
    PayloadOffset = Descriptor->Offset + TableSize64;
    PayloadSize = Descriptor->CompressedSize - TableSize64;
    OutOffset = 0;

    {
        uint64_t I;
        const uint8_t *Table = Reader->Image + (size_t)Descriptor->Offset;
        for (I = 0; I < NumChunks; ++I) {
            uint64_t ChunkStart = I == 0 ? 0 :
                WimGetChunkOffset(Table, I - 1, EntrySize);
            uint64_t ChunkEnd = I + 1 == NumChunks ? PayloadSize :
                WimGetChunkOffset(Table, I, EntrySize);
            uint64_t ChunkStoredSize;
            size_t ChunkOutputSize;
            const uint8_t *ChunkInput;
            uint8_t *ChunkOutput;

            if (ChunkStart < PreviousOffset || ChunkEnd <= ChunkStart ||
                ChunkEnd > PayloadSize)
                return WIM_READER_CORRUPT_DATA;
            ChunkStoredSize = ChunkEnd - ChunkStart;
            ChunkOutputSize = (size_t)(OutputSize64 - OutOffset);
            if (ChunkOutputSize > Reader->Header.ChunkSize)
                ChunkOutputSize = Reader->Header.ChunkSize;
            if (ChunkStoredSize > SIZE_MAX)
                return WIM_READER_CORRUPT_DATA;

            ChunkInput = Reader->Image + (size_t)(PayloadOffset + ChunkStart);
            ChunkOutput = Output + (size_t)OutOffset;
            if (ChunkStoredSize == ChunkOutputSize) {
                memcpy(ChunkOutput, ChunkInput, ChunkOutputSize);
            } else {
                WIM_CODEC_OPTIONS CodecOptions;
                WIM_CODEC_INSTANCE *Codec = NULL;
                WIM_CODEC_STATUS CodecStatus;
                size_t InputUsed = 0, ChunkOutputUsed = 0;

                if (ChunkStoredSize > ChunkOutputSize)
                    return WIM_READER_CORRUPT_DATA;
                if (!Algorithm ||
                    !(WimCodecGetCapabilities(Algorithm) &
                      WIM_CODEC_CAP_WIM_CHUNKS))
                    return WIM_READER_UNSUPPORTED;
                memset(&CodecOptions, 0, sizeof(CodecOptions));
                CodecOptions.StructureSize = sizeof(CodecOptions);
                CodecOptions.ChunkSize = Reader->Header.ChunkSize;
                CodecOptions.ExpectedOutputSize = ChunkOutputSize;
                CodecOptions.MaximumOutputSize = ChunkOutputSize;
                CodecOptions.Allocator = Reader->Options.Allocator;
                CodecStatus = WimCodecOpen(&Codec, Algorithm,
                                           WIM_CODEC_DECODE, &CodecOptions);
                if (CodecStatus != WIM_CODEC_STATUS_OK)
                    return CodecStatus == WIM_CODEC_STATUS_OUT_OF_MEMORY
                        ? WIM_READER_OUT_OF_MEMORY : WIM_READER_UNSUPPORTED;
                CodecStatus = WimCodecProcess(
                    Codec, ChunkInput, (size_t)ChunkStoredSize, &InputUsed,
                    ChunkOutput, ChunkOutputSize, &ChunkOutputUsed,
                    WIM_CODEC_PROCESS_FINAL);
                WimCodecClose(Codec);
                if (CodecStatus != WIM_CODEC_STATUS_STREAM_END ||
                    InputUsed != ChunkStoredSize ||
                    ChunkOutputUsed != ChunkOutputSize)
                    return WIM_READER_CORRUPT_DATA;
            }

            OutOffset += ChunkOutputSize;
            PreviousOffset = ChunkEnd;
        }
    }

    if (OutOffset != OutputSize64 || PreviousOffset != PayloadSize)
        return WIM_READER_CORRUPT_DATA;
    *OutputUsed = OutputSize;
    return WIM_READER_OK;
}
