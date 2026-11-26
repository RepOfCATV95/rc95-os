/*
 * PROJECT: RC95-OS WIM image utility
 * PURPOSE: Inspect WIM headers/resources and extract the XML resource.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <libwim/wim_read.h>
#include <libwim/wim_io.h>
#include <libwim/wim_image.h>
#include <libwim/wim_capture.h>

#define WIMAGE_VERSION "0.2"
#define WIMAGE_MAX_FILE_SIZE ((size_t)1024 * 1024 * 1024)

static void *WimageAllocate(void *Context, size_t Size)
{
    (void)Context;
    return malloc(Size);
}

static void WimageFree(void *Context, void *Allocation)
{
    (void)Context;
    free(Allocation);
}

static const char *ResourceName(WIM_RESOURCE_KIND Kind)
{
    static const char *Names[WIM_RESOURCE_KIND_COUNT] = {
        "lookup table", "XML data", "boot metadata", "integrity table"
    };
    return (unsigned)Kind < WIM_RESOURCE_KIND_COUNT ? Names[Kind] : "unknown";
}

static const char *CompressionName(uint32_t Flags)
{
    switch (Flags & WIM_HEADER_FLAG_COMPRESSION_MASK) {
    case WIM_HEADER_FLAG_COMPRESS_XPRESS:
    case WIM_HEADER_FLAG_COMPRESS_XPRESS_2: return "XPRESS";
    case WIM_HEADER_FLAG_COMPRESS_LZX: return "LZX";
    case WIM_HEADER_FLAG_COMPRESS_LZMS: return "LZMS";
    default: return "none/unknown";
    }
}

static int LoadFile(const char *Path, uint8_t **Data, size_t *Size)
{
    WIM_IO *Input = NULL;
    uint64_t Length = 0;
    uint8_t *Buffer;
    WIM_IO_STATUS IoStatus;

    *Data = NULL;
    *Size = 0;
    IoStatus = WimIoOpenFile(&Input, Path, WIM_IO_FILE_READ);
    if (IoStatus != WIM_IO_OK) {
        fprintf(stderr, "wimage: cannot open '%s' (I/O status %u)\n",
                Path, (unsigned)IoStatus);
        return 0;
    }
    IoStatus = WimIoGetSize(Input, &Length);
    if (IoStatus != WIM_IO_OK || Length > (uint64_t)WIMAGE_MAX_FILE_SIZE ||
        Length > (uint64_t)(size_t)-1) {
        fprintf(stderr, "wimage: invalid or oversized input '%s'\n", Path);
        WimIoClose(Input);
        return 0;
    }

    Buffer = (uint8_t *)malloc(Length ? (size_t)Length : 1);
    if (!Buffer) {
        WimIoClose(Input);
        fprintf(stderr, "wimage: out of memory\n");
        return 0;
    }
    IoStatus = WimIoReadExact(Input, Buffer, (size_t)Length);
    WimIoClose(Input);
    if (IoStatus != WIM_IO_OK) {
        fprintf(stderr, "wimage: error reading '%s' (I/O status %u)\n",
                Path, (unsigned)IoStatus);
        free(Buffer);
        return 0;
    }
    *Data = Buffer;
    *Size = (size_t)Length;
    return 1;
}

static WIM_READER *OpenImage(const char *Path, uint8_t **ImageData)
{
    WIM_READER_OPTIONS Options;
    WIM_READER *Reader = NULL;
    size_t ImageSize;
    WIM_READER_STATUS Status;

    if (!LoadFile(Path, ImageData, &ImageSize))
        return NULL;
    memset(&Options, 0, sizeof(Options));
    Options.StructureSize = sizeof(Options);
    Options.MaximumResourceSize = WIMAGE_MAX_FILE_SIZE;
    Options.Allocator.Allocate = WimageAllocate;
    Options.Allocator.Free = WimageFree;
    Status = WimReaderOpenMemory(&Reader, *ImageData, ImageSize, &Options);
    if (Status != WIM_READER_OK) {
        fprintf(stderr, "wimage: cannot parse WIM '%s' (reader status %u)\n",
                Path, (unsigned)Status);
        free(*ImageData);
        *ImageData = NULL;
        return NULL;
    }
    return Reader;
}

static int InspectImage(const char *Path)
{
    uint8_t *ImageData;
    WIM_READER *Reader = OpenImage(Path, &ImageData);
    WIM_HEADER_INFO Header;
    unsigned I;
    if (!Reader)
        return 1;
    WimReaderGetHeaderInfo(Reader, &Header);
    printf("WIM header: valid\n");
    printf("Version: 0x%08lx\n", (unsigned long)Header.Version);
    printf("Images: %lu\n", (unsigned long)Header.ImageCount);
    printf("Boot index: %lu\n", (unsigned long)Header.BootIndex);
    printf("Part: %u of %u\n", Header.PartNumber, Header.TotalParts);
    printf("Chunk size: %lu bytes\n", (unsigned long)Header.ChunkSize);
    printf("Compression: %s\n", CompressionName(Header.Flags));
    {
        size_t LookupCount = 0, MetadataCount = 0, EntryIndex;
        WIM_READER_STATUS LookupStatus =
            WimReaderGetLookupEntryCount(Reader, &LookupCount);
        if (LookupStatus == WIM_READER_OK) {
            for (EntryIndex = 0; EntryIndex < LookupCount; ++EntryIndex) {
                WIM_LOOKUP_ENTRY Entry;
                if (WimReaderGetLookupEntry(Reader, EntryIndex, &Entry) ==
                    WIM_READER_OK &&
                    (Entry.Resource.Flags & WIM_RESOURCE_FLAG_METADATA))
                    ++MetadataCount;
            }
            printf("Lookup entries: %lu (%lu metadata resources)\n",
                   (unsigned long)LookupCount, (unsigned long)MetadataCount);
        } else {
            printf("Lookup table: unavailable (reader status %u)\n",
                   (unsigned)LookupStatus);
        }
    }
    for (I = 0; I < WIM_RESOURCE_KIND_COUNT; ++I) {
        WIM_RESOURCE_DESCRIPTOR D;
        WimReaderGetResource(Reader, (WIM_RESOURCE_KIND)I, &D);
        if (!D.OriginalSize)
            continue;
        printf("Resource %-16s offset=%llu stored=%llu original=%llu %s\n",
               ResourceName((WIM_RESOURCE_KIND)I),
               (unsigned long long)D.Offset,
               (unsigned long long)D.CompressedSize,
               (unsigned long long)D.OriginalSize,
               (D.Flags & WIM_RESOURCE_FLAG_COMPRESSED) ? "compressed" : "raw");
    }
    WimReaderClose(Reader);
    free(ImageData);
    return 0;
}

static int ExtractXml(const char *ImagePath, const char *OutputPath)
{
    uint8_t *ImageData, *XmlData;
    WIM_READER *Reader = OpenImage(ImagePath, &ImageData);
    WIM_RESOURCE_DESCRIPTOR Descriptor;
    size_t OutputUsed = 0;
    WIM_READER_STATUS Status;
    FILE *OutputFile;
    if (!Reader)
        return 1;
    Status = WimReaderGetResource(Reader, WIM_RESOURCE_XML_DATA, &Descriptor);
    if (Status != WIM_READER_OK || !Descriptor.OriginalSize ||
        Descriptor.OriginalSize > WIMAGE_MAX_FILE_SIZE) {
        fprintf(stderr, "wimage: WIM has no usable XML resource\n");
        WimReaderClose(Reader);
        free(ImageData);
        return 1;
    }
    XmlData = (uint8_t *)malloc((size_t)Descriptor.OriginalSize);
    if (!XmlData) {
        fprintf(stderr, "wimage: out of memory\n");
        WimReaderClose(Reader);
        free(ImageData);
        return 1;
    }
    Status = WimReaderReadResource(Reader, &Descriptor, XmlData,
                                   (size_t)Descriptor.OriginalSize,
                                   &OutputUsed);
    if (Status != WIM_READER_OK) {
        fprintf(stderr, "wimage: XML resource decode failed (status %u)\n",
                (unsigned)Status);
        free(XmlData);
        WimReaderClose(Reader);
        free(ImageData);
        return 1;
    }
    OutputFile = fopen(OutputPath, "wb");
    if (!OutputFile ||
        fwrite(XmlData, 1, OutputUsed, OutputFile) != OutputUsed) {
        fprintf(stderr, "wimage: cannot write '%s'\n", OutputPath);
        if (OutputFile)
            fclose(OutputFile);
        free(XmlData);
        WimReaderClose(Reader);
        free(ImageData);
        return 1;
    }
    fclose(OutputFile);
    printf("Extracted %lu bytes of WIM XML to %s\n",
           (unsigned long)OutputUsed, OutputPath);
    free(XmlData);
    WimReaderClose(Reader);
    free(ImageData);
    return 0;
}


static int ListImages(const char *Path)
{
    uint8_t *ImageData;
    WIM_READER *Reader=OpenImage(Path,&ImageData);
    size_t Count,i;
    WIM_READER_STATUS Status;
    if(!Reader) return 1;
    Status=WimReaderGetImageCount(Reader,&Count);
    if(Status!=WIM_READER_OK) {
        fprintf(stderr,"wimage: cannot parse image catalogue (status %u)\n",(unsigned)Status);
        WimReaderClose(Reader); free(ImageData); return 1;
    }
    for(i=0;i<Count;i++) {
        WIM_IMAGE_INFO Info;
        Status=WimReaderGetImageInfo(Reader,i,&Info);
        if(Status!=WIM_READER_OK) { WimReaderClose(Reader); free(ImageData); return 1; }
        printf("%lu: %s  (%lu directories, %lu files, %llu bytes)\n",
               (unsigned long)Info.Index,Info.Name?Info.Name:"",
               (unsigned long)Info.DirectoryCount,(unsigned long)Info.FileCount,
               (unsigned long long)Info.TotalBytes);
        WimImageInfoFree(Reader,&Info);
    }
    WimReaderClose(Reader); free(ImageData); return 0;
}
static void PrintTree(const WIM_DIRECTORY_NODE *Node,unsigned Depth)
{
    size_t i;
    for(i=0;i<Depth;i++) fputs("  ",stdout);
    printf("%s%s",Node->Name?Node->Name:"<root>",Node->IsDirectory?"/":"");
    if(!Node->IsDirectory && Node->HasMainStream)
        printf(" (%llu bytes)",(unsigned long long)Node->MainStreamSize);
    putchar(10);
    for(i=0;i<Node->ChildCount;i++) PrintTree(Node->Children[i],Depth+1);
}

static int ParseImageIndex(const char *text, size_t *zeroBased);

static int ListTree(const char *Path,const char *IndexText)
{
    uint8_t *ImageData; WIM_READER *Reader=OpenImage(Path,&ImageData);
    size_t ImageIndex; WIM_DIRECTORY_NODE *Root=NULL;
    WIM_READER_STATUS Status;
    if(!Reader) return 1;
    if(!ParseImageIndex(IndexText,&ImageIndex)) { fprintf(stderr,"wimage: invalid image index\n"); WimReaderClose(Reader); free(ImageData); return 2; }
    Status=WimReaderLoadImageDirectoryTree(Reader,ImageIndex,&Root);
    if(Status!=WIM_READER_OK) {
        fprintf(stderr,"wimage: cannot load image directory tree (status %u)\n",(unsigned)Status);
        WimReaderClose(Reader); free(ImageData); return 1;
    }
    PrintTree(Root,0); WimImageDirectoryTreeFree(Reader,Root);
    WimReaderClose(Reader); free(ImageData); return 0;
}


static int ParseImageIndex(const char *text, size_t *zeroBased)
{
    char *end;
    unsigned long long value;
    if (!text || !text[0] || !zeroBased) return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value == 0 ||
        value - 1 > (unsigned long long)SIZE_MAX) return 0;
    *zeroBased = (size_t)(value - 1);
    return 1;
}

#define WIMAGE_PATH_CAPACITY 4096
static int SafeComponent(const char *s) {
    size_t n,i; if(!s||!s[0]||!strcmp(s,".")||!strcmp(s,"..")) return 0;
    n=strlen(s); if(s[0]=='/'||s[0]=='\\'||s[n-1]=='.'||s[n-1]==' ') return 0;
    for(i=0;i<n;i++){ unsigned char c=(unsigned char)s[i]; if(c<32||c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|') return 0; }
    {
        char base[5]; size_t k=0;
        while(k<n && s[k]!='.' && k<4) { base[k]=(char)toupper((unsigned char)s[k]); ++k; }
        base[k]=0;
        if(!strcmp(base,"CON")||!strcmp(base,"AUX")||!strcmp(base,"NUL")||
           !strcmp(base,"PRN")||!strcmp(base,"CONIN")||!strcmp(base,"CONOUT")||
           (k==4&&((!strncmp(base,"COM",3)||!strncmp(base,"LPT",3))&&base[3]>='1'&&base[3]<='9'))) return 0;
    }
    return 1;
}
static int IsSafeDirectory(const char *p) {
#ifdef _WIN32
    DWORD a=GetFileAttributesA(p); return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st; return lstat(p,&st)==0&&S_ISDIR(st.st_mode)&&!S_ISLNK(st.st_mode);
#endif
}
static int MakeChildDirectory(const char *p) {
#ifdef _WIN32
    if(CreateDirectoryA(p,NULL)) return 1; return GetLastError()==ERROR_ALREADY_EXISTS&&IsSafeDirectory(p);
#else
    if(mkdir(p,0700)==0) return 1; return errno==EEXIST&&IsSafeDirectory(p);
#endif
}
static int BuildChildPath(char *o,size_t cap,const char *parent,const char *part) {
    size_t a=strlen(parent),b=strlen(part),sep;
    if(!SafeComponent(part)||a>=cap) return 0;
    sep=(a&&parent[a-1]!='/'&&parent[a-1]!='\\')?1u:0u;
    if(b>=cap-a-sep) return 0;
    memcpy(o,parent,a); if(sep) o[a++]=
#ifdef _WIN32
    '\\';
#else
    '/';
#endif
    memcpy(o+a,part,b+1); return 1;
}
static int WriteNewFile(const char *p,const uint8_t *data,size_t n,int sparse) {
    int fd,ok=1,useSparse=sparse;
#ifdef _WIN32
    fd=_open(p,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
    fd=open(p,O_WRONLY|O_CREAT|O_EXCL
#ifdef O_NOFOLLOW
    |O_NOFOLLOW
#endif
    ,0600);
#endif
    if(fd<0) return 0;
    if(sparse && n) {
#ifdef _WIN32
        DWORD returned=0; HANDLE h=(HANDLE)_get_osfhandle(fd);
        if(h==INVALID_HANDLE_VALUE || !DeviceIoControl(h,FSCTL_SET_SPARSE,NULL,0,NULL,0,&returned,NULL)) useSparse=0;
#else
        /* lseek() below creates holes; ftruncate() establishes the logical EOF. */
#endif
    }
    if(ok && n) {
        size_t pos=0;
        while(pos<n) {
            size_t zeroEnd=pos, writeEnd;
            if(useSparse) {
                while(zeroEnd<n && data[zeroEnd]==0) ++zeroEnd;
                if(zeroEnd-pos>=4096) {
#ifdef _WIN32
                    LARGE_INTEGER where; where.QuadPart=(LONGLONG)zeroEnd;
                    if(!SetFilePointerEx((HANDLE)_get_osfhandle(fd),where,NULL,FILE_BEGIN)){ok=0;break;}
#else
                    if(lseek(fd,(off_t)zeroEnd,SEEK_SET)<0){ok=0;break;}
#endif
                    pos=zeroEnd;continue;
                }
            }
            writeEnd=pos;
            if(useSparse) {
                while(writeEnd<n) {
                    size_t z=writeEnd;while(z<n&&data[z]==0)++z;
                    if(z-writeEnd>=4096)break;
                    writeEnd=z;
                    if(writeEnd<n)++writeEnd;
                }
                if(writeEnd==pos)writeEnd=n;
            } else writeEnd=n;
            while(pos<writeEnd) {
                size_t left=writeEnd-pos;
#ifdef _WIN32
                unsigned chunk=left>0x7fffffffU?0x7fffffffU:(unsigned)left;
                int wrote=_write(fd,data+pos,chunk);
#else
                size_t chunk=left;
                ssize_t wrote=write(fd,data+pos,chunk);
#endif
                if(wrote<=0){ok=0;break;}
                pos+=(size_t)wrote;
            }
            if(!ok)break;
        }
    }
    if(ok && useSparse) {
#ifdef _WIN32
        LARGE_INTEGER end; end.QuadPart=(LONGLONG)n;
        if(!SetFilePointerEx((HANDLE)_get_osfhandle(fd),end,NULL,FILE_BEGIN)||
           !SetEndOfFile((HANDLE)_get_osfhandle(fd)))ok=0;
#else
        if(ftruncate(fd,(off_t)n)!=0)ok=0;
#endif
    }
#ifdef _WIN32
    if(_close(fd)!=0)ok=0;
#else
    if(close(fd)!=0)ok=0;
#endif
    return ok;
}
static void StreamSlug(char *o,size_t cap,const char *s) {
    size_t i,j=0; for(i=0;s[i]&&j+1<cap;i++){unsigned char c=(unsigned char)s[i];o[j++]=((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')?(char)c:'_';}
    o[j]=0; if(!j&&cap>1){o[0]='_';o[1]=0;}
}

static int ExtractNamedStreams(WIM_READER *r,const WIM_DIRECTORY_NODE *n,const char *parent)
{
    size_t i,used; uint8_t *data; WIM_READER_STATUS st;
    for(i=0;i<n->NamedStreamCount;i++) {
        const WIM_NAMED_STREAM *s=&n->NamedStreams[i];
        char side[WIMAGE_PATH_CAPACITY],component[WIMAGE_PATH_CAPACITY],slug[WIMAGE_PATH_CAPACITY];
        StreamSlug(slug,sizeof(slug),s->Name);
        if(strlen(n->Name)+strlen(slug)+12>=sizeof(component)) return 0;
        snprintf(component,sizeof(component),"%s.wimstream.%s",n->Name,slug);
        if(!BuildChildPath(side,sizeof(side),parent,component)||
           s->Size>WIMAGE_MAX_FILE_SIZE||s->Size>(uint64_t)(size_t)-1) return 0;
        data=(uint8_t*)malloc((size_t)s->Size?(size_t)s->Size:1);
        if(!data) return 0;
        st=WimImageReadNamedStream(r,n,i,data,(size_t)s->Size,&used);
        if(st!=WIM_READER_OK||used!=s->Size||!WriteNewFile(side,data,used,0)) { free(data); return 0; }
        free(data);
    }
    return 1;
}

static int ExtractNode(WIM_READER *r,const WIM_DIRECTORY_NODE *n,const char *parent) {
    char path[WIMAGE_PATH_CAPACITY]; size_t i,used; uint8_t *data; WIM_READER_STATUS st;
    if(!BuildChildPath(path,sizeof(path),parent,n->Name)) return 0;
    if(n->IsDirectory){if(!MakeChildDirectory(path))return 0;for(i=0;i<n->ChildCount;i++)if(!ExtractNode(r,n->Children[i],path))return 0;return ExtractNamedStreams(r,n,parent);}
    if(n->HasMainStream){
        if(n->MainStreamSize>WIMAGE_MAX_FILE_SIZE||n->MainStreamSize>(uint64_t)(size_t)-1)return 0;
        data=(uint8_t*)malloc((size_t)n->MainStreamSize?(size_t)n->MainStreamSize:1);if(!data)return 0;
        st=WimImageReadMainStream(r,n,data,(size_t)n->MainStreamSize,&used);
        if(st!=WIM_READER_OK||used!=n->MainStreamSize||!WriteNewFile(path,data,used,(n->Attributes&0x200u)!=0)){free(data);return 0;}free(data);
    }else if(!WriteNewFile(path,NULL,0,0))return 0;
    return ExtractNamedStreams(r,n,parent);
}
static int ExtractImage(const char *image,const char *idx,const char *dest) {
    uint8_t *bytes;WIM_READER *r=OpenImage(image,&bytes);WIM_DIRECTORY_NODE *root=NULL;size_t index;WIM_READER_STATUS st;size_t i;int ok=1;
    if(!r)return 1;if(!ParseImageIndex(idx,&index)){WimReaderClose(r);free(bytes);return 2;}
    if(!IsSafeDirectory(dest)){fprintf(stderr,"wimage: destination must exist and must not be a symlink/reparse point\n");WimReaderClose(r);free(bytes);return 2;}
    st=WimReaderLoadImageDirectoryTree(r,index,&root);
    if(st!=WIM_READER_OK){fprintf(stderr,"wimage: cannot load image tree (status %u)\n",(unsigned)st);WimReaderClose(r);free(bytes);return 1;}
    for(i=0;i<root->ChildCount;i++)if(!ExtractNode(r,root->Children[i],dest)){fprintf(stderr,"wimage: extraction stopped at '%s' (unsafe path, existing target, or stream read/write failure)\n",root->Children[i]->Name?root->Children[i]->Name:"<unnamed>");ok=0;break;}
    WimImageDirectoryTreeFree(r,root);WimReaderClose(r);free(bytes);return ok?0:1;
}

static int VerifyImage(const char *Path)
{
    uint8_t *ImageData;
    WIM_READER *Reader = OpenImage(Path, &ImageData);
    WIM_HEADER_INFO Header;
    size_t Count = 0, I;
    size_t Verified = 0, Skipped = 0;
    WIM_READER_STATUS Status;
    int Failed = 0;

    if (!Reader)
        return 1;
    Status = WimReaderGetHeaderInfo(Reader, &Header);
    if (Status != WIM_READER_OK) {
        WimReaderClose(Reader);
        free(ImageData);
        return 1;
    }
    Status = WimReaderGetLookupEntryCount(Reader, &Count);
    if (Status != WIM_READER_OK) {
        fprintf(stderr, "wimage: cannot load lookup table (status %u)\n",
                (unsigned)Status);
        WimReaderClose(Reader);
        free(ImageData);
        return 1;
    }

    for (I = 0; I < Count; ++I) {
        WIM_LOOKUP_ENTRY Entry;
        uint8_t *Buffer;
        size_t Used = 0;

        Status = WimReaderGetLookupEntry(Reader, I, &Entry);
        if (Status != WIM_READER_OK) {
            fprintf(stderr, "wimage: lookup entry %lu cannot be read (status %u)\n",
                    (unsigned long)I, (unsigned)Status);
            Failed = 1;
            break;
        }
        if ((Entry.Resource.Flags & WIM_RESOURCE_FLAG_FREE) ||
            Entry.Resource.OriginalSize == 0)
            continue;
        if (Entry.PartNumber != Header.PartNumber) {
            ++Skipped;
            continue;
        }
        if (Entry.Resource.OriginalSize > WIMAGE_MAX_FILE_SIZE ||
            Entry.Resource.OriginalSize > (uint64_t)(size_t)-1) {
            fprintf(stderr, "wimage: lookup entry %lu exceeds verification limit\n",
                    (unsigned long)I);
            Failed = 1;
            continue;
        }

        Buffer = (uint8_t *)malloc((size_t)Entry.Resource.OriginalSize);
        if (!Buffer) {
            fprintf(stderr, "wimage: out of memory while verifying entry %lu\n",
                    (unsigned long)I);
            Failed = 1;
            break;
        }
        Status = WimReaderReadLookupResource(Reader, I, Buffer,
                    (size_t)Entry.Resource.OriginalSize, &Used);
        free(Buffer);
        if (Status != WIM_READER_OK ||
            Used != (size_t)Entry.Resource.OriginalSize) {
            fprintf(stderr, "wimage: lookup entry %lu failed verification (status %u)\n",
                    (unsigned long)I, (unsigned)Status);
            Failed = 1;
            continue;
        }
        ++Verified;
    }

    printf("Verified lookup resources: %lu\n", (unsigned long)Verified);
    if (Skipped)
        printf("Skipped resources in other WIM parts: %lu\n",
               (unsigned long)Skipped);
    if (Skipped)
        fprintf(stderr, "wimage: verification is incomplete; provide every split WIM part to verify all resources\n");
    WimReaderClose(Reader);
    free(ImageData);
    return Failed ? 1 : (Skipped ? 3 : 0);
}

static int CaptureImage(const char *source,const char *output,const char *name)
{
    WIM_CAPTURE_OPTIONS options;
    WIM_CAPTURE_STATUS status;
    memset(&options,0,sizeof(options));
    options.StructureSize=sizeof(options);
    options.ImageName=name&&name[0]?name:"Captured Image";
    status=WimCaptureDirectory(source,output,&options);
    if(status!=WIM_CAPTURE_OK) {
        fprintf(stderr,"wimage: capture failed (status %u)\n",(unsigned)status);
        return 1;
    }
    printf("Captured filesystem '%s' to %s\n",source,output);
    return 0;
}

static void PrintUsage(const char *Program)
{
    printf("Usage:\n");
    printf("  %s --help\n", Program);
    printf("  %s --version\n", Program);
    printf("  %s inspect <image.wim>\n", Program);
    printf("  %s verify <image.wim>\n", Program);
    printf("  %s extract-xml <image.wim> <output.xml>\n", Program);
    printf("  %s list-images <image.wim>\n", Program);
    printf("  %s list-tree <image.wim> <image-index>\n", Program);
    printf("  %s extract <image.wim> <image-index> <existing-directory>\n", Program);
    printf("  %s capture <source-directory> <output.wim> [image-name]\n", Program);
}

int main(int argc, char **argv)
{
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        PrintUsage(argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("wimage %s\n", WIMAGE_VERSION);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "inspect") == 0)
        return InspectImage(argv[2]);
    if (argc == 3 && strcmp(argv[1], "verify") == 0)
        return VerifyImage(argv[2]);
    if (argc == 4 && strcmp(argv[1], "extract-xml") == 0)
        return ExtractXml(argv[2], argv[3]);
    if (argc == 3 && strcmp(argv[1], "list-images") == 0)
        return ListImages(argv[2]);
    if (argc == 4 && strcmp(argv[1], "list-tree") == 0)
        return ListTree(argv[2], argv[3]);
    if (argc == 5 && strcmp(argv[1], "extract") == 0)
        return ExtractImage(argv[2], argv[3], argv[4]);
    if (argc == 4 && strcmp(argv[1], "capture") == 0)
        return CaptureImage(argv[2], argv[3], NULL);
    if (argc == 5 && strcmp(argv[1], "capture") == 0)
        return CaptureImage(argv[2], argv[3], argv[4]);
    PrintUsage(argv[0]);
    return 2;
}
