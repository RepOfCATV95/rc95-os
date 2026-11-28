#include "rc95ldr_ntfs_runlist.h"
#include <stdio.h>
#include <string.h>
static int TestFailure(int Code,const char *Name)
{fprintf(stderr,"%s failed at check %d\\n",Name,Code);return Code;}

int main(void)
{
    static const uint8_t Pairs[] = {
        0x11, 3, 5,       /* VCN 0..2 -> LCN 5..7 */
        0x11, 2, 0xfe,    /* VCN 3..4 -> LCN 3..4 (signed delta -2) */
        0x01, 4,          /* VCN 5..8 -> sparse */
        0x11, 1, 5,       /* VCN 9 -> LCN 8 (delta from last physical LCN) */
        0
    };
    static const uint8_t OverlappingPhysicalRuns[] = {
        0x11, 3, 5,       /* VCN 0..2 -> LCN 5..7 */
        0x11, 1, 1,       /* VCN 3 -> LCN 6: aliases the first run */
        0
    };
    static const uint8_t BadHeader[] = { 0x10, 0 };
    static const uint8_t Truncated[] = { 0x21, 1, 2 };
    static const uint8_t NegativeFirst[] = { 0x11, 1, 0xff, 0 };
    static const uint8_t MissingTerminator[] = { 0x11, 1, 1 };
    static const uint8_t OverlappingRuns[] = {
        0x11, 3, 5,       /* LCN 5..7 */
        0x11, 2, 2,       /* LCN 7..8 overlaps previous run */
        0
    };
    static const uint8_t TooManyClusters[] = { 0x11, 20, 1, 0 };
    static const uint8_t WideOffset[] = { 0x91, 1, 1, 2, 3, 4, 5, 6, 7, 8, 0 };
    static const uint8_t LcnOverflow[] = {
        0x81, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
        0x81, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
        0x11, 1, 2, 0
    };
    RC95LDR_NTFS_RUN Runs[4];
    size_t Count;
    uint64_t Next;
    RC95LDR_STATUS S;
    memset(Runs, 0, sizeof(Runs));
    S = Rc95LdrNtfsDecodeRunlist(Pairs, sizeof(Pairs), 0, 32, 1,
                                 Runs, 4, &Count, &Next);
    if (S != Rc95LdrSuccess || Count != 4 || Next != 10 ||
        Runs[0].Vcn != 0 || Runs[0].Lcn != 5 || Runs[0].ClusterCount != 3 ||
        Runs[1].Vcn != 3 || Runs[1].Lcn != 3 || Runs[1].ClusterCount != 2 ||
        Runs[2].Vcn != 5 || !Runs[2].Sparse || Runs[2].ClusterCount != 4 ||
        Runs[3].Vcn != 9 || Runs[3].Lcn != 8)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(OverlappingPhysicalRuns,
                                 sizeof(OverlappingPhysicalRuns), 0, 32, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(Pairs, sizeof(Pairs), 0, 32, 0,
                                 Runs, 4, &Count, &Next) != Rc95LdrUnsupported)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(Pairs, sizeof(Pairs), 0, 32, 1,
                                 Runs, 3, &Count, &Next) != Rc95LdrBufferTooSmall)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(BadHeader, sizeof(BadHeader), 0, 32, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(Truncated, sizeof(Truncated), 0, 32, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(NegativeFirst, sizeof(NegativeFirst), 0, 32, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(MissingTerminator, sizeof(MissingTerminator), 0, 32, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(TooManyClusters, sizeof(TooManyClusters), 0, 16, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(OverlappingRuns, sizeof(OverlappingRuns),
                                 0, 32, 1, Runs, 4, &Count, &Next) !=
        Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(Pairs, sizeof(Pairs), 33, 32, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrInvalidParameter)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(WideOffset, sizeof(WideOffset), 0, 32, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrCorruptData)
        return TestFailure(1,"ntfs_runlist");
    if (Rc95LdrNtfsDecodeRunlist(LcnOverflow, sizeof(LcnOverflow), 0, UINT64_MAX, 1,
                                 Runs, 4, &Count, &Next) != Rc95LdrIntegerOverflow)
        return TestFailure(1,"ntfs_runlist");

    {
        RC95LDR_NTFS_RUN Merged[5], First[2], Second[2], Bad[1];
        size_t MergedCount=0;uint64_t Cursor=0;RC95LDR_STATUS AppendStatus;
        First[0].Vcn=0;First[0].Lcn=4;First[0].ClusterCount=3;First[0].Sparse=0;
        First[1].Vcn=3;First[1].Lcn=0;First[1].ClusterCount=2;First[1].Sparse=1;
        Second[0].Vcn=5;Second[0].Lcn=20;Second[0].ClusterCount=2;Second[0].Sparse=0;
        Second[1].Vcn=7;Second[1].Lcn=22;Second[1].ClusterCount=1;Second[1].Sparse=0;
        AppendStatus=Rc95LdrNtfsAppendRunlistExtent(First,2,0,64,Merged,5,&MergedCount,&Cursor);
        if(AppendStatus!=Rc95LdrSuccess||MergedCount!=2||Cursor!=5){
            fprintf(stderr,"first append status=%d count=%lu cursor=%llu\\n",
                (int)AppendStatus,(unsigned long)MergedCount,(unsigned long long)Cursor);
            return TestFailure(21,"ntfs_runlist");
        }
        AppendStatus=Rc95LdrNtfsAppendRunlistExtent(Second,2,5,64,Merged,5,&MergedCount,&Cursor);
        if(AppendStatus!=Rc95LdrSuccess||MergedCount!=4||Cursor!=8){
            fprintf(stderr,"second append status=%d count=%lu cursor=%llu\\n",
                (int)AppendStatus,(unsigned long)MergedCount,(unsigned long long)Cursor);
            return TestFailure(22,"ntfs_runlist");
        }
        Bad[0]=Second[0];Bad[0].Vcn=9;
        AppendStatus=Rc95LdrNtfsAppendRunlistExtent(Bad,1,8,64,Merged,4,&MergedCount,&Cursor);
        if(AppendStatus!=Rc95LdrCorruptData||MergedCount!=4||Cursor!=8)
            return TestFailure(23,"ntfs_runlist");
        Bad[0]=Second[0];Bad[0].Vcn=8;Bad[0].Lcn=5;
        AppendStatus=Rc95LdrNtfsAppendRunlistExtent(Bad,1,8,64,Merged,4,&MergedCount,&Cursor);
        if(AppendStatus!=Rc95LdrCorruptData||MergedCount!=4||Cursor!=8)
            return TestFailure(24,"ntfs_runlist");
    }
    puts("RC95LDR NTFS runlist tests passed");
    return 0;
}
