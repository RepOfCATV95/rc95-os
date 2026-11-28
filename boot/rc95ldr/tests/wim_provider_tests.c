#include "../fs/include/rc95ldr_wim_provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *TestAllocate(void *C,size_t N){(void)C;return malloc(N);}
static void TestFree(void *C,void *P){(void)C;free(P);}
#define CHECK(x) do { if(!(x)){fprintf(stderr,"WIM provider test failed at %d: %s\n",__LINE__,#x);return 1;} } while(0)
int main(void)
{
    RC95LDR_WIM_CONTEXT C; RC95LDR_WIM_ALLOCATOR A; RC95LDR_VOLUME V;
    uint8_t BadImage[WIM_HEADER_MIN_SIZE]; WIM_HEADER_INFO H; size_t Count=99;
    memset(&C,0,sizeof(C)); memset(&V,0,sizeof(V)); memset(BadImage,0,sizeof(BadImage));
    A.Context=NULL; A.Allocate=TestAllocate; A.Free=TestFree;
    CHECK(Rc95LdrWimMountMemory(&C,BadImage,sizeof(BadImage),0,0,0,&A,&V,&H,NULL)==Rc95LdrCorruptData);
    CHECK(C.Reader==NULL && C.Root==NULL && C.Mounted==0);
    CHECK(Rc95LdrWimGetImageCount(&C,&Count)==WIM_READER_INVALID_ARGUMENT);
    Rc95LdrWimUnmount(&C);
    puts("RC95LDR WIM provider negative-path tests passed");
    return 0;
}
