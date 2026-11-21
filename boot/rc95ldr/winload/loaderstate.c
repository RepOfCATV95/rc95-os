#include "rc95ldrloaderstate.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>

static size_t AlignUp(size_t n,size_t a)
{ return (n+(a-1))&~(a-1); }
static int AddSize(size_t *n,size_t a)
{ if(*n>SIZE_MAX-a)return 0;*n+=a;return 1; }

void Rc95LdrReleaseNtLoaderState(RC95LDR_LOADERSTATE_RESULT *out)
{
    if(!out)return;
    if(out->OwnsAllocation)
        Rc95LdrPhysMemRelease(&out->PhysicalMemory,&out->Allocation);
    memset(out,0,sizeof(*out));
}

RC95LDR_STATUS Rc95LdrBuildNtLoaderState(
    const RC95LDR_LOADERSTATE_INPUT *in,RC95LDR_LOADERSTATE_RESULT *out)
{
    RC95LDR_LOADERSTATE_RESULT tmp;
    uint8_t *b;
    size_t need=0,offNodes=0,i;
    uint64_t pages;
    RC95LDR_NT_CONFIGURATION_COMPONENT_DATA *nodes;
    RC95LDR_NT_PTR root=0;
    RC95LDR_STATUS status;
    if(!in||!out||in->StructureSize<sizeof(*in)||
       in->Version!=RC95LDR_LOADERSTATE_VERSION||!in->PhysicalMemory||
       in->ComponentCount>RC95LDR_CONFIG_COMPONENT_MAX||
       (in->ComponentCount&&!in->Components)||
       in->NtVersion<Rc95LdrNtVersion4||in->NtVersion>Rc95LdrNtVersion10)
        return Rc95LdrInvalidParameter;
    memset(&tmp,0,sizeof(tmp));
    if(in->ComponentCount){
        for(i=0;i<in->ComponentCount;i++){
            const RC95LDR_CONFIG_COMPONENT_INPUT *c=&in->Components[i];
            size_t id=c->Identifier?strlen(c->Identifier)+1:0;
            if(!c->Identifier||id>UINT32_MAX||c->ConfigurationDataLength>65536u||
               (c->ConfigurationDataLength&&!c->ConfigurationData)||
               c->ParentIndex>=(int32_t)i||c->ParentIndex< -1)
                return Rc95LdrInvalidParameter;
            if(!AddSize(&need,id))return Rc95LdrIntegerOverflow;
            if(c->ConfigurationDataLength){
                need=AlignUp(need,4);
                if(!AddSize(&need,c->ConfigurationDataLength))return Rc95LdrIntegerOverflow;
            }
        }
        offNodes=AlignUp(need,sizeof(void*));
        if(in->ComponentCount>(SIZE_MAX-offNodes)/sizeof(*nodes))return Rc95LdrIntegerOverflow;
        need=offNodes+(size_t)in->ComponentCount*sizeof(*nodes);
    }
    need=AlignUp(need,sizeof(void*));
    /* No synthetic LoaderParameterExtension is emitted.  A generation-specific
       ABI builder must supply it before non--NT4 handoff can be marked ready. */
    if(!need)need=sizeof(void*);
    if(need>UINT64_MAX-(RC95LDR_PHYSMEM_PAGE_SIZE-1u))return Rc95LdrIntegerOverflow;
    pages=(need+RC95LDR_PHYSMEM_PAGE_SIZE-1u)/RC95LDR_PHYSMEM_PAGE_SIZE;
    tmp.PhysicalMemory=*in->PhysicalMemory;
    status=Rc95LdrPhysMemAllocate(&tmp.PhysicalMemory,pages,1,
        RC95LDR_PHYSMEM_ALLOC_ZERO,RC95LDR_PHYSMEM_MAP_WRITABLE|RC95LDR_PHYSMEM_MAP_CACHED,
        &tmp.Allocation);
    if(status!=Rc95LdrSuccess)return status;
    tmp.OwnsAllocation=1;
    b=(uint8_t*)tmp.Allocation.WritableMapping;
    if(in->ComponentCount){
        size_t stringAt=0;
        nodes=(void*)(b+offNodes);
        for(i=0;i<in->ComponentCount;i++){
            const RC95LDR_CONFIG_COMPONENT_INPUT *c=&in->Components[i];
            size_t idLen=strlen(c->Identifier)+1;
            RC95LDR_NT_PTR id=(RC95LDR_NT_PTR)(uintptr_t)(b+stringAt);
            memcpy(b+stringAt,c->Identifier,idLen);stringAt+=idLen;
            nodes[i].ComponentEntry.Class=c->Class;
            nodes[i].ComponentEntry.Type=c->Type;
            nodes[i].ComponentEntry.Flags=c->Flags;
            nodes[i].ComponentEntry.Version=c->Version;
            nodes[i].ComponentEntry.Revision=c->Revision;
            nodes[i].ComponentEntry.Key=c->Key;
            nodes[i].ComponentEntry.AffinityMask=c->AffinityMask;
            nodes[i].ComponentEntry.IdentifierLength=(uint32_t)(idLen-1);
            nodes[i].ComponentEntry.Identifier=id;
            if(c->ConfigurationDataLength){
                stringAt=AlignUp(stringAt,4);
                nodes[i].ConfigurationData=(RC95LDR_NT_PTR)(uintptr_t)(b+stringAt);
                memcpy(b+stringAt,c->ConfigurationData,c->ConfigurationDataLength);
                stringAt+=c->ConfigurationDataLength;
                nodes[i].ComponentEntry.ConfigurationDataLength=c->ConfigurationDataLength;
            }
            if(c->ParentIndex<0){
                if(root){status=Rc95LdrInvalidParameter;goto Fail;}
                root=(RC95LDR_NT_PTR)(uintptr_t)&nodes[i];
            }else{
                RC95LDR_NT_CONFIGURATION_COMPONENT_DATA *p=&nodes[c->ParentIndex],*q;
                RC95LDR_NT_PTR *link=&p->Child;
                while(*link){q=(void*)(uintptr_t)*link;link=&q->Sibling;}
                *link=(RC95LDR_NT_PTR)(uintptr_t)&nodes[i];
                nodes[i].Parent=(RC95LDR_NT_PTR)(uintptr_t)p;
            }
        }
        if(!root){status=Rc95LdrInvalidParameter;goto Fail;}
    }
    tmp.ConfigurationRoot=root;tmp.BytesUsed=need;
    *out=tmp;
    return Rc95LdrSuccess;
Fail:
    Rc95LdrReleaseNtLoaderState(&tmp);
    return status;
}
