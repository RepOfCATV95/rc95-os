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
static void key(unsigned char*b,unsigned o,const char*n,unsigned count,unsigned list){unsigned char*q=b+o+4;size_t z=strlen(n);p16(q,0x6b6e);p16(q+2,0x20);p32(q+20,count);p32(q+28,list);p32(q+36,0);p32(q+40,0);p16(q+72,(unsigned)z);memcpy(q+76,n,z);}
static RC95LDR_STATUS blockread(void *ctx,uint64_t lba,uint32_t count,void *buf,size_t len){(void)ctx;(void)lba;(void)count;(void)buf;(void)len;return Rc95LdrSuccess;}
static const unsigned char *gFile; static size_t gFileSize;
static RC95LDR_STATUS fsopen(void *ctx,const char *path,void **handle,uint64_t *size){(void)ctx;if(strcmp(path,"\\EFI\\Microsoft\\Boot\\BCD"))return Rc95LdrNotFound;*handle=(void*)gFile;*size=gFileSize;return Rc95LdrSuccess;}
static RC95LDR_STATUS fsread(void *ctx,void *handle,uint64_t off,void *buf,size_t len){(void)ctx;if(handle!=gFile||off>gFileSize||len>gFileSize-(size_t)off)return Rc95LdrOutOfBounds;memcpy(buf,gFile+(size_t)off,len);return Rc95LdrSuccess;}
static void fsclose(void *ctx,void *handle){(void)ctx;(void)handle;}
static void make(unsigned char*h){unsigned char*b=h+BIN;memset(h,0,HIVE_SIZE);p32(h,0x66676572);p32(h+4,1);p32(h+8,1);p32(h+0x14,1);p32(h+0x18,5);p32(h+0x1c,0);p32(h+0x20,1);p32(h+0x24,ROOT);p32(h+0x28,4096);p32(b,0x6e696268);p32(b+4,0);p32(b+8,4096);cell(b,ROOT,88,1);key(b,ROOT,"ROOT",1,INDEX);cell(b,INDEX,16,1);p16(b+INDEX+4,0x696c);p16(b+INDEX+6,1);p32(b+INDEX+8,CHILD);cell(b,CHILD,88,1);key(b,CHILD,"Objects",0,0xffffffffu);cell(b,224,4096-224,0);p32(h+0x1fc,sum(h));}
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

int main(int argc,char**argv){unsigned char*h=(unsigned char*)malloc(HIVE_SIZE);RC95BCD_HIVE v;RC95BCD_KEY_INFO root;RC95LDR_BCD_BOOT_CANDIDATE candidates[2];size_t candidateCount=99;uint32_t child=0;RC95LDR_STATUS s;if(!h)return 2;make(h);s=Rc95BcdOpenHive(h,HIVE_SIZE,&v);if(s||Rc95BcdGetRootKey(&v,&root)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)||child!=CHILD){fprintf(stderr,"synthetic hive/index failure %d\n",s);return 1;}
{RC95LDR_BLOCK_DEVICE dev;RC95LDR_FILESYSTEM fs;RC95LDR_VOLUME vol;size_t got=0;unsigned char *copy=(unsigned char*)malloc(HIVE_SIZE);memset(&dev,0,sizeof(dev));dev.BlockSize=512;dev.BlockCount=64;dev.Read=blockread;fs.FileSystemContext=NULL;fs.OpenRead=fsopen;fs.ReadAt=fsread;fs.Close=fsclose;vol.BlockDevice=&dev;vol.FileSystem=&fs;gFile=h;gFileSize=HIVE_SIZE;if(!copy||Rc95BcdLoadFromVolume(&vol,"\\EFI\\Microsoft\\Boot\\BCD",copy,HIVE_SIZE,&got,&v)!=Rc95LdrSuccess||got!=HIVE_SIZE)return 1;free(copy);}
p32(h+BIN+ROOT,0xfffffff8u);if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)==Rc95LdrSuccess){fprintf(stderr,"bad cell accepted\n");return 1;}
make(h);p16(h+BIN+INDEX+4,0x1234);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)!=Rc95LdrCorruptData){fprintf(stderr,"bad index accepted\n");return 1;}
make(h);p16(h+BIN+INDEX+6,2);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)!=Rc95LdrCorruptData)return 1;
make(h);p16(h+BIN+INDEX+4,0x6972);p16(h+BIN+INDEX+6,1);p32(h+BIN+INDEX+8,INDEX);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)||Rc95BcdFindSubKey(&v,v.RootCell,"Objects",&child)!=Rc95LdrCorruptData)return 1;
make(h);p32(h+8,2);p32(h+0x1fc,sum(h));if(Rc95BcdOpenHive(h,HIVE_SIZE,&v)!=Rc95LdrCorruptData)return 1;
if(!test_elements()){fprintf(stderr,"typed BCD element payload tests failed\n");return 1;}
/* Optional genuine regression input: create it on Windows with bcdedit /export
 * rc95-bcd-real.hiv, then pass that path as argv[1]. It is never modified. */
if(argc>1){FILE*f=fopen(argv[1],"rb");long n;if(!f)return 2;fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);if(n<4096||n>16*1024*1024)return 2;{unsigned char*r=(unsigned char*)malloc((size_t)n);if(!r||fread(r,1,(size_t)n,f)!=(size_t)n)return 2;s=Rc95BcdOpenHive(r,(size_t)n,&v);free(r);if(s){fprintf(stderr,"real-world hive rejected: %d\n",s);return 1;}}fclose(f);}
free(h);puts("RC95LDR BCD tests passed");return 0;}
