#include "rc95ldrsystemhive.h"
#include "rc95ldrntprepare.h"
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

static uint16_t U16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t U32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static int Eq(const char *a,const char *b) { while (*a && *b) { if (tolower((unsigned char)*a++)!=tolower((unsigned char)*b++)) return 0; } return *a==0 && *b==0; }
static int Cell(const RC95LDR_SYSTEM_HIVE *h,uint32_t off,const uint8_t **p,uint32_t *len)
{
    uint32_t pos,size,binBase,binSize,within; int32_t signedSize;
    if (!h || off==0xffffffffu || off>=h->DataLength || off>UINT32_MAX-HBIN_BASE) return 0;
    pos=HBIN_BASE+off; binBase=HBIN_BASE+(off&~0xfffu); within=off&0xfffu;
    if (pos>h->Size-4 || binBase>h->Size-0x20 || memcmp(h->Bytes+binBase,"hbin",4)!=0) return 0;
    binSize=U32(h->Bytes+binBase+8);
    if (binSize<0x1000 || within<0x20 || within>=binSize) return 0;
    signedSize=(int32_t)U32(h->Bytes+pos);
    if (signedSize>=0 || signedSize==INT32_MIN) return 0; /* positive cells are free */
    size=(uint32_t)(signedSize<0?-signedSize:signedSize);
    if (size<8 || (size&7u) || size>h->Size-pos || size>binSize-within) return 0;
    *p=h->Bytes+pos+4; *len=size-4; return 1;
}
static void DecodeName(const uint8_t *p,uint16_t n,int compressed,char *out,size_t cap)
{
    size_t i,count=n;
    if (compressed) { if (count>=cap) count=cap-1; for(i=0;i<count;i++) out[i]=(char)p[i]; }
    else { count=n/2; if(count>=cap)count=cap-1; for(i=0;i<count;i++){ uint16_t c=U16(p+i*2); out[i]=(c<128)?(char)c:'?'; } }
    out[count]=0;
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
    DecodeName(p+0x4c,nameLen,(k->Flags&NK_COMPRESSED)!=0,k->Name,sizeof(k->Name));
    if(k->SubkeyCount>65536u||k->VolatileSubkeyCount>65536u||k->SubkeyCount>65536u-k->VolatileSubkeyCount||k->ValueCount>65536u)return Rc95LdrCorruptData;
    return Rc95LdrSuccess;
}
static RC95LDR_STATUS FindSubkey(const RC95LDR_SYSTEM_HIVE *h,uint32_t list,const char *name,uint32_t *found,unsigned depth)
{
    const uint8_t *p;uint32_t n,i,entries;uint16_t sig;
    if(depth>8||!Cell(h,list,&p,&n)||n<4)return Rc95LdrCorruptData;
    sig=U16(p);entries=U16(p+2);
    if(sig==REG_RI){
        if(entries>(n-4)/4)return Rc95LdrCorruptData;
        for(i=0;i<entries;i++){RC95LDR_STATUS s=FindSubkey(h,U32(p+4+i*4),name,found,depth+1);if(s==Rc95LdrSuccess)return s;if(s!=Rc95LdrNotFound)return s;}
        return Rc95LdrNotFound;
    }
    if(sig!=REG_LI&&sig!=REG_LF&&sig!=REG_LH)return Rc95LdrCorruptData;
    if(entries>(n-4)/(sig==REG_LF||sig==REG_LH?8u:4u))return Rc95LdrCorruptData;
    for(i=0;i<entries;i++){
        uint32_t child=U32(p+4+i*(sig==REG_LF||sig==REG_LH?8u:4u));
        RC95LDR_HIVE_KEY k;RC95LDR_STATUS s=ReadKey(h,child,&k);
        if(s!=Rc95LdrSuccess)return s;
        if(Eq(k.Name,name)){*found=child;return Rc95LdrSuccess;}
    }
    return Rc95LdrNotFound;
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
    if(!p.SubkeyCount)return Rc95LdrNotFound;
    s=Rc95LdrNotFound;
    if(p.SubkeyCount){s=FindSubkey(h,p.SubkeyList,name,&child,0);if(s!=Rc95LdrSuccess&&s!=Rc95LdrNotFound)return s;}
    if(s==Rc95LdrNotFound&&p.VolatileSubkeyCount){s=FindSubkey(h,p.VolatileSubkeyList,name,&child,0);if(s!=Rc95LdrSuccess&&s!=Rc95LdrNotFound)return s;}
    if(s!=Rc95LdrSuccess)return s;
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
    const uint8_t *list,*p;uint32_t len,i,off;uint16_t nl,flags;char decoded[RC95LDR_HIVE_MAX_NAME_CHARS+1];
    if(!h||!k||!name||!v)return Rc95LdrInvalidParameter;
    if(!k->ValueCount)return Rc95LdrNotFound;
    if(!Cell(h,k->ValueList,&list,&len)||len<k->ValueCount*4u)return Rc95LdrCorruptData;
    for(i=0;i<k->ValueCount;i++){
        off=U32(list+i*4);if(!Cell(h,off,&p,&len)||len<0x14||U16(p)!=REG_VK)return Rc95LdrCorruptData;
        nl=U16(p+2);if(nl>len-0x14)return Rc95LdrCorruptData;flags=U16(p+0x10);if(!(flags&VK_COMPRESSED)&&(nl&1u))return Rc95LdrCorruptData;
        DecodeName(p+0x14,nl,(flags&VK_COMPRESSED)!=0,decoded,sizeof(decoded));
        if(Eq(decoded,name)){memset(v,0,sizeof(*v));v->Cell=off;v->NameLength=nl;memcpy(v->Name,decoded,strlen(decoded)+1);v->DataInline=(U32(p+4)&0x80000000u)!=0;v->DataLength=U32(p+4)&0x7fffffffu;v->DataCell=U32(p+8);v->Type=U32(p+0xc);return Rc95LdrSuccess;}
    }
    return Rc95LdrNotFound;
}
RC95LDR_STATUS Rc95LdrHiveReadValue(const RC95LDR_SYSTEM_HIVE *h,const RC95LDR_HIVE_VALUE *v,void *buffer,size_t cap,size_t *read)
{
    const uint8_t *p;uint32_t len,n=v?v->DataLength:0;
    if(!h||!v||!read||(!buffer&&n))return Rc95LdrInvalidParameter;
    *read=n;if(n>cap)return Rc95LdrBufferTooSmall;if(!n)return Rc95LdrSuccess;
    if(v->DataInline){ /* Small values are stored in the four-byte data-offset field. */ uint32_t x=v->DataCell;uint8_t raw[4]={(uint8_t)x,(uint8_t)(x>>8),(uint8_t)(x>>16),(uint8_t)(x>>24)};if(n>4)return Rc95LdrCorruptData;memcpy(buffer,raw,n);return Rc95LdrSuccess;}
    if(!Cell(h,v->DataCell,&p,&len)||n>len)return Rc95LdrCorruptData;
    memcpy(buffer,p,n);return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrHiveGetDword(const RC95LDR_SYSTEM_HIVE *h,const RC95LDR_HIVE_KEY *k,const char *name,uint32_t *data)
{
    RC95LDR_HIVE_VALUE v;uint8_t b[4];size_t n;RC95LDR_STATUS s;
    if(!data)return Rc95LdrInvalidParameter;s=Rc95LdrHiveGetValue(h,k,name,&v);if(s!=Rc95LdrSuccess)return s;
    if(v.Type!=4||v.DataLength!=4)return Rc95LdrCorruptData;s=Rc95LdrHiveReadValue(h,&v,b,sizeof(b),&n);if(s==Rc95LdrSuccess)*data=U32(b);return s;
}
RC95LDR_STATUS Rc95LdrHiveResolveCurrentControlSet(const RC95LDR_SYSTEM_HIVE *h,RC95LDR_HIVE_KEY *set)
{
    RC95LDR_HIVE_KEY select;uint32_t current;char name[32];RC95LDR_STATUS s;
    if(!h||!set)return Rc95LdrInvalidParameter;s=Rc95LdrHiveOpenPath(h,"Select",&select);if(s!=Rc95LdrSuccess)return s;
    s=Rc95LdrHiveGetDword(h,&select,"Current",&current);if(s!=Rc95LdrSuccess)return s;
    if(current==0||current>999)return Rc95LdrCorruptData;
    if(current<10)snprintf(name,sizeof(name),"ControlSet00%u",current);
    else if(current<100)snprintf(name,sizeof(name),"ControlSet0%u",current);
    else snprintf(name,sizeof(name),"ControlSet%u",current);
    return Rc95LdrHiveOpenPath(h,name,set);
}

static RC95LDR_STATUS EnumerateList(const RC95LDR_SYSTEM_HIVE *h,uint32_t off,
    RC95LDR_HIVE_SUBKEY_CALLBACK cb,void *ctx,unsigned depth,uint32_t *seen)
{
    const uint8_t *p;uint32_t n,i,count;uint16_t sig;RC95LDR_STATUS s;
    if(depth>8||!Cell(h,off,&p,&n)||n<4)return Rc95LdrCorruptData;
    sig=U16(p);count=U16(p+2);
    if(sig==REG_RI){
        if(count>(n-4)/4)return Rc95LdrCorruptData;
        for(i=0;i<count;i++){s=EnumerateList(h,U32(p+4+i*4),cb,ctx,depth+1,seen);if(s!=Rc95LdrSuccess)return s;}
        return Rc95LdrSuccess;
    }
    if(sig!=REG_LI&&sig!=REG_LF&&sig!=REG_LH)return Rc95LdrCorruptData;
    if(count>(n-4)/(sig==REG_LF||sig==REG_LH?8u:4u))return Rc95LdrCorruptData;
    if(*seen>65536u-count)return Rc95LdrCorruptData;
    for(i=0;i<count;i++){
        RC95LDR_HIVE_KEY key;uint32_t child=U32(p+4+i*(sig==REG_LF||sig==REG_LH?8u:4u));
        s=ReadKey(h,child,&key);if(s!=Rc95LdrSuccess)return s;
        ++(*seen);
        if(cb){s=cb(ctx,&key);if(s!=Rc95LdrSuccess)return s;}
    }
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrHiveEnumerateSubkeys(const RC95LDR_SYSTEM_HIVE*h,
    const RC95LDR_HIVE_KEY*parent,RC95LDR_HIVE_SUBKEY_CALLBACK cb,void*ctx)
{
    uint32_t seen=0;RC95LDR_STATUS s;
    if(!h||!parent||!cb)return Rc95LdrInvalidParameter;
    if(parent->SubkeyCount){
        s=EnumerateList(h,parent->SubkeyList,0,0,0,&seen);if(s!=Rc95LdrSuccess)return s;
    }
    if(parent->VolatileSubkeyCount){
        s=EnumerateList(h,parent->VolatileSubkeyList,0,0,0,&seen);if(s!=Rc95LdrSuccess)return s;
    }
    if(seen!=parent->SubkeyCount+parent->VolatileSubkeyCount)return Rc95LdrCorruptData;
    seen=0;
    if(parent->SubkeyCount){s=EnumerateList(h,parent->SubkeyList,cb,ctx,0,&seen);if(s!=Rc95LdrSuccess)return s;}
    if(parent->VolatileSubkeyCount){s=EnumerateList(h,parent->VolatileSubkeyList,cb,ctx,0,&seen);if(s!=Rc95LdrSuccess)return s;}
    return seen==parent->SubkeyCount+parent->VolatileSubkeyCount?Rc95LdrSuccess:Rc95LdrCorruptData;
}
static RC95LDR_STATUS ReadStringValue(const RC95LDR_SYSTEM_HIVE*h,
    const RC95LDR_HIVE_KEY*k,const char*name,char*out,size_t cap)
{
    RC95LDR_HIVE_VALUE v;uint8_t raw[RC95LDR_HIVE_MAX_NAME_CHARS*2+2];size_t got,i,n;RC95LDR_STATUS s;
    if(!cap)return Rc95LdrInvalidParameter;
    s=Rc95LdrHiveGetValue(h,k,name,&v);if(s!=Rc95LdrSuccess)return s;
    if((v.Type!=1&&v.Type!=2)||(v.DataLength&1u)||v.DataLength>sizeof(raw))return Rc95LdrCorruptData;
    s=Rc95LdrHiveReadValue(h,&v,raw,sizeof(raw),&got);if(s!=Rc95LdrSuccess)return s;
    n=got/2;if(n>=cap)return Rc95LdrOutOfBounds;
    for(i=0;i<n;i++){uint16_t ch=U16(raw+i*2);out[i]=(ch<128)?(char)ch:'?';}
    out[n]=0;return Rc95LdrSuccess;
}
static RC95LDR_STATUS OptionalDword(const RC95LDR_SYSTEM_HIVE*h,
    const RC95LDR_HIVE_KEY*k,const char*name,uint32_t*value,uint32_t fallback)
{
    RC95LDR_STATUS s=Rc95LdrHiveGetDword(h,k,name,value);
    if(s==Rc95LdrNotFound){*value=fallback;return Rc95LdrSuccess;}return s;
}
typedef struct _DRIVER_ENUM_CONTEXT {
    const RC95LDR_SYSTEM_HIVE *Hive;
    RC95LDR_HIVE_BOOT_DRIVER_CALLBACK Callback;
    void *Context;
} DRIVER_ENUM_CONTEXT;
static RC95LDR_STATUS VisitService(void *opaque,const RC95LDR_HIVE_KEY *service)
{
    DRIVER_ENUM_CONTEXT *c=(DRIVER_ENUM_CONTEXT*)opaque;RC95LDR_HIVE_BOOT_DRIVER d;
    RC95LDR_STATUS s;memset(&d,0,sizeof(d));
    if(strlen(service->Name)>=sizeof(d.ServiceName))return Rc95LdrOutOfBounds;
    memcpy(d.ServiceName,service->Name,strlen(service->Name)+1);
    s=OptionalDword(c->Hive,service,"Start",&d.Start,3);if(s!=Rc95LdrSuccess)return s;
    s=OptionalDword(c->Hive,service,"Type",&d.Type,0);if(s!=Rc95LdrSuccess)return s;
    s=OptionalDword(c->Hive,service,"ErrorControl",&d.ErrorControl,1);if(s!=Rc95LdrSuccess)return s;
    s=OptionalDword(c->Hive,service,"Tag",&d.Tag,0);if(s!=Rc95LdrSuccess)return s;
    if(d.Start>1||(d.Type!=1&&d.Type!=2))return Rc95LdrSuccess;
    s=ReadStringValue(c->Hive,service,"ImagePath",d.ImagePath,sizeof(d.ImagePath));
    if(s==Rc95LdrNotFound){
        size_t n=strlen(d.ServiceName);
        if(n+22>=sizeof(d.ImagePath))return Rc95LdrOutOfBounds;
        memcpy(d.ImagePath,"System32\\drivers\\",17);
        memcpy(d.ImagePath+17,d.ServiceName,n);
        memcpy(d.ImagePath+17+n,".sys",5);
    }else if(s!=Rc95LdrSuccess)return s;
    s=ReadStringValue(c->Hive,service,"Group",d.Group,sizeof(d.Group));
    if(s==Rc95LdrNotFound)d.Group[0]=0;else if(s!=Rc95LdrSuccess)return s;
    return c->Callback(c->Context,&d);
}
RC95LDR_STATUS Rc95LdrHiveEnumerateBootDrivers(const RC95LDR_SYSTEM_HIVE*h,
    RC95LDR_HIVE_BOOT_DRIVER_CALLBACK cb,void*ctx)
{
    RC95LDR_HIVE_KEY control,services;DRIVER_ENUM_CONTEXT c;RC95LDR_STATUS s;
    if(!h||!cb)return Rc95LdrInvalidParameter;
    s=Rc95LdrHiveResolveCurrentControlSet(h,&control);if(s!=Rc95LdrSuccess)return s;
    s=Rc95LdrHiveOpenKey(h,control.Cell,"Services",&services);if(s!=Rc95LdrSuccess)return s;
    c.Hive=h;c.Callback=cb;c.Context=ctx;
    return Rc95LdrHiveEnumerateSubkeys(h,&services,VisitService,&c);
}
