#include "rc95ldrntprepare.h"
#include <string.h>
static uint32_t ReadLe32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
RC95LDR_STATUS Rc95LdrOrderBootDrivers(const RC95LDR_BOOT_DRIVER_NODE *Nodes,
    size_t Count, uint32_t *OrderedIndices, size_t OrderedCapacity)
{
    uint64_t emitted=0; size_t out=0,i;
    if ((!Nodes && Count) || (!OrderedIndices && Count) ||
        Count>RC95LDR_MAX_BOOT_DRIVERS || OrderedCapacity<Count)
        return Rc95LdrInvalidParameter;
    if (!Count) return Rc95LdrSuccess;
    for (i=0;i<Count;i++) {
        uint64_t valid=Count==64?UINT64_MAX:((UINT64_C(1)<<Count)-1);
        if ((Nodes[i].Dependencies&~valid) ||
            (Nodes[i].Dependencies&(UINT64_C(1)<<i)))
            return Rc95LdrCorruptData;
    }
    while (out<Count) {
        int progress=0;
        for (i=0;i<Count;i++) {
            uint64_t bit=UINT64_C(1)<<i;
            if (!(emitted&bit) && !(Nodes[i].Dependencies&~emitted)) {
                OrderedIndices[out++]=Nodes[i].OriginalIndex;
                emitted|=bit; progress=1;
            }
        }
        if (!progress) return Rc95LdrCorruptData;
    }
    return Rc95LdrSuccess;
}
RC95LDR_STATUS Rc95LdrValidateSystemHive(const void *HiveBytes,size_t HiveSize,
    uint32_t *HiveDataLength)
{
    const uint8_t *h=(const uint8_t *)HiveBytes;
    uint32_t seq1,seq2,major,minor,type,format,length,checksum,calculated=0;
    size_t i;
    if (!h || !HiveDataLength || HiveSize<RC95LDR_REGF_HEADER_SIZE)
        return Rc95LdrInvalidParameter;
    if (ReadLe32(h)!=0x66676572u) return Rc95LdrCorruptData;
    seq1=ReadLe32(h+4); seq2=ReadLe32(h+8);
    major=ReadLe32(h+0x14); minor=ReadLe32(h+0x18);
    type=ReadLe32(h+0x1c); format=ReadLe32(h+0x20);
    length=ReadLe32(h+0x28); checksum=ReadLe32(h+0x1fc);
    if (seq1!=seq2 || major!=1 || minor<3 || minor>6 ||
        type!=0 || format!=1 || (length&0xfffu) ||
        length>HiveSize-RC95LDR_REGF_HEADER_SIZE)
        return Rc95LdrCorruptData;
    for (i=0;i<0x1fc;i+=4) calculated^=ReadLe32(h+i);
    if (calculated==0xffffffffu) calculated=0xfffffffeu;
    else if (!calculated) calculated=1u;
    if (checksum!=calculated) return Rc95LdrCorruptData;
    *HiveDataLength=length;
    return Rc95LdrSuccess;
}
