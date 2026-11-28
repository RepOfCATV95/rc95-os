#include "rc95ldrsystemhive.h"
#include "rc95ldrntprepare.h"
#include "rc95ldrserviceorder.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define HBIN_BASE 0x1000u
#define REG_NK 0x6b6eu
#define REG_VK 0x6b76u
#define REG_LI 0x696cu
#define REG_LF 0x666cu
#define REG_LH 0x686cu
#define REG_RI 0x6972u
#define NK_COMPRESSED 0x20u
#define VK_COMPRESSED 0x0001u
#define HIVE_MAX_INDEX_CELLS 256u

typedef struct _HIVE_INDEX_VISITED { uint32_t Cells[HIVE_MAX_INDEX_CELLS]; uint32_t Count; } HIVE_INDEX_VISITED;
static int MarkIndexCell(HIVE_INDEX_VISITED *visited,uint32_t cell)
{
    uint32_t i;
    if(!visited)return 0;
    for(i=0;i<visited->Count;i++)if(visited->Cells[i]==cell)return 0;
    if(visited->Count>=HIVE_MAX_INDEX_CELLS)return 0;
    visited->Cells[visited->Count++]=cell;
    return 1;
}

static uint16_t U16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t U32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static int Eq(const char *a,const char *b) { while (*a && *b) { if (tolower((unsigned char)*a++)!=tolower((unsigned char)*b++)) return 0; } return *a==0 && *b==0; }
static int Cell(const RC95LDR_SYSTEM_HIVE *h,uint32_t off,const uint8_t **p,uint32_t *len)
{
    uint32_t pos,size,binBase,binSize,within; int32_t signedSize;
    if (!h || !h->Bytes || !p || !len || h->Size<HBIN_BASE ||
        h->DataLength==0 || h->DataLength>h->Size-HBIN_BASE ||
        h->DataLength>UINT32_MAX-HBIN_BASE || off==0xffffffffu || (off&7u) || off>=h->DataLength || off>UINT32_MAX-HBIN_BASE) return 0;
    pos=HBIN_BASE+off; binBase=HBIN_BASE+(off&~0xfffu); within=off&0xfffu;
    if (pos>h->Size-4 || binBase>h->Size-0x20 ||
        binBase>HBIN_BASE+h->DataLength-0x20 ||
        memcmp(h->Bytes+binBase,"hbin",4)!=0) return 0;
    binSize=U32(h->Bytes+binBase+8);
    if (binSize<0x1000 || (binSize&0xfffu) ||
        binSize>HBIN_BASE+h->DataLength-binBase ||
        within<0x20 || within>=binSize) return 0;
    signedSize=(int32_t)U32(h->Bytes+pos);
    if (signedSize>=0 || signedSize==INT32_MIN) return 0; /* positive cells are free */
    size=(uint32_t)(signedSize<0?-signedSize:signedSize);
    if (size<8 || (size&7u) || size>h->Size-pos || size>binSize-within) return 0;
    *p=h->Bytes+pos+4; *len=size-4; return 1;
}
static RC95LDR_STATUS DecodeName(const uint8_t *p,uint16_t n,int compressed,char *out,size_t cap)
{
    size_t i,count=compressed?n:n/2;
    if (!p || !out || !cap || (!compressed && (n&1u)))
        return Rc95LdrCorruptData;
    if (count>=cap) return Rc95LdrOutOfBounds;
    for(i=0;i<count;i++){
        uint16_t ch=compressed?p[i]:U16(p+i*2);
        /* The portable reader exposes narrow ASCII names. Never replace an
         * unrepresentable registry identity with '?' or truncate it: doing
         * so can alias two distinct service/value names. */
        if(ch==0 || ch<0x20 || ch==0x7f) return Rc95LdrCorruptData;
        if(ch>0x7f) return Rc95LdrUnsupported;
        out[i]=(char)ch;
    }
    out[count]=0;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS ReadKey(const RC95LDR_SYSTEM_HIVE *h,uint32_t off,RC95LDR_HIVE_KEY *k)
{
    const uint8_t *p; uint32_t n; uint16_t nameLen;
    if(!Cell(h,off,&p,&n)||n<0x4c||U16(p)!=REG_NK)return Rc95LdrCorruptData;
    nameLen=U16(p+0x48);
    if(nameLen>n-0x4c||nameLen>RC95LDR_HIVE_MAX_NAME_CHARS*2u||(!(U16(p+2)&NK_COMPRESSED)&&(nameLen&1u)))return Rc95LdrCorruptData;
    memset(k,0,sizeof(*k));k->Cell=off;k->Flags=U16(p+2);
    k->SubkeyCount=U32(p+0x14);k->SubkeyList=U32(p+0x1c);
    k->VolatileSubkeyCount=U32(p+0x18);k->VolatileSubkeyList=U32(p+0x20);
    k->ValueCount=U32(p+0x24);k->ValueList=U32(p+0x28);k->NameLength=nameLen;
    {RC95LDR_STATUS status=DecodeName(p+0x4c,nameLen,(k->Flags&NK_COMPRESSED)!=0,k->Name,sizeof(k->Name));
     if(status!=Rc95LdrSuccess)return status;}
    if(k->SubkeyCount>65536u||k->VolatileSubkeyCount>65536u||k->SubkeyCount>65536u-k->VolatileSubkeyCount||k->ValueCount>65536u)return Rc95LdrCorruptData;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS FindSubkey(const RC95LDR_SYSTEM_HIVE *h,uint32_t list,
    const char *name,uint32_t *found,unsigned depth,uint32_t *total,HIVE_INDEX_VISITED *visited)
{
    const uint8_t *p;uint32_t n,i,entries,width;uint16_t sig;
    if(depth>8||!MarkIndexCell(visited,list)||!Cell(h,list,&p,&n)||n<4)return Rc95LdrCorruptData;
    sig=U16(p);entries=U16(p+2);
    if(sig==REG_RI){
        if(!entries||entries>(n-4)/4)return Rc95LdrCorruptData;
        for(i=0;i<entries;i++){
            RC95LDR_STATUS s=FindSubkey(h,U32(p+4+i*4),name,found,depth+1,total,visited);
            if(s!=Rc95LdrSuccess&&s!=Rc95LdrNotFound)return s;
        }
        return *found!=0xffffffffu?Rc95LdrSuccess:Rc95LdrNotFound;
    }
    if(sig!=REG_LI&&sig!=REG_LF&&sig!=REG_LH)return Rc95LdrCorruptData;
    width=(sig==REG_LF||sig==REG_LH)?8u:4u;
    if(!entries||entries>(n-4)/width||*total>65536u-entries)return Rc95LdrCorruptData;
    for(i=0;i<entries;i++){
        uint32_t child=U32(p+4+i*width);RC95LDR_HIVE_KEY k;
        RC95LDR_STATUS s=ReadKey(h,child,&k);
        if(s!=Rc95LdrSuccess)return s;
        if(Eq(k.Name,name)){
            if(*found!=0xffffffffu)return Rc95LdrCorruptData;
            *found=child;
        }
        ++*total;
    }
    return *found!=0xffffffffu?Rc95LdrSuccess:Rc95LdrNotFound;
}
RC95LDR_STATUS Rc95LdrOpenSystemHive(const void *bytes,size_t size,RC95LDR_SYSTEM_HIVE *h)
{
    const uint8_t *b=(const uint8_t*)bytes;uint32_t data,root,pos,end;
    if(!b||!h||size<RC95LDR_REGF_HEADER_SIZE)return Rc95LdrInvalidParameter;
    if(Rc95LdrValidateSystemHive(bytes,size,&data)!=Rc95LdrSuccess)return Rc95LdrCorruptData;
    root=U32(b+0x24);pos=HBIN_BASE;
    if(data>UINT32_MAX-HBIN_BASE)return Rc95LdrOutOfBounds;
    end=HBIN_BASE+data;
    if(end>size)return Rc95LdrOutOfBounds;
    while(pos<end){
        uint32_t binSize;
        if(end-pos<0x20||memcmp(b+pos,"hbin",4)!=0)return Rc95LdrCorruptData;
        binSize=U32(b+pos+8);
        if(binSize<0x1000||(binSize&0xfffu)||binSize>end-pos)return Rc95LdrCorruptData;
        pos+=binSize;
    }
    if(pos!=end)return Rc95LdrCorruptData;
    h->Bytes=b;h->Size=size;h->DataLength=data;h->RootCell=root;
    {RC95LDR_HIVE_KEY k;return ReadKey(h,root,&k);}
}
RC95LDR_STATUS Rc95LdrHiveOpenKey(const RC95LDR_SYSTEM_HIVE *h,uint32_t parent,const char *name,RC95LDR_HIVE_KEY *key)
{
    RC95LDR_HIVE_KEY p;uint32_t child;RC95LDR_STATUS s;
    if(!h||!name||!key)return Rc95LdrInvalidParameter;
    s=ReadKey(h,parent,&p);if(s!=Rc95LdrSuccess)return s;
    /* Volatile children are a separate hive index. A key can legally have
       no stable subkeys while still owning volatile subkeys, so do not
       short-circuit on SubkeyCount alone. */
    {
        uint32_t stableSeen=0,volatileSeen=0;
        uint32_t stableFound=0xffffffffu,volatileFound=0xffffffffu;
        HIVE_INDEX_VISITED visited;memset(&visited,0,sizeof(visited));
        if(p.SubkeyCount){
            s=FindSubkey(h,p.SubkeyList,name,&stableFound,0,&stableSeen,&visited);
            if(s!=Rc95LdrSuccess&&s!=Rc95LdrNotFound)return s;
            if(stableSeen!=p.SubkeyCount)return Rc95LdrCorruptData;
        }
        if(p.VolatileSubkeyCount){
            s=FindSubkey(h,p.VolatileSubkeyList,name,&volatileFound,0,&volatileSeen,&visited);
            if(s!=Rc95LdrSuccess&&s!=Rc95LdrNotFound)return s;
            if(volatileSeen!=p.VolatileSubkeyCount)return Rc95LdrCorruptData;
        }
        if(stableFound!=0xffffffffu&&volatileFound!=0xffffffffu)return Rc95LdrCorruptData;
        child=stableFound!=0xffffffffu?stableFound:volatileFound;
        if(child==0xffffffffu)return Rc95LdrNotFound;
    }
    return ReadKey(h,child,key);
}
RC95LDR_STATUS Rc95LdrHiveOpenPath(const RC95LDR_SYSTEM_HIVE *h,const char *path,RC95LDR_HIVE_KEY *key)
{
    char part[RC95LDR_HIVE_MAX_NAME_CHARS+1];const char *p=path;RC95LDR_HIVE_KEY cur;RC95LDR_STATUS s;
    if(!h||!path||!key||!*path)return Rc95LdrInvalidParameter;
    s=ReadKey(h,h->RootCell,&cur);if(s!=Rc95LdrSuccess)return s;
    while(*p=='\\'||*p=='/')p++;
    while(*p){size_t n=0;while(p[n]&&p[n]!='\\'&&p[n]!='/')n++;if(!n||n>=sizeof(part))return Rc95LdrInvalidParameter;memcpy(part,p,n);part[n]=0;s=Rc95LdrHiveOpenKey(h,cur.Cell,part,&cur);if(s!=Rc95LdrSuccess)return s;p+=n;while(*p=='\\'||*p=='/')p++;}
    *key=cur;return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrHiveGetValue(const RC95LDR_SYSTEM_HIVE *h,const RC95LDR_HIVE_KEY *k,const char *name,RC95LDR_HIVE_VALUE *v)
{
    const uint8_t *list,*p;uint32_t len,i,off;uint16_t nl,flags;
    char decoded[RC95LDR_HIVE_MAX_NAME_CHARS+1];
    RC95LDR_HIVE_KEY verified;RC95LDR_HIVE_VALUE match;int found=0;
    if(!h||!k||!name||!v)return Rc95LdrInvalidParameter;
    /* Public key descriptors are caller-owned; never trust their counts or
       list offsets without revalidating the referenced NK cell. */
    {RC95LDR_STATUS status=ReadKey(h,k->Cell,&verified);
     if(status!=Rc95LdrSuccess)return status;}
    k=&verified;
    if(k->ValueCount>65536u)return Rc95LdrCorruptData;
    if(!k->ValueCount)return Rc95LdrNotFound;
    if(!Cell(h,k->ValueList,&list,&len)||k->ValueCount>len/4u)return Rc95LdrCorruptData;
    for(i=0;i<k->ValueCount;i++){
        RC95LDR_HIVE_VALUE candidate;
        off=U32(list+i*4);
        if(!Cell(h,off,&p,&len)||len<0x14||U16(p)!=REG_VK)return Rc95LdrCorruptData;
        nl=U16(p+2);
        if(nl>len-0x14||nl>RC95LDR_HIVE_MAX_NAME_CHARS*2u)return Rc95LdrCorruptData;
        flags=U16(p+0x10);
        /* VK flag bit zero selects the compressed-name encoding. Unknown
           bits are reserved; accepting them risks interpreting an alternate
           on-disk representation as an ordinary value. */
        if(flags&~VK_COMPRESSED)return Rc95LdrCorruptData;
        if(!(flags&VK_COMPRESSED)&&(nl&1u))return Rc95LdrCorruptData;
        {RC95LDR_STATUS status=DecodeName(p+0x14,nl,(flags&VK_COMPRESSED)!=0,decoded,sizeof(decoded));
         if(status!=Rc95LdrSuccess)return status;}
        /* Validate every value's data locator during the list walk, not only
           the requested name. Otherwise a malformed later VK can remain
           latent while callers treat an earlier value as trusted. Big-data
           registry cells are not supported by this bounded reader. */
        {
            uint32_t encoded=U32(p+4),dataLength=encoded&0x7fffffffu;
            if(encoded&0x80000000u){
                if(dataLength>4)return Rc95LdrCorruptData;
            }else if(dataLength){
                const uint8_t *valueData;uint32_t valueDataLength;
                if(!Cell(h,U32(p+8),&valueData,&valueDataLength)||
                   dataLength>valueDataLength)return Rc95LdrCorruptData;
            }
        }
        if(Eq(decoded,name)){
            if(found)return Rc95LdrCorruptData;
            memset(&candidate,0,sizeof(candidate));
            candidate.Cell=off;candidate.NameLength=nl;
            memcpy(candidate.Name,decoded,strlen(decoded)+1);
            candidate.DataInline=(U32(p+4)&0x80000000u)!=0;
            candidate.DataLength=U32(p+4)&0x7fffffffu;
            candidate.DataCell=U32(p+8);candidate.Type=U32(p+0xc);
            if(candidate.DataInline&&candidate.DataLength>4)return Rc95LdrCorruptData;
            match=candidate;found=1;
        }
    }
    if(!found)return Rc95LdrNotFound;
    *v=match;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrHiveReadValue(const RC95LDR_SYSTEM_HIVE *h,const RC95LDR_HIVE_VALUE *v,void *buffer,size_t cap,size_t *read)
{
    const uint8_t *vk,*data;uint32_t vkLen,dataLen,encodedLen,dataCell;
    uint16_t nameLen,nameFlags;int inlineData;char decoded[RC95LDR_HIVE_MAX_NAME_CHARS+1];
    RC95LDR_STATUS status;
    if(!read)return Rc95LdrInvalidParameter;
    /* Never publish an untrusted length on failure. Callers commonly use
       BytesRead to decide how much of a destination buffer is initialized. */
    *read=0;
    if(!h||!v||(!buffer&&v->DataLength))return Rc95LdrInvalidParameter;
    /* A value descriptor is caller-owned. Revalidate the complete VK identity
       (name, type and data locator), not merely its cell and byte count. This
       prevents a stale or forged descriptor from changing the value's type
       or being replayed against a different value cell. */
    if(!Cell(h,v->Cell,&vk,&vkLen)||vkLen<0x14||U16(vk)!=REG_VK)
        return Rc95LdrCorruptData;
    nameLen=U16(vk+2);nameFlags=U16(vk+0x10);
    if((nameFlags&~VK_COMPRESSED)||nameLen>vkLen-0x14||
       nameLen>RC95LDR_HIVE_MAX_NAME_CHARS*2u||
       (!(nameFlags&VK_COMPRESSED)&&(nameLen&1u)))
        return Rc95LdrCorruptData;
    status=DecodeName(vk+0x14,nameLen,(nameFlags&VK_COMPRESSED)!=0,
                      decoded,sizeof(decoded));
    if(status!=Rc95LdrSuccess)return status;
    encodedLen=U32(vk+4);inlineData=(encodedLen&0x80000000u)!=0;
    dataLen=encodedLen&0x7fffffffu;dataCell=U32(vk+8);
    if(v->DataLength!=dataLen||v->DataInline!=(unsigned)inlineData||
       v->DataCell!=dataCell||v->Type!=U32(vk+0x0c)||
       v->NameLength!=nameLen||strcmp(v->Name,decoded)!=0)
        return Rc95LdrCorruptData;
    if(inlineData&&dataLen>4)return Rc95LdrCorruptData;
    if(dataLen>cap)return Rc95LdrBufferTooSmall;
    if(dataLen>0&&!buffer)return Rc95LdrInvalidParameter;
    if(!dataLen)return Rc95LdrSuccess;
    if(inlineData){
        uint8_t raw[4]={(uint8_t)dataCell,(uint8_t)(dataCell>>8),
                        (uint8_t)(dataCell>>16),(uint8_t)(dataCell>>24)};
        memcpy(buffer,raw,dataLen);*read=dataLen;return Rc95LdrSuccess;
    }
    if(!Cell(h,dataCell,&data,&vkLen)||dataLen>vkLen)return Rc95LdrCorruptData;
    memcpy(buffer,data,dataLen);*read=dataLen;return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrHiveGetDword(const RC95LDR_SYSTEM_HIVE *h,const RC95LDR_HIVE_KEY *k,const char *name,uint32_t *data)
{
    RC95LDR_HIVE_VALUE v;uint8_t b[4];size_t n;RC95LDR_STATUS s;
    if (!data)
        return Rc95LdrInvalidParameter;
    s = Rc95LdrHiveGetValue(h, k, name, &v);
    if (s != Rc95LdrSuccess)
        return s;
    if (v.Type != 4 || v.DataLength != 4)
        return Rc95LdrCorruptData;
    s = Rc95LdrHiveReadValue(h, &v, b, sizeof(b), &n);
    if (s == Rc95LdrSuccess)
        *data = U32(b);
    return s;
}
RC95LDR_STATUS Rc95LdrHiveResolveCurrentControlSet(const RC95LDR_SYSTEM_HIVE *h,RC95LDR_HIVE_KEY *set)
{
    RC95LDR_HIVE_KEY select;uint32_t current;char name[32];RC95LDR_STATUS s;
    if (!h || !set)
        return Rc95LdrInvalidParameter;
    s = Rc95LdrHiveOpenPath(h, "Select", &select);
    if (s != Rc95LdrSuccess)
        return s;
    s=Rc95LdrHiveGetDword(h,&select,"Current",&current);if(s!=Rc95LdrSuccess)return s;
    if(current==0||current>999)return Rc95LdrCorruptData;
    if(current<10)snprintf(name,sizeof(name),"ControlSet00%u",current);
    else if(current<100)snprintf(name,sizeof(name),"ControlSet0%u",current);
    else snprintf(name,sizeof(name),"ControlSet%u",current);
    return Rc95LdrHiveOpenPath(h,name,set);
}

static RC95LDR_STATUS EnumerateList(const RC95LDR_SYSTEM_HIVE *h,uint32_t off,
    RC95LDR_HIVE_SUBKEY_CALLBACK cb,void *ctx,unsigned depth,uint32_t *seen,HIVE_INDEX_VISITED *visited)
{
    const uint8_t *p;uint32_t n,i,count;uint16_t sig;RC95LDR_STATUS s;
    if(depth>8||!MarkIndexCell(visited,off)||!Cell(h,off,&p,&n)||n<4)return Rc95LdrCorruptData;
    sig=U16(p);count=U16(p+2);
    if(sig==REG_RI){
        if(!count||count>(n-4)/4)return Rc95LdrCorruptData;
        for(i=0;i<count;i++){s=EnumerateList(h,U32(p+4+i*4),cb,ctx,depth+1,seen,visited);if(s!=Rc95LdrSuccess)return s;}
        return Rc95LdrSuccess;
    }
    if(sig!=REG_LI&&sig!=REG_LF&&sig!=REG_LH)return Rc95LdrCorruptData;
    if(!count||count>(n-4)/(sig==REG_LF||sig==REG_LH?8u:4u))return Rc95LdrCorruptData;
    if(*seen>65536u-count)return Rc95LdrCorruptData;
    for(i=0;i<count;i++){
        RC95LDR_HIVE_KEY key;uint32_t child=U32(p+4+i*(sig==REG_LF||sig==REG_LH?8u:4u));
        s=ReadKey(h,child,&key);if(s!=Rc95LdrSuccess)return s;
        ++(*seen);
        if(cb){s=cb(ctx,&key);if(s!=Rc95LdrSuccess)return s;}
    }
    return Rc95LdrSuccess;
}
typedef struct _HIVE_ENUM_VALIDATE_CONTEXT {
    const RC95LDR_SYSTEM_HIVE *Hive;
    uint32_t ParentCell;
} HIVE_ENUM_VALIDATE_CONTEXT;
static RC95LDR_STATUS ValidateEnumeratedIdentity(void *opaque,
    const RC95LDR_HIVE_KEY *key)
{
    HIVE_ENUM_VALIDATE_CONTEXT *c=(HIVE_ENUM_VALIDATE_CONTEXT*)opaque;
    RC95LDR_HIVE_KEY unique;
    RC95LDR_STATUS s=Rc95LdrHiveOpenKey(c->Hive,c->ParentCell,key->Name,&unique);
    if(s!=Rc95LdrSuccess)return s;
    if(unique.Cell!=key->Cell)return Rc95LdrCorruptData;
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrHiveEnumerateSubkeys(const RC95LDR_SYSTEM_HIVE*h,
    const RC95LDR_HIVE_KEY*parent,RC95LDR_HIVE_SUBKEY_CALLBACK cb,void*ctx)
{
    uint32_t seen=0;RC95LDR_STATUS s;RC95LDR_HIVE_KEY verified;
    HIVE_ENUM_VALIDATE_CONTEXT validation;HIVE_INDEX_VISITED visited;memset(&visited,0,sizeof(visited));
    if(!h||!parent||!cb)return Rc95LdrInvalidParameter;
    /* Re-read the NK cell so forged/stale public descriptors cannot redirect
       enumeration to arbitrary cells or supply inconsistent subkey counts. */
    s=ReadKey(h,parent->Cell,&verified);if(s!=Rc95LdrSuccess)return s;
    parent=&verified;
    if(parent->SubkeyCount>65536u||parent->VolatileSubkeyCount>65536u||
       parent->SubkeyCount>65536u-parent->VolatileSubkeyCount)return Rc95LdrCorruptData;
    if(parent->SubkeyCount){
        s=EnumerateList(h,parent->SubkeyList,0,0,0,&seen,&visited);if(s!=Rc95LdrSuccess)return s;
    }
    if(parent->VolatileSubkeyCount){
        s=EnumerateList(h,parent->VolatileSubkeyList,0,0,0,&seen,&visited);if(s!=Rc95LdrSuccess)return s;
    }
    if(seen!=parent->SubkeyCount+parent->VolatileSubkeyCount)return Rc95LdrCorruptData;
    /* Validate the complete set of stable and volatile identities before
       exposing any callback result. Re-resolving each name through both trees
       rejects duplicates split across leaves or between the two index sets. */
    validation.Hive=h;validation.ParentCell=parent->Cell;
    seen=0;memset(&visited,0,sizeof(visited));
    if(parent->SubkeyCount){s=EnumerateList(h,parent->SubkeyList,ValidateEnumeratedIdentity,&validation,0,&seen,&visited);if(s!=Rc95LdrSuccess)return s;}
    if(parent->VolatileSubkeyCount){s=EnumerateList(h,parent->VolatileSubkeyList,ValidateEnumeratedIdentity,&validation,0,&seen,&visited);if(s!=Rc95LdrSuccess)return s;}
    if(seen!=parent->SubkeyCount+parent->VolatileSubkeyCount)return Rc95LdrCorruptData;
    /* Only after the entire tree and every identity have passed preflight may
       caller callbacks observe any subkey. */
    seen=0;memset(&visited,0,sizeof(visited));
    if(parent->SubkeyCount){s=EnumerateList(h,parent->SubkeyList,cb,ctx,0,&seen,&visited);if(s!=Rc95LdrSuccess)return s;}
    if(parent->VolatileSubkeyCount){s=EnumerateList(h,parent->VolatileSubkeyList,cb,ctx,0,&seen,&visited);if(s!=Rc95LdrSuccess)return s;}
    return seen==parent->SubkeyCount+parent->VolatileSubkeyCount?Rc95LdrSuccess:Rc95LdrCorruptData;
}
static RC95LDR_STATUS ReadStringValue(const RC95LDR_SYSTEM_HIVE*h,
    const RC95LDR_HIVE_KEY*k,const char*name,char*out,size_t cap)
{
    RC95LDR_HIVE_VALUE v;uint8_t raw[RC95LDR_HIVE_MAX_NAME_CHARS*2+2];size_t got,i,n;RC95LDR_STATUS s;
    if(!cap)return Rc95LdrInvalidParameter;
    s=Rc95LdrHiveGetValue(h,k,name,&v);if(s!=Rc95LdrSuccess)return s;
    /* The portable hive reader has no environment-expansion context. Do
       not pass REG_EXPAND_SZ through as if it were a literal path: values
       such as %SystemRoot%\\System32\\drivers\\foo.sys would otherwise
       be handed to the image provider unresolved. The in-tree CM adapter
       uses CmpFindDrivers, which supplies its already-resolved FilePath. */
    if(v.Type==2)return Rc95LdrUnsupported;
    if(v.Type!=1||(v.DataLength&1u)||v.DataLength<2||v.DataLength>sizeof(raw))
        return Rc95LdrCorruptData;
    s=Rc95LdrHiveReadValue(h,&v,raw,sizeof(raw),&got);if(s!=Rc95LdrSuccess)return s;
    if(got<2||U16(raw+(got-2))!=0)return Rc95LdrCorruptData;
    n=got/2-1;if(n>=cap)return Rc95LdrOutOfBounds;
    for(i=0;i<n;i++){
        uint16_t ch=U16(raw+i*2);
        /* This adapter's public path fields are narrow ASCII. Reject
           unrepresentable and embedded-NUL characters rather than silently
           changing a registry path into a different file name. */
        if(ch==0)return Rc95LdrCorruptData;
        if(ch>0x7f)return Rc95LdrUnsupported;
        out[i]=(char)ch;
    }
    out[n]=0;return Rc95LdrSuccess;
}
static RC95LDR_STATUS OptionalDword(const RC95LDR_SYSTEM_HIVE*h,
    const RC95LDR_HIVE_KEY*k,const char*name,uint32_t*value,uint32_t fallback)
{
    RC95LDR_STATUS s=Rc95LdrHiveGetDword(h,k,name,value);
    if(s==Rc95LdrNotFound){*value=fallback;return Rc95LdrSuccess;}return s;
}

/* Validate a REG_MULTI_SZ dependency value without allocating a hive buffer.
   A valid non-empty list remains Unsupported until graph resolution exists. */
RC95LDR_STATUS Rc95LdrHiveValidateDependencyMultiSz(const void *data,size_t length)
{
    const uint8_t *raw=(const uint8_t*)data;
    size_t i=0;int hasEntry=0,unsupported=0;
    if(!raw)return Rc95LdrInvalidParameter;
    if(length<4u||(length&1u))return Rc95LdrCorruptData;
    if(U16(raw+length-2u)!=0||U16(raw+length-4u)!=0)return Rc95LdrCorruptData;
    while(i<length-2u){
        uint16_t ch;size_t entryStart=i;
        if(U16(raw+i)==0){
            if(i!=0||U16(raw+i+2u)!=0)return Rc95LdrCorruptData;
            i+=2u;break;
        }
        hasEntry=1;
        for(;;){
            if(i>=length-2u)return Rc95LdrCorruptData;
            ch=U16(raw+i);
            if(ch==0){i+=2u;break;}
            if(ch<0x20u||ch==0x7fu||ch=='\\'||ch=='/'||ch==':')
                return Rc95LdrCorruptData;
            /* Continue validating the complete list after encountering an
               unsupported name so malformed trailing data is not masked. */
            if(ch>0x7fu)unsupported=1;
            i+=2u;
        }
        /* Dependency names are Services-key components and must fit the
           same bounded identity representation used by ReadKey. Do not
           classify an overlong ASCII component as merely unsupported: it
           cannot be resolved without truncation or aliasing. */
        if((i-entryStart)/2u>RC95LDR_HIVE_MAX_NAME_CHARS)
            return Rc95LdrCorruptData;
        if(i-entryStart==4u&&U16(raw+entryStart)=='.')return Rc95LdrCorruptData;
        if(i-entryStart==6u&&U16(raw+entryStart)=='.'&&
           U16(raw+entryStart+2u)=='.')return Rc95LdrCorruptData;
        if(i<length-2u&&U16(raw+i)==0)return Rc95LdrCorruptData;
    }
    if(i!=length-2u)return Rc95LdrCorruptData;
    return unsupported?Rc95LdrUnsupported:
           (hasEntry?Rc95LdrUnsupported:Rc95LdrSuccess);
}
#define HIVE_MAX_DEPENDENCY_VALUE_BYTES 4098u

typedef struct _HIVE_DEPENDENCY_RESOLVE_CONTEXT {
    const RC95LDR_SYSTEM_HIVE *Hive;
    uint32_t ServicesCell;
} HIVE_DEPENDENCY_RESOLVE_CONTEXT;

static RC95LDR_STATUS ValidateServiceDependencyReference(void *opaque,
    const char *name)
{
    HIVE_DEPENDENCY_RESOLVE_CONTEXT *context =
        (HIVE_DEPENDENCY_RESOLVE_CONTEXT *)opaque;
    RC95LDR_HIVE_KEY target;
    RC95LDR_STATUS status;
    status = Rc95LdrHiveOpenKey(context->Hive, context->ServicesCell,
                                name, &target);
    /* A missing DependOnService target is not an optional dependency: it
       makes the registry policy impossible to honor deterministically. */
    if (status == Rc95LdrNotFound) return Rc95LdrCorruptData;
    return status;
}

static RC95LDR_STATUS ValidateUnsupportedDependencies(
    const RC95LDR_SYSTEM_HIVE *h, uint32_t servicesCell,
    const RC95LDR_HIVE_KEY *service, const char *valueName)
{
    RC95LDR_HIVE_VALUE value;
    uint8_t raw[HIVE_MAX_DEPENDENCY_VALUE_BYTES];
    size_t got;
    RC95LDR_STATUS s = Rc95LdrHiveGetValue(h, service, valueName, &value);
    if (s == Rc95LdrNotFound) return Rc95LdrSuccess;
    if (s != Rc95LdrSuccess) return s;
    if (value.Type != 7u || (value.DataLength & 1u) ||
        value.DataLength < 4u)
        return Rc95LdrCorruptData;
    if (value.DataLength > sizeof(raw)) return Rc95LdrUnsupported;
    s = Rc95LdrHiveReadValue(h, &value, raw, sizeof(raw), &got);
    if (s != Rc95LdrSuccess) return s;

    if (Eq(valueName, "DependOnService")) {
        HIVE_DEPENDENCY_RESOLVE_CONTEXT context;
        context.Hive = h;
        context.ServicesCell = servicesCell;
        s = Rc95LdrEnumerateServiceDependencies(raw, got,
            ValidateServiceDependencyReference, &context);
        if (s != Rc95LdrSuccess) return s;
        /* References are checked here for every Services key. Boot-driver
           edges are materialized after the complete candidate set is known. */
        return Rc95LdrSuccess;
    }

    /* DependOnGroup uses group identities rather than service-key names.
       Keep it fail-closed until group expansion is wired into the graph. */
    return Rc95LdrHiveValidateDependencyMultiSz(raw, got);
}
typedef struct _DRIVER_ENUM_CONTEXT {
    const RC95LDR_SYSTEM_HIVE *Hive;
    uint32_t ServicesCell;
    RC95LDR_HIVE_BOOT_DRIVER_CALLBACK Callback;
    void *Context;
} DRIVER_ENUM_CONTEXT;
static RC95LDR_STATUS VisitService(void *opaque,const RC95LDR_HIVE_KEY *service)
{
    DRIVER_ENUM_CONTEXT *c=(DRIVER_ENUM_CONTEXT*)opaque;RC95LDR_HIVE_BOOT_DRIVER d;
    RC95LDR_HIVE_KEY unique;
    RC95LDR_STATUS s;memset(&d,0,sizeof(d));
    /* Reject ambiguous service identities caused by duplicate names in the
       stable/volatile index trees. HiveOpenKey validates the full index and
       reports duplicate matches instead of selecting one arbitrarily. */
    s=Rc95LdrHiveOpenKey(c->Hive,c->ServicesCell,service->Name,&unique);
    if(s!=Rc95LdrSuccess)return s;
    if(unique.Cell!=service->Cell)return Rc95LdrCorruptData;
    if(strlen(service->Name)>=sizeof(d.ServiceName))return Rc95LdrOutOfBounds;
    memcpy(d.ServiceName,service->Name,strlen(service->Name)+1);
    s=OptionalDword(c->Hive,service,"Start",&d.Start,3);if(s!=Rc95LdrSuccess)return s;
    s=OptionalDword(c->Hive,service,"Type",&d.Type,0);if(s!=Rc95LdrSuccess)return s;
    s=OptionalDword(c->Hive,service,"ErrorControl",&d.ErrorControl,1);if(s!=Rc95LdrSuccess)return s;
    s=OptionalDword(c->Hive,service,"Tag",&d.Tag,0);if(s!=Rc95LdrSuccess)return s;
    /* These are registry-controlled boot policy fields. Reject values outside
       the documented SCM ranges instead of treating malformed metadata as a
       harmless non-boot service and continuing with a partially trusted hive. */
    if(d.Start>4u||d.ErrorControl>3u)return Rc95LdrCorruptData;
    /* SERVICE_* type values are bit flags. Only kernel and file-system driver
       types are candidates for the winload boot-driver list; the remaining
       defined service types are valid but are not mapped here. */
    if(d.Type!=0u&&d.Type!=1u&&d.Type!=2u&&d.Type!=4u&&
       d.Type!=8u&&d.Type!=16u&&d.Type!=32u)
        return Rc95LdrCorruptData;
    /* Validate dependency metadata for every Services key, not just boot
       drivers. DependOnService references are checked here; boot-driver
       prerequisite edges are materialized after candidate collection.
       DependOnGroup remains fail-closed until group expansion is supported. */
    s=ValidateUnsupportedDependencies(c->Hive,c->ServicesCell,service,"DependOnService");
    if(s!=Rc95LdrSuccess)return s;
    s=ValidateUnsupportedDependencies(c->Hive,c->ServicesCell,service,"DependOnGroup");
    if(s!=Rc95LdrSuccess)return s;
    /* Match NTOSKRNL's CmpFindDrivers(..., BootLoad, ...): only Start=0
       services are mapped by winload; Start=1 is kernel system-start work. */
    if(d.Start!=0||(d.Type!=1&&d.Type!=2))return Rc95LdrSuccess;
    s=ReadStringValue(c->Hive,service,"ImagePath",d.ImagePath,sizeof(d.ImagePath));
    if(s==Rc95LdrNotFound){
        size_t n=strlen(d.ServiceName);
        size_t j;
        /* The fallback concatenates a registry key name into a path. Keep it
           a single filename component; never let a service identity inject
           a separator, drive prefix, or parent-directory traversal. */
        if(!strcmp(d.ServiceName,".")||!strcmp(d.ServiceName,".."))
            return Rc95LdrCorruptData;
        for(j=0;j<n;j++)
            if(d.ServiceName[j]=='\\'||d.ServiceName[j]=='/'||
               d.ServiceName[j]==':')return Rc95LdrCorruptData;
        if(n+22>=sizeof(d.ImagePath))return Rc95LdrOutOfBounds;
        memcpy(d.ImagePath,"System32\\drivers\\",17);
        memcpy(d.ImagePath+17,d.ServiceName,n);
        memcpy(d.ImagePath+17+n,".sys",5);
    }else if(s!=Rc95LdrSuccess)return s;
    if(!d.ImagePath[0])return Rc95LdrCorruptData;
    s=ReadStringValue(c->Hive,service,"Group",d.Group,sizeof(d.Group));
    if(s==Rc95LdrNotFound)d.Group[0]=0;else if(s!=Rc95LdrSuccess)return s;
    /* A NULL callback is the validation-only pass. */
    return c->Callback?c->Callback(c->Context,&d):Rc95LdrSuccess;
}
#define HIVE_MAX_GROUP_ORDER_BYTES 1028u
#define HIVE_MAX_SERVICE_GROUP_ORDER_BYTES 4098u

typedef struct _DRIVER_DEPENDENCY_CONTEXT {
    RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE *Workspace;
    uint32_t Dependent;
} DRIVER_DEPENDENCY_CONTEXT;

static RC95LDR_STATUS AddBootDriverDependency(void *opaque, const char *name)
{
    DRIVER_DEPENDENCY_CONTEXT *context =
        (DRIVER_DEPENDENCY_CONTEXT *)opaque;
    RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE *workspace = context->Workspace;
    uint32_t i, prerequisite = UINT32_MAX;
    for (i = 0; i < workspace->Count; ++i) {
        if (Eq(workspace->Drivers[i].ServiceName, name)) {
            prerequisite = i;
            break;
        }
    }
    /* A service dependency outside the boot-start driver set cannot be
       honored by this early loader phase. Do not silently discard it. */
    if (prerequisite == UINT32_MAX) return Rc95LdrUnsupported;
    if (prerequisite == context->Dependent) return Rc95LdrCorruptData;
    if (workspace->DependencyEdgeCount >=
        RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES *
        RC95LDR_SERVICE_MAX_DEPENDENCIES)
        return Rc95LdrUnsupported;
    workspace->DependencyEdges[workspace->DependencyEdgeCount].Prerequisite =
        prerequisite;
    workspace->DependencyEdges[workspace->DependencyEdgeCount].Dependent =
        context->Dependent;
    ++workspace->DependencyEdgeCount;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS BuildBootDriverDependencyOrder(
    const RC95LDR_SYSTEM_HIVE *h, uint32_t servicesCell,
    RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE *workspace)
{
    uint32_t i;
    RC95LDR_STATUS status;
    workspace->DependencyEdgeCount = 0;
    if (!workspace->Count) return Rc95LdrSuccess;
    for (i = 0; i < workspace->Count; ++i) {
        RC95LDR_HIVE_KEY service;
        RC95LDR_HIVE_VALUE value;
        uint8_t raw[HIVE_MAX_DEPENDENCY_VALUE_BYTES];
        size_t got = 0;
        DRIVER_DEPENDENCY_CONTEXT context;
        status = Rc95LdrHiveOpenKey(h, servicesCell,
                                    workspace->Drivers[i].ServiceName,
                                    &service);
        if (status != Rc95LdrSuccess) return status;
        status = Rc95LdrHiveGetValue(h, &service, "DependOnService", &value);
        if (status == Rc95LdrNotFound) continue;
        if (status != Rc95LdrSuccess) return status;
        if (value.Type != 7u || (value.DataLength & 1u) ||
            value.DataLength < 4u)
            return Rc95LdrCorruptData;
        if (value.DataLength > sizeof(raw)) return Rc95LdrUnsupported;
        status = Rc95LdrHiveReadValue(h, &value, raw, sizeof(raw), &got);
        if (status != Rc95LdrSuccess) return status;
        context.Workspace = workspace;
        context.Dependent = i;
        status = Rc95LdrEnumerateServiceDependencies(raw, got,
            AddBootDriverDependency, &context);
        if (status != Rc95LdrSuccess) return status;
    }
    status = Rc95LdrResolveServiceOrder(workspace->Count,
        workspace->DependencyEdges, workspace->DependencyEdgeCount,
        workspace->Order, RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES,
        workspace->SortWorkspace,
        RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES * 3u);
    if (status != Rc95LdrSuccess) return status;
    /* Apply source-at-destination permutation cycles in place. The order
       array doubles as the visited map, avoiding another 50 KiB driver array. */
    for (i = 0; i < workspace->Count; ++i) {
        if (workspace->Order[i] != i) {
            uint32_t position = i;
            RC95LDR_HIVE_BOOT_DRIVER saved = workspace->Drivers[i];
            while (workspace->Order[position] != i) {
                uint32_t source = workspace->Order[position];
                workspace->Drivers[position] = workspace->Drivers[source];
                workspace->Order[position] = position;
                position = source;
            }
            workspace->Drivers[position] = saved;
            workspace->Order[position] = position;
        }
    }
    return Rc95LdrSuccess;
}

typedef struct _DRIVER_COLLECT_CONTEXT {
    RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE *Workspace;
} DRIVER_COLLECT_CONTEXT;

static RC95LDR_STATUS CollectBootDriver(void *opaque,
    const RC95LDR_HIVE_BOOT_DRIVER *driver)
{
    DRIVER_COLLECT_CONTEXT *c=(DRIVER_COLLECT_CONTEXT*)opaque;
    uint32_t i;
    if(!c||!c->Workspace||!driver)return Rc95LdrInvalidParameter;
    if(c->Workspace->Count>=RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES)
        return Rc95LdrUnsupported;
    for(i=0;i<c->Workspace->Count;i++)
        if(Eq(c->Workspace->Drivers[i].ServiceName,driver->ServiceName))
            return Rc95LdrCorruptData;
    c->Workspace->Drivers[c->Workspace->Count++]=*driver;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS ReadGroupRank(const RC95LDR_SYSTEM_HIVE *h,
    const RC95LDR_HIVE_KEY *control,const char *group,uint32_t *rank)
{
    RC95LDR_HIVE_KEY key;
    RC95LDR_HIVE_VALUE value;
    uint8_t raw[HIVE_MAX_SERVICE_GROUP_ORDER_BYTES];
    size_t got;
    RC95LDR_STATUS s;
    if(!group[0])return Rc95LdrNotFound;
    {
        RC95LDR_HIVE_KEY controlKey;
        s=Rc95LdrHiveOpenKey(h,control->Cell,"Control",&controlKey);
        if(s!=Rc95LdrSuccess)return s;
        s=Rc95LdrHiveOpenKey(h,controlKey.Cell,"ServiceGroupOrder",&key);
        if(s!=Rc95LdrSuccess)return s;
    }
    s=Rc95LdrHiveGetValue(h,&key,"List",&value);
    if(s!=Rc95LdrSuccess)return s;
    if(value.Type!=7u||value.DataLength>sizeof(raw))
        return value.DataLength>sizeof(raw)?Rc95LdrUnsupported:Rc95LdrCorruptData;
    s=Rc95LdrHiveReadValue(h,&value,raw,sizeof(raw),&got);
    if(s!=Rc95LdrSuccess)return s;
    return Rc95LdrFindServiceGroupOrder(raw,got,group,rank);
}

static RC95LDR_STATUS ReadTagRank(const RC95LDR_SYSTEM_HIVE *h,
    const char *group,uint32_t tag,uint32_t *rank)
{
    RC95LDR_HIVE_KEY key;
    RC95LDR_HIVE_VALUE value;
    uint8_t raw[HIVE_MAX_GROUP_ORDER_BYTES];
    size_t got;
    RC95LDR_STATUS s;
    if(!group[0]||!tag)return Rc95LdrNotFound;
    {
        RC95LDR_HIVE_KEY controlKey;
        RC95LDR_HIVE_KEY controlSet;
        s=Rc95LdrHiveResolveCurrentControlSet(h,&controlSet);
        if(s!=Rc95LdrSuccess)return s;
        s=Rc95LdrHiveOpenKey(h,controlSet.Cell,"Control",&controlKey);
        if(s!=Rc95LdrSuccess)return s;
        s=Rc95LdrHiveOpenKey(h,controlKey.Cell,"GroupOrderList",&key);
        if(s!=Rc95LdrSuccess)return s;
    }
    s=Rc95LdrHiveGetValue(h,&key,group,&value);
    if(s==Rc95LdrNotFound)return s;
    if(s!=Rc95LdrSuccess)return s;
    if(value.Type!=3u||value.DataLength>sizeof(raw))
        return value.DataLength>sizeof(raw)?Rc95LdrUnsupported:Rc95LdrCorruptData;
    s=Rc95LdrHiveReadValue(h,&value,raw,sizeof(raw),&got);
    if(s!=Rc95LdrSuccess)return s;
    return Rc95LdrFindServiceTagOrder(raw,got,tag,rank);
}

static RC95LDR_STATUS OrderBootDriverCandidates(
    const RC95LDR_SYSTEM_HIVE *h,RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE *c)
{
    RC95LDR_HIVE_KEY control;
    uint32_t i,j;
    RC95LDR_STATUS s=Rc95LdrHiveResolveCurrentControlSet(h,&control);
    if(s!=Rc95LdrSuccess)return s;
    for(i=0;i<c->Count;i++){
        c->GroupRanks[i]=UINT32_MAX;
        c->TagRanks[i]=UINT32_MAX;
        if(c->Drivers[i].Group[0]){
            s=ReadGroupRank(h,&control,c->Drivers[i].Group,&c->GroupRanks[i]);
            if(s==Rc95LdrNotFound)c->GroupRanks[i]=UINT32_MAX;
            else if(s!=Rc95LdrSuccess)return s;
            if(c->Drivers[i].Tag){
                s=ReadTagRank(h,c->Drivers[i].Group,c->Drivers[i].Tag,&c->TagRanks[i]);
                if(s==Rc95LdrNotFound)c->TagRanks[i]=UINT32_MAX;
                else if(s!=Rc95LdrSuccess)return s;
            }
        }
    }
    /* Stable insertion sort: listed groups precede unlisted groups; within
       each group, listed tags precede unlisted tags. Original hive order is
       retained for ties and for services without a configured rank. */
    for(i=1;i<c->Count;i++){
        RC95LDR_HIVE_BOOT_DRIVER driver=c->Drivers[i];
        uint32_t gr=c->GroupRanks[i],tr=c->TagRanks[i];
        j=i;
        while(j>0){
            uint32_t p=j-1;
            int sameGroup=Eq(c->Drivers[p].Group,driver.Group);
            int before=(c->GroupRanks[p]>gr)||
                (c->GroupRanks[p]==gr&&sameGroup&&c->TagRanks[p]>tr);
            if(!before)break;
            c->Drivers[j]=c->Drivers[p];
            c->GroupRanks[j]=c->GroupRanks[p];
            c->TagRanks[j]=c->TagRanks[p];
            --j;
        }
        c->Drivers[j]=driver;
        c->GroupRanks[j]=gr;
        c->TagRanks[j]=tr;
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrHiveEnumerateBootDriversWithWorkspace(
    const RC95LDR_SYSTEM_HIVE*h,RC95LDR_HIVE_BOOT_DRIVER_CALLBACK cb,void*ctx,
    RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE *workspace)
{
    RC95LDR_HIVE_KEY control,services;
    DRIVER_ENUM_CONTEXT validation;
    DRIVER_COLLECT_CONTEXT collected;
    RC95LDR_STATUS s;
    uint32_t i;
    if(!h||!cb||!workspace)return Rc95LdrInvalidParameter;
    s=Rc95LdrHiveResolveCurrentControlSet(h,&control);if(s!=Rc95LdrSuccess)return s;
    s=Rc95LdrHiveOpenKey(h,control.Cell,"Services",&services);if(s!=Rc95LdrSuccess)return s;
    validation.Hive=h;validation.ServicesCell=services.Cell;
    validation.Callback=0;validation.Context=0;
    /* Preflight the entire Services tree before collecting or exposing any
       candidates. DependOnService names are validated here and boot-driver
       edges are resolved after collection; non-empty DependOnGroup values
       remain fail-closed until group expansion is implemented. */
    s=Rc95LdrHiveEnumerateSubkeys(h,&services,VisitService,&validation);
    if(s!=Rc95LdrSuccess)return s;
    memset(workspace,0,sizeof(*workspace));
    collected.Workspace=workspace;
    validation.Callback=CollectBootDriver;validation.Context=&collected;
    s=Rc95LdrHiveEnumerateSubkeys(h,&services,VisitService,&validation);
    if(s!=Rc95LdrSuccess)return s;
    s=OrderBootDriverCandidates(h,workspace);
    if(s!=Rc95LdrSuccess)return s;
    s=BuildBootDriverDependencyOrder(h,services.Cell,workspace);
    if(s!=Rc95LdrSuccess)return s;
    for(i=0;i<workspace->Count;i++){
        s=cb(ctx,&workspace->Drivers[i]);
        if(s!=Rc95LdrSuccess)return s;
    }
    return Rc95LdrSuccess;
}

/* Compatibility entry point. Firmware callers should provide persistent
 * scratch through the workspace API to avoid this large automatic object. */
RC95LDR_STATUS Rc95LdrHiveEnumerateBootDrivers(const RC95LDR_SYSTEM_HIVE *h,
    RC95LDR_HIVE_BOOT_DRIVER_CALLBACK cb,void *ctx)
{
    RC95LDR_HIVE_BOOT_DRIVER_WORKSPACE workspace;
    return Rc95LdrHiveEnumerateBootDriversWithWorkspace(h,cb,ctx,&workspace);
}
