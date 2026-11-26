#include <string.h>
#include <stdlib.h>
#include <libwim/wim_image.h>

#define WIM_DENTRY_FIXED_SIZE 102u
#define WIM_EXTRA_STREAM_FIXED_SIZE 38u
#define WIM_FILE_ATTRIBUTE_DIRECTORY 0x10u
#define WIM_MAX_TREE_DEPTH 256u
#define WIM_MAX_XML_SIZE (64u * 1024u * 1024u)

typedef struct _WIM_IMAGE_RECORD {
    WIM_IMAGE_INFO Info;
    size_t LookupIndex;
} WIM_IMAGE_RECORD;

struct _WIM_IMAGE_CATALOGUE {
    WIM_IMAGE_RECORD *Images;
    size_t Count;
};

static uint16_t R16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t R32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t R64(const uint8_t *p) { return (uint64_t)R32(p) | ((uint64_t)R32(p+4)<<32); }
static void *A(WIM_READER *r, size_t n) { return r->Options.Allocator.Allocate(r->Options.Allocator.Context,n?n:1); }
static void F(WIM_READER *r, void *p) { if(p) r->Options.Allocator.Free(r->Options.Allocator.Context,p); }

static const uint8_t *FindBytes(const uint8_t *p,const uint8_t *end,const char *s,size_t n)
{
    while(p<=end && (size_t)(end-p)>=n) { if(!memcmp(p,s,n)) return p; ++p; } return NULL;
}
static int NameEq(const uint8_t *p,size_t n,const char *s)
{
    size_t i, m=strlen(s);
    if(n!=m) return 0;
    for(i=0;i<n;i++) { unsigned char c=p[i]; if(c>='a'&&c<='z') c=(unsigned char)(c-'a'+'A'); if(c!=(unsigned char)s[i]) return 0; }
    return 1;
}
static const uint8_t *FindTag(const uint8_t *p,const uint8_t *end,const char *tag)
{
    size_t n=strlen(tag);
    while(p<end) {
        if(*p=='<' && (size_t)(end-p)>n+2 && p[1]!='/' && p[1]!='!' && p[1]!='?' &&
           NameEq(p+1,n,tag) && (p[n+1]=='>' || p[n+1]==' ' || p[n+1]=='\t' || p[n+1]=='\r' || p[n+1]=='\n'))
            return p;
        ++p;
    }
    return NULL;
}
static const uint8_t *TagEnd(const uint8_t *p,const uint8_t *end)
{
    while(p<end && *p!='>') ++p;
    return p<end?p:NULL;
}
static char *DecodeText(WIM_READER *r,const uint8_t *p,size_t n);
static char *GetAttribute(WIM_READER *r,const uint8_t *tag,const uint8_t *gt,const char *wanted)
{
    const uint8_t *p=tag+1;
    while(p<gt) {
        const uint8_t *name,*value; size_t nameLen;
        while(p<gt && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p=='/')) ++p;
        name=p;
        while(p<gt && ((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-')) ++p;
        nameLen=(size_t)(p-name);
        if(!nameLen) { ++p; continue; }
        while(p<gt && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) ++p;
        if(p>=gt || *p!='=') continue;
        ++p;
        while(p<gt && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) ++p;
        if(p>=gt || (*p!='\'' && *p!='"')) continue;
        { uint8_t quote=*p++; value=p; while(p<gt && *p!=quote) ++p; }
        if(NameEq(name,nameLen,wanted)) return DecodeText(r,value,(size_t)(p-value));
        if(p<gt) ++p;
    }
    return NULL;
}
static char *DecodeText(WIM_READER *r,const uint8_t *p,size_t n)
{
    char *out=(char*)A(r,n+1); size_t i=0,j=0;
    if(!out) return NULL;
    while(i<n) {
        if(p[i]=='&') {
            const uint8_t *semi=(const uint8_t*)memchr(p+i,';',n-i);
            if(semi) {
                size_t k=(size_t)(semi-(p+i)); char c=0;
                if(k==4&&!memcmp(p+i,"&amp;",5)) c='&';
                else if(k==3&&!memcmp(p+i,"&lt;",4)) c='<';
                else if(k==3&&!memcmp(p+i,"&gt;",4)) c='>';
                else if(k==5&&!memcmp(p+i,"&quot;",6)) c='"';
                else if(k==5&&!memcmp(p+i,"&apos;",6)) c='\'';
                if(c) { out[j++]=c; i+=k+1; continue; }
            }
        }
        if(p[i]==0) { F(r,out); return NULL; }
        out[j++]=(char)p[i++];
    }
    out[j]=0;
    return out;
}
static char *GetElementText(WIM_READER *r,const uint8_t *start,const uint8_t *end,const char *tag)
{
    const uint8_t *t=FindTag(start,end,tag), *gt, *close;
    char closeTag[80]; size_t n=strlen(tag), len;
    if(!t || n+4>=sizeof(closeTag)) return NULL;
    gt=TagEnd(t,end); if(!gt) return NULL;
    closeTag[0]='<'; closeTag[1]='/'; memcpy(closeTag+2,tag,n); closeTag[n+2]='>'; closeTag[n+3]=0;
    close=(const uint8_t*)FindBytes(gt+1,end,closeTag,n+3);
    if(!close) return NULL;
    len=(size_t)(close-(gt+1));
    return DecodeText(r,gt+1,len);
}
static char *DecodeUtf16Name(WIM_READER *r,const uint8_t *p,size_t n)
{
    size_t i=0,j=0,cap=n*2+1; char *out=(char*)A(r,cap);
    if(!out) return NULL;
    while(i+1<n) {
        uint32_t cp=(uint32_t)p[i]|((uint32_t)p[i+1]<<8); i+=2;
        if(cp>=0xD800 && cp<=0xDBFF) {
            uint32_t lo;
            if(i+1>=n) { F(r,out); return NULL; }
            lo=(uint32_t)p[i]|((uint32_t)p[i+1]<<8); i+=2;
            if(lo<0xDC00 || lo>0xDFFF) { F(r,out); return NULL; }
            cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00);
        } else if(cp>=0xDC00 && cp<=0xDFFF) { F(r,out); return NULL; }
        if(cp==0) { F(r,out); return NULL; }
        if(cp<0x80) out[j++]=(char)cp;
        else if(cp<0x800) { out[j++]=(char)(0xC0|(cp>>6)); out[j++]=(char)(0x80|(cp&63)); }
        else if(cp<0x10000) { out[j++]=(char)(0xE0|(cp>>12)); out[j++]=(char)(0x80|((cp>>6)&63)); out[j++]=(char)(0x80|(cp&63)); }
        else { out[j++]=(char)(0xF0|(cp>>18)); out[j++]=(char)(0x80|((cp>>12)&63)); out[j++]=(char)(0x80|((cp>>6)&63)); out[j++]=(char)(0x80|(cp&63)); }
    }
    if(i!=n) { F(r,out); return NULL; }
    out[j]=0; return out;
}
static uint64_t ParseU64(const char *s)
{
    uint64_t v=0; if(!s||!*s) return 0;
    while(*s>='0'&&*s<='9') { unsigned d=(unsigned)(*s++-'0'); if(v>(UINT64_MAX-d)/10) return 0; v=v*10+d; }
    return *s?0:v;
}
static void FreeInfo(WIM_READER *r,WIM_IMAGE_INFO *i)
{ F(r,i->Name); F(r,i->Description); F(r,i->EditionId); memset(i,0,sizeof(*i)); }

WIM_READER_STATUS WimReaderLoadImageCatalogue(WIM_READER *r)
{
    WIM_RESOURCE_DESCRIPTOR d; uint8_t *xml=NULL; size_t used=0, cap, count=0, idx=0;
    const uint8_t *p,*end; WIM_IMAGE_RECORD *records=NULL; size_t lookupCount=0, i, meta=0;
    WIM_READER_STATUS st;
    if(!r) return WIM_READER_INVALID_ARGUMENT;
    if(r->Catalogue) return WIM_READER_OK;
    st=WimReaderGetResource(r,WIM_RESOURCE_XML_DATA,&d);
    if(st!=WIM_READER_OK||!d.OriginalSize||d.OriginalSize>WIM_MAX_XML_SIZE||d.OriginalSize>(uint64_t)(size_t)-1) return WIM_READER_BAD_RESOURCE;
    cap=(size_t)d.OriginalSize; xml=(uint8_t*)A(r,cap+1); if(!xml) return WIM_READER_OUT_OF_MEMORY;
    st=WimReaderReadResource(r,&d,xml,cap,&used); if(st!=WIM_READER_OK) { F(r,xml); return st; }
    xml[used]=0; p=xml; end=xml+used;
    while((p=FindTag(p,end,"IMAGE"))!=NULL) { const uint8_t *gt=TagEnd(p,end); if(!gt) break; ++count; p=gt+1; }
    if(count!=r->Header.ImageCount || count==0) { F(r,xml); return WIM_READER_CORRUPT_DATA; }
    st=WimReaderGetLookupEntryCount(r,&lookupCount); if(st!=WIM_READER_OK) { F(r,xml); return st; }
    /* The XML limit bounds count in practice, but keep the multiplication
       checked on 32-bit builds before allocating attacker-controlled records. */
    if (count > SIZE_MAX / sizeof(*records)) {
        F(r,xml);
        return WIM_READER_CORRUPT_DATA;
    }
    records=(WIM_IMAGE_RECORD*)A(r,count*sizeof(*records)); if(!records) { F(r,xml); return WIM_READER_OUT_OF_MEMORY; }
    memset(records,0,count*sizeof(*records)); p=xml;
    while(idx<count && (p=FindTag(p,end,"IMAGE"))!=NULL) {
        const uint8_t *gt=TagEnd(p,end), *close; char *indexText; uint64_t imageIndex;
        char closeTag[]="</IMAGE>";
        if(!gt) { st=WIM_READER_CORRUPT_DATA; goto fail; }
        close=(const uint8_t*)FindBytes(gt+1,end,closeTag,8);
        if(!close) { st=WIM_READER_CORRUPT_DATA; goto fail; }
        indexText=GetAttribute(r,p,gt,"INDEX");
        imageIndex=ParseU64(indexText); F(r,indexText);
        if(imageIndex!=idx+1) { st=WIM_READER_CORRUPT_DATA; goto fail; }
        records[idx].Info.Index=(uint32_t)imageIndex;
        records[idx].Info.Name=GetElementText(r,gt+1,close,"NAME");
        records[idx].Info.Description=GetElementText(r,gt+1,close,"DESCRIPTION");
        records[idx].Info.EditionId=GetElementText(r,gt+1,close,"EDITIONID");
        { char *v=GetElementText(r,gt+1,close,"DIRCOUNT"); records[idx].Info.DirectoryCount=ParseU64(v); F(r,v); }
        { char *v=GetElementText(r,gt+1,close,"FILECOUNT"); records[idx].Info.FileCount=ParseU64(v); F(r,v); }
        { char *v=GetElementText(r,gt+1,close,"TOTALBYTES"); records[idx].Info.TotalBytes=ParseU64(v); F(r,v); }
        records[idx].LookupIndex=(size_t)-1;
        p=close+8; ++idx;
    }
    for(i=0;i<lookupCount;i++) {
        WIM_LOOKUP_ENTRY e;
        st=WimReaderGetLookupEntry(r,i,&e); if(st!=WIM_READER_OK) goto fail;
        if((e.Resource.Flags&WIM_RESOURCE_FLAG_METADATA) &&
           !(e.Resource.Flags&WIM_RESOURCE_FLAG_FREE) &&
           e.ReferenceCount!=0 && e.Resource.OriginalSize!=0) {
            if(meta>=count) { st=WIM_READER_CORRUPT_DATA; goto fail; }
            records[meta++].LookupIndex=i;
        }
    }
    if(meta!=count) { st=WIM_READER_CORRUPT_DATA; goto fail; }
    F(r,xml);
    r->Catalogue=(WIM_IMAGE_CATALOGUE*)A(r,sizeof(*r->Catalogue));
    if(!r->Catalogue) { for(i=0;i<count;i++) FreeInfo(r,&records[i].Info); F(r,records); return WIM_READER_OUT_OF_MEMORY; }
    r->Catalogue->Images=records; r->Catalogue->Count=count;
    return WIM_READER_OK;
fail:
    F(r,xml);
    for(i=0;i<count;i++) FreeInfo(r,&records[i].Info);
    F(r,records);
    return st;
}
WIM_READER_STATUS WimReaderGetImageCount(WIM_READER *r,size_t *count)
{
    WIM_READER_STATUS st; if(!r||!count) return WIM_READER_INVALID_ARGUMENT; *count=0;
    st=WimReaderLoadImageCatalogue(r); if(st==WIM_READER_OK) *count=r->Catalogue->Count; return st;
}
WIM_READER_STATUS WimReaderGetImageInfo(WIM_READER *r,size_t n,WIM_IMAGE_INFO *info)
{
    WIM_READER_STATUS st; if(!r||!info) return WIM_READER_INVALID_ARGUMENT;
    st=WimReaderLoadImageCatalogue(r); if(st!=WIM_READER_OK) return st;
    if(n>=r->Catalogue->Count) return WIM_READER_INVALID_ARGUMENT;
    memset(info,0,sizeof(*info));
    info->Index=r->Catalogue->Images[n].Info.Index;
    info->DirectoryCount=r->Catalogue->Images[n].Info.DirectoryCount;
    info->FileCount=r->Catalogue->Images[n].Info.FileCount;
    info->TotalBytes=r->Catalogue->Images[n].Info.TotalBytes;
    if(r->Catalogue->Images[n].Info.Name) { size_t z=strlen(r->Catalogue->Images[n].Info.Name)+1; info->Name=(char*)A(r,z); if(!info->Name) goto oom; memcpy(info->Name,r->Catalogue->Images[n].Info.Name,z); }
    if(r->Catalogue->Images[n].Info.Description) { size_t z=strlen(r->Catalogue->Images[n].Info.Description)+1; info->Description=(char*)A(r,z); if(!info->Description) goto oom; memcpy(info->Description,r->Catalogue->Images[n].Info.Description,z); }
    if(r->Catalogue->Images[n].Info.EditionId) { size_t z=strlen(r->Catalogue->Images[n].Info.EditionId)+1; info->EditionId=(char*)A(r,z); if(!info->EditionId) goto oom; memcpy(info->EditionId,r->Catalogue->Images[n].Info.EditionId,z); }
    return WIM_READER_OK;
oom: FreeInfo(r,info); return WIM_READER_OUT_OF_MEMORY;
}
void WimImageInfoFree(WIM_READER *r,WIM_IMAGE_INFO *info) { if(r&&info) FreeInfo(r,info); }

static void FreeNode(WIM_READER *r,WIM_DIRECTORY_NODE *n)
{
    size_t i; if(!n) return; for(i=0;i<n->ChildCount;i++) FreeNode(r,n->Children[i]);
    { size_t j; for(j=0;j<n->NamedStreamCount;j++) F(r,n->NamedStreams[j].Name); }
    F(r,n->NamedStreams); F(r,n->Children); F(r,n->Name); F(r,n);
}
void WimImageDirectoryTreeFree(WIM_READER *r,WIM_DIRECTORY_NODE *root) { if(r) FreeNode(r,root); }

static WIM_READER_STATUS ParseDentry(WIM_READER *r,const uint8_t *buf,size_t len,
                                      uint64_t *offset,WIM_DIRECTORY_NODE *parent,
                                      WIM_DIRECTORY_NODE **out)
{
    uint64_t start=*offset, dlen, pos; uint16_t shortLen,nameLen,streams; uint32_t attrs;
    WIM_DIRECTORY_NODE *n; size_t i;
    *out=NULL;
    if(start>len || len-start<8) return WIM_READER_CORRUPT_DATA;
    dlen=R64(buf+(size_t)start); if(dlen<=8) { *offset=start+8; return WIM_READER_OK; }
    if(dlen< WIM_DENTRY_FIXED_SIZE || (dlen&7) || dlen>len-start) return WIM_READER_CORRUPT_DATA;
    attrs=R32(buf+(size_t)start+8); shortLen=R16(buf+(size_t)start+98); nameLen=R16(buf+(size_t)start+100);
    streams=R16(buf+(size_t)start+96);
    if((shortLen&1)||(nameLen&1)) return WIM_READER_CORRUPT_DATA;
    pos=start+WIM_DENTRY_FIXED_SIZE;
    if((uint64_t)nameLen+((nameLen)?2:0)+(uint64_t)shortLen+((shortLen)?2:0)>dlen-WIM_DENTRY_FIXED_SIZE) return WIM_READER_CORRUPT_DATA;
    n=(WIM_DIRECTORY_NODE*)A(r,sizeof(*n)); if(!n) return WIM_READER_OUT_OF_MEMORY;
    memset(n,0,sizeof(*n)); n->Attributes=attrs; n->SecurityId=R32(buf+(size_t)start+12);
    n->SubdirectoryOffset=R64(buf+(size_t)start+16); n->CreationTime=R64(buf+(size_t)start+40); n->LastAccessTime=R64(buf+(size_t)start+48); n->LastWriteTime=R64(buf+(size_t)start+56);
    memcpy(n->MainSha1,buf+(size_t)start+64,20); n->HardLinkGroupId=R64(buf+(size_t)start+88); n->Parent=parent; n->IsDirectory=(attrs&WIM_FILE_ATTRIBUTE_DIRECTORY)!=0;
    if(nameLen) {
        if(buf[(size_t)pos+nameLen]!=0 || buf[(size_t)pos+nameLen+1]!=0) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
        n->Name=DecodeUtf16Name(r,buf+(size_t)pos,nameLen);
        if(!n->Name) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
        pos+=(uint64_t)nameLen+2;
    }
    if(shortLen) {
        if(buf[(size_t)pos+shortLen]!=0 || buf[(size_t)pos+shortLen+1]!=0) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
        if(!n->Name) {
            n->Name=DecodeUtf16Name(r,buf+(size_t)pos,shortLen);
            if(!n->Name) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
        }
        pos+=(uint64_t)shortLen+2;
    }
    if(!n->IsDirectory && n->SubdirectoryOffset!=0) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
    n->MainStreamLookupIndex=(size_t)-1;
    pos=start+dlen;
    if(streams) {
        if((size_t)streams>SIZE_MAX/sizeof(*n->NamedStreams)) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
        n->NamedStreams=(WIM_NAMED_STREAM*)A(r,(size_t)streams*sizeof(*n->NamedStreams));
        if(!n->NamedStreams) { FreeNode(r,n); return WIM_READER_OUT_OF_MEMORY; }
        memset(n->NamedStreams,0,(size_t)streams*sizeof(*n->NamedStreams));
    }
    for(i=0;i<streams;i++) {
        uint64_t slen; uint16_t sn; WIM_NAMED_STREAM *stream=&n->NamedStreams[i];
        if(pos>len||len-pos<WIM_EXTRA_STREAM_FIXED_SIZE) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
        slen=R64(buf+(size_t)pos); sn=R16(buf+(size_t)pos+36);
        if(slen<WIM_EXTRA_STREAM_FIXED_SIZE || (slen&7) || slen>len-pos ||
           (sn&1) || (uint64_t)sn>slen-WIM_EXTRA_STREAM_FIXED_SIZE || sn==0) {
            FreeNode(r,n); return WIM_READER_CORRUPT_DATA;
        }
        memcpy(stream->Sha1,buf+(size_t)pos+8,20); stream->Size=R64(buf+(size_t)pos+28);
        stream->LookupIndex=(size_t)-1;
        stream->Name=DecodeUtf16Name(r,buf+(size_t)pos+38,sn);
        if(!stream->Name||!stream->Name[0]) { FreeNode(r,n); return WIM_READER_CORRUPT_DATA; }
        ++n->NamedStreamCount; pos+=slen;
    }
    *offset=pos; *out=n; return WIM_READER_OK;
}
static WIM_READER_STATUS ParseChildren(WIM_READER *r,const uint8_t *buf,size_t len,
                                        uint64_t minimumOffset,
                                        WIM_DIRECTORY_NODE *dir,unsigned depth)
{
    uint64_t off=dir->SubdirectoryOffset;
    if(depth>=WIM_MAX_TREE_DEPTH) return WIM_READER_CORRUPT_DATA;
    if(off==0) return WIM_READER_OK;
    if(off<minimumOffset || off>=len || (off&7)) return WIM_READER_CORRUPT_DATA;
    for(;;) {
        WIM_DIRECTORY_NODE *child=NULL, **array;
        uint64_t before=off;
        WIM_READER_STATUS st=ParseDentry(r,buf,len,&off,dir,&child);
        if(st!=WIM_READER_OK) return st;
        if(!child) return WIM_READER_OK;
        if(off<=before) { FreeNode(r,child); return WIM_READER_CORRUPT_DATA; }
        if(!child->Name || !child->Name[0] || !strcmp(child->Name,".") ||
           !strcmp(child->Name,"..")) { FreeNode(r,child); continue; }
        if(dir->ChildCount==dir->ChildCapacity) {
            size_t newCapacity=dir->ChildCapacity?dir->ChildCapacity*2:8;
            if(newCapacity<dir->ChildCapacity || newCapacity>SIZE_MAX/sizeof(*array)) {
                FreeNode(r,child); return WIM_READER_OUT_OF_MEMORY;
            }
            array=(WIM_DIRECTORY_NODE**)A(r,newCapacity*sizeof(*array));
            if(!array) { FreeNode(r,child); return WIM_READER_OUT_OF_MEMORY; }
            if(dir->ChildCount) memcpy(array,dir->Children,dir->ChildCount*sizeof(*array));
            F(r,dir->Children); dir->Children=array; dir->ChildCapacity=newCapacity;
        }
        dir->Children[dir->ChildCount++]=child;
        if(child->SubdirectoryOffset && child->IsDirectory) {
            st=ParseChildren(r,buf,len,minimumOffset,child,depth+1);
            if(st!=WIM_READER_OK) return st;
        }
    }
}
static int IsZeroSha1(const uint8_t hash[20])
{
    size_t i; for(i=0;i<20;i++) if(hash[i]) return 0; return 1;
}
static WIM_READER_STATUS ValidateSecurityIds(const WIM_DIRECTORY_NODE *node,uint32_t count)
{
    size_t i;
    if(node->SecurityId!=UINT32_MAX && node->SecurityId>=count)
        return WIM_READER_CORRUPT_DATA;
    for(i=0;i<node->ChildCount;i++) {
        WIM_READER_STATUS st=ValidateSecurityIds(node->Children[i],count);
        if(st!=WIM_READER_OK) return st;
    }
    return WIM_READER_OK;
}
static WIM_READER_STATUS ResolveMainStream(WIM_READER *r,WIM_DIRECTORY_NODE *node)
{
    size_t count,i;
    WIM_READER_STATUS st;
    if(!node) return WIM_READER_OK;
    if(!node->IsDirectory && !IsZeroSha1(node->MainSha1)) {
        st=WimReaderGetLookupEntryCount(r,&count);
        if(st!=WIM_READER_OK) return st;
        for(i=0;i<count;i++) {
            WIM_LOOKUP_ENTRY e;
            st=WimReaderGetLookupEntry(r,i,&e);
            if(st!=WIM_READER_OK) return st;
            if(!(e.Resource.Flags&WIM_RESOURCE_FLAG_METADATA) &&
               !(e.Resource.Flags&WIM_RESOURCE_FLAG_FREE) &&
               memcmp(e.Sha1,node->MainSha1,20)==0) {
                node->MainStreamLookupIndex=i;
                node->MainStreamSize=e.Resource.OriginalSize;
                node->HasMainStream=1;
                break;
            }
        }
        if(!node->HasMainStream || node->MainStreamSize==0) return WIM_READER_CORRUPT_DATA;
    }
    for(i=0;i<node->NamedStreamCount;i++) {
        size_t j; WIM_NAMED_STREAM *stream=&node->NamedStreams[i];
        if(IsZeroSha1(stream->Sha1)) { if(stream->Size!=0) return WIM_READER_CORRUPT_DATA; continue; }
        st=WimReaderGetLookupEntryCount(r,&count); if(st!=WIM_READER_OK) return st;
        for(j=0;j<count;j++) {
            WIM_LOOKUP_ENTRY e; st=WimReaderGetLookupEntry(r,j,&e); if(st!=WIM_READER_OK) return st;
            if(!(e.Resource.Flags&(WIM_RESOURCE_FLAG_METADATA|WIM_RESOURCE_FLAG_FREE)) &&
               e.ReferenceCount!=0 && memcmp(e.Sha1,stream->Sha1,20)==0 &&
               e.Resource.OriginalSize==stream->Size) {
                stream->LookupIndex=j; stream->HasLookup=1; break;
            }
        }
        if(!stream->HasLookup) return WIM_READER_CORRUPT_DATA;
    }
    for(i=0;i<node->ChildCount;i++) {
        st=ResolveMainStream(r,node->Children[i]);
        if(st!=WIM_READER_OK) return st;
    }
    return WIM_READER_OK;
}

WIM_READER_STATUS WimReaderLoadImageDirectoryTree(WIM_READER *r,size_t imageIndex,WIM_DIRECTORY_NODE **root)
{
    WIM_READER_STATUS st; WIM_LOOKUP_ENTRY e; uint8_t *buf; size_t used=0; uint64_t secLen,rootOff,off; WIM_DIRECTORY_NODE *node=NULL;
    if(!r||!root) return WIM_READER_INVALID_ARGUMENT;
    *root=NULL;
    st=WimReaderLoadImageCatalogue(r); if(st!=WIM_READER_OK) return st;
    if(imageIndex>=r->Catalogue->Count) return WIM_READER_INVALID_ARGUMENT;
    st=WimReaderGetLookupEntry(r,r->Catalogue->Images[imageIndex].LookupIndex,&e); if(st!=WIM_READER_OK) return st;
    if(e.PartNumber!=r->Header.PartNumber) return WIM_READER_UNSUPPORTED;
    if(e.Resource.OriginalSize==0) return WIM_READER_CORRUPT_DATA;
    if(e.Resource.OriginalSize>r->Options.MaximumResourceSize && r->Options.MaximumResourceSize) return WIM_READER_BAD_RESOURCE;
    if(e.Resource.OriginalSize>(uint64_t)(size_t)-1) return WIM_READER_BAD_RESOURCE;
    buf=(uint8_t*)A(r,(size_t)e.Resource.OriginalSize); if(!buf) return WIM_READER_OUT_OF_MEMORY;
    st=WimReaderReadLookupResource(r,r->Catalogue->Images[imageIndex].LookupIndex,buf,(size_t)e.Resource.OriginalSize,&used);
    if(st!=WIM_READER_OK) { F(r,buf); return st; }
    if(used<8) { F(r,buf); return WIM_READER_CORRUPT_DATA; }
    secLen=R32(buf); if(secLen==0) secLen=8; secLen=(secLen+7)&~UINT64_C(7);
    if(secLen<8 || secLen>used) { F(r,buf); return WIM_READER_CORRUPT_DATA; }
    { uint32_t n=R32(buf+4); uint64_t sizes=(uint64_t)n*8, total=8+sizes, j;
      if(total>secLen) { F(r,buf); return WIM_READER_CORRUPT_DATA; }
      for(j=0;j<n;j++) { uint64_t sz=R64(buf+8+(size_t)j*8); if(sz>UINT32_MAX || sz>secLen || total>secLen-sz) { F(r,buf); return WIM_READER_CORRUPT_DATA; } total+=sz; }
      if(((total+7)&~UINT64_C(7))!=secLen) { F(r,buf); return WIM_READER_CORRUPT_DATA; }
    }
    rootOff=secLen; off=rootOff;
    st=ParseDentry(r,buf,used,&off,NULL,&node);
    if(st!=WIM_READER_OK || !node || !node->IsDirectory) { FreeNode(r,node); F(r,buf); return st==WIM_READER_OK?WIM_READER_CORRUPT_DATA:st; }
    node->Parent=node;
    st=ParseChildren(r,buf,used,rootOff,node,0);
    if(st==WIM_READER_OK) st=ValidateSecurityIds(node,R32(buf+4));
    if(st==WIM_READER_OK) st=ResolveMainStream(r,node);
    if(st!=WIM_READER_OK) { FreeNode(r,node); F(r,buf); return st; }
    F(r,buf);
    *root=node;
    return WIM_READER_OK;
}

void WimReaderFreeImageCatalogueInternal(WIM_READER *r)
{
    size_t i;
    if(!r || !r->Catalogue) return;
    for(i=0;i<r->Catalogue->Count;i++) FreeInfo(r,&r->Catalogue->Images[i].Info);
    F(r,r->Catalogue->Images);
    F(r,r->Catalogue);
    r->Catalogue=NULL;
}

WIM_READER_STATUS WimImageReadNamedStream(WIM_READER *r,
                                           const WIM_DIRECTORY_NODE *node,
                                           size_t streamIndex,
                                           uint8_t *output,
                                           size_t capacity,
                                           size_t *used)
{
    const WIM_NAMED_STREAM *stream;
    if(!r||!node||!used||(!output&&capacity)) return WIM_READER_INVALID_ARGUMENT;
    *used=0;
    if(streamIndex>=node->NamedStreamCount) return WIM_READER_INVALID_ARGUMENT;
    stream=&node->NamedStreams[streamIndex];
    if(!stream->HasLookup) return WIM_READER_OK;
    if(stream->Size>capacity) return WIM_READER_BUFFER_TOO_SMALL;
    return WimReaderReadLookupResource(r,stream->LookupIndex,output,capacity,used);
}

WIM_READER_STATUS WimImageReadMainStream(WIM_READER *r,
                                          const WIM_DIRECTORY_NODE *node,
                                          uint8_t *output,
                                          size_t capacity,
                                          size_t *used)
{
    if(!r || !node || !used || (!output && capacity))
        return WIM_READER_INVALID_ARGUMENT;
    *used=0;
    if(node->IsDirectory) return WIM_READER_INVALID_ARGUMENT;
    if(!node->HasMainStream) return WIM_READER_OK;
    if(node->MainStreamSize>capacity) return WIM_READER_BUFFER_TOO_SMALL;
    return WimReaderReadLookupResource(r,node->MainStreamLookupIndex,
                                       output,capacity,used);
}
