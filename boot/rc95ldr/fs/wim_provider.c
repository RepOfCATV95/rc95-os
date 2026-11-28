#include "include/rc95ldr_wim_provider.h"
#include <string.h>
#include <stddef.h>

static void *WimAllocate(void *Opaque, size_t Size)
{
    RC95LDR_WIM_CONTEXT *C=(RC95LDR_WIM_CONTEXT *)Opaque;
    return C->Allocator.Allocate(C->Allocator.Context,Size);
}
static void WimFree(void *Opaque,void *Allocation)
{
    RC95LDR_WIM_CONTEXT *C=(RC95LDR_WIM_CONTEXT *)Opaque;
    if (Allocation) C->Allocator.Free(C->Allocator.Context,Allocation);
}
static RC95LDR_STATUS WimMapStatus(WIM_READER_STATUS S)
{
    switch (S) {
    case WIM_READER_OK: return Rc95LdrSuccess;
    case WIM_READER_OUT_OF_MEMORY: return Rc95LdrOutOfMemory;
    case WIM_READER_UNSUPPORTED: return Rc95LdrUnsupported;
    case WIM_READER_INVALID_ARGUMENT: return Rc95LdrInvalidParameter;
    case WIM_READER_BAD_SIGNATURE: case WIM_READER_BAD_HEADER:
    case WIM_READER_BAD_RESOURCE: case WIM_READER_CORRUPT_DATA:
        return Rc95LdrCorruptData;
    case WIM_READER_BUFFER_TOO_SMALL: return Rc95LdrBufferTooSmall;
    default: return Rc95LdrInvalidImage;
    }
}
static int WimEqualName(const char *A,const char *B,size_t N)
{
    size_t I;
    if (strlen(A)!=N) return 0;
    for (I=0;I<N;++I) {
        unsigned char X=(unsigned char)A[I],Y=(unsigned char)B[I];
        if (X>='A'&&X<='Z') X=(unsigned char)(X+('a'-'A'));
        if (Y>='A'&&Y<='Z') Y=(unsigned char)(Y+('a'-'A'));
        if (X!=Y) return 0;
    }
    return 1;
}
static int WimSafePath(const char *Path)
{
    const char *P=Path;
    if (!P||!*P||*P=='/'||*P=='\\') return 0;
    while (*P) {
        const char *Start=P; size_t N;
        while (*P&&*P!='/'&&*P!='\\') {
            unsigned char C=(unsigned char)*P;
            if (C<0x20||C==':'||C=='*'||C=='?'||C=='"'||
                C=='<'||C=='>'||C=='|') return 0;
            ++P;
        }
        N=(size_t)(P-Start);
        if (!N||(N==1&&Start[0]=='.')||
            (N==2&&Start[0]=='.'&&Start[1]=='.')) return 0;
        if (*P) ++P;
        if (!*P&&(P[-1]=='/'||P[-1]=='\\')) return 0;
    }
    return 1;
}
static WIM_DIRECTORY_NODE *WimFindNode(RC95LDR_WIM_CONTEXT *C,const char *Path)
{
    WIM_DIRECTORY_NODE *Node=C->Root; const char *P=Path;
    while (*P) {
        const char *Start=P; size_t N,I; WIM_DIRECTORY_NODE *Found=NULL;
        while (*P&&*P!='/'&&*P!='\\') ++P;
        N=(size_t)(P-Start);
        for (I=0;I<Node->ChildCount;++I) {
            WIM_DIRECTORY_NODE *Child=Node->Children[I];
            if (Child->Name&&WimEqualName(Child->Name,Start,N)) {
                Found=Child; break;
            }
        }
        if (!Found) return NULL;
        Node=Found;
        if (*P) ++P;
        if (*P&&!Node->IsDirectory) return NULL;
    }
    return Node;
}
static void *WimEncodeHandle(size_t I,uintptr_t G)
{
    return (void *)((G<<4)|(uintptr_t)(I+1));
}
static RC95LDR_WIM_FILE_SLOT *WimDecodeHandle(RC95LDR_WIM_CONTEXT *C,void *H)
{
    uintptr_t V=(uintptr_t)H,S=V&15u,G=V>>4;
    RC95LDR_WIM_FILE_SLOT *Slot;
    if (!C||!V||!S||S>RC95LDR_WIM_OPEN_SLOTS||!G) return NULL;
    Slot=&C->Slots[S-1];
    return Slot->InUse&&Slot->Generation==G?Slot:NULL;
}
static RC95LDR_STATUS WimOpenRead(void *Opaque,const char *Path,
                                  void **Handle,uint64_t *FileSize)
{
    RC95LDR_WIM_CONTEXT *C=(RC95LDR_WIM_CONTEXT *)Opaque;
    WIM_DIRECTORY_NODE *Node; RC95LDR_WIM_FILE_SLOT *Slot=NULL;
    size_t I,Used=0; WIM_READER_STATUS S;
    if (!C||!C->Mounted||!Handle||!FileSize||!WimSafePath(Path))
        return Rc95LdrInvalidParameter;
    *Handle=NULL; *FileSize=0; Node=WimFindNode(C,Path);
    if (!Node) return Rc95LdrNotFound;
    if (Node->IsDirectory) return Rc95LdrInvalidParameter;
    if (Node->MainStreamSize>C->MaximumFileSize||
        Node->MainStreamSize>(uint64_t)SIZE_MAX) return Rc95LdrOutOfBounds;
    for (I=0;I<RC95LDR_WIM_OPEN_SLOTS;++I)
        if (!C->Slots[I].InUse) { Slot=&C->Slots[I]; break; }
    if (!Slot) return Rc95LdrOutOfMemory;
    Slot->Data=(uint8_t *)C->Allocator.Allocate(C->Allocator.Context,
                       Node->MainStreamSize?(size_t)Node->MainStreamSize:1u);
    if (!Slot->Data) return Rc95LdrOutOfMemory;
    if (Node->MainStreamSize) {
        S=WimImageReadMainStream(C->Reader,Node,Slot->Data,
                                 (size_t)Node->MainStreamSize,&Used);
        if (S!=WIM_READER_OK||Used!=(size_t)Node->MainStreamSize) {
            C->Allocator.Free(C->Allocator.Context,Slot->Data); Slot->Data=NULL;
            return S==WIM_READER_OK?Rc95LdrCorruptData:WimMapStatus(S);
        }
    }
    Slot->Generation=(Slot->Generation+1u)&(UINTPTR_MAX>>4);
    if (!Slot->Generation) Slot->Generation=1;
    Slot->Size=Node->MainStreamSize; Slot->InUse=1;
    *Handle=WimEncodeHandle(I,Slot->Generation); *FileSize=Slot->Size;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS WimReadAt(void *Opaque,void *Handle,uint64_t Offset,
                                void *Buffer,size_t Length)
{
    RC95LDR_WIM_CONTEXT *C=(RC95LDR_WIM_CONTEXT *)Opaque;
    RC95LDR_WIM_FILE_SLOT *Slot=WimDecodeHandle(C,Handle);
    if (!Slot||(!Buffer&&Length)) return Rc95LdrInvalidParameter;
    if (Offset>Slot->Size||(uint64_t)Length>Slot->Size-Offset)
        return Rc95LdrOutOfBounds;
    if (Length) memcpy(Buffer,Slot->Data+(size_t)Offset,Length);
    return Rc95LdrSuccess;
}
static void WimClose(void *Opaque,void *Handle)
{
    RC95LDR_WIM_CONTEXT *C=(RC95LDR_WIM_CONTEXT *)Opaque;
    RC95LDR_WIM_FILE_SLOT *Slot=WimDecodeHandle(C,Handle);
    if (!Slot) return;
    C->Allocator.Free(C->Allocator.Context,Slot->Data);
    Slot->Data=NULL; Slot->Size=0; Slot->InUse=0;
}
static void WimSetFilesystem(RC95LDR_WIM_CONTEXT *C,RC95LDR_VOLUME *V)
{
    memset(&C->FileSystem,0,sizeof(C->FileSystem));
    C->FileSystem.FileSystemContext=C;
    C->FileSystem.OpenRead=WimOpenRead;
    C->FileSystem.ReadAt=WimReadAt;
    C->FileSystem.Close=WimClose;
    V->BlockDevice=NULL; V->FileSystem=&C->FileSystem;
}
RC95LDR_STATUS Rc95LdrWimOpenMemory(
    RC95LDR_WIM_CONTEXT *C,const void *Image,size_t ImageSize,
    uint64_t MaxResource,uint64_t MaxFile,const RC95LDR_WIM_ALLOCATOR *A,
    WIM_HEADER_INFO *Header)
{
    WIM_READER_OPTIONS O; WIM_READER_STATUS S; size_t Count=0;
    if (!C||!Image||!ImageSize||!A||!A->Allocate||!A->Free)
        return Rc95LdrInvalidParameter;
    memset(C,0,sizeof(*C)); C->Allocator=*A;
    C->Image=(const uint8_t *)Image; C->ImageSize=ImageSize;
    C->MaximumFileSize=MaxFile?MaxFile:RC95LDR_WIM_DEFAULT_MAX_FILE_SIZE;
    memset(&O,0,sizeof(O)); O.StructureSize=sizeof(O);
    O.MaximumResourceSize=MaxResource?MaxResource:RC95LDR_WIM_DEFAULT_MAX_RESOURCE_SIZE;
    O.Allocator.Context=C; O.Allocator.Allocate=WimAllocate; O.Allocator.Free=WimFree;
    S=(WimCodecRegisterBuiltins()==WIM_CODEC_STATUS_OK)?
      WimReaderOpenMemory(&C->Reader,Image,ImageSize,&O):WIM_READER_UNSUPPORTED;
    if (S!=WIM_READER_OK) { C->Reader=NULL; return WimMapStatus(S); }
    S=WimReaderGetImageCount(C->Reader,&Count);
    if (S!=WIM_READER_OK || !Count) {
        WimReaderClose(C->Reader); C->Reader=NULL;
        return S==WIM_READER_OK?Rc95LdrInvalidImage:WimMapStatus(S);
    }
    if (Header && WimReaderGetHeaderInfo(C->Reader,Header)!=WIM_READER_OK) {
        WimReaderClose(C->Reader); C->Reader=NULL; return Rc95LdrCorruptData;
    }
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrWimSelectImage(RC95LDR_WIM_CONTEXT *C,size_t Index,
                                      RC95LDR_VOLUME *V,WIM_IMAGE_INFO *Info)
{
    WIM_READER_STATUS S; WIM_DIRECTORY_NODE *Root=NULL;
    if (!C||!C->Reader||!V) return Rc95LdrInvalidParameter;
    if (C->Mounted) return Rc95LdrInvalidParameter;
    S=WimReaderLoadImageDirectoryTree(C->Reader,Index,&Root);
    if (S!=WIM_READER_OK) return WimMapStatus(S);
    if (Info) {
        S=WimReaderGetImageInfo(C->Reader,Index,Info);
        if (S!=WIM_READER_OK) {
            WimImageDirectoryTreeFree(C->Reader,Root);
            return WimMapStatus(S);
        }
    }
    C->Root=Root; C->Mounted=1; WimSetFilesystem(C,V);
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrWimMountMemory(
    RC95LDR_WIM_CONTEXT *C,const void *Image,size_t ImageSize,size_t Index,
    uint64_t MaxResource,uint64_t MaxFile,const RC95LDR_WIM_ALLOCATOR *A,
    RC95LDR_VOLUME *V,WIM_HEADER_INFO *Header,WIM_IMAGE_INFO *Info)
{
    RC95LDR_STATUS St=Rc95LdrWimOpenMemory(C,Image,ImageSize,MaxResource,
                                           MaxFile,A,Header);
    if (St!=Rc95LdrSuccess) return St;
    St=Rc95LdrWimSelectImage(C,Index,V,Info);
    if (St!=Rc95LdrSuccess) Rc95LdrWimUnmount(C);
    return St;
}
RC95LDR_STATUS Rc95LdrWimOpenFile(
    RC95LDR_WIM_CONTEXT *C,const RC95LDR_VOLUME *Source,const char *Path,
    uint64_t MaxImage,uint64_t MaxResource,uint64_t MaxFile,
    const RC95LDR_WIM_ALLOCATOR *A,WIM_HEADER_INFO *Header)
{
    void *File=NULL; uint64_t Size=0; uint8_t *Bytes; RC95LDR_STATUS St;
    if (!C||!Source||!Source->FileSystem||!Source->FileSystem->OpenRead||
        !Source->FileSystem->ReadAt||!Source->FileSystem->Close||
        !Path||!A||!A->Allocate||!A->Free) return Rc95LdrInvalidParameter;
    St=Source->FileSystem->OpenRead(Source->FileSystem->FileSystemContext,
                                    Path,&File,&Size);
    if (St!=Rc95LdrSuccess) return St;
    if (!File||!Size||Size>(MaxImage?MaxImage:RC95LDR_WIM_DEFAULT_MAX_IMAGE_SIZE)||
        Size>(uint64_t)SIZE_MAX) {
        Source->FileSystem->Close(Source->FileSystem->FileSystemContext,File);
        return Rc95LdrOutOfBounds;
    }
    Bytes=(uint8_t *)A->Allocate(A->Context,(size_t)Size);
    if (!Bytes) {
        Source->FileSystem->Close(Source->FileSystem->FileSystemContext,File);
        return Rc95LdrOutOfMemory;
    }
    St=Source->FileSystem->ReadAt(Source->FileSystem->FileSystemContext,
                                  File,0,Bytes,(size_t)Size);
    Source->FileSystem->Close(Source->FileSystem->FileSystemContext,File);
    if (St!=Rc95LdrSuccess) { A->Free(A->Context,Bytes); return St; }
    St=Rc95LdrWimOpenMemory(C,Bytes,(size_t)Size,MaxResource,MaxFile,A,Header);
    if (St!=Rc95LdrSuccess) { A->Free(A->Context,Bytes); return St; }
    C->ImageOwned=1;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrWimMountFile(
    RC95LDR_WIM_CONTEXT *C,const RC95LDR_VOLUME *Source,const char *Path,
    size_t Index,uint64_t MaxImage,uint64_t MaxResource,uint64_t MaxFile,
    const RC95LDR_WIM_ALLOCATOR *A,RC95LDR_VOLUME *V,
    WIM_HEADER_INFO *Header,WIM_IMAGE_INFO *Info)
{
    RC95LDR_STATUS St=Rc95LdrWimOpenFile(C,Source,Path,MaxImage,
                                         MaxResource,MaxFile,A,Header);
    if (St!=Rc95LdrSuccess) return St;
    St=Rc95LdrWimSelectImage(C,Index,V,Info);
    if (St!=Rc95LdrSuccess) Rc95LdrWimUnmount(C);
    return St;
}
void Rc95LdrWimUnmount(RC95LDR_WIM_CONTEXT *C)
{
    size_t I; const uint8_t *Image;
    if (!C) return;
    Image=C->Image;
    for (I=0;I<RC95LDR_WIM_OPEN_SLOTS;++I)
        if (C->Slots[I].InUse) WimClose(C,WimEncodeHandle(I,C->Slots[I].Generation));
    if (C->Root&&C->Reader) WimImageDirectoryTreeFree(C->Reader,C->Root);
    if (C->Reader) WimReaderClose(C->Reader);
    if (C->ImageOwned&&Image) C->Allocator.Free(C->Allocator.Context,(void *)Image);
    memset(C,0,sizeof(*C));
}
WIM_READER_STATUS Rc95LdrWimGetImageCount(RC95LDR_WIM_CONTEXT *C,size_t *N)
{
    return C&&C->Reader?WimReaderGetImageCount(C->Reader,N):WIM_READER_INVALID_ARGUMENT;
}
WIM_READER_STATUS Rc95LdrWimGetImageInfo(RC95LDR_WIM_CONTEXT *C,size_t I,WIM_IMAGE_INFO *Info)
{
    return C&&C->Reader?WimReaderGetImageInfo(C->Reader,I,Info):WIM_READER_INVALID_ARGUMENT;
}
