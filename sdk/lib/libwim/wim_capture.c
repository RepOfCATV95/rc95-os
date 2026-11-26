/*
 * PROJECT: RC95-OS libwim
 * LICENSE: GPL-3.0-or-later
 * PURPOSE: Capture a directory tree into an uncompressed, single-part WIM.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <libwim/wim_capture.h>
#include <libwim/wim_write.h>
#include <libwim/wim_format.h>
#include <libwim/sha1.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <aclapi.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#endif

#define CAPTURE_DEFAULT_LIMIT (UINT64_C(1024) * 1024 * 1024)
#define CAPTURE_MAX_DEPTH 256u
#define DENTRY_FIXED 102u

typedef struct CAP_BUFFER { uint8_t *p; size_t n, cap; } CAP_BUFFER;
typedef struct CAP_RESOURCE { uint8_t *data; size_t size; uint8_t sha1[20]; uint32_t refs; } CAP_RESOURCE;
typedef struct CAP_STREAM { char *name; uint8_t sha1[20]; uint64_t size; int has_data; } CAP_STREAM;
typedef struct CAP_SECURITY { uint8_t *data; uint32_t size, refs; } CAP_SECURITY;
#ifdef _WIN32
typedef struct CAP_HARDLINK { DWORD volume, index_high, index_low; uint64_t group; size_t first_node; } CAP_HARDLINK;
#endif
typedef struct CAP_NODE {
    char *name;
    uint32_t attrs;
    uint64_t ctime, atime, mtime, size;
    uint8_t sha1[20];
    uint32_t security_id;
    uint64_t hardlink_group;
    CAP_STREAM *streams; size_t stream_count, stream_cap;
    int has_data, directory;
    size_t *children, child_count, child_cap, dentry_offset;
    char *path;
    uint8_t *data;
} CAP_NODE;
typedef struct CAP_CONTEXT {
    CAP_NODE *nodes; size_t count, cap;
    CAP_RESOURCE *resources; size_t resource_count, resource_cap;
    CAP_SECURITY *security; size_t security_count, security_cap;
#ifdef _WIN32
    CAP_HARDLINK *hardlinks; size_t hardlink_count, hardlink_cap; uint64_t next_hardlink_group;
#endif
    uint64_t total_bytes, limit;
    WIM_CAPTURE_STATUS failure;
    size_t files, directories;
} CAP_CONTEXT;

static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){put16(p,(uint16_t)v);put16(p+2,(uint16_t)(v>>16));}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static int add_size(size_t a,size_t b,size_t *r){if(b>SIZE_MAX-a)return 0;*r=a+b;return 1;}
static int buf_reserve(CAP_BUFFER *b,size_t extra)
{
    size_t need,nc;uint8_t *p;
    if(!add_size(b->n,extra,&need))return 0;
    if(need<=b->cap)return 1;
    nc=b->cap?b->cap:256;
    while(nc<need){if(nc>SIZE_MAX/2){nc=need;break;}nc*=2;}
    p=(uint8_t*)realloc(b->p,nc);if(!p)return 0;b->p=p;b->cap=nc;return 1;
}
static int buf_append(CAP_BUFFER *b,const void *p,size_t n)
{if(!buf_reserve(b,n))return 0;if(n)memcpy(b->p+b->n,p,n);b->n+=n;return 1;}
static int buf_zero(CAP_BUFFER *b,size_t n)
{if(!buf_reserve(b,n))return 0;memset(b->p+b->n,0,n);b->n+=n;return 1;}
static char *dupstr(const char *s)
{size_t n=strlen(s)+1;char *p=(char*)malloc(n);if(p)memcpy(p,s,n);return p;}

static int utf8_next(const unsigned char **p,uint32_t *cp)
{
    const unsigned char *s=*p;uint32_t c;
    if(s[0]<0x80){c=s[0];s+=1;}
    else if((s[0]&0xE0)==0xC0){if(!s[1]||(s[1]&0xC0)!=0x80)return 0;c=((uint32_t)(s[0]&31)<<6)|(s[1]&63);s+=2;if(c<0x80)return 0;}
    else if((s[0]&0xF0)==0xE0){if(!s[1]||!s[2]||(s[1]&0xC0)!=0x80||(s[2]&0xC0)!=0x80)return 0;c=((uint32_t)(s[0]&15)<<12)|((uint32_t)(s[1]&63)<<6)|(s[2]&63);s+=3;if(c<0x800||(c>=0xD800&&c<=0xDFFF))return 0;}
    else if((s[0]&0xF8)==0xF0){if(!s[1]||!s[2]||!s[3]||(s[1]&0xC0)!=0x80||(s[2]&0xC0)!=0x80||(s[3]&0xC0)!=0x80)return 0;c=((uint32_t)(s[0]&7)<<18)|((uint32_t)(s[1]&63)<<12)|((uint32_t)(s[2]&63)<<6)|(s[3]&63);s+=4;if(c<0x10000||c>0x10FFFF)return 0;}
    else return 0;
    *p=s;*cp=c;return 1;
}
static int append_utf16(CAP_BUFFER *b,const char *name)
{
    const unsigned char *p=(const unsigned char*)name;uint32_t cp;
    while(*p){if(!utf8_next(&p,&cp))return 0;
        if(cp<0x10000){uint8_t x[2];put16(x,(uint16_t)cp);if(!buf_append(b,x,2))return 0;}
        else{uint8_t x[4];cp-=0x10000;put16(x,(uint16_t)(0xD800+(cp>>10)));put16(x+2,(uint16_t)(0xDC00+(cp&1023)));if(!buf_append(b,x,4))return 0;}
    }return 1;
}
static int xml_text(CAP_BUFFER *b,const char *s)
{
    const char *p=s;while(*p){const char *q=NULL;switch(*p){case '&':q="&amp;";break;case '<':q="&lt;";break;case '>':q="&gt;";break;case '"':q="&quot;";break;case '\'':q="&apos;";break;}
        if(q){if(!buf_append(b,q,strlen(q)))return 0;++p;}else{if(!buf_append(b,p,1))return 0;++p;}}
    return 1;
}
static void free_context(CAP_CONTEXT *c)
{
    size_t i;for(i=0;i<c->count;i++){free(c->nodes[i].name);free(c->nodes[i].path);free(c->nodes[i].data);free(c->nodes[i].children);{size_t j;for(j=0;j<c->nodes[i].stream_count;j++)free(c->nodes[i].streams[j].name);}free(c->nodes[i].streams);}
    for(i=0;i<c->resource_count;i++)free(c->resources[i].data);
    for(i=0;i<c->security_count;i++)free(c->security[i].data);
    free(c->security);
#ifdef _WIN32
    free(c->hardlinks);
#endif
    free(c->nodes);free(c->resources);memset(c,0,sizeof(*c));
}
static int add_node(CAP_CONTEXT *c,size_t parent,const char *name,const char *path,
                    int directory,uint32_t attrs,uint64_t ct,uint64_t at,uint64_t mt,
                    size_t *index)
{
    CAP_NODE *n;size_t i,newcap;CAP_NODE *v;
    if(c->count==c->cap){newcap=c->cap?c->cap*2:32;if(newcap<c->cap||newcap>SIZE_MAX/sizeof(*v))return 0;v=(CAP_NODE*)realloc(c->nodes,newcap*sizeof(*v));if(!v)return 0;c->nodes=v;c->cap=newcap;}
    i=c->count++;n=&c->nodes[i];memset(n,0,sizeof(*n));n->name=dupstr(name);n->path=dupstr(path);
    if(!n->name||!n->path){c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}n->directory=directory;n->attrs=attrs;n->security_id=UINT32_MAX;n->ctime=ct;n->atime=at;n->mtime=mt;
    if(directory)++c->directories;else ++c->files;
    if(parent!=(size_t)-1){CAP_NODE *p=&c->nodes[parent];if(p->child_count==p->child_cap){newcap=p->child_cap?p->child_cap*2:8;if(newcap<p->child_cap||newcap>SIZE_MAX/sizeof(size_t))return 0;v=NULL;p->children=(size_t*)realloc(p->children,newcap*sizeof(size_t));if(!p->children)return 0;p->child_cap=newcap;}p->children[p->child_count++]=i;}
    *index=i;return 1;
}
static int add_resource(CAP_CONTEXT *c,CAP_NODE *n,uint8_t *data,size_t size)
{
    uint8_t hash[20];size_t i,newcap;CAP_RESOURCE *v;
    if(!size){free(data);return 1;}
    WimSha1(data,size,hash);
    for(i=0;i<c->resource_count;i++)if(c->resources[i].size==size&&!memcmp(c->resources[i].sha1,hash,20)&&!memcmp(c->resources[i].data,data,size)){
        if(c->resources[i].refs==UINT32_MAX){free(data);return 0;}++c->resources[i].refs;memcpy(n->sha1,hash,20);n->has_data=1;n->size=size;free(data);return 1;}
    if(c->resource_count==c->resource_cap){newcap=c->resource_cap?c->resource_cap*2:32;if(newcap<c->resource_cap||newcap>SIZE_MAX/sizeof(*v)){free(data);return 0;}v=(CAP_RESOURCE*)realloc(c->resources,newcap*sizeof(*v));if(!v){free(data);return 0;}c->resources=v;c->resource_cap=newcap;}
    v=&c->resources[c->resource_count++];v->data=data;v->size=size;memcpy(v->sha1,hash,20);v->refs=1;memcpy(n->sha1,hash,20);n->has_data=1;n->size=size;return 1;
}
#ifdef _WIN32
static int capture_hardlink(CAP_CONTEXT *c,CAP_NODE *n)
{
    HANDLE h;BY_HANDLE_FILE_INFORMATION info;size_t i,newcap;CAP_HARDLINK *v;
    h=CreateFileA(n->path,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,
                  OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(h==INVALID_HANDLE_VALUE){c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    if(!GetFileInformationByHandle(h,&info)){CloseHandle(h);c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    CloseHandle(h);
    if(info.nNumberOfLinks<2)return 1;
    for(i=0;i<c->hardlink_count;i++)if(c->hardlinks[i].volume==info.dwVolumeSerialNumber&&
       c->hardlinks[i].index_high==info.nFileIndexHigh&&c->hardlinks[i].index_low==info.nFileIndexLow){
        c->nodes[c->hardlinks[i].first_node].hardlink_group=c->hardlinks[i].group;
        n->hardlink_group=c->hardlinks[i].group;return 1;
    }
    if(c->next_hardlink_group==UINT64_MAX){c->failure=WIM_CAPTURE_LIMIT_EXCEEDED;return 0;}
    if(c->hardlink_count==c->hardlink_cap){
        newcap=c->hardlink_cap?c->hardlink_cap*2:16;
        if(newcap<c->hardlink_cap||newcap>SIZE_MAX/sizeof(*v)){c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        v=(CAP_HARDLINK*)realloc(c->hardlinks,newcap*sizeof(*v));
        if(!v){c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        c->hardlinks=v;c->hardlink_cap=newcap;
    }
    c->hardlinks[c->hardlink_count].group=++c->next_hardlink_group;
    c->hardlinks[c->hardlink_count].first_node=(size_t)(n-c->nodes);
    c->hardlinks[c->hardlink_count].volume=info.dwVolumeSerialNumber;
    c->hardlinks[c->hardlink_count].index_high=info.nFileIndexHigh;
    c->hardlinks[c->hardlink_count].index_low=info.nFileIndexLow;
    ++c->hardlink_count;return 1;
}
static int capture_security(CAP_CONTEXT *c,CAP_NODE *n)
{
    DWORD needed=0,flags=OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR sd;DWORD length;size_t i,newcap;CAP_SECURITY *v;
    GetFileSecurityA(n->path,flags,NULL,0,&needed);
    {
        DWORD error=GetLastError();
        if(error!=ERROR_INSUFFICIENT_BUFFER||!needed){
        /* FAT and other filesystems without Windows security descriptors. */
        if(error==ERROR_INVALID_FUNCTION||error==ERROR_NOT_SUPPORTED||
           error==ERROR_CALL_NOT_IMPLEMENTED)return 1;
        c->failure=WIM_CAPTURE_IO_ERROR;return 0;
        }
    }
    sd=(PSECURITY_DESCRIPTOR)malloc(needed);if(!sd){c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
    if(!GetFileSecurityA(n->path,flags,sd,needed,&needed)){free(sd);c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    length=GetSecurityDescriptorLength(sd);
    for(i=0;i<c->security_count;i++)if(c->security[i].size==length&&!memcmp(c->security[i].data,sd,length)){
        if(c->security[i].refs==UINT32_MAX){free(sd);c->failure=WIM_CAPTURE_LIMIT_EXCEEDED;return 0;}
        ++c->security[i].refs;n->security_id=(uint32_t)i;free(sd);return 1;
    }
    if(c->security_count>=UINT32_C(0x80000000)){free(sd);c->failure=WIM_CAPTURE_LIMIT_EXCEEDED;return 0;}
    if(c->security_count==c->security_cap){
        newcap=c->security_cap?c->security_cap*2:16;
        if(newcap<c->security_cap||newcap>SIZE_MAX/sizeof(*v)){free(sd);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        v=(CAP_SECURITY*)realloc(c->security,newcap*sizeof(*v));
        if(!v){free(sd);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        c->security=v;c->security_cap=newcap;
    }
    c->security[c->security_count].data=(uint8_t*)sd;
    c->security[c->security_count].size=length;
    c->security[c->security_count].refs=1;
    n->security_id=(uint32_t)c->security_count++;
    return 1;
}
#endif

#ifdef _WIN32
static int add_named_stream(CAP_CONTEXT *c,CAP_NODE *n,const char *name,
                            uint8_t *data,size_t size)
{
    CAP_STREAM *s;size_t newcap;CAP_NODE temp;
    if(n->stream_count==n->stream_cap){
        newcap=n->stream_cap?n->stream_cap*2:4;
        if(newcap<n->stream_cap||newcap>SIZE_MAX/sizeof(*s)){free(data);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        s=(CAP_STREAM*)realloc(n->streams,newcap*sizeof(*s));
        if(!s){free(data);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        n->streams=s;n->stream_cap=newcap;
    }
    s=&n->streams[n->stream_count];memset(s,0,sizeof(*s));s->name=dupstr(name);
    if(!s->name){free(data);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
    memset(&temp,0,sizeof(temp));
    if(!add_resource(c,&temp,data,size)){free(s->name);s->name=NULL;c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
    memcpy(s->sha1,temp.sha1,20);s->size=temp.size;s->has_data=temp.has_data;
    ++n->stream_count;return 1;
}
static wchar_t *capture_wide_path(const char *path)
{
    int n=MultiByteToWideChar(CP_ACP,0,path,-1,NULL,0);wchar_t *w;
    if(!n)return NULL;w=(wchar_t*)malloc((size_t)n*sizeof(wchar_t));if(!w)return NULL;
    if(!MultiByteToWideChar(CP_ACP,0,path,-1,w,n)){free(w);return NULL;}return w;
}
static int capture_ads(CAP_CONTEXT *c,CAP_NODE *n)
{
    WIN32_FIND_STREAM_DATA fd;HANDLE find;wchar_t *wide=capture_wide_path(n->path);
    if(!wide){c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
    find=FindFirstStreamW(wide,FindStreamInfoStandard,&fd,0);
    if(find==INVALID_HANDLE_VALUE){DWORD e=GetLastError();free(wide);if(e==ERROR_HANDLE_EOF||e==ERROR_INVALID_PARAMETER)return 1;c->failure=WIM_CAPTURE_UNSUPPORTED;return 0;c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    do{
        char *streamName;int bytes;wchar_t *full;size_t plen,wlen;HANDLE file;LARGE_INTEGER size;uint8_t *data;DWORD got;size_t used=0;
        if(!wcscmp(fd.cStreamName,L"::$DATA"))continue;
        bytes=WideCharToMultiByte(CP_UTF8,0,fd.cStreamName,-1,NULL,0,NULL,NULL);
        if(!bytes){FindClose(find);free(wide);c->failure=WIM_CAPTURE_UNSUPPORTED;return 0;}
        streamName=(char*)malloc((size_t)bytes);if(!streamName){FindClose(find);free(wide);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        if(!WideCharToMultiByte(CP_UTF8,0,fd.cStreamName,-1,streamName,bytes,NULL,NULL)){free(streamName);FindClose(find);free(wide);c->failure=WIM_CAPTURE_UNSUPPORTED;return 0;}
        plen=wcslen(wide);wlen=wcslen(fd.cStreamName);
        if(plen>SIZE_MAX/sizeof(wchar_t)-wlen-1){free(streamName);FindClose(find);free(wide);c->failure=WIM_CAPTURE_LIMIT_EXCEEDED;return 0;}
        full=(wchar_t*)malloc((plen+wlen+1)*sizeof(wchar_t));
        if(!full){free(streamName);FindClose(find);free(wide);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        memcpy(full,wide,plen*sizeof(wchar_t));memcpy(full+plen,fd.cStreamName,(wlen+1)*sizeof(wchar_t));
        file=CreateFileW(full,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_BACKUP_SEMANTICS,NULL);
        free(full);
        if(file==INVALID_HANDLE_VALUE){free(streamName);FindClose(find);free(wide);c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
        if(!GetFileSizeEx(file,&size)||size.QuadPart<0||(uint64_t)size.QuadPart>c->limit-c->total_bytes||
           (uint64_t)size.QuadPart>(uint64_t)SIZE_MAX){CloseHandle(file);free(streamName);FindClose(find);free(wide);c->failure=WIM_CAPTURE_LIMIT_EXCEEDED;return 0;}
        data=(uint8_t*)malloc(size.QuadPart?(size_t)size.QuadPart:1);
        if(!data){CloseHandle(file);free(streamName);FindClose(find);free(wide);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
        while(used<(size_t)size.QuadPart){DWORD chunk=(DWORD)(((size_t)size.QuadPart-used)>0x7ffff000u?0x7ffff000u:(size_t)size.QuadPart-used);
            if(!ReadFile(file,data+used,chunk,&got,NULL)||!got){free(data);CloseHandle(file);free(streamName);FindClose(find);free(wide);c->failure=WIM_CAPTURE_IO_ERROR;return 0;}used+=got;}
        CloseHandle(file);c->total_bytes+=(uint64_t)size.QuadPart;
        if(!add_named_stream(c,n,streamName,data,used)){free(streamName);FindClose(find);free(wide);return 0;}
        free(streamName);
    }while(FindNextStreamW(find,&fd));
    {DWORD e=GetLastError();FindClose(find);free(wide);if(e!=ERROR_HANDLE_EOF){c->failure=WIM_CAPTURE_IO_ERROR;return 0;}}
    return 1;
}
#endif

static int read_file(CAP_CONTEXT *c,CAP_NODE *n)
{
    FILE *f;long end;uint8_t *p;size_t got;
    f=fopen(n->path,"rb");if(!f){c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    if(fseek(f,0,SEEK_END)!=0||(end=ftell(f))<0||fseek(f,0,SEEK_SET)!=0){fclose(f);c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    /* The configured capture limit also keeps the stdio long-based size path bounded. */
    if((uint64_t)end>c->limit-c->total_bytes||(uint64_t)end>(uint64_t)SIZE_MAX){fclose(f);c->failure=WIM_CAPTURE_LIMIT_EXCEEDED;return 0;}
    p=(uint8_t*)malloc(end?(size_t)end:1);if(!p){fclose(f);c->failure=WIM_CAPTURE_OUT_OF_MEMORY;return 0;}
    got=end?fread(p,1,(size_t)end,f):0;
    if(got!=(size_t)end||ferror(f)){fclose(f);free(p);c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    if(fclose(f)!=0){free(p);c->failure=WIM_CAPTURE_IO_ERROR;return 0;}
    c->total_bytes+=(uint64_t)end;return add_resource(c,n,p,(size_t)end);
}
static int join_path(char *out,size_t cap,const char *base,const char *name)
{
    size_t a=strlen(base),b=strlen(name);
    size_t sep=(a&&base[a-1]!='/'&&base[a-1]!='\\')?1:0;
    if(a>=cap||sep>=cap-a||b>cap-a-sep-1)return 0;
    memcpy(out,base,a);
    if(sep)out[a++]=
#ifdef _WIN32
'\\';
#else
'/';
#endif
    memcpy(out+a,name,b+1);
    return 1;
}
#ifdef _WIN32
static char *ansi_to_utf8(const char *text)
{
    int wideCount,utf8Count;wchar_t *wide;char *utf8;
    wideCount=MultiByteToWideChar(CP_ACP,0,text,-1,NULL,0);
    if(!wideCount)return NULL;
    wide=(wchar_t*)malloc((size_t)wideCount*sizeof(wchar_t));if(!wide)return NULL;
    if(!MultiByteToWideChar(CP_ACP,0,text,-1,wide,wideCount)){free(wide);return NULL;}
    utf8Count=WideCharToMultiByte(CP_UTF8,0,wide,-1,NULL,0,NULL,NULL);
    if(!utf8Count){free(wide);return NULL;}
    utf8=(char*)malloc((size_t)utf8Count);if(!utf8){free(wide);return NULL;}
    if(!WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,utf8Count,NULL,NULL)){free(wide);free(utf8);return NULL;}
    free(wide);return utf8;
}
static uint64_t filetime_value(FILETIME t){return ((uint64_t)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static int enumerate_dir(CAP_CONTEXT *c,size_t parent,const char *path,unsigned depth)
{
    char pattern[4096],child[4096];WIN32_FIND_DATAA fd;HANDLE h;BOOL more;
    if(depth>CAPTURE_MAX_DEPTH||!join_path(pattern,sizeof(pattern),path,"*"))return 0;
    h=FindFirstFileA(pattern,&fd);if(h==INVALID_HANDLE_VALUE)return 0;
    do{
        size_t idx;int dir;uint32_t attrs;uint64_t ct,at,mt;char *utf8Name;
        if(!strcmp(fd.cFileName,".")||!strcmp(fd.cFileName,".."))continue;
        if(fd.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))continue;
        dir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        if(!join_path(child,sizeof(child),path,fd.cFileName)){FindClose(h);return 0;}
        attrs=fd.dwFileAttributes;
        ct=filetime_value(fd.ftCreationTime);at=filetime_value(fd.ftLastAccessTime);mt=filetime_value(fd.ftLastWriteTime);
        utf8Name=ansi_to_utf8(fd.cFileName);if(!utf8Name){FindClose(h);return 0;}
        if(!add_node(c,parent,utf8Name,child,dir,attrs,ct,at,mt,&idx)){free(utf8Name);FindClose(h);return 0;}
        free(utf8Name);
        if(!capture_security(c,&c->nodes[idx])||(!dir&&!capture_hardlink(c,&c->nodes[idx]))){FindClose(h);return 0;}
        if(!capture_ads(c,&c->nodes[idx])){FindClose(h);return 0;}
        if(dir){if(!enumerate_dir(c,idx,child,depth+1)){FindClose(h);return 0;}}
        else if(!read_file(c,&c->nodes[idx])){FindClose(h);return 0;}
    }while((more=FindNextFileA(h,&fd))!=0);
    {DWORD e=GetLastError();FindClose(h);return e==ERROR_NO_MORE_FILES;}
}
#else
static uint64_t unix_filetime(time_t sec,long nsec){return ((uint64_t)(sec+INT64_C(11644473600))*10000000u)+(uint64_t)(nsec/100);}
static int enumerate_dir(CAP_CONTEXT *c,size_t parent,const char *path,unsigned depth)
{
    DIR *d;struct dirent *de;char child[4096];struct stat st;
    if(depth>CAPTURE_MAX_DEPTH)return 0;d=opendir(path);if(!d)return 0;
    while((de=readdir(d))!=NULL){size_t idx;int dir;uint32_t attrs;uint64_t ct,at,mt;
        if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;
        if(!join_path(child,sizeof(child),path,de->d_name)||lstat(child,&st)!=0) {closedir(d);return 0;}
        if(S_ISLNK(st.st_mode))continue;if(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode))continue;
        dir=S_ISDIR(st.st_mode);attrs=dir?0x10u:0x80u;
#ifdef S_ISREG
        if(!dir&&st.st_size>0&&st.st_blocks>=0&&
           (uint64_t)st.st_blocks<((uint64_t)st.st_size+511u)/512u)
            attrs|=0x200u; /* FILE_ATTRIBUTE_SPARSE_FILE */
#endif
#if defined(__APPLE__)
        ct=unix_filetime(st.st_ctimespec.tv_sec,st.st_ctimespec.tv_nsec);at=unix_filetime(st.st_atimespec.tv_sec,st.st_atimespec.tv_nsec);mt=unix_filetime(st.st_mtimespec.tv_sec,st.st_mtimespec.tv_nsec);
#else
        ct=unix_filetime(st.st_ctim.tv_sec,st.st_ctim.tv_nsec);at=unix_filetime(st.st_atim.tv_sec,st.st_atim.tv_nsec);mt=unix_filetime(st.st_mtim.tv_sec,st.st_mtim.tv_nsec);
#endif
        if(!add_node(c,parent,de->d_name,child,dir,attrs,ct,at,mt,&idx)){closedir(d);return 0;}
        if(dir){if(!enumerate_dir(c,idx,child,depth+1)){closedir(d);return 0;}}
        else if(!read_file(c,&c->nodes[idx])){closedir(d);return 0;}
    }closedir(d);return 1;
}
#endif

static int compare_names(const char *a,const char *b)
{
    while(*a&&*b) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A'&&x<='Z')x=(unsigned char)(x-'A'+'a');
        if(y>='A'&&y<='Z')y=(unsigned char)(y-'A'+'a');
        if(x!=y)return x<y?-1:1;
    }
    return *a?1:(*b?-1:0);
}
static void sort_tree(CAP_CONTEXT *c,size_t index)
{
    CAP_NODE *n=&c->nodes[index];size_t i,j,key;
    for(i=1;i<n->child_count;i++) {
        key=n->children[i];j=i;
        while(j&&compare_names(c->nodes[n->children[j-1]].name,c->nodes[key].name)>0) {
            n->children[j]=n->children[j-1];--j;
        }
        n->children[j]=key;
    }
    for(i=0;i<n->child_count;i++)
        if(c->nodes[n->children[i]].directory)sort_tree(c,n->children[i]);
}

static int append_dentry(CAP_BUFFER *m,CAP_NODE *n)
{
    CAP_BUFFER name={0};size_t total,pad;uint8_t fixed[DENTRY_FIXED];int ok=0;
    if(!append_utf16(&name,n->name)||name.n>UINT16_MAX)goto done;
    n->dentry_offset=m->n;memset(fixed,0,sizeof(fixed));
    put32(fixed+8,n->attrs);put32(fixed+12,n->security_id);
    put64(fixed+40,n->ctime);put64(fixed+48,n->atime);put64(fixed+56,n->mtime);
    if(n->has_data)memcpy(fixed+64,n->sha1,20);
    put64(fixed+88,n->hardlink_group);
    if(n->stream_count>UINT16_MAX)goto done;put16(fixed+96,(uint16_t)n->stream_count);put16(fixed+98,0);put16(fixed+100,(uint16_t)name.n);
    total=DENTRY_FIXED+name.n+2;pad=(8-(total&7))&7;put64(fixed,(uint64_t)(total+pad));
    if(!buf_append(m,fixed,sizeof(fixed))||!buf_append(m,name.p,name.n)||!buf_zero(m,2+pad))goto done;
    {
        size_t i;
        for(i=0;i<n->stream_count;i++){
            CAP_STREAM *s=&n->streams[i];CAP_BUFFER sn={0};uint8_t extra[38];size_t length,spad;
            if(!append_utf16(&sn,s->name)||sn.n==0||sn.n>UINT16_MAX){free(sn.p);goto done;}
            length=38+sn.n;spad=(8-(length&7))&7;memset(extra,0,sizeof(extra));
            put64(extra,(uint64_t)(length+spad));memcpy(extra+8,s->sha1,20);put64(extra+28,s->size);put16(extra+36,(uint16_t)sn.n);
            if(!buf_append(m,extra,sizeof(extra))||!buf_append(m,sn.p,sn.n)||!buf_zero(m,spad)){free(sn.p);goto done;}
            free(sn.p);
        }
    }
    ok=1;
done:free(name.p);return ok;
}
static int serialize_children(CAP_CONTEXT *c,CAP_BUFFER *m,size_t dir,unsigned depth)
{
    CAP_NODE *n=&c->nodes[dir];size_t i;
    if(depth>CAPTURE_MAX_DEPTH)return 0;
    if(!n->child_count)return 1;
    for(i=0;i<n->child_count;i++)
        if(!append_dentry(m,&c->nodes[n->children[i]]))return 0;
    if(!buf_zero(m,8))return 0;
    for(i=0;i<n->child_count;i++) {
        CAP_NODE *child=&c->nodes[n->children[i]];
        if(child->directory) {
            if(child->child_count) {
                put64(m->p+child->dentry_offset+16,(uint64_t)m->n);
                if(!serialize_children(c,m,n->children[i],depth+1))return 0;
            }
        }
    }
    return 1;
}
static int build_metadata(CAP_CONTEXT *c,CAP_BUFFER *m)
{
    uint8_t security[8]={0};size_t root=0,i;CAP_BUFFER sec={0};
    put32(security+4,(uint32_t)c->security_count);
    if(!buf_append(&sec,security,sizeof(security)))return 0;
    /* WIM security data stores a 64-bit size array followed by the raw
       self-relative descriptors; it does not store per-entry refcounts. */
    for(i=0;i<c->security_count;i++){
        uint8_t sizeBytes[8];put64(sizeBytes,c->security[i].size);
        if(!buf_append(&sec,sizeBytes,sizeof(sizeBytes))){free(sec.p);return 0;}
    }
    for(i=0;i<c->security_count;i++)
        if(!buf_append(&sec,c->security[i].data,c->security[i].size)){free(sec.p);return 0;}
    if(sec.n>SIZE_MAX-7){free(sec.p);return 0;}
    if(!buf_zero(&sec,(8-(sec.n&7))&7)){free(sec.p);return 0;}
    if(sec.n>UINT32_MAX){free(sec.p);return 0;}
    put32(sec.p,(uint32_t)sec.n);
    if(!buf_append(m,sec.p,sec.n)){free(sec.p);return 0;}free(sec.p);
    if(!append_dentry(m,&c->nodes[root]))return 0;
    if(c->nodes[root].child_count) {
        put64(m->p+c->nodes[root].dentry_offset+16,(uint64_t)m->n);
        if(!serialize_children(c,m,root,0))return 0;
    }
    return 1;
}
static int build_xml(CAP_BUFFER *xml,const char *name,const char *description,
                     const CAP_CONTEXT *c)
{
    char number[64];
#define APP(s) do{if(!buf_append(xml,(s),sizeof(s)-1))return 0;}while(0)
    APP("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    APP("<WIM><IMAGE INDEX=\"1\"><NAME>");
    if(!xml_text(xml,name))return 0;
    APP("</NAME><DESCRIPTION>");
    if(!xml_text(xml,description?description:""))return 0;
    APP("</DESCRIPTION><DIRCOUNT>");
    snprintf(number,sizeof(number),"%lu",(unsigned long)c->directories);
    if(!buf_append(xml,number,strlen(number)))return 0;
    APP("</DIRCOUNT><FILECOUNT>");
    snprintf(number,sizeof(number),"%lu",(unsigned long)c->files);
    if(!buf_append(xml,number,strlen(number)))return 0;
    APP("</FILECOUNT><TOTALBYTES>");
    snprintf(number,sizeof(number),"%llu",(unsigned long long)c->total_bytes);
    if(!buf_append(xml,number,strlen(number)))return 0;
    APP("</TOTALBYTES></IMAGE></WIM>");
#undef APP
    return 1;
}
WIM_CAPTURE_STATUS WimCaptureDirectory(const char *source,const char *output,
                                       const WIM_CAPTURE_OPTIONS *options)
{
    CAP_CONTEXT c;CAP_BUFFER metadata={0},xml={0};WIM_WRITE_BLOB *blobs=NULL;
    WIM_WRITER_OPTIONS writer;WIM_IO *io=NULL;WIM_WRITER_STATUS ws;
    const char *imageName="Captured Image",*description="";
    uint64_t limit=CAPTURE_DEFAULT_LIMIT;size_t i,blobCount;int result=WIM_CAPTURE_FAILED;
    if(!source||!source[0]||!output||!output[0])return WIM_CAPTURE_INVALID_ARGUMENT;
    if(options) {
        if(options->StructureSize<sizeof(*options))return WIM_CAPTURE_INVALID_ARGUMENT;
        if(options->ImageName&&options->ImageName[0])imageName=options->ImageName;
        if(options->Description)description=options->Description;
        if(options->MaximumTotalBytes)limit=options->MaximumTotalBytes;
    }
    memset(&c,0,sizeof(c));c.limit=limit;c.failure=WIM_CAPTURE_FAILED;
    {
        size_t root;
#ifdef _WIN32
        DWORD a=GetFileAttributesA(source);
        if(a==INVALID_FILE_ATTRIBUTES||!(a&FILE_ATTRIBUTE_DIRECTORY)||(a&FILE_ATTRIBUTE_REPARSE_POINT))goto done;
#else
        struct stat st;if(lstat(source,&st)!=0||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))goto done;
#endif
        if(!add_node(&c,(size_t)-1,"",source,1,
#ifdef _WIN32
            FILE_ATTRIBUTE_DIRECTORY,
#else
            0x10u,
#endif
            0,0,0,&root))goto done;
#ifdef _WIN32
        if(!capture_security(&c,&c.nodes[root]))goto done;
#endif
        if(!enumerate_dir(&c,root,source,0))goto done;
        sort_tree(&c,root);
    }
    if(!build_metadata(&c,&metadata)||!build_xml(&xml,imageName,description,&c))goto done;
    if(c.resource_count>SIZE_MAX-1)goto done;
    blobCount=c.resource_count+1;
    if(blobCount>SIZE_MAX/sizeof(*blobs))goto done;
    blobs=(WIM_WRITE_BLOB*)calloc(blobCount,sizeof(*blobs));if(!blobs)goto done;
    blobs[0].Data=metadata.p;blobs[0].Size=metadata.n;blobs[0].ReferenceCount=1;blobs[0].Flags=WIM_RESOURCE_FLAG_METADATA;
    for(i=0;i<c.resource_count;i++){blobs[i+1].Data=c.resources[i].data;blobs[i+1].Size=c.resources[i].size;blobs[i+1].ReferenceCount=c.resources[i].refs;blobs[i+1].Flags=0;}
    memset(&writer,0,sizeof(writer));writer.StructureSize=sizeof(writer);writer.ImageCount=1;
    writer.Flags=WIM_HEADER_FLAG_COMPRESS_XPRESS;writer.ChunkSize=32768;
    writer.Blobs=blobs;writer.BlobCount=blobCount;writer.XmlData=xml.p;writer.XmlSize=xml.n;
    if(WimIoOpenFile(&io,output,WIM_IO_FILE_CREATE)!=WIM_IO_OK)goto done;
    ws=WimWriterWrite(io,&writer);
    if(ws!=WIM_WRITER_OK)goto done;
    result=WIM_CAPTURE_OK;
done:
    if(result!=WIM_CAPTURE_OK&&c.failure!=WIM_CAPTURE_FAILED)result=(int)c.failure;
    if(io)WimIoClose(io);
    free(blobs);free(metadata.p);free(xml.p);free_context(&c);
    return (WIM_CAPTURE_STATUS)result;
}
