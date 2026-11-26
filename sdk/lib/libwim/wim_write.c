/*
 * PROJECT: RC95-OS libwim
 * LICENSE: GPL-3.0-or-later
 * PURPOSE: Serialize single-part WIM resources with optional XPRESS chunks.
 */
#include <stdlib.h>
#include <string.h>
#include <libwim/wim_write.h>
#include <libwim/wim_codec.h>
#include <libwim/sha1.h>

#define WIM_WRITER_HEADER_SIZE 208u
#define WIM_WRITER_LOOKUP_ENTRY_SIZE 50u
#define WIM_WRITER_DEFAULT_VERSION 0x00010D00u
#define WIM_WRITER_DEFAULT_CHUNK 32768u

typedef struct _WIM_WRITER_BUFFER { uint8_t *Data; size_t Size, Capacity; } WIM_WRITER_BUFFER;

static void WimWriterPut16(uint8_t *P,uint16_t V){P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);}
static void WimWriterPut32(uint8_t *P,uint32_t V){WimWriterPut16(P,(uint16_t)V);WimWriterPut16(P+2,(uint16_t)(V>>16));}
static void WimWriterPut64(uint8_t *P,uint64_t V){WimWriterPut32(P,(uint32_t)V);WimWriterPut32(P+4,(uint32_t)(V>>32));}
static uint32_t WimWriterCrc32(const uint8_t *D,size_t N)
{uint32_t C=0xFFFFFFFFu;size_t I;unsigned B;for(I=0;I<N;I++){C^=D[I];for(B=0;B<8;B++)C=(C>>1)^(0xEDB88320u&(0u-(C&1u)));}return ~C;}
static int WimWriterBufferAppend(WIM_WRITER_BUFFER *B,const void *P,size_t N)
{size_t Need,Cap;uint8_t *Q;if(N>SIZE_MAX-B->Size)return 0;Need=B->Size+N;if(Need>B->Capacity){Cap=B->Capacity?B->Capacity:256;while(Cap<Need){if(Cap>SIZE_MAX/2){Cap=Need;break;}Cap*=2;}Q=(uint8_t*)realloc(B->Data,Cap);if(!Q)return 0;B->Data=Q;B->Capacity=Cap;}if(N){if(P)memcpy(B->Data+B->Size,P,N);else memset(B->Data+B->Size,0,N);}B->Size=Need;return 1;}
static WIM_WRITER_STATUS WimWriterAlign(WIM_IO *Out)
{uint64_t Pos;uint8_t Z[8]={0};size_t N;if(WimIoTell(Out,&Pos)!=WIM_IO_OK)return WIM_WRITER_IO_ERROR;N=(size_t)((8-(Pos&7))&7);return WimIoWriteExact(Out,Z,N)==WIM_IO_OK?WIM_WRITER_OK:WIM_WRITER_IO_ERROR;}
static void *WimWriterAlloc(void *C,size_t N){(void)C;return malloc(N);}
static void WimWriterFree(void *C,void *P){(void)C;free(P);}

static WIM_WRITER_STATUS
WimWriterEncodeXpress(const uint8_t *Data,size_t Size,uint32_t ChunkSize,
                      WIM_WRITER_BUFFER *Encoded,int *AnyCompressed)
{
    uint64_t Chunks64=(Size-1)/ChunkSize+1,Entries64=Chunks64-1;
    size_t EntrySize=Size<=UINT32_MAX?4:8,TableSize,Offset=0;
    uint64_t I;
    WIM_WRITER_BUFFER Payload={0};
    if(Entries64>SIZE_MAX/EntrySize)return WIM_WRITER_SIZE_OVERFLOW;
    TableSize=(size_t)Entries64*EntrySize;
    if(!WimWriterBufferAppend(Encoded,NULL,TableSize))return WIM_WRITER_OUT_OF_MEMORY;
    for(I=0;I<Chunks64;I++){
        size_t InSize=Size-Offset,Bound,UsedIn=0,UsedOut=0;
        uint8_t *Compressed=NULL;
        WIM_CODEC_OPTIONS Opt;
        WIM_CODEC_INSTANCE *Codec=NULL;
        WIM_CODEC_STATUS Cs;
        if(InSize>ChunkSize)InSize=ChunkSize;
        Bound=WimCodecMaximumCompressedSize(WIM_CODEC_XPRESS,InSize,ChunkSize);
        if(!Bound){free(Encoded->Data);memset(Encoded,0,sizeof(*Encoded));free(Payload.Data);return WIM_WRITER_UNSUPPORTED;}
        Compressed=(uint8_t*)malloc(Bound);
        if(!Compressed){free(Encoded->Data);memset(Encoded,0,sizeof(*Encoded));free(Payload.Data);return WIM_WRITER_OUT_OF_MEMORY;}
        memset(&Opt,0,sizeof(Opt));Opt.StructureSize=sizeof(Opt);Opt.ChunkSize=ChunkSize;
        Opt.MaximumOutputSize=Bound;Opt.Allocator.Allocate=WimWriterAlloc;Opt.Allocator.Free=WimWriterFree;
        Cs=WimCodecOpen(&Codec,WIM_CODEC_XPRESS,WIM_CODEC_ENCODE,&Opt);
        if(Cs==WIM_CODEC_STATUS_OK){
            Cs=WimCodecProcess(Codec,Data+Offset,InSize,&UsedIn,Compressed,Bound,&UsedOut,WIM_CODEC_PROCESS_FINAL);
            WimCodecClose(Codec);
        }
        if(Cs!=WIM_CODEC_STATUS_STREAM_END||UsedIn!=InSize||UsedOut==0){
            free(Compressed);free(Encoded->Data);memset(Encoded,0,sizeof(*Encoded));free(Payload.Data);
            return Cs==WIM_CODEC_STATUS_OUT_OF_MEMORY?WIM_WRITER_OUT_OF_MEMORY:WIM_WRITER_UNSUPPORTED;
        }
        if(UsedOut>=InSize){
            if(!WimWriterBufferAppend(&Payload,Data+Offset,InSize)){free(Compressed);free(Encoded->Data);memset(Encoded,0,sizeof(*Encoded));free(Payload.Data);return WIM_WRITER_OUT_OF_MEMORY;}
        }else{
            if(!WimWriterBufferAppend(&Payload,Compressed,UsedOut)){free(Compressed);free(Encoded->Data);memset(Encoded,0,sizeof(*Encoded));free(Payload.Data);return WIM_WRITER_OUT_OF_MEMORY;}
            *AnyCompressed=1;
        }
        free(Compressed);
        if(I+1<Chunks64){
            size_t TablePos=(size_t)I*EntrySize;
            if(EntrySize==4&&Payload.Size>UINT32_MAX){free(Encoded->Data);memset(Encoded,0,sizeof(*Encoded));free(Payload.Data);return WIM_WRITER_SIZE_OVERFLOW;}
            if(EntrySize==4)WimWriterPut32(Encoded->Data+TablePos,(uint32_t)Payload.Size);
            else WimWriterPut64(Encoded->Data+TablePos,(uint64_t)Payload.Size);
        }
        Offset+=InSize;
    }
    if(!WimWriterBufferAppend(Encoded,Payload.Data,Payload.Size)){free(Encoded->Data);memset(Encoded,0,sizeof(*Encoded));free(Payload.Data);return WIM_WRITER_OUT_OF_MEMORY;}
    free(Payload.Data);
    return WIM_WRITER_OK;
}

static WIM_WRITER_STATUS
WimWriterWriteResource(WIM_IO *Out,const void *Data,uint64_t Size,uint8_t Flags,
                       uint32_t ChunkSize,int Compress,WIM_RESOURCE_DESCRIPTOR *D)
{
    uint64_t Pos;WIM_WRITER_STATUS S;WIM_WRITER_BUFFER Enc={0};int Any=0;
    if(!D)return WIM_WRITER_INVALID_ARGUMENT;memset(D,0,sizeof(*D));
    if(!Size)return WIM_WRITER_OK;
    if(!Data||Size>WIM_RESOURCE_SIZE_MASK||Size>(uint64_t)SIZE_MAX)return WIM_WRITER_INVALID_ARGUMENT;
    if(Compress&&Size>1){
        S=WimWriterEncodeXpress((const uint8_t*)Data,(size_t)Size,ChunkSize,&Enc,&Any);
        if(S!=WIM_WRITER_OK)return S;
    }
    S=WimWriterAlign(Out);if(S!=WIM_WRITER_OK){free(Enc.Data);return S;}
    if(WimIoTell(Out,&Pos)!=WIM_IO_OK){free(Enc.Data);return WIM_WRITER_IO_ERROR;}
    D->Offset=Pos;D->OriginalSize=Size;D->Flags=Flags;
    if(Any){
        if(Enc.Size>WIM_RESOURCE_SIZE_MASK){free(Enc.Data);return WIM_WRITER_SIZE_OVERFLOW;}
        D->CompressedSize=Enc.Size;D->Flags|=WIM_RESOURCE_FLAG_COMPRESSED;
        S=WimIoWriteExact(Out,Enc.Data,Enc.Size)==WIM_IO_OK?WIM_WRITER_OK:WIM_WRITER_IO_ERROR;
    }else{
        D->CompressedSize=Size;
        S=WimIoWriteExact(Out,Data,(size_t)Size)==WIM_IO_OK?WIM_WRITER_OK:WIM_WRITER_IO_ERROR;
    }
    free(Enc.Data);return S;
}
static void WimWriterEncodeResource(uint8_t *P,const WIM_RESOURCE_DESCRIPTOR *D)
{WimWriterPut64(P,D->CompressedSize|((uint64_t)D->Flags<<56));WimWriterPut64(P+8,D->Offset);WimWriterPut64(P+16,D->OriginalSize);}

WIM_WRITER_STATUS
WimWriterWrite(WIM_IO *Output,const WIM_WRITER_OPTIONS *Options)
{
    WIM_RESOURCE_DESCRIPTOR *BlobResources=NULL,LookupResource,XmlResource,BootResource,IntegrityResource;
    uint8_t *LookupBytes=NULL,Header[WIM_WRITER_HEADER_SIZE];
    uint64_t ExistingSize;size_t LookupSize,I,MetadataCount=0;uint32_t Version,ChunkSize;
    int Compress;WIM_WRITER_STATUS Result=WIM_WRITER_INVALID_ARGUMENT;
    if(!Output||!Options||Options->StructureSize<sizeof(*Options)||!Options->ImageCount||
       Options->BootIndex>Options->ImageCount||!Options->Blobs||!Options->BlobCount||
       !Options->XmlData||!Options->XmlSize||
       ((Options->Flags&WIM_HEADER_FLAG_COMPRESSION_MASK)!=0&&
        (Options->Flags&WIM_HEADER_FLAG_COMPRESSION_MASK)!=WIM_HEADER_FLAG_COMPRESS_XPRESS))return WIM_WRITER_INVALID_ARGUMENT;
    Compress=(Options->Flags&WIM_HEADER_FLAG_COMPRESS_XPRESS)!=0;
    ChunkSize=Options->ChunkSize?Options->ChunkSize:WIM_WRITER_DEFAULT_CHUNK;
    if(Compress&&(ChunkSize<4096||ChunkSize>16u*1024u*1024u||(ChunkSize&(ChunkSize-1))!=0))
        return WIM_WRITER_INVALID_ARGUMENT;
    if(Options->BlobCount>SIZE_MAX/WIM_WRITER_LOOKUP_ENTRY_SIZE||
       Options->BlobCount>SIZE_MAX/sizeof(*BlobResources))return WIM_WRITER_SIZE_OVERFLOW;
    LookupSize=Options->BlobCount*WIM_WRITER_LOOKUP_ENTRY_SIZE;
    if(WimIoGetSize(Output,&ExistingSize)!=WIM_IO_OK||ExistingSize!=0||
       WimIoSeek(Output,0,WIM_IO_SEEK_BEGIN)!=WIM_IO_OK)return WIM_WRITER_IO_ERROR;
    for(I=0;I<Options->BlobCount;I++){
        const WIM_WRITE_BLOB *B=&Options->Blobs[I];
        if(!B->Data||!B->Size||B->Size>WIM_RESOURCE_SIZE_MASK||B->Size>(uint64_t)SIZE_MAX||
           (B->Flags&~(uint8_t)WIM_RESOURCE_FLAG_METADATA)||!B->ReferenceCount)return WIM_WRITER_INVALID_ARGUMENT;
        if(B->Flags&WIM_RESOURCE_FLAG_METADATA)++MetadataCount;
    }
    if(MetadataCount!=Options->ImageCount||
       (Options->BootMetadataSize&&!Options->BootMetadata)||
       (Options->IntegrityTableSize&&!Options->IntegrityTable)||
       Options->XmlSize>WIM_RESOURCE_SIZE_MASK||Options->BootMetadataSize>WIM_RESOURCE_SIZE_MASK||
       Options->IntegrityTableSize>WIM_RESOURCE_SIZE_MASK)return WIM_WRITER_INVALID_ARGUMENT;
    BlobResources=(WIM_RESOURCE_DESCRIPTOR*)calloc(Options->BlobCount,sizeof(*BlobResources));
    LookupBytes=(uint8_t*)calloc(LookupSize?LookupSize:1,1);
    if(!BlobResources||!LookupBytes){Result=WIM_WRITER_OUT_OF_MEMORY;goto Cleanup;}
    memset(Header,0,sizeof(Header));
    if(WimIoWriteExact(Output,Header,sizeof(Header))!=WIM_IO_OK){Result=WIM_WRITER_IO_ERROR;goto Cleanup;}
    for(I=0;I<Options->BlobCount;I++){
        const WIM_WRITE_BLOB *B=&Options->Blobs[I];uint8_t *E=LookupBytes+I*WIM_WRITER_LOOKUP_ENTRY_SIZE,Digest[20];
        Result=WimWriterWriteResource(Output,B->Data,B->Size,B->Flags,ChunkSize,Compress,&BlobResources[I]);
        if(Result!=WIM_WRITER_OK)goto Cleanup;
        WimWriterPut64(E,BlobResources[I].CompressedSize|((uint64_t)BlobResources[I].Flags<<56));
        WimWriterPut64(E+8,BlobResources[I].Offset);WimWriterPut64(E+16,BlobResources[I].OriginalSize);
        WimWriterPut16(E+24,1);WimWriterPut32(E+26,B->ReferenceCount);
        WimSha1(B->Data,(size_t)B->Size,Digest);memcpy(E+30,Digest,sizeof(Digest));
    }
    Result=WimWriterWriteResource(Output,LookupBytes,LookupSize,0,ChunkSize,0,&LookupResource);if(Result!=WIM_WRITER_OK)goto Cleanup;
    Result=WimWriterWriteResource(Output,Options->XmlData,Options->XmlSize,0,ChunkSize,0,&XmlResource);if(Result!=WIM_WRITER_OK)goto Cleanup;
    Result=WimWriterWriteResource(Output,Options->BootMetadata,Options->BootMetadataSize,0,ChunkSize,0,&BootResource);if(Result!=WIM_WRITER_OK)goto Cleanup;
    Result=WimWriterWriteResource(Output,Options->IntegrityTable,Options->IntegrityTableSize,0,ChunkSize,0,&IntegrityResource);if(Result!=WIM_WRITER_OK)goto Cleanup;
    memcpy(Header,WIM_HEADER_SIGNATURE,8);WimWriterPut32(Header+8,WIM_WRITER_HEADER_SIZE);
    Version=Options->Version?Options->Version:WIM_WRITER_DEFAULT_VERSION;WimWriterPut32(Header+12,Version);
    WimWriterPut32(Header+16,Options->Flags);WimWriterPut32(Header+20,Compress?ChunkSize:0);
    if(Options->Guid)memcpy(Header+24,Options->Guid,16);
    WimWriterPut16(Header+40,1);WimWriterPut16(Header+42,1);WimWriterPut32(Header+44,Options->ImageCount);
    WimWriterEncodeResource(Header+48,&LookupResource);WimWriterEncodeResource(Header+72,&XmlResource);
    WimWriterEncodeResource(Header+96,&BootResource);WimWriterPut32(Header+120,Options->BootIndex);
    WimWriterEncodeResource(Header+124,&IntegrityResource);WimWriterPut32(Header+204,WimWriterCrc32(Header,204));
    if(WimIoSeek(Output,0,WIM_IO_SEEK_BEGIN)!=WIM_IO_OK||
       WimIoWriteExact(Output,Header,sizeof(Header))!=WIM_IO_OK||WimIoFlush(Output)!=WIM_IO_OK){Result=WIM_WRITER_IO_ERROR;goto Cleanup;}
    Result=WIM_WRITER_OK;
Cleanup:free(LookupBytes);free(BlobResources);return Result;
}
