/*
 * PROJECT: RC95-OS libwim tests
 * PURPOSE: Exercise filesystem capture, metadata parsing and resource deduplication.
 */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libwim/wim_capture.h>
#include <libwim/wim_read.h>
#include <libwim/wim_image.h>

static void *TestAllocate(void *Context,size_t Size){(void)Context;return malloc(Size);}
static void TestFree(void *Context,void *Allocation){(void)Context;free(Allocation);}
#define CHECK(x) do { if(!(x)){fprintf(stderr,"libwim capture test failed at line %d: %s\n",__LINE__,#x);goto Fail;} } while(0)

static int WriteText(const char *Path,const char *Text)
{
    FILE *f=fopen(Path,"wb");size_t n=strlen(Text);int ok;
    if(!f)return 0;
    ok=fwrite(Text,1,n,f)==n;
    if(fclose(f)!=0)ok=0;
    return ok;
}
int main(void)
{
    char temp[MAX_PATH]={0},source[MAX_PATH]={0},nested[MAX_PATH]={0};
    char file1[MAX_PATH]={0},file2[MAX_PATH]={0},file3[MAX_PATH]={0},file4[MAX_PATH]={0},file5[MAX_PATH]={0},ads[MAX_PATH]={0},image[MAX_PATH]={0};
    FILE *f=NULL;long length;uint8_t *bytes=NULL;WIM_READER *reader=NULL;
    WIM_READER_OPTIONS ro;WIM_CAPTURE_OPTIONS co;WIM_DIRECTORY_NODE *root=NULL,*file=NULL,*hardlink=NULL,*sparse=NULL;
    WIM_IMAGE_INFO info;WIM_HEADER_INFO header;WIM_READER_STATUS rs;size_t used=0,lookupCount=0,i;int compressed=0;
    uint8_t decoded[32],*sparseDecoded=NULL;int result=1;char fsName[32]={0};

    CHECK(GetTempPathA(MAX_PATH,temp)!=0);
    CHECK(GetVolumeInformationA(temp,NULL,0,NULL,NULL,0,fsName,sizeof(fsName)));
    if(_stricmp(fsName,"NTFS")!=0){puts("libwim capture NTFS metadata test skipped");result=0;goto Fail;}
    CHECK(GetTempFileNameA(temp,"wcap",0,source)!=0);
    CHECK(DeleteFileA(source));CHECK(CreateDirectoryA(source,NULL));
    CHECK(GetTempFileNameA(temp,"wim",0,image)!=0);
    CHECK(snprintf(nested,sizeof(nested),"%s\\nested",source)>0);
    CHECK(snprintf(file1,sizeof(file1),"%s\\a.txt",source)>0);
    CHECK(snprintf(file2,sizeof(file2),"%s\\b.txt",nested)>0);
    CHECK(snprintf(file3,sizeof(file3),"%s\\empty.txt",source)>0);
    CHECK(snprintf(file4,sizeof(file4),"%s\\hardlink.txt",source)>0);
    CHECK(snprintf(file5,sizeof(file5),"%s\\sparse.bin",source)>0);
    CHECK(snprintf(ads,sizeof(ads),"%s:rc95.capture:$DATA",file1)>0);
    CHECK(CreateDirectoryA(nested,NULL));
    CHECK(WriteText(file1,"capture-data"));
    CHECK(WriteText(file2,"capture-data"));
    CHECK(WriteText(file3,""));
    CHECK(CreateHardLinkA(file4,file1,NULL));
    CHECK(WriteText(ads,"alternate-stream"));
    {
        HANDLE sparseFile=CreateFileA(file5,GENERIC_WRITE,0,NULL,CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL,NULL);
        DWORD returned=0;LARGE_INTEGER end;
        CHECK(sparseFile!=INVALID_HANDLE_VALUE);
        CHECK(DeviceIoControl(sparseFile,FSCTL_SET_SPARSE,NULL,0,NULL,0,&returned,NULL));
        end.QuadPart=1024*1024;
        CHECK(SetFilePointerEx(sparseFile,end,NULL,FILE_BEGIN));
        CHECK(SetEndOfFile(sparseFile));
        CHECK(CloseHandle(sparseFile));
    }

    memset(&co,0,sizeof(co));co.StructureSize=sizeof(co);co.ImageName="Capture Test";
    CHECK(WimCaptureDirectory(source,image,&co)==WIM_CAPTURE_OK);
    f=fopen(image,"rb");CHECK(f!=NULL);CHECK(fseek(f,0,SEEK_END)==0);
    length=ftell(f);CHECK(length>0);CHECK(fseek(f,0,SEEK_SET)==0);
    bytes=(uint8_t*)malloc((size_t)length);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1,(size_t)length,f)==(size_t)length);
    {int closeStatus=fclose(f);f=NULL;CHECK(closeStatus==0);}

    memset(&ro,0,sizeof(ro));ro.StructureSize=sizeof(ro);
    ro.Allocator.Allocate=TestAllocate;ro.Allocator.Free=TestFree;
    rs=WimReaderOpenMemory(&reader,bytes,(size_t)length,&ro);CHECK(rs==WIM_READER_OK);
    CHECK(WimReaderGetHeaderInfo(reader,&header)==WIM_READER_OK);
    CHECK((header.Flags&WIM_HEADER_FLAG_COMPRESS_XPRESS)!=0&&header.ChunkSize==32768);
    CHECK(WimReaderGetImageCount(reader,&i)==WIM_READER_OK&&i==1);
    CHECK(WimReaderGetImageInfo(reader,0,&info)==WIM_READER_OK);
    CHECK(info.Index==1&&info.Name&&strcmp(info.Name,"Capture Test")==0);
    CHECK(info.FileCount==5&&info.DirectoryCount==2);
    WimImageInfoFree(reader,&info);
    CHECK(WimReaderGetLookupEntryCount(reader,&lookupCount)==WIM_READER_OK);
    CHECK(lookupCount==4); /* metadata, main data, named stream, and sparse logical data */
    for(i=0;i<lookupCount;i++){WIM_LOOKUP_ENTRY entry;CHECK(WimReaderGetLookupEntry(reader,i,&entry)==WIM_READER_OK);if(entry.Resource.Flags&WIM_RESOURCE_FLAG_COMPRESSED)compressed=1;}
    CHECK(compressed);
    CHECK(WimReaderLoadImageDirectoryTree(reader,0,&root)==WIM_READER_OK);
    CHECK(root->ChildCount==5);
    for(i=0;i<root->ChildCount;i++) {
        if(root->Children[i]->Name&&strcmp(root->Children[i]->Name,"a.txt")==0)file=root->Children[i];
        if(root->Children[i]->Name&&strcmp(root->Children[i]->Name,"hardlink.txt")==0)hardlink=root->Children[i];
        if(root->Children[i]->Name&&strcmp(root->Children[i]->Name,"sparse.bin")==0)sparse=root->Children[i];
    }
    CHECK(file!=NULL&&file->HasMainStream&&file->MainStreamSize==12);
    CHECK(file->SecurityId!=UINT32_MAX);
    CHECK(hardlink!=NULL&&file->HardLinkGroupId!=0&&file->HardLinkGroupId==hardlink->HardLinkGroupId);
    CHECK(file->NamedStreamCount==1&&file->NamedStreams[0].Size==16);
    CHECK(sparse!=NULL&&(sparse->Attributes&FILE_ATTRIBUTE_SPARSE_FILE)!=0&&sparse->MainStreamSize==1024*1024);
    sparseDecoded=(uint8_t*)malloc((size_t)sparse->MainStreamSize);CHECK(sparseDecoded!=NULL);
    CHECK(WimImageReadMainStream(reader,sparse,sparseDecoded,(size_t)sparse->MainStreamSize,&used)==WIM_READER_OK);
    CHECK(used==(size_t)sparse->MainStreamSize);
    for(i=0;i<used;i++)CHECK(sparseDecoded[i]==0);
    free(sparseDecoded);sparseDecoded=NULL;
    CHECK(WimImageReadMainStream(reader,file,decoded,sizeof(decoded),&used)==WIM_READER_OK);
    CHECK(used==12&&memcmp(decoded,"capture-data",12)==0);
    CHECK(WimImageReadNamedStream(reader,file,0,decoded,sizeof(decoded),&used)==WIM_READER_OK);
    CHECK(used==16&&memcmp(decoded,"alternate-stream",16)==0);
    result=0;
Fail:
    if(root&&reader)WimImageDirectoryTreeFree(reader,root);
    if(reader)WimReaderClose(reader);
    if(f)fclose(f);
    free(bytes);free(sparseDecoded);
    if(ads[0])DeleteFileA(ads);
    if(file5[0])DeleteFileA(file5);
    if(file4[0])DeleteFileA(file4);
    if(file1[0])DeleteFileA(file1);
    if(file2[0])DeleteFileA(file2);
    if(file3[0])DeleteFileA(file3);
    if(nested[0])RemoveDirectoryA(nested);
    if(source[0])RemoveDirectoryA(source);
    if(image[0])DeleteFileA(image);
    return result;
}
