#include "rc95bcd.h"
#include "rc95bcdwalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HIVE_SIZE 8192u
#define BIN 4096u
#define ROOT 32u
#define INDEX 120u
#define CHILD 136u
static void p16(unsigned char*p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void p32(unsigned char*p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static unsigned sum(const unsigned char*p){unsigned s=0,i;for(i=0;i<0x1fc;i+=4)s^=(unsigned)p[i]|((unsigned)p[i+1]<<8)|((unsigned)p[i+2]<<16)|((unsigned)p[i+3]<<24);if(s==0xffffffffu)s=0xfffffffeu;else if(!s)s=1;return s;}
static void cell(unsigned char*b,unsigned o,unsigned n,int used){p32(b+o,used?(0u-n):n);}
static void key(unsigned char*b,unsigned o,const char*n,unsigned count,unsigned list){unsigned char*q=b+o+4;size_t z=strlen(n);p16(q,0x6b6e);p16(q+2,0x20);p32(q+20,count);p32(q+28,list);p32(q+36,0);p32(q+40,0xffffffffu);p16(q+72,(unsigned)z);memcpy(q+76,n,z);}
static void make(unsigned char*h);
static void key_ex(unsigned char*b,unsigned o,const char*n,unsigned sc,unsigned sl,unsigned vc,unsigned vl){key(b,o,n,sc,sl);p32(b+o+4+36,vc);p32(b+o+4+40,vl);}
static int enum_seen;
static RC95LDR_STATUS enum_element(unsigned id,const RC95BCD_VALUE*v,void*ctx){RC95BCD_ELEMENT_VIEW e;(void)ctx;if(id!=0x12000030u||v->Type!=3u||v->Length!=8u)return Rc95LdrCorruptData;if(Rc95BcdDecodeElement(id,v,&e)!=Rc95LdrSuccess||e.Kind!=Rc95BcdElementString||e.StringCharacters!=3u)return Rc95LdrCorruptData;enum_seen++;return Rc95LdrSuccess;}
static void make_elements(unsigned char*h){unsigned char*b=h+BIN;static const unsigned char payload[8]={0x2d,0,0x6e,0,0x6f,0,0,0};make(h);key_ex(b,CHILD,"Objects",1,224,0,0xffffffffu);cell(b,224,16,1);p16(b+228,0x696c);p16(b+230,1);p32(b+232,240);cell(b,240,88,1);key_ex(b,240,"Elements",1,328,0,0xffffffffu);cell(b,328,16,1);p16(b+332,0x696c);p16(b+334,1);p32(b+336,344);cell(b,344,88,1);key_ex(b,344,"12000030",0,0xffffffffu,1,432);cell(b,432,8,1);p32(b+436,448);cell(b,440,8,0);cell(b,448,40,1);p16(b+452,0x6b76);p16(b+454,7);p32(b+456,8);p32(b+460,520);p32(b+464,3);p16(b+468,1);memcpy(b+472,"Element",7);cell(b,488,32,0);cell(b,520,16,1);memcpy(b+524,payload,8);cell(b,536,4096-536,0);p32(h+0x1fc,sum(h));}

typedef struct _TEST_HIVE_BUILDER { unsigned char *Bin; unsigned Cursor; } TEST_HIVE_BUILDER;
static unsigned test_alloc(TEST_HIVE_BUILDER *t,unsigned bytes)
{
    unsigned total=(bytes+4u+7u)&~7u,off=t->Cursor;
    if(total<8u || off>4096u || total>4096u-off) return 0xffffffffu;
    cell(t->Bin,off,total,1); memset(t->Bin+off+4,0,total-4); t->Cursor+=total;
    return off;
}
static unsigned test_index(TEST_HIVE_BUILDER*t,const unsigned *children,unsigned count)
{
    unsigned off=test_alloc(t,4u+count*4u),i;
    if(off==0xffffffffu)return off;
    p16(t->Bin+off+4,0x696c);p16(t->Bin+off+6,count);
    for(i=0;i<count;i++)p32(t->Bin+off+8+i*4,children[i]);
    return off;
}
static unsigned test_key(TEST_HIVE_BUILDER*t,const char*name,unsigned subCount,unsigned subList,unsigned valueCount,unsigned valueList)
{
    unsigned n=(unsigned)strlen(name),off=test_alloc(t,76u+n);unsigned char*q;
    if(off==0xffffffffu)return off;
    q=t->Bin+off+4;p16(q,0x6b6e);p16(q+2,0x20);
    p32(q+20,subCount);p32(q+28,subList);p32(q+36,valueCount);p32(q+40,valueList);
    p16(q+72,n);memcpy(q+76,name,n);return off;
}
static unsigned test_value_element(TEST_HIVE_BUILDER*t,unsigned id,const unsigned char*data,unsigned length)
{
    char name[9];static const char hex[]="0123456789abcdef";
    unsigned dataOff,vkOff,listOff,keyOff,i;
    for(i=0;i<8;i++)name[i]=hex[(id>>((7-i)*4))&15u];name[8]=0;
    dataOff=test_alloc(t,length);if(dataOff==0xffffffffu)return dataOff;
    memcpy(t->Bin+dataOff+4,data,length);
    vkOff=test_alloc(t,27);if(vkOff==0xffffffffu)return vkOff;
    {unsigned char*q=t->Bin+vkOff+4;p16(q,0x6b76);p16(q+2,7);p32(q+4,length);p32(q+8,dataOff);p32(q+12,3);p16(q+16,1);memcpy(q+20,"Element",7);}
    listOff=test_alloc(t,4);if(listOff==0xffffffffu)return listOff;
    p32(t->Bin+listOff+4,vkOff);
    keyOff=test_key(t,name,0,0xffffffffu,1,listOff);
    return keyOff;
}
static unsigned test_ascii_utf16(TEST_HIVE_BUILDER*t,const char*s)
{
    (void)t;
    return ((unsigned)strlen(s)+1u)*2u;
}
static unsigned test_build_loader_object(TEST_HIVE_BUILDER*t,const char*guidName,int omitSystemRoot)
{
    static const unsigned char device[12]={2,0,0,0,1,0,0,0,0xaa,0xbb,0xcc,0xdd};
    unsigned elements[5],index,off,child,object,len;
    unsigned char *tmp;
    len=test_ascii_utf16(t,"\\Windows\\System32\\winload.efi");
    tmp=(unsigned char*)malloc(len);if(!tmp)return 0xffffffffu;
    {unsigned i;const char*s="\\Windows\\System32\\winload.efi";for(i=0;i<len/2-1;i++){tmp[i*2]=(unsigned char)s[i];tmp[i*2+1]=0;}tmp[len-2]=0;tmp[len-1]=0;}
    elements[0]=test_value_element(t,0x11000001u,device,sizeof(device));
    elements[1]=test_value_element(t,0x12000002u,tmp,len);free(tmp);
    elements[2]=test_value_element(t,0x21000001u,device,sizeof(device));
    if(elements[0]==0xffffffffu||elements[1]==0xffffffffu||elements[2]==0xffffffffu)return 0xffffffffu;
    if(!omitSystemRoot){
        len=test_ascii_utf16(t,"\\Windows");tmp=(unsigned char*)malloc(len);if(!tmp)return 0xffffffffu;
        {unsigned i;const char*s="\\Windows";for(i=0;i<len/2-1;i++){tmp[i*2]=(unsigned char)s[i];tmp[i*2+1]=0;}tmp[len-2]=0;tmp[len-1]=0;}
        elements[3]=test_value_element(t,0x22000002u,tmp,len);free(tmp);
        if(elements[3]==0xffffffffu)return 0xffffffffu;
    }
    len=test_ascii_utf16(t,"-no");tmp=(unsigned char*)malloc(len);if(!tmp)return 0xffffffffu;
    tmp[0]='-';tmp[1]=0;tmp[2]='n';tmp[3]=0;tmp[4]='o';tmp[5]=0;tmp[6]=0;tmp[7]=0;
    elements[omitSystemRoot?3:4]=test_value_element(t,0x12000030u,tmp,len);free(tmp);
    if(elements[omitSystemRoot?3:4]==0xffffffffu)return 0xffffffffu;
    index=test_index(t,elements,omitSystemRoot?4u:5u);
    if(index==0xffffffffu)return 0xffffffffu;
    off=test_key(t,"Elements",omitSystemRoot?4u:5u,index,0,0xffffffffu);
    if(off==0xffffffffu)return off;
    {unsigned a[1]={off};child=test_index(t,a,1);}
    if(child==0xffffffffu)return 0xffffffffu;
    object=test_key(t,guidName,1,child,0,0xffffffffu);
    return object;
}
static int make_candidate_hive(unsigned char*h,int duplicateOrder,int omitSystemRoot,int twoCandidates)
{
    static const unsigned char guid1[16]={0x78,0x56,0x34,0x12,0x34,0x12,0xcd,0xab,1,2,3,4,5,6,7,8};
    static const unsigned char guid2[16]={0xef,0xcd,0xab,0x90,0x78,0x56,0x34,0x12,9,10,11,12,13,14,15,16};
    unsigned char order[36]={0},*b;
    unsigned managerElements[2],objectKeys[3],rootChildren[1];
    unsigned managerIndex,objectsIndex,rootIndex;
    unsigned managerObj,loaderObj,objectsObj,rootObj,elemIndex;
    unsigned loader2Obj=0xffffffffu,off;TEST_HIVE_BUILDER t;
    unsigned orderCount=(duplicateOrder||twoCandidates)?2u:1u;
    memset(h,0,HIVE_SIZE);b=h+BIN;t.Bin=b;t.Cursor=32;
    p32(order,orderCount);memcpy(order+4,guid1,16);
    if(orderCount==2)memcpy(order+20,duplicateOrder?guid1:guid2,16);
    managerElements[0]=test_value_element(&t,0x23000003u,guid1,16);
    managerElements[1]=test_value_element(&t,0x24000001u,order,4u+orderCount*16u);
    if(managerElements[0]==0xffffffffu||managerElements[1]==0xffffffffu)return 0;
    managerIndex=test_index(&t,managerElements,2);
    if(managerIndex==0xffffffffu)return 0;
    off=test_key(&t,"Elements",2,managerIndex,0,0xffffffffu);
    if(off==0xffffffffu)return 0;
    {unsigned a[1]={off};elemIndex=test_index(&t,a,1);}
    managerObj=test_key(&t,"{9dea862c-5cdd-4e70-acc1-f32b344d4795}",1,elemIndex,0,0xffffffffu);
    if(managerObj==0xffffffffu)return 0;

    loaderObj=test_build_loader_object(&t,
        "{12345678-1234-abcd-0102-030405060708}",omitSystemRoot);
    if(loaderObj==0xffffffffu)return 0;
    if(twoCandidates&&!duplicateOrder){
        loader2Obj=test_build_loader_object(&t,
            "{90abcdef-5678-1234-090a-0b0c0d0e0f10}",0);
        if(loader2Obj==0xffffffffu)return 0;
    }
    objectKeys[0]=managerObj;objectKeys[1]=loaderObj;
    if(loader2Obj!=0xffffffffu)objectKeys[2]=loader2Obj;
    objectsIndex=test_index(&t,objectKeys,loader2Obj!=0xffffffffu?3u:2u);
    objectsObj=test_key(&t,"Objects",loader2Obj!=0xffffffffu?3u:2u,objectsIndex,0,0xffffffffu);
    rootChildren[0]=objectsObj;rootIndex=test_index(&t,rootChildren,1);
    rootObj=test_key(&t,"ROOT",1,rootIndex,0,0xffffffffu);
    if(rootObj==0xffffffffu||t.Cursor>4096u)return 0;
    cell(b,t.Cursor,4096u-t.Cursor,0);
    p32(h,0x66676572);p32(h+4,1);p32(h+8,1);p32(h+0x14,1);p32(h+0x18,5);
    p32(h+0x1c,0);p32(h+0x20,1);p32(h+0x24,rootObj);p32(h+0x28,4096);
    p32(b,0x6e696268);p32(b+4,0);p32(b+8,4096);p32(h+0x1fc,sum(h));
    return 1;
}
static int candidate_view_uses_owned_payload(const RC95LDR_BCD_BOOT_CANDIDATE*c,uint32_t id,const RC95BCD_ELEMENT_VIEW*v)
{
    uint32_t i;
    for(i=0;i<c->ElementCount;i++)if(c->Elements[i].ElementId==id)
        return v->Data==c->ElementPayloadStorage+c->Elements[i].PayloadOffset &&
               v->Length==c->Elements[i].PayloadLength &&
               v->DeviceData==v->Data+8;
    return 0;
}
static int test_candidate_collection(void)
{
    unsigned char*h=(unsigned char*)malloc(HIVE_SIZE);RC95BCD_HIVE hive;
    RC95LDR_BCD_BOOT_CANDIDATE candidates[2];size_t count=0;RC95LDR_STATUS s;
    static const uint16_t loader[]={ '\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','w','i','n','l','o','a','d','.','e','f','i',0 };
    static const uint16_t root[]={ '\\','W','i','n','d','o','w','s',0 };
    if(!h)return 0;
    if(!make_candidate_hive(h,0,0,0)){free(h);return 0;}
    s=Rc95BcdOpenHive(h,HIVE_SIZE,&hive);
    if(s==Rc95LdrSuccess)s=Rc95BcdCollectWindowsBootCandidates(&hive,candidates,1,&count);
    if(s!=Rc95LdrSuccess||count!=1||!candidates[0].IsDefault||
       !candidates[0].IsWindowsLoader||!candidates[0].DeviceElement||
       !candidates[0].OsDeviceElement||candidates[0].ElementCount!=5||
       candidates[0].DeviceElement->Kind!=Rc95BcdElementDevice||
       candidates[0].OsDeviceElement->Kind!=Rc95BcdElementDevice||
       candidates[0].DeviceElement->DeviceType!=2u||
       !candidate_view_uses_owned_payload(&candidates[0],0x11000001u,candidates[0].DeviceElement)||
       !candidate_view_uses_owned_payload(&candidates[0],0x21000001u,candidates[0].OsDeviceElement)||
       candidates[0].LoaderPathCharacters!=sizeof(loader)/sizeof(loader[0])-1||
       candidates[0].SystemRootCharacters!=sizeof(root)/sizeof(root[0])-1||
       memcmp(candidates[0].LoaderPathUtf16,loader,sizeof(loader))||
       memcmp(candidates[0].SystemRootUtf16,root,sizeof(root))||
       candidates[0].BootOptionsCharacters!=3||
       candidates[0].BootOptionsStorage[0]!='-'||
       candidates[0].BootOptionsStorage[1]!='n'||
       candidates[0].BootOptionsStorage[2]!='o'||
       candidates[0].BootOptionsStorage[3]!=0){
        fprintf(stderr,"end-to-end BCD candidate collection failed: status=%d count=%lu\n",(int)s,(unsigned long)count);
        free(h);return 0;
    }
    /* Duplicate DisplayOrder GUIDs are malformed, not duplicate menu entries. */
    if(!make_candidate_hive(h,1,0,0) ||
       Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess){
        free(h);return 0;
    }
    count=77;
    s=Rc95BcdCollectWindowsBootCandidates(&hive,candidates,1,&count);
    if(s!=Rc95LdrCorruptData||count!=0){free(h);return 0;}

    /* Structurally valid but incomplete loader objects are skipped. */
    if(!make_candidate_hive(h,0,1,0) ||
       Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess){
        free(h);return 0;
    }
    count=77;
    s=Rc95BcdCollectWindowsBootCandidates(&hive,candidates,1,&count);
    if(s!=Rc95LdrNotFound||count!=0){free(h);return 0;}

    /* Two valid entries with one output slot must report exhaustion. */
    if(!make_candidate_hive(h,0,0,1) ||
       Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess){
        free(h);return 0;
    }
    count=77;
    s=Rc95BcdCollectWindowsBootCandidates(&hive,candidates,1,&count);
    if(s!=Rc95LdrBufferTooSmall || count!=0){free(h);return 0;}

    /* With sufficient storage, preserve both DisplayOrder entries and mark
     * only the Boot Manager Default object as default. The second entry must
     * retain its own GUID, loader path, SystemRoot, devices and load options. */
    count=0;
    s=Rc95BcdCollectWindowsBootCandidates(&hive,candidates,2,&count);
    if(s!=Rc95LdrSuccess || count!=2 ||
       !candidates[0].IsDefault || candidates[0].DisplayOrder!=0 ||
       candidates[1].IsDefault || candidates[1].DisplayOrder!=1 ||
       candidates[0].ObjectId.Data1!=0x12345678u ||
       candidates[1].ObjectId.Data1!=0x90abcdefu ||
       !candidates[1].DeviceElement || !candidates[1].OsDeviceElement ||
       candidates[1].LoaderPathCharacters!=sizeof(loader)/sizeof(loader[0])-1 ||
       candidates[1].SystemRootCharacters!=sizeof(root)/sizeof(root[0])-1 ||
       candidates[1].BootOptionsCharacters!=3 ||
       memcmp(candidates[1].LoaderPathUtf16,loader,sizeof(loader)) ||
       memcmp(candidates[1].SystemRootUtf16,root,sizeof(root)) ||
       candidates[1].BootOptionsStorage[0]!='-' ||
       candidates[1].BootOptionsStorage[1]!='n' ||
       candidates[1].BootOptionsStorage[2]!='o' ||
       candidates[1].BootOptionsStorage[3]!=0 ||
       !candidate_view_uses_owned_payload(&candidates[1],0x11000001u,candidates[1].DeviceElement) ||
       !candidate_view_uses_owned_payload(&candidates[1],0x21000001u,candidates[1].OsDeviceElement)){
        fprintf(stderr,"multi-entry BCD DisplayOrder projection failed: status=%d count=%lu\\n",(int)s,(unsigned long)count);
        free(h);return 0;
    }

    /* Zero capacity is rejected and the output count is reset. */
    count=77;
    s=Rc95BcdCollectWindowsBootCandidates(&hive,candidates,0,&count);
    if(s!=Rc95LdrInvalidParameter||count!=0){free(h);return 0;}
    free(h);return 1;
}
static int test_enumeration(void){unsigned char*h=(unsigned char*)malloc(HIVE_SIZE);RC95BCD_HIVE hive;RC95LDR_STATUS s;if(!h)return 0;make_elements(h);s=Rc95BcdOpenHive(h,HIVE_SIZE,&hive);enum_seen=0;if(s==Rc95LdrSuccess)s=Rc95BcdEnumerateElements(&hive,CHILD,enum_element,NULL);free(h);return s==Rc95LdrSuccess&&enum_seen==1;}
static RC95LDR_STATUS blockread(void *ctx,uint64_t lba,uint32_t count,void *buf,size_t len){(void)ctx;(void)lba;(void)count;(void)buf;(void)len;return Rc95LdrSuccess;}
static const unsigned char *gFile; static size_t gFileSize;
static RC95LDR_STATUS fsopen(void *ctx,const char *path,void **handle,uint64_t *size){(void)ctx;if(strcmp(path,"\\EFI\\Microsoft\\Boot\\BCD"))return Rc95LdrNotFound;*handle=(void*)gFile;*size=gFileSize;return Rc95LdrSuccess;}
static RC95LDR_STATUS fsread(void *ctx,void *handle,uint64_t off,void *buf,size_t len){(void)ctx;if(handle!=gFile||off>gFileSize||len>gFileSize-(size_t)off)return Rc95LdrOutOfBounds;memcpy(buf,gFile+(size_t)off,len);return Rc95LdrSuccess;}
static void fsclose(void *ctx,void *handle){(void)ctx;(void)handle;}
static void make(unsigned char*h){unsigned char*b=h+BIN;memset(h,0,HIVE_SIZE);p32(h,0x66676572);p32(h+4,1);p32(h+8,1);p32(h+0x14,1);p32(h+0x18,5);p32(h+0x1c,0);p32(h+0x20,1);p32(h+0x24,ROOT);p32(h+0x28,4096);p32(b,0x6e696268);p32(b+4,0);p32(b+8,4096);cell(b,ROOT,88,1);key(b,ROOT,"ROOT",1,INDEX);cell(b,INDEX,16,1);p16(b+INDEX+4,0x696c);p16(b+INDEX+6,1);p32(b+INDEX+8,CHILD);cell(b,CHILD,88,1);key(b,CHILD,"Objects",0,0xffffffffu);cell(b,224,4096-224,0);p32(h+0x1fc,sum(h));}

static int test_windows_loader_object_creation(void)
{
    static const unsigned char mbrDevice[24]={
        1,0,0,0, 0,0,0,0, 0,0,0,0, 0x78,0x56,0x34,0x12,
        0x00,0x08,0x00,0x00,0,0,0,0
    };
    static const unsigned char gptDevice[44]={
        1,0,0,0, 0,0,0,0, 1,0,0,0,
        0x11,0x11,0x11,0x11,0x22,0x22,0x33,0x33,0x44,0x44,0x55,0x55,0x66,0x66,0x77,0x77,
        0x88,0x88,0x99,0x99,0xaa,0xaa,0xbb,0xbb,0xcc,0xcc,0xdd,0xdd,0xee,0xee,0xff,0xff
    };
    static const unsigned char biosPath[]={
        '\\',0,'W',0,'i',0,'n',0,'d',0,'o',0,'w',0,'s',0,'\\',0,
        'S',0,'y',0,'s',0,'t',0,'e',0,'m',0,'3',0,'2',0,'\\',0,
        'w',0,'i',0,'n',0,'l',0,'o',0,'a',0,'d',0,'.',0,'e',0,'x',0,'e',0,0,0
    };
    static const unsigned char uefiPath[]={
        '\\',0,'W',0,'i',0,'n',0,'d',0,'o',0,'w',0,'s',0,'\\',0,
        'S',0,'y',0,'s',0,'t',0,'e',0,'m',0,'3',0,'2',0,'\\',0,
        'w',0,'i',0,'n',0,'l',0,'o',0,'a',0,'d',0,'.',0,'e',0,'f',0,'i',0,0,0
    };
    static const unsigned char systemRoot[]={
        '\\',0,'W',0,'i',0,'n',0,'d',0,'o',0,'w',0,'s',0,0,0
    };
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char *scratch=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char typeData[4]={3,0,0x20,0x10};
    RC95BCD_HIVE hive;
    RC95BCD_VALUE value;
    RC95BCD_ELEMENT_VIEW element;
    uint32_t objects,object,description,elements,keyCell,vk,dataCell;
    unsigned mode;
    int ok=0;
    if(!h||!scratch)goto done;
    for(mode=0;mode<2;mode++)
    {
        const unsigned char *device=mode?gptDevice:mbrDevice;
        uint32_t deviceLength=mode?(uint32_t)sizeof(gptDevice):(uint32_t)sizeof(mbrDevice);
        const unsigned char *path=mode?uefiPath:biosPath;
        uint32_t pathLength=mode?(uint32_t)sizeof(uefiPath):(uint32_t)sizeof(biosPath);
        make(h);
        if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
           Rc95BcdFindSubKey(&hive,hive.RootCell,"Objects",&objects)!=Rc95LdrSuccess)
            goto done;
        if(Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,objects,
             mode?"{90abcdef-5678-1234-090a-0b0c0d0e0f10}":
                  "{12345678-1234-abcd-0102-030405060708}",&object)!=Rc95LdrSuccess ||
           Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,object,
             "Description",&description)!=Rc95LdrSuccess ||
           Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,description,
             "Type",4u,typeData,sizeof(typeData),&vk,&dataCell)!=Rc95LdrSuccess ||
           Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,object,
             "Elements",&elements)!=Rc95LdrSuccess)
            goto done;
        if(Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,elements,"11000001",&keyCell)!=Rc95LdrSuccess ||
           Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,keyCell,"Element",3u,
             device,deviceLength,&vk,&dataCell)!=Rc95LdrSuccess ||
           Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,elements,"21000001",&keyCell)!=Rc95LdrSuccess ||
           Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,keyCell,"Element",3u,
             device,deviceLength,&vk,&dataCell)!=Rc95LdrSuccess)
            goto done;
        if(Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,elements,"12000002",&keyCell)!=Rc95LdrSuccess ||
           Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,keyCell,"Element",1u,
             path,pathLength,&vk,&dataCell)!=Rc95LdrSuccess ||
           Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,elements,"22000002",&keyCell)!=Rc95LdrSuccess ||
           Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,keyCell,"Element",1u,
             systemRoot,sizeof(systemRoot),&vk,&dataCell)!=Rc95LdrSuccess)
            goto done;
        if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
           Rc95BcdFindValue(&hive,description,"Type",&value)!=Rc95LdrSuccess ||
           value.Type!=4u || value.Length!=4u ||
           memcmp(value.Data,typeData,4)!=0)
            goto done;
        if(Rc95BcdFindElement(&hive,object,0x11000001u,&value)!=Rc95LdrSuccess ||
           Rc95BcdDecodeElement(0x11000001u,&value,&element)!=Rc95LdrSuccess ||
           element.Kind!=Rc95BcdElementDevice || element.DeviceType!=1u ||
           element.AdditionalOptions!=0u || element.DeviceDataLength!=deviceLength-8u ||
           memcmp(element.DeviceData,device+8,deviceLength-8u)!=0)
            goto done;
        if(Rc95BcdFindElement(&hive,object,0x21000001u,&value)!=Rc95LdrSuccess ||
           Rc95BcdDecodeElement(0x21000001u,&value,&element)!=Rc95LdrSuccess ||
           element.DeviceType!=1u || element.DeviceDataLength!=deviceLength-8u)
            goto done;
        if(Rc95BcdFindElement(&hive,object,0x12000002u,&value)!=Rc95LdrSuccess ||
           value.Type!=1u || value.Length!=pathLength ||
           memcmp(value.Data,path,pathLength)!=0)
            goto done;
        if(Rc95BcdFindElement(&hive,object,0x22000002u,&value)!=Rc95LdrSuccess ||
           value.Type!=1u || value.Length!=sizeof(systemRoot) ||
           memcmp(value.Data,systemRoot,sizeof(systemRoot))!=0)
            goto done;
        if(Rc95BcdValidateHiveBins(h,HIVE_SIZE)!=Rc95LdrSuccess)
            goto done;
    }
    ok=1;
done:
    free(h);free(scratch);return ok;
}


static int test_elements(void)
{
    RC95BCD_VALUE v;
    RC95BCD_ELEMENT_VIEW e;
    unsigned char device[12]={2,0,0,0,1,0,0,0,0xaa,0xbb,0xcc,0xdd};
    unsigned char string[4]={'A',0,0,0};
    unsigned char badString[2]={0x00,0xd8};
    unsigned char guid[16]={0x78,0x56,0x34,0x12,0x34,0x12,0xcd,0xab,1,2,3,4,5,6,7,8};
    unsigned char guidList[20]={1,0,0,0};
    unsigned char integer[8]={0x78,0x56,0x34,0x12,0,0,0,0};
    unsigned char boolean[4]={1,0,0,0};
    unsigned char badBoolean[4]={2,0,0,0};
    unsigned char integerList[12]={1,0,0,0,0x2a,0,0,0,0,0,0,0};
    unsigned char badList[4]={0xff,0xff,0xff,0xff};

    memset(&v,0,sizeof(v));
    v.Data=device; v.Length=sizeof(device);
    if(Rc95BcdDecodeElement(0x11000001u,&v,&e)!=Rc95LdrSuccess ||
       e.Kind!=Rc95BcdElementDevice || e.DeviceType!=2 ||
       e.AdditionalOptions!=1 || e.DeviceDataLength!=4) return 0;

    v.Data=string; v.Length=sizeof(string);
    if(Rc95BcdDecodeElement(0x22000002u,&v,&e)!=Rc95LdrSuccess ||
       e.Kind!=Rc95BcdElementString || e.StringCharacters!=1) return 0;
    /* BcdLibraryString_LoadOptionsString is 0x12000030. */
    if(Rc95BcdDecodeElement(0x12000030u,&v,&e)!=Rc95LdrSuccess ||
       e.Kind!=Rc95BcdElementString || e.StringCharacters!=1) return 0;
    v.Data=badString; v.Length=sizeof(badString);
    if(Rc95BcdDecodeElement(0x22000002u,&v,&e)!=Rc95LdrCorruptData) return 0;

    v.Data=guid; v.Length=sizeof(guid);
    if(Rc95BcdDecodeElement(0x23000003u,&v,&e)!=Rc95LdrSuccess ||
       e.ObjectGuid.Data1!=0x12345678u || e.ObjectGuid.Data2!=0x1234u || e.ObjectGuid.Data3!=0xabcdu) return 0;
    memcpy(guidList+4,guid,sizeof(guid));
    v.Data=guidList; v.Length=sizeof(guidList);
    if(Rc95BcdDecodeElement(0x24000001u,&v,&e)!=Rc95LdrSuccess ||
       e.ItemCount!=1 || e.Items!=guidList+4) return 0;
    { RC95BCD_GUID item; if(Rc95BcdReadGuid(e.Items,16,&item)!=Rc95LdrSuccess || item.Data1!=0x12345678u) return 0; }
    v.Data=badList; v.Length=sizeof(badList);
    if(Rc95BcdDecodeElement(0x24000001u,&v,&e)!=Rc95LdrCorruptData) return 0;

    v.Data=integer; v.Length=sizeof(integer);
    if(Rc95BcdDecodeElement(0x25000001u,&v,&e)!=Rc95LdrSuccess ||
       e.IntegerValue!=0x12345678u) return 0;
    v.Data=boolean; v.Length=sizeof(boolean);
    if(Rc95BcdDecodeElement(0x26000001u,&v,&e)!=Rc95LdrSuccess ||
       e.BooleanValue!=1) return 0;
    v.Data=badBoolean; v.Length=sizeof(badBoolean);
    if(Rc95BcdDecodeElement(0x26000001u,&v,&e)!=Rc95LdrCorruptData) return 0;

    v.Data=integerList; v.Length=sizeof(integerList);
    if(Rc95BcdDecodeElement(0x27000001u,&v,&e)!=Rc95LdrSuccess ||
       e.ItemCount!=1 || Rc95LdrReadU64(e.Items)!=42) return 0;
    v.Data=badList; v.Length=sizeof(badList);
    if(Rc95BcdDecodeElement(0x27000001u,&v,&e)!=Rc95LdrCorruptData) return 0;

    v.Data=integer; v.Length=sizeof(integer);
    if(Rc95BcdDecodeElement(0x28000001u,&v,&e)!=Rc95LdrSuccess ||
       e.Kind!=Rc95BcdElementBinary) return 0;
    return 1;
}


static int test_binary_value_update(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char replacement[8]={'B',0,'C',0,'D',0,0,0};
    RC95BCD_HIVE hive; RC95BCD_VALUE value;
    uint32_t objects,elements,element; int ok=0;
    if(!h)return 0;
    make_elements(h);
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdFindSubKey(&hive,hive.RootCell,"Objects",&objects)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdFindSubKey(&hive,objects,"Elements",&elements)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdFindSubKey(&hive,elements,"12000030",&element)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdReplaceBinaryValue(h,HIVE_SIZE,element,"Element",replacement,8)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdFindValue(&hive,element,"Element",&value)!=Rc95LdrSuccess)goto done;
    if(value.Type!=3u||value.Length!=8u||memcmp(value.Data,replacement,8)!=0)goto done;
    if(Rc95BcdReplaceBinaryValue(h,HIVE_SIZE,element,"Element",replacement,4)!=Rc95LdrUnsupported)goto done;
    ok=1;
done:
    free(h); return ok;
}




static int test_cell_offset_is_reclaimed(const unsigned char *Hive,
    uint32_t Offset)
{
    uint32_t BinOffset = 0;
    uint32_t BinsSize = HIVE_SIZE - BIN;
    while (BinOffset < BinsSize)
    {
        const unsigned char *Bin = Hive + BIN + BinOffset;
        uint32_t BinSize = Rc95LdrReadU32(Bin + 8);
        uint32_t Cursor = 32;
        if (BinSize < 32u || BinSize > BinsSize - BinOffset)
            return 0;
        while (Cursor < BinSize)
        {
            int32_t SignedSize = (int32_t)Rc95LdrReadU32(Bin + Cursor);
            uint32_t CellSize = (uint32_t)(SignedSize < 0 ?
                -(int64_t)SignedSize : SignedSize);
            uint32_t CellOffset = BinOffset + Cursor;
            if (!CellSize || CellSize > BinSize - Cursor)
                return 0;
            if (Offset >= CellOffset && Offset - CellOffset < CellSize)
                return SignedSize > 0;
            Cursor += CellSize;
        }
        if (Cursor != BinSize)
            return 0;
        BinOffset += BinSize;
    }
    return 0;
}

static int test_variable_length_value_replacement(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char *scratch=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char first[12]={1,2,3,4,5,6,7,8,9,10,11,12};
    unsigned char second[28], small[3]={0xa1,0xb2,0xc3};
    RC95BCD_HIVE hive;
    RC95BCD_VALUE value, other;
    uint32_t oldVk,oldData,oldList,newVk,newData,otherVk;
    unsigned i;
    int ok=0;
    if(!h||!scratch)goto done;
    for(i=0;i<sizeof(second);++i)second[i]=(unsigned char)(0x80u+i);
    make(h);
    if(Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                              "ReplaceMe",3u,first,sizeof(first),
                              &oldVk,&oldData)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                              "KeepMe",1u,small,sizeof(small),
                              &otherVk,NULL)!=Rc95LdrSuccess)goto done;
    oldList=Rc95LdrReadU32(h+BIN+CHILD+4+40);
    if(oldVk==0xffffffffu||oldData==0xffffffffu||oldList==0xffffffffu)
        goto done;

    if(Rc95BcdReplaceValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                               "ReplaceMe",3u,second,sizeof(second),
                               &newVk,&newData)!=Rc95LdrSuccess)
        goto done;
    if(newVk==oldVk||newData==0xffffffffu||newData==oldData||
       !test_cell_offset_is_reclaimed(h,oldList)||
       !test_cell_offset_is_reclaimed(h,oldVk)||
       !test_cell_offset_is_reclaimed(h,oldData))
        goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess||
       Rc95BcdFindValue(&hive,CHILD,"ReplaceMe",&value)!=Rc95LdrSuccess||
       value.ValueCell!=newVk||value.Type!=3u||value.Length!=sizeof(second)||
       memcmp(value.Data,second,sizeof(second))!=0||
       Rc95BcdFindValue(&hive,CHILD,"KeepMe",&other)!=Rc95LdrSuccess||
       other.ValueCell!=otherVk||other.Length!=sizeof(small)||
       memcmp(other.Data,small,sizeof(small))!=0)
        goto done;

    oldList=Rc95LdrReadU32(h+BIN+CHILD+4+40);
    oldVk=newVk;oldData=newData;
    if(Rc95BcdReplaceValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                               "ReplaceMe",1u,small,sizeof(small),
                               &newVk,&newData)!=Rc95LdrSuccess)
        goto done;
    if(newVk==oldVk||newData!=0xffffffffu||
       !test_cell_offset_is_reclaimed(h,oldList)||
       !test_cell_offset_is_reclaimed(h,oldVk)||
       !test_cell_offset_is_reclaimed(h,oldData))
        goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess||
       Rc95BcdFindValue(&hive,CHILD,"ReplaceMe",&value)!=Rc95LdrSuccess||
       value.ValueCell!=newVk||value.Type!=1u||value.Length!=sizeof(small)||
       memcmp(value.Data,small,sizeof(small))!=0||
       Rc95BcdValidateHiveBins(h,HIVE_SIZE)!=Rc95LdrSuccess)
        goto done;

    /* Exhaustion is transactional: no sequence, link or cell changes escape. */
    {
        unsigned char *before=(unsigned char*)malloc(HIVE_SIZE);
        unsigned char dummy=0;
        if(!before)goto done;
        memcpy(before,h,HIVE_SIZE);
        if(Rc95BcdReplaceValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                                   "ReplaceMe",3u,&dummy,0x7ffffff0u,
                                   &newVk,&newData)==Rc95LdrSuccess||
           memcmp(before,h,HIVE_SIZE)!=0)
        {free(before);goto done;}
        free(before);
    }
    ok=1;
done:
    free(h);free(scratch);return ok;
}

static int test_free_cell_allocator(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char snapshot[HIVE_SIZE];
    RC95BCD_HIVE hive;
    uint32_t first=0,capacity=0,second=0,secondCapacity=0;
    uint32_t firstTotal,freeSize;
    int ok=0;
    if(!h)return 0;
    make(h);
    if(Rc95BcdAllocateCell(h,HIVE_SIZE,33,&first,&capacity)!=Rc95LdrSuccess)
        goto done;
    firstTotal=capacity+4u;
    if(first!=224u || capacity<33u || (firstTotal&7u)!=0)goto done;
    if((int32_t)Rc95LdrReadU32(h+BIN+first)>=0)goto done;
    freeSize=Rc95LdrReadU32(h+BIN+first+firstTotal);
    if(freeSize==0 || (freeSize&7u)!=0 ||
       freeSize!=(4096u-first-firstTotal))goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdAllocateCell(h,HIVE_SIZE,65,&second,&secondCapacity)!=Rc95LdrSuccess ||
       second!=first+firstTotal || secondCapacity<65u ||
       secondCapacity+4u>freeSize)goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess)goto done;
    memcpy(snapshot,h,HIVE_SIZE);
    if(Rc95BcdAllocateCell(h,HIVE_SIZE,UINT32_MAX,&first,&capacity)==Rc95LdrSuccess ||
       memcmp(snapshot,h,HIVE_SIZE)!=0)goto done;
    if(first!=0xffffffffu || capacity!=0)goto done;
    ok=1;
done:
    free(h);
    return ok;
}


static int test_nk_vk_cell_creation(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char *scratch=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char large[12]={0,1,2,3,4,5,6,7,8,9,10,11};
    unsigned char small[4]={0xaa,0xbb,0xcc,0xdd};
    RC95BCD_HIVE hive;
    uint32_t nk,nk2,vk,vkLarge,dataCell,found;
    int ok=0;
    if(!h||!scratch)goto done;
    make(h);
    if(Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                            "NewLoader",&nk)!=Rc95LdrSuccess)goto done;
    /* Creating a subkey must immediately publish a searchable parent index. */
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
       Rc95BcdFindSubKey(&hive,CHILD,"NewLoader",&found)!=Rc95LdrSuccess ||
       found!=nk)goto done;
    if(Rc95BcdCreateKeyCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                            "SecondLoader",&nk2)!=Rc95LdrSuccess)goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
       Rc95BcdFindSubKey(&hive,CHILD,"NewLoader",&found)!=Rc95LdrSuccess ||
       found!=nk ||
       Rc95BcdFindSubKey(&hive,CHILD,"SecondLoader",&found)!=Rc95LdrSuccess ||
       found!=nk2)goto done;
    if(Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                              "Element",3u,large,sizeof(large),
                              &vk,&dataCell)!=Rc95LdrSuccess)goto done;
    if(dataCell==0xffffffffu || vk==0xffffffffu)goto done;
    vkLarge=vk;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess)goto done;
    /* The VK is already linked into CHILD's value list; also verify its
     * external data cell contains the complete payload. */
    if(Rc95LdrReadU16(h+BIN+nk+4)!=0x6b6eu ||
       Rc95LdrReadU16(h+BIN+vk+4)!=0x6b76 ||
       memcmp(h+BIN+dataCell+4,large,sizeof(large))!=0)goto done;

    /* Small data must use the VK inline representation and no data cell. */
    if(Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                              "Inline",1u,small,sizeof(small),
                              &vk,&dataCell)!=Rc95LdrSuccess)goto done;
    if(dataCell!=0xffffffffu ||
       (Rc95LdrReadU32(h+BIN+vk+8)&0x80000000u)==0 ||
       memcmp(h+BIN+vk+12,small,sizeof(small))!=0)goto done;
    {
        RC95BCD_VALUE parsedLarge, parsedInline;
        if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
           Rc95BcdFindValue(&hive,CHILD,"Element",&parsedLarge)!=Rc95LdrSuccess ||
           parsedLarge.ValueCell!=vkLarge || parsedLarge.Length!=sizeof(large) ||
           memcmp(parsedLarge.Data,large,sizeof(large))!=0 ||
           Rc95BcdFindValue(&hive,CHILD,"Inline",&parsedInline)!=Rc95LdrSuccess ||
           parsedInline.ValueCell!=vk || parsedInline.Length!=sizeof(small) ||
           memcmp(parsedInline.Data,small,sizeof(small))!=0)
            goto done;
    }

    /* Automatic value-list rebuilding must retain earlier values. */
    {
        RC95BCD_VALUE parsed;
        if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
           Rc95BcdFindValue(&hive,CHILD,"Element",&parsed)!=Rc95LdrSuccess ||
           parsed.ValueCell!=vkLarge ||
           Rc95BcdFindValue(&hive,CHILD,"Inline",&parsed)!=Rc95LdrSuccess ||
           parsed.ValueCell!=vk ||
           Rc95BcdFindSubKey(&hive,CHILD,"NewLoader",&found)!=Rc95LdrSuccess ||
           found!=nk ||
           Rc95BcdFindSubKey(&hive,CHILD,"SecondLoader",&found)!=Rc95LdrSuccess ||
           found!=nk2)
            goto done;
    }
    /* Duplicate value names must not mutate either the hive or its value list. */
    {
        unsigned char *before=(unsigned char*)malloc(HIVE_SIZE);
        if(!before)goto done;
        memcpy(before,h,HIVE_SIZE);
        if(Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                                  "Inline",1u,small,sizeof(small),
                                  &vk,&dataCell)==Rc95LdrSuccess ||
           memcmp(before,h,HIVE_SIZE)!=0){free(before);goto done;}
        free(before);
    }

    /* A failed allocation must leave the original image byte-for-byte intact. */
    {
        unsigned char *before=(unsigned char*)malloc(HIVE_SIZE);
        if(!before)goto done;
        memcpy(before,h,HIVE_SIZE);
        if(Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                                  "TooLarge",3u,large,UINT32_MAX,
                                  &vk,&dataCell)==Rc95LdrSuccess ||
           memcmp(before,h,HIVE_SIZE)!=0){free(before);goto done;}
        free(before);
    }
    ok=1;
done:
    free(h);free(scratch);return ok;
}




static int test_rebuild_reclaims_index_trees(void)
{
    static const unsigned leafSignatures[3]={0x696c,0x666c,0x686c};
    unsigned n;
    for(n=0;n<3;n++)
    {
        unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
        unsigned char *scratch=(unsigned char*)malloc(HIVE_SIZE);
        unsigned char *b;
        unsigned children[1]={CHILD};
        RC95BCD_HIVE hive;
        uint32_t found=0;
        RC95LDR_STATUS rebuildStatus;
        int ok=0;
        if(!h||!scratch){free(h);free(scratch);return 0;}
        make(h);b=h+BIN;
        /* Parent ownership is required by the rebuild validator. */
        p32(b+CHILD+4+16,ROOT);
        /* Replace the original LI with an RI root and one typed leaf. */
        cell(b,224,16,1);
        p16(b+INDEX+4,0x6972);p16(b+INDEX+6,1);p32(b+INDEX+8,224);
        p16(b+224+4,leafSignatures[n]);p16(b+224+6,1);
        p32(b+224+8,CHILD);
        cell(b,240,4096-240,0);
        p32(h+0x1fc,sum(h));
        if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess)goto tree_done;
        rebuildStatus=Rc95BcdRebuildKeyStructure(h,HIVE_SIZE,scratch,HIVE_SIZE,
                                      ROOT,children,1,NULL,0);
        if(rebuildStatus!=Rc95LdrSuccess)
        {
            fprintf(stderr,"index-tree rebuild case %u returned %d\\n",n,(int)rebuildStatus);
            goto tree_done;
        }
        /* Both the RI root and its LI/LF/LH descendant must be free. */
        if((int32_t)Rc95LdrReadU32(b+INDEX)<=0 ||
           (int32_t)Rc95LdrReadU32(b+224)<=0 ||
           Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
           Rc95BcdFindSubKey(&hive,ROOT,"Objects",&found)!=Rc95LdrSuccess ||
           found!=CHILD ||
           Rc95BcdValidateHiveBins(h,HIVE_SIZE)!=Rc95LdrSuccess)
        {
            fprintf(stderr,"index-tree post-rebuild verification failed for case %u\\n",n);
            goto tree_done;
        }
        ok=1;
tree_done:
        free(h);free(scratch);
        if(!ok)return 0;
    }
    return 1;
}

static int test_rebuild_reclaims_value_list(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char *scratch=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char data[8]={1,2,3,4,5,6,7,8};
    RC95BCD_HIVE hive;
    RC95BCD_VALUE value;
    uint32_t vk=0xffffffffu,dataCell=0xffffffffu,oldList,newList;
    int ok=0;
    if(!h||!scratch)goto done;
    make(h);
    if(Rc95BcdCreateValueCell(h,HIVE_SIZE,scratch,HIVE_SIZE,CHILD,
                              "RebuildValue",3u,data,sizeof(data),
                              &vk,&dataCell)!=Rc95LdrSuccess)goto done;
    oldList=Rc95LdrReadU32(h+BIN+CHILD+4+40);
    if(oldList==0xffffffffu || vk==0xffffffffu)goto done;
    if(Rc95BcdRebuildKeyStructure(h,HIVE_SIZE,scratch,HIVE_SIZE,
                                  CHILD,NULL,0,&vk,1)!=Rc95LdrSuccess)
        goto done;
    newList=Rc95LdrReadU32(h+BIN+CHILD+4+40);
    if(newList==oldList || (int32_t)Rc95LdrReadU32(h+BIN+oldList)<=0)
        goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
       Rc95BcdFindValue(&hive,CHILD,"RebuildValue",&value)!=Rc95LdrSuccess ||
       value.ValueCell!=vk || value.Length!=sizeof(data) ||
       memcmp(value.Data,data,sizeof(data))!=0 ||
       Rc95BcdValidateHiveBins(h,HIVE_SIZE)!=Rc95LdrSuccess)
        goto done;
    ok=1;
done:
    free(h);free(scratch);return ok;
}


static int test_rebuild_reclamation_rollback(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char *scratch=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char *before=(unsigned char*)malloc(HIVE_SIZE);
    unsigned children[1]={CHILD};
    unsigned char *b;
    int ok=0;
    if(!h||!scratch||!before)goto done;
    make(h);b=h+BIN;
    p32(b+CHILD+4+16,ROOT);
    /* A well-formed RI cell with an invalid descendant must abort the rebuild. */
    p16(b+INDEX+4,0x6972);p16(b+INDEX+6,1);p32(b+INDEX+8,0xfffffff8u);
    p32(h+0x1fc,sum(h));
    memcpy(before,h,HIVE_SIZE);
    if(Rc95BcdRebuildKeyStructure(h,HIVE_SIZE,scratch,HIVE_SIZE,
                                  ROOT,children,1,NULL,0)==Rc95LdrSuccess ||
       memcmp(before,h,HIVE_SIZE)!=0)
        goto done;
    ok=1;
done:
    free(h);free(scratch);free(before);return ok;
}

static int test_cell_reclamation_and_coalescing(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    unsigned char *scratch=(unsigned char*)malloc(HIVE_SIZE);
    RC95BCD_HIVE hive;
    uint32_t first=0,firstCapacity=0,second=0,secondCapacity=0;
    uint32_t firstTotal,secondTotal,merged;
    int ok=0;
    if(!h||!scratch)goto done;
    make(h);
    if(Rc95BcdAllocateCell(h,HIVE_SIZE,24,&first,&firstCapacity)!=Rc95LdrSuccess ||
       Rc95BcdAllocateCell(h,HIVE_SIZE,24,&second,&secondCapacity)!=Rc95LdrSuccess)
        goto done;
    firstTotal=firstCapacity+4u;
    secondTotal=secondCapacity+4u;
    if(second!=first+firstTotal)goto done;

    /* A referenced cell must never be reclaimed. The root's index points to
     * INDEX, so this attempt must fail without changing the hive. */
    {
        unsigned char *before=(unsigned char*)malloc(HIVE_SIZE);
        if(!before)goto done;
        memcpy(before,h,HIVE_SIZE);
        if(Rc95BcdReclaimDetachedCell(h,HIVE_SIZE,scratch,HIVE_SIZE,INDEX)==
           Rc95LdrSuccess || memcmp(before,h,HIVE_SIZE)!=0)
        { free(before); goto done; }
        free(before);
    }

    /* Free in reverse order so the first cell remains a valid boundary after
     * the second cell is coalesced with the following free extent. */
    if(Rc95BcdReclaimDetachedCell(h,HIVE_SIZE,scratch,HIVE_SIZE,second)!=
       Rc95LdrSuccess)goto done;
    if(Rc95BcdReclaimDetachedCell(h,HIVE_SIZE,scratch,HIVE_SIZE,first)!=
       Rc95LdrSuccess)goto done;
    merged=Rc95LdrReadU32(h+BIN+first);
    if(merged!=(firstTotal+secondTotal+4096u-(second+secondTotal)))
        goto done;
    if(Rc95BcdOpenHive(h,HIVE_SIZE,&hive)!=Rc95LdrSuccess ||
       Rc95BcdValidateHiveBins(h,HIVE_SIZE)!=Rc95LdrSuccess)goto done;
    ok=1;
done:
    free(h);free(scratch);return ok;
}

static RC95LDR_STATUS
count_object_key(const RC95BCD_KEY_INFO *Key, void *Context)
{
    unsigned *Count = (unsigned *)Context;
    if (!Key || !Count || Key->NameLength != 38u ||
        (Key->Flags & 0x20u) == 0)
        return Rc95LdrCorruptData;
    ++*Count;
    return Rc95LdrSuccess;
}

static int
test_subkey_enumeration(void)
{
    unsigned char *h=(unsigned char*)malloc(HIVE_SIZE);
    RC95BCD_HIVE Hive;
    RC95BCD_KEY_INFO Root;
    uint32_t Objects;
    unsigned Count=0;
    RC95LDR_STATUS Status;
    int Success=0;
    if(!h) return 0;
    if(!make_candidate_hive(h,0,0,1)) goto Done;
    Status=Rc95BcdOpenHive(h,HIVE_SIZE,&Hive);
    if(Status!=Rc95LdrSuccess) goto Done;
    Status=Rc95BcdGetRootKey(&Hive,&Root);
    if(Status==Rc95LdrSuccess)
        Status=Rc95BcdFindSubKey(&Hive,Root.Cell,"Objects",&Objects);
    if(Status==Rc95LdrSuccess)
        Status=Rc95BcdEnumerateSubKeys(&Hive,Objects,count_object_key,&Count);
    Success=(Status==Rc95LdrSuccess && Count==3u);
Done:
    free(h);
    return Success;
}

int main(int argc,char**argv){unsigned char*h=(unsigned char*)malloc(HIVE_SIZE);RC95BCD_HIVE v;RC95BCD_KEY_INFO root;RC95LDR_BCD_BOOT_CANDIDATE candidates[2];size_t candidateCount=99;uint32_t child=0;RC95LDR_STATUS s;if(!h)return 2;make(h);s=Rc95BcdOpenHive(h,HIVE_SIZE,&v);if(s||Rc95BcdGetRootKey(&v,&root)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)||child!=CHILD){fprintf(stderr,"synthetic hive/index failure %d\n",s);return 1;}
{RC95LDR_BLOCK_DEVICE dev;RC95LDR_FILESYSTEM fs;RC95LDR_VOLUME vol;size_t got=0;unsigned char *copy=(unsigned char*)malloc(HIVE_SIZE);memset(&dev,0,sizeof(dev));dev.BlockSize=512;dev.BlockCount=64;dev.Read=blockread;fs.FileSystemContext=NULL;fs.OpenRead=fsopen;fs.ReadAt=fsread;fs.Close=fsclose;vol.BlockDevice=&dev;vol.FileSystem=&fs;gFile=h;gFileSize=HIVE_SIZE;if(!copy||Rc95BcdLoadFromVolume(&vol,"\\EFI\\Microsoft\\Boot\\BCD",copy,HIVE_SIZE,&got,&v)!=Rc95LdrSuccess||got!=HIVE_SIZE)return 1;free(copy);}
p32(h+BIN+ROOT,0xfffffff8u);if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)==Rc95LdrSuccess){fprintf(stderr,"bad cell accepted\n");return 1;}
make(h);p16(h+BIN+INDEX+4,0x1234);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)!=Rc95LdrCorruptData){fprintf(stderr,"bad index accepted\n");return 1;}
make(h);p16(h+BIN+INDEX+6,2);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)!=Rc95LdrCorruptData)return 1;
make(h);p16(h+BIN+INDEX+4,0x6972);p16(h+BIN+INDEX+6,1);p32(h+BIN+INDEX+8,INDEX);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)!=Rc95LdrCorruptData)return 1;
make(h);p32(h+8,2);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)!=Rc95LdrCorruptData)return 1;
if(!test_binary_value_update()){fprintf(stderr,"BCD in-place binary update failed\n");return 1;}
if(!test_free_cell_allocator()){fprintf(stderr,"BCD free-cell allocation/splitting failed\n");return 1;}
if(!test_variable_length_value_replacement()){fprintf(stderr,"transactional variable-length VK replacement failed\n");return 1;}
if(!test_rebuild_reclaims_index_trees()){fprintf(stderr,"transactional RI/LF/LH/LI tree reclamation failed\\n");return 1;}
if(!test_rebuild_reclamation_rollback()){fprintf(stderr,"transactional structure-rebuild rollback failed\\n");return 1;}
if(!test_rebuild_reclaims_value_list()){fprintf(stderr,"transactional VK value-list reclamation failed\\n");return 1;}
if(!test_cell_reclamation_and_coalescing()){fprintf(stderr,"BCD detached-cell reclamation/coalescing failed\\n");return 1;}
if(!test_nk_vk_cell_creation()){fprintf(stderr,"BCD NK/VK cell creation failed\n");return 1;}
if(!test_windows_loader_object_creation()){fprintf(stderr,"Windows loader object creation fixtures failed\n");return 1;}
if(!test_elements()){fprintf(stderr,"typed BCD element payload tests failed\n");return 1;}
if(!test_enumeration()){fprintf(stderr,"BCD Elements registry enumeration failed\n");return 1;}
if(!test_subkey_enumeration()){fprintf(stderr,"BCD object subkey enumeration failed\n");return 1;}
if(!test_candidate_collection()){fprintf(stderr,"Boot Manager to Windows-loader candidate fixture failed\n");return 1;}
/* Optional genuine regression input: create it on Windows with bcdedit /export
 * rc95-bcd-real.hiv, then pass that path as argv[1]. It is never modified. */
if(argc>1){FILE*f=fopen(argv[1],"rb");long n;if(!f)return 2;fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);if(n<4096||n>16*1024*1024)return 2;{unsigned char*r=(unsigned char*)malloc((size_t)n);if(!r||fread(r,1,(size_t)n,f)!=(size_t)n)return 2;s=Rc95BcdOpenHive(r,(size_t)n,&v);free(r);if(s){fprintf(stderr,"real-world hive rejected: %d\n",s);return 1;}}fclose(f);}
free(h);puts("RC95LDR BCD tests passed");return 0;}
