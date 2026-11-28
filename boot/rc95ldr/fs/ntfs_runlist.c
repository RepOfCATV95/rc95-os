#include "rc95ldr_ntfs_runlist.h"
#include <string.h>

static int
NtfsRunsOverlap(uint64_t FirstLcn, uint64_t FirstCount,
                uint64_t SecondLcn, uint64_t SecondCount)
{
    /* Callers have already checked each end against VolumeClusterCount. */
    return FirstLcn < SecondLcn + SecondCount &&
           SecondLcn < FirstLcn + FirstCount;
}

static uint64_t
NtfsReadUnsigned(const uint8_t *Bytes, unsigned Length)
{
    uint64_t Value = 0;
    unsigned I;
    for (I = 0; I < Length; ++I)
        Value |= (uint64_t)Bytes[I] << (I * 8u);
    return Value;
}

RC95LDR_STATUS
Rc95LdrNtfsDecodeRunlist(
    const uint8_t *MappingPairs,
    size_t MappingPairsLength,
    uint64_t StartingVcn,
    uint64_t VolumeClusterCount,
    int AllowSparse,
    RC95LDR_NTFS_RUN *Runs,
    size_t RunCapacity,
    size_t *RunCount,
    uint64_t *NextVcn)
{
    size_t Offset = 0, Count = 0;
    uint64_t Vcn = StartingVcn, PreviousLcn = 0;
    int HavePreviousLcn = 0;
    if (!MappingPairs || !MappingPairsLength || !VolumeClusterCount ||
        !RunCount || !NextVcn || (!Runs && RunCapacity) ||
        StartingVcn > VolumeClusterCount)
        return Rc95LdrInvalidParameter;
    *RunCount = 0;
    *NextVcn = StartingVcn;
    while (Offset < MappingPairsLength)
    {
        uint8_t Header = MappingPairs[Offset++];
        unsigned LengthBytes, OffsetBytes;
        uint64_t Clusters, Lcn = 0;
        RC95LDR_NTFS_RUN Run;
        if (Header == 0)
        {
            *RunCount = Count;
            *NextVcn = Vcn;
            return Rc95LdrSuccess;
        }
        LengthBytes = Header & 0x0fu;
        OffsetBytes = Header >> 4;
        if (!LengthBytes || LengthBytes > 8u || OffsetBytes > 8u ||
            LengthBytes + OffsetBytes > MappingPairsLength - Offset)
            return Rc95LdrCorruptData;
        Clusters = NtfsReadUnsigned(MappingPairs + Offset, LengthBytes);
        Offset += LengthBytes;
        if (!Clusters || Clusters > UINT64_MAX - Vcn ||
            Vcn + Clusters > VolumeClusterCount)
            return Rc95LdrCorruptData;
        if (!OffsetBytes)
        {
            if (!AllowSparse)
                return Rc95LdrUnsupported;
            Run.Sparse = 1;
            Run.Lcn = 0;
        }
        else
        {
            uint64_t Raw = NtfsReadUnsigned(MappingPairs + Offset, OffsetBytes);
            uint64_t Magnitude;
            int Negative = (MappingPairs[Offset + OffsetBytes - 1u] & 0x80u) != 0;
            if (Negative)
            {
                unsigned Bits = OffsetBytes * 8u;
                Magnitude = ((~Raw) & (Bits == 64u ? UINT64_MAX :
                              ((UINT64_C(1) << Bits) - 1u))) + 1u;
                if (!HavePreviousLcn || Magnitude > PreviousLcn)
                    return Rc95LdrCorruptData;
                Lcn = PreviousLcn - Magnitude;
            }
            else
            {
                if (Raw > UINT64_MAX - PreviousLcn)
                    return Rc95LdrIntegerOverflow;
                Lcn = PreviousLcn + Raw;
            }
            if (Lcn >= VolumeClusterCount ||
                Clusters > VolumeClusterCount - Lcn)
                return Rc95LdrCorruptData;

            /* Physical allocation runs must never alias the same clusters. */
            {
                size_t I;
                for (I = 0; I < Count; ++I)
                {
                    if (!Runs[I].Sparse &&
                        NtfsRunsOverlap(Lcn, Clusters,
                                        Runs[I].Lcn, Runs[I].ClusterCount))
                        return Rc95LdrCorruptData;
                }
            }
            PreviousLcn = Lcn;
            HavePreviousLcn = 1;
            Run.Sparse = 0;
        }
        if (Count >= RunCapacity || !Runs)
            return Rc95LdrBufferTooSmall;
        Run.Vcn = Vcn;
        Run.Lcn = Lcn;
        Run.ClusterCount = Clusters;
        Runs[Count++] = Run;
        Vcn += Clusters;
        Offset += OffsetBytes;
    }
    return Rc95LdrCorruptData; /* missing mapping-pairs terminator */
}

RC95LDR_STATUS
Rc95LdrNtfsAppendRunlistExtent(const RC95LDR_NTFS_RUN *Extent,
    size_t ExtentCount,uint64_t ExpectedStartingVcn,
    uint64_t VolumeClusterCount,RC95LDR_NTFS_RUN *Runs,
    size_t RunCapacity,size_t *RunCount,uint64_t *NextVcn)
{
    size_t I,J;
    uint64_t Cursor=ExpectedStartingVcn;
    if((!Extent&&ExtentCount)||!Runs||!RunCount||!NextVcn||
       !VolumeClusterCount||*RunCount>RunCapacity||
       *NextVcn!=ExpectedStartingVcn)
        return Rc95LdrInvalidParameter;
    for(J=0;J<*RunCount;J++){
        const RC95LDR_NTFS_RUN *Old=&Runs[J];
        if(!Old->ClusterCount||Old->Sparse>1u||
           Old->Vcn>UINT64_MAX-Old->ClusterCount||
           Old->Vcn+Old->ClusterCount>VolumeClusterCount||
           (!Old->Sparse&&(Old->Lcn>=VolumeClusterCount||
             Old->ClusterCount>VolumeClusterCount-Old->Lcn)))
            return Rc95LdrCorruptData;
    }
    for(I=0;I<ExtentCount;I++){
        const RC95LDR_NTFS_RUN *R=&Extent[I];
        if(!R->ClusterCount||R->Vcn!=Cursor||
           R->ClusterCount>UINT64_MAX-Cursor||
           Cursor+R->ClusterCount>VolumeClusterCount||
           R->Sparse>1u)
            return Rc95LdrCorruptData;
        if(!R->Sparse&&(R->Lcn>=VolumeClusterCount||
           R->ClusterCount>VolumeClusterCount-R->Lcn))
            return Rc95LdrCorruptData;
        if(!R->Sparse){
            for(J=0;J<*RunCount;J++){
                const RC95LDR_NTFS_RUN *Old=&Runs[J];
                if(!Old->Sparse&&R->Lcn<Old->Lcn+Old->ClusterCount&&
                   Old->Lcn<R->Lcn+R->ClusterCount)
                    return Rc95LdrCorruptData;
            }
            for(J=0;J<I;J++){
                const RC95LDR_NTFS_RUN *Old=&Extent[J];
                if(!Old->Sparse&&R->Lcn<Old->Lcn+Old->ClusterCount&&
                   Old->Lcn<R->Lcn+R->ClusterCount)
                    return Rc95LdrCorruptData;
            }
        }
        Cursor+=R->ClusterCount;
    }
    if(ExtentCount>RunCapacity-*RunCount)
        return Rc95LdrBufferTooSmall;
    /* Commit only after the entire extent has passed validation. */
    for(I=0;I<ExtentCount;I++)Runs[*RunCount+I]=Extent[I];
    *RunCount+=ExtentCount;
    *NextVcn=Cursor;
    return Rc95LdrSuccess;
}
