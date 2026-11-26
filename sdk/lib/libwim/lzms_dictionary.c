/*
 * Clean-room sliding LZ dictionary and bounded hash-chain match finder.
 * All storage is caller-owned; no legacy codec code or hidden allocations.
 */
#include <libwim/lzms_dictionary.h>

/* Avoid compiler-runtime 64-bit division helpers on 32-bit NT targets. */
static size_t LzmsRingIndex(uint64_t Position, size_t WindowSize)
{
    uint64_t Remainder = 0;
    int Bit;
    for (Bit = 63; Bit >= 0; --Bit) {
        Remainder = (Remainder << 1) | ((Position >> Bit) & 1u);
        if (Remainder >= (uint64_t)WindowSize)
            Remainder -= (uint64_t)WindowSize;
    }
    return (size_t)Remainder;
}
LZMS_DICTIONARY_STATUS LzmsDictionaryInitialize(LZMS_DICTIONARY*d,uint8_t*w,size_t n){if(!d||!w||!n||n>UINT32_MAX)return LZMS_DICTIONARY_INVALID_ARGUMENT;d->Window=w;d->WindowSize=n;d->TotalBytes=0;return LZMS_DICTIONARY_OK;}
LZMS_DICTIONARY_STATUS LzmsDictionaryAppend(LZMS_DICTIONARY*d,const uint8_t*p,size_t n){size_t i;if(!d||!d->Window||(!p&&n))return LZMS_DICTIONARY_INVALID_ARGUMENT;if((uint64_t)n>UINT64_MAX-d->TotalBytes)return LZMS_DICTIONARY_BAD_STATE;for(i=0;i<n;i++){d->Window[LzmsRingIndex(d->TotalBytes,d->WindowSize)]=p[i];++d->TotalBytes;}return LZMS_DICTIONARY_OK;}
uint8_t LzmsDictionaryGetByte(const LZMS_DICTIONARY*d,uint64_t p){if(!d||!d->Window||p>=d->TotalBytes||d->TotalBytes-p>d->WindowSize)return 0;return d->Window[LzmsRingIndex(p,d->WindowSize)];}
size_t LzmsDictionaryAvailable(const LZMS_DICTIONARY*d){if(!d||!d->Window)return 0;return d->TotalBytes<d->WindowSize?(size_t)d->TotalBytes:d->WindowSize;}
LZMS_DICTIONARY_STATUS LzmsDictionaryCopyMatch(LZMS_DICTIONARY*d,uint32_t dist,uint32_t len){uint32_t i;if(!d||!d->Window)return LZMS_DICTIONARY_INVALID_ARGUMENT;if(!dist||dist>LzmsDictionaryAvailable(d))return LZMS_DICTIONARY_BAD_DISTANCE;if((uint64_t)len>UINT64_MAX-d->TotalBytes)return LZMS_DICTIONARY_BAD_STATE;for(i=0;i<len;i++){uint8_t b=d->Window[LzmsRingIndex(d->TotalBytes-dist,d->WindowSize)];d->Window[LzmsRingIndex(d->TotalBytes,d->WindowSize)]=b;++d->TotalBytes;}return LZMS_DICTIONARY_OK;}
static uint32_t hash3(const uint8_t*p){return(uint32_t)p[0]*0x1e35a7bdu^((uint32_t)p[1]*0x9e3779b1u)^((uint32_t)p[2]*0x85ebca6bu);}
LZMS_DICTIONARY_STATUS LzmsMatchFinderInitialize(LZMS_MATCH_FINDER*f,LZMS_DICTIONARY*d,uint64_t*heads,size_t hn,uint64_t*prev,uint64_t*slots,uint32_t chain){size_t i;if(!f||!d||!d->Window||!heads||!hn||!prev||!slots||!chain)return LZMS_DICTIONARY_INVALID_ARGUMENT;f->Dictionary=d;f->HashHeads=heads;f->HashSize=hn;f->Previous=prev;f->SlotPositions=slots;f->MaximumChain=chain;for(i=0;i<hn;i++)heads[i]=LZMS_DICTIONARY_NO_POSITION;for(i=0;i<d->WindowSize;i++){prev[i]=LZMS_DICTIONARY_NO_POSITION;slots[i]=LZMS_DICTIONARY_NO_POSITION;}return LZMS_DICTIONARY_OK;}
LZMS_DICTIONARY_STATUS LzmsMatchFinderInsert(LZMS_MATCH_FINDER*f,const uint8_t*p,size_t n){size_t i;if(!f||!f->Dictionary||(!p&&n))return LZMS_DICTIONARY_INVALID_ARGUMENT;if((uint64_t)n>UINT64_MAX-f->Dictionary->TotalBytes)return LZMS_DICTIONARY_BAD_STATE;for(i=0;i<n;i++){LZMS_DICTIONARY*d=f->Dictionary;uint64_t pos=d->TotalBytes;size_t slotbyte=LzmsRingIndex(pos,d->WindowSize);d->Window[slotbyte]=p[i];if(pos>=2){uint64_t start=pos-2;size_t slot=LzmsRingIndex(start,d->WindowSize);uint8_t key[3];size_t bucket;key[0]=LzmsDictionaryGetByte(d,start);key[1]=LzmsDictionaryGetByte(d,start+1);key[2]=p[i];bucket=hash3(key)%f->HashSize;f->Previous[slot]=f->HashHeads[bucket];f->SlotPositions[slot]=start;f->HashHeads[bucket]=start;}++d->TotalBytes;}return LZMS_DICTIONARY_OK;}
LZMS_DICTIONARY_STATUS LzmsMatchFinderFind(LZMS_MATCH_FINDER*f,const uint8_t*look,size_t n,uint32_t min,uint32_t max,LZMS_MATCH*m){LZMS_DICTIONARY*d;uint64_t candidate,current;uint32_t best=0,dist=0,chains=0,limit;size_t bucket;if(!f||!f->Dictionary||!m||(!look&&n)||min<3||max<min)return LZMS_DICTIONARY_INVALID_ARGUMENT;m->Length=0;m->Distance=0;if(n<min||n<3)return LZMS_DICTIONARY_OK;d=f->Dictionary;current=d->TotalBytes;bucket=hash3(look)%f->HashSize;candidate=f->HashHeads[bucket];limit=(uint32_t)(n<max?n:max);while(candidate!=LZMS_DICTIONARY_NO_POSITION&&chains++<f->MaximumChain){uint64_t distance,next;uint32_t length=0;size_t slot=LzmsRingIndex(candidate,d->WindowSize);if(f->SlotPositions[slot]!=candidate||candidate>=current)break;next=f->Previous[slot];distance=current-candidate;if(distance>d->WindowSize)break;while(length<limit){uint64_t source=candidate+length;uint8_t b;if(source<current)b=LzmsDictionaryGetByte(d,source);else{uint64_t ix=source-current;if(ix>=n)break;b=look[ix];}if(b!=look[length])break;++length;}if(length>=min&&length>best){best=length;dist=(uint32_t)distance;if(length==limit)break;}candidate=next;}m->Length=best;m->Distance=dist;return LZMS_DICTIONARY_OK;}
