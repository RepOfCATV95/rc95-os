#include "veneer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t DiskImage[64u * 1024u * 1024u + 8192u];
static uint32_t ImageBlockSize = 512u;
static unsigned ReadCalls;
static unsigned WriteCalls;
static RC95LDR_STATUS WriteImage(void *Context, uint64_t Lba, uint32_t Blocks,
                                 const void *Buffer, size_t Length)
{
    (void)Context; (void)Lba; (void)Blocks; (void)Buffer; (void)Length;
    ++WriteCalls;
    return Rc95LdrIoError;
}
static RC95LDR_STATUS ReadImage(void *Context, uint64_t Lba, uint32_t Blocks,
                                void *Buffer, size_t Length)
{
    uint64_t Offset = Lba * ImageBlockSize;
    (void)Context;
    ++ReadCalls;
    if (Offset > sizeof(DiskImage) || Length > sizeof(DiskImage) - (size_t)Offset ||
        Length != (size_t)Blocks * ImageBlockSize) return Rc95LdrOutOfBounds;
    memcpy(Buffer, DiskImage + (size_t)Offset, Length);
    return Rc95LdrSuccess;
}
static void Put16(uint8_t *P, uint16_t V) { P[0]=(uint8_t)V; P[1]=(uint8_t)(V>>8); }
static void Put32(uint8_t *P, uint32_t V) { P[0]=(uint8_t)V;P[1]=(uint8_t)(V>>8);P[2]=(uint8_t)(V>>16);P[3]=(uint8_t)(V>>24); }
static void Put64(uint8_t *P, uint64_t V) { Put32(P,(uint32_t)V);Put32(P+4,(uint32_t)(V>>32)); }
static void MakeSuperblock(uint8_t *S, uint64_t Bytenr)
{
    memset(S,0,4096);
    Put64(S+48,Bytenr);
    Put64(S+64,RC95LDR_BTRFS_MAGIC);
    Put64(S+72,1);
    Put64(S+80,0x20000);
    Put64(S+88,0x30000);
    Put64(S+112,512u*1024u);
    Put64(S+120,4096);
    Put64(S+128,6); /* root_dir_objectid */
    Put64(S+136,1);
    Put64(S+0x32bu,256); /* first-chunk-tree objectid */
    Put64(S+201,1); /* dev_item.devid */
    Put64(S+241,512u*1024u); /* dev_item.total_bytes */
    Put64(S+249,4096); /* dev_item.bytes_used */
    Put32(S+265,4096); /* dev_item.sector_size */
    Put32(S+144,4096);
    Put32(S+148,16384);
    Put32(S+152,16384);
    Put32(S+156,4096);
    Put32(S+160,97);
    S[0x32bu+8]=0xe4;
    Put64(S+0x32bu+17,4096); /* logical chunk start */
    Put64(S+0x32bu+17+8,4096); /* chunk length */
    Put64(S+0x32bu+17+16,4096); /* stripe_len */
    Put64(S+0x32bu+17+24,2);    /* SYSTEM block group, SINGLE profile */
    Put32(S+0x32bu+17+40,4096); /* chunk sector size */
    Put16(S+0x32bu+17+44,1);
    Put64(S+0x32bu+17+48,1);    /* devid */
    Put64(S+0x32bu+17+56,0);    /* physical stripe offset */
    Put16(S+196,RC95LDR_BTRFS_CSUM_CRC32C);
    Put32(S,Rc95LdrVeneerCrc32c(S+32,4064));
}
int main(void)
{
    RC95LDR_BLOCK_DEVICE Device;
    RC95LDR_VENEER_CONTEXT Context;
    RC95LDR_BTRFS_SUPERBLOCK Info;
    uint8_t Scratch[4096];
    uint64_t Selected=0;
    uint8_t *Sb=DiskImage+65536;
    memset(&Device,0,sizeof(Device));
    Device.BlockSize=512;Device.BlockCount=sizeof(DiskImage)/512;Device.Read=ReadImage;Device.Write=WriteImage;
    Context.Device=&Device;Context.PartitionStartLba=0;
    Context.PartitionBlockCount=Device.BlockCount;
    if(Rc95LdrVeneerCrc32c("123456789",9)!=0xe3069283u) return 10;
    MakeSuperblock(Sb,65536);
    if(Rc95LdrVeneerDiscoverBtrfs(&Context,Scratch,sizeof(Scratch),&Info,&Selected)!=Rc95LdrSuccess ||
       Selected!=65536 || Info.NumDevices!=1 || Info.SectorSize!=4096) return 1;
    if (ReadCalls == 0u) return 305;
    Sb[300]^=1;
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 2;
    MakeSuperblock(Sb,65536);
    Put16(Sb+196,1);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 3;
    MakeSuperblock(Sb,65536);
    Put64(Sb+136,2);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 4;

    /* A chunk that is not a supported single-device SYSTEM chunk is rejected. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+24,0);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 5;

    /* A DATA-only chunk is not a valid system-array mapping. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+24,1);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 7;

    /* Type bits must identify SYSTEM alone, not DATA|SYSTEM or METADATA|SYSTEM. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+24,3u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 9;

    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+24,6u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 11;

    /* METADATA-only is not SYSTEM: DATA=1, SYSTEM=2, METADATA=4. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+24,4u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 32;

    /* RAID profiles are not accepted by the single-device boot profile. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+24,2u | (1u << 4));
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 8;

    /* Reject every unsupported multi-device/profile bit, including parity. */
    {
        static const uint64_t Profiles[] = {
            1ull << 3, /* RAID0 */
            1ull << 5, /* DUP */
            1ull << 6, /* RAID10 */
            1ull << 7, /* RAID5 */
            1ull << 8  /* RAID6 */
        };
        size_t ProfileIndex;
        for (ProfileIndex = 0;
             ProfileIndex < sizeof(Profiles) / sizeof(Profiles[0]);
             ++ProfileIndex)
        {
            MakeSuperblock(Sb,65536);
            Put64(Sb+0x32bu+17+24,2u | Profiles[ProfileIndex]);
            Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
            if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=
               Rc95LdrUnsupported) return 31 + (int)ProfileIndex;
        }
    }

    /* A physical stripe must begin on a filesystem-sector boundary. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+48,1);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 12;

    /* Stripe length must not be smaller than the filesystem sector. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+16,2048);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 13;

    /* A single device's advertised capacity must match the filesystem total. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+241,2048u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 14;

    /* A chunk's physical range may not extend beyond the device item. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu+17+56,512u*1024u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrOutOfBounds) return 22;

    /* Overlapping logical chunk ranges are corrupt even when each stripe fits. */
    MakeSuperblock(Sb,65536);
    memcpy(Sb+0x32bu+97,Sb+0x32bu,97);
    Put64(Sb+0x32bu+97+9,4096);
    Put32(Sb+160,194);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 23;

    /* Adjacent, ordered SYSTEM chunks are valid. */
    MakeSuperblock(Sb,65536);
    memcpy(Sb+0x32bu+97,Sb+0x32bu,97);
    Put64(Sb+0x32bu+97+9,8192);
    Put64(Sb+0x32bu+97+17,4096);
    Put64(Sb+0x32bu+97+56,4096);
    Put32(Sb+160,194);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrSuccess) return 24;

    MakeSuperblock(Sb,65536);
    Put64(Sb+249,1024u*1024u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 15;

    MakeSuperblock(Sb,65536);
    Put32(Sb+265,512);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 16;

    /* dev_item.total_bytes must also fit the selected partition. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+241,2u*1024u*1024u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerDiscoverBtrfs(&Context,Scratch,sizeof(Scratch),&Info,&Selected)!=Rc95LdrCorruptData) return 17;

    /* Superblock total_bytes may not extend beyond the selected partition. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+112,2u*1024u*1024u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerDiscoverBtrfs(&Context,Scratch,sizeof(Scratch),&Info,&Selected)!=Rc95LdrCorruptData) return 6;

    /* Corrupt primary superblock must fall back to the valid 64 MiB mirror. */
    memset(DiskImage,0,sizeof(DiskImage));
    MakeSuperblock(DiskImage+65536,65536);
    DiskImage[65536+300]^=1;
    MakeSuperblock(DiskImage+64u*1024u*1024u,64u*1024u*1024u);
    Selected=0;
    if(Rc95LdrVeneerDiscoverBtrfs(&Context,Scratch,sizeof(Scratch),&Info,&Selected)!=Rc95LdrSuccess ||
       Selected!=64u*1024u*1024u) return 18;

    /* Discovery must apply partition-relative LBAs with 4 KiB logical blocks. */
    memset(DiskImage,0,sizeof(DiskImage));
    ImageBlockSize=4096u;
    Device.BlockSize=ImageBlockSize;
    Device.BlockCount=sizeof(DiskImage)/ImageBlockSize;
    Context.PartitionStartLba=2u;
    Context.PartitionBlockCount=128u;
    MakeSuperblock(DiskImage+2u*ImageBlockSize+65536u,65536u);
    Selected=0;
    if(Rc95LdrVeneerDiscoverBtrfs(&Context,Scratch,sizeof(Scratch),&Info,&Selected)!=Rc95LdrSuccess ||
       Selected!=65536u) return 19;

    /* Reject non-power-of-two block sizes before attempting block reads. */
    ImageBlockSize=3072u;
    Device.BlockSize=ImageBlockSize;
    if(Rc95LdrVeneerDiscoverBtrfs(&Context,Scratch,sizeof(Scratch),&Info,&Selected)!=Rc95LdrInvalidParameter) return 20;

    /* Reject partition extents that overflow the backing device. */
    ImageBlockSize=512u;
    Device.BlockSize=ImageBlockSize;
    Device.BlockCount=16u;
    Context.PartitionStartLba=8u;
    Context.PartitionBlockCount=16u;
    if(Rc95LdrVeneerDiscoverBtrfs(&Context,Scratch,sizeof(Scratch),&Info,&Selected)!=Rc95LdrOutOfBounds) return 21;

    /* Only first-chunk-tree keys may appear in the superblock system array. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+0x32bu,257);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrUnsupported) return 28;

    /* Root and chunk-root pointers must leave room for a complete tree node. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+112,520192);
    Put64(Sb+241,520192);
    Put64(Sb+80,507904);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 29;

    /* The superblock's root-directory object ID is part of the format contract. */
    MakeSuperblock(Sb,65536);
    Put64(Sb+128,7);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 30;

    /* The public validator rejects truncated input and a mismatched mirror address. */
    MakeSuperblock(Sb,65536);
    if(Rc95LdrVeneerValidateSuperblock(Sb,4095,65536,&Info)!=Rc95LdrBufferTooSmall) return 25;
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,131072,&Info)!=Rc95LdrCorruptData) return 26;

    /* A malformed system-array length cannot consume bytes outside the superblock. */
    MakeSuperblock(Sb,65536);
    Put32(Sb+160,RC95LDR_BTRFS_MAX_SYSTEM_CHUNK_ARRAY+1u);
    Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
    if(Rc95LdrVeneerValidateSuperblock(Sb,4096,65536,&Info)!=Rc95LdrCorruptData) return 27;


    /* Exhaustively reject every currently unsupported incompatibility bit.
       This contributes 64 independent feature-gate regression cases. */
    {
        unsigned Bit;
        for (Bit = 0; Bit != 64u; ++Bit)
        {
            MakeSuperblock(Sb,65536);
            Put64(Sb+188,(uint64_t)1u << Bit);
            Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
            if (Rc95LdrVeneerValidateSuperblock(
                    Sb,4096,65536,&Info) != Rc95LdrUnsupported)
                return 100 + (int)Bit;
        }
    }

    /* Only CRC32C is implemented. Exercise a range of unknown checksum
       identifiers so future checksum additions require deliberate tests. */
    {
        unsigned Type;
        for (Type = 1; Type != 65u; ++Type)
        {
            MakeSuperblock(Sb,65536);
            Put16(Sb+196,(uint16_t)Type);
            Put32(Sb,Rc95LdrVeneerCrc32c(Sb+32,4064));
            if (Rc95LdrVeneerValidateSuperblock(
                    Sb,4096,65536,&Info) != Rc95LdrUnsupported)
                return 200 + (int)Type;
        }
    }

    /* Deterministic mutation fuzzing: exercise short buffers, malformed
       metadata, checksum-valid mutations and random checksum failures. */
    {
        uint8_t Baseline[4096], Candidate[4096];
        uint32_t State = 0x95b7f00du;
        unsigned Iteration;
        MakeSuperblock(Baseline,65536);
        for (Iteration = 0; Iteration != 1024u; ++Iteration)
        {
            unsigned Mutations = 1u + (Iteration % 8u);
            unsigned Mutation;
            size_t InputLength;
            memcpy(Candidate, Baseline, sizeof(Candidate));
            for (Mutation = 0; Mutation != Mutations; ++Mutation)
            {
                size_t Position;
                State ^= State << 13;
                State ^= State >> 17;
                State ^= State << 5;
                Position = 32u + (State % (sizeof(Candidate) - 32u));
                Candidate[Position] ^= (uint8_t)(1u << ((State >> 8) & 7u));
            }
            if ((Iteration & 1u) == 0)
                Put32(Candidate, Rc95LdrVeneerCrc32c(Candidate + 32, 4064));
            State ^= State << 13;
            State ^= State >> 17;
            State ^= State << 5;
            InputLength = (size_t)(State % (sizeof(Candidate) + 1u));
            (void)Rc95LdrVeneerValidateSuperblock(
                Candidate, InputLength, 65536, &Info);
        }
    }

    /* Discovery must use only the block-device read path. */
    if (WriteCalls != 0u) return 300;

    /* Buffer sizing is explicit and failed attempts do not publish stale data. */
    memset(&Info, 0xa5, sizeof(Info));
    Selected = 0xfeedu;
    if (Rc95LdrVeneerDiscoverBtrfs(&Context, Scratch, 4095u, &Info, &Selected) !=
        Rc95LdrBufferTooSmall) return 301;
    if (Selected != 0u || Info.Root != 0u || Info.TotalBytes != 0u) return 302;

    Context.PartitionStartLba = Device.BlockCount;
    Context.PartitionBlockCount = 1u;
    memset(&Info, 0xa5, sizeof(Info));
    Selected = 0xfeedu;
    if (Rc95LdrVeneerDiscoverBtrfs(&Context, Scratch, sizeof(Scratch), &Info, &Selected) !=
        Rc95LdrOutOfBounds) return 303;
    if (Selected != 0u || Info.Root != 0u || Info.TotalBytes != 0u) return 304;

    puts("RC95LDR VENEER BTRFS tests passed");
    return 0;
}
