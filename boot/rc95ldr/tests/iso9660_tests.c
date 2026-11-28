#include "rc95ldr_iso9660.h"
#include <assert.h>
#include <string.h>

#define BLOCKS 40u
typedef struct _IMAGE { uint8_t Data[BLOCKS][2048]; } IMAGE;
static RC95LDR_STATUS Read(void *C,uint64_t L,uint32_t N,void *B,size_t Z)
{
    IMAGE *I=C;
    if(L>BLOCKS||N>BLOCKS-L||Z!=(size_t)N*2048u)return Rc95LdrOutOfBounds;
    memcpy(B,&I->Data[L][0],Z);return Rc95LdrSuccess;
}
static void Dual16(uint8_t *P,uint16_t V)
{P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>8);P[3]=(uint8_t)V;}
static void Dual32(uint8_t *P,uint32_t V)
{P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24);P[4]=(uint8_t)(V>>24);P[5]=(uint8_t)(V>>16);P[6]=(uint8_t)(V>>8);P[7]=(uint8_t)V;}
static uint8_t *Record(uint8_t *P,uint32_t Lba,uint32_t Size,uint8_t Flags,const char *Name,uint8_t Len)
{
    uint8_t N=(uint8_t)(33u+Len+(Len%2u==0?1u:0u));
    memset(P,0,N);P[0]=N;Dual32(P+2,Lba);Dual32(P+10,Size);
    P[25]=Flags;Dual16(P+28,1);P[32]=Len;memcpy(P+33,Name,Len);return P+N;
}
static void MakeImage(IMAGE *I)
{
    uint8_t *D,*P;
    memset(I,0,sizeof(*I));
    D=I->Data[16];D[0]=1;memcpy(D+1,"CD001",5);D[6]=1;
    Dual32(D+80,BLOCKS);Dual16(D+128,2048);
    P=D+156;Record(P,20,2048,2,"\0",1);P[33]=0; /* root id byte */
    D=I->Data[17];D[0]=255;memcpy(D+1,"CD001",5);D[6]=1;
    P=I->Data[20];P=Record(P,20,2048,2,"\0",1);
    P=Record(P,20,2048,2,"\1",1);
    P=Record(P,21,5,0,"BOOT.BIN;1",10);
    P=Record(P,22,2048,2,"BOOT",4);
    P=Record(P,24,2050,0,"BIG.BIN;1",9);
    memcpy(I->Data[21],"HELLO",5);
    P=I->Data[22];
    P=Record(P,22,2048,2,"\0",1);
    P=Record(P,20,2048,2,"\1",1);
    P=Record(P,23,7,0,"WINLOAD.EXE;1",13);
    memcpy(I->Data[23],"WINLOAD",7);
    memset(I->Data[24],'A',2048);
    I->Data[25][0]='B'; I->Data[25][1]='C';
}
static uint8_t *JolietRecord(uint8_t *P,uint32_t Lba,uint32_t Size,
                             uint8_t Flags,const char *Name,size_t Chars)
{
    size_t I; uint8_t Len=(uint8_t)(Chars*2u);
    uint8_t N=(uint8_t)(33u+Len+1u);
    memset(P,0,N);P[0]=N;Dual32(P+2,Lba);Dual32(P+10,Size);
    P[25]=Flags;Dual16(P+28,1);P[32]=Len;
    for(I=0;I<Chars;I++){P[33+I*2]=0;P[34+I*2]=(uint8_t)Name[I];}
    return P+N;
}
static void MakeJolietImage(IMAGE *I)
{
    uint8_t *D,*P;
    MakeImage(I);
    D=I->Data[17];D[0]=2;memcpy(D+1,"CD001",5);D[6]=1;
    D[88]='%';D[89]='/';D[90]='E';
    Dual32(D+80,BLOCKS);Dual16(D+128,2048);
    Record(D+156,20,2048,2,"\0",1);
    D=I->Data[18];D[0]=255;memcpy(D+1,"CD001",5);D[6]=1;
    P=I->Data[20];
    P=Record(P,20,2048,2,"\0",1);
    P=Record(P,20,2048,2,"\1",1);
    P=JolietRecord(P,21,5,0,"BOOT.BIN;1",10);
}
int main(void)
{
    IMAGE Image;RC95LDR_BLOCK_DEVICE Dev;RC95LDR_ISO9660 Iso;
    RC95LDR_FILE_INFORMATION Info;
    const RC95LDR_FILE_PROVIDER *Provider;void *File;char Data[8]={0};size_t Got=0;
    MakeImage(&Image);memset(&Dev,0,sizeof(Dev));
    Dev.DeviceContext=&Image;Dev.BlockCount=BLOCKS;Dev.BlockSize=2048;Dev.Read=Read;
    assert(Rc95LdrIso9660Mount(&Iso,&Dev)==Rc95LdrSuccess);
    Provider=Rc95LdrIso9660GetFileProvider(&Iso);assert(Provider);
    assert(Provider->Open(Provider->VolumeContext,"/BOOT.BIN",&File)==Rc95LdrSuccess);
    assert(Provider->GetFileInformation(Provider->VolumeContext,File,&Info)==Rc95LdrSuccess);
    assert(Info.Size==5);
    assert(Provider->Read(Provider->VolumeContext,File,Data,sizeof(Data),&Got)==Rc95LdrSuccess);
    assert(Got==5&&memcmp(Data,"HELLO",5)==0);
    assert(Provider->Seek(Provider->VolumeContext,File,2)==Rc95LdrSuccess);
    memset(Data,0,sizeof(Data));
    assert(Provider->Read(Provider->VolumeContext,File,Data,3,&Got)==Rc95LdrSuccess);
    assert(Got==3&&memcmp(Data,"LLO",3)==0);
    Provider->Close(Provider->VolumeContext,File);
    assert(Provider->Open(Provider->VolumeContext,"MISSING",&File)==Rc95LdrNotFound);
    assert(Provider->Open(Provider->VolumeContext,
        "/BOOT/WINLOAD.EXE",&File)==Rc95LdrSuccess);
    memset(Data,0,sizeof(Data));
    assert(Provider->Read(Provider->VolumeContext,File,Data,7,&Got)==Rc95LdrSuccess);
    assert(Got==7&&memcmp(Data,"WINLOAD",7)==0);
    Provider->Close(Provider->VolumeContext,File);
    {
        char Large[2050];
        assert(Provider->Open(Provider->VolumeContext,"BIG.BIN",&File)==Rc95LdrSuccess);
        assert(Provider->Read(Provider->VolumeContext,File,Large,sizeof(Large),&Got)==Rc95LdrSuccess);
        assert(Got==sizeof(Large));
        assert(Large[0]=='A'&&Large[2047]=='A'&&Large[2048]=='B'&&Large[2049]=='C');
        Provider->Close(Provider->VolumeContext,File);
    }
    Rc95LdrIso9660Unmount(&Iso);
    assert(Rc95LdrIso9660GetFileProvider(&Iso)==NULL);

    MakeJolietImage(&Image);
    assert(Rc95LdrIso9660Mount(&Iso,&Dev)==Rc95LdrSuccess);
    assert(Iso.Joliet==1);
    Provider=Rc95LdrIso9660GetFileProvider(&Iso);
    assert(Provider->Open(Provider->VolumeContext,"boot.bin",&File)==Rc95LdrSuccess);
    memset(Data,0,sizeof(Data));
    assert(Provider->Read(Provider->VolumeContext,File,Data,5,&Got)==Rc95LdrSuccess);
    assert(Got==5&&memcmp(Data,"HELLO",5)==0);
    Provider->Close(Provider->VolumeContext,File);
    Rc95LdrIso9660Unmount(&Iso);
    return 0;
}
