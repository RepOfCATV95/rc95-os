#include "rc95ldr_ntfs_geometry.h"
#include <string.h>

static int
NtfsPowerOfTwo(uint32_t Value)
{
    return Value != 0 && (Value & (Value - 1u)) == 0;
}

static RC95LDR_STATUS
NtfsDecodeRecordSize(uint8_t Encoding, uint32_t ClusterBytes, uint32_t *Bytes)
{
    uint32_t Value;
    if (Encoding == 0 || !Bytes)
        return Rc95LdrCorruptData;
    if (Encoding < 0x80u)
    {
        if ((uint32_t)Encoding > UINT32_MAX / ClusterBytes)
            return Rc95LdrIntegerOverflow;
        Value = (uint32_t)Encoding * ClusterBytes;
    }
    else
    {
        unsigned Shift = 256u - (unsigned)Encoding;
        if (Shift >= 32u)
            return Rc95LdrCorruptData;
        Value = 1u << Shift;
    }
    if (Value < 512u || Value > 65536u || !NtfsPowerOfTwo(Value))
        return Rc95LdrCorruptData;
    *Bytes = Value;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrNtfsParseGeometry(
    const void *BootSector,
    size_t BootSectorLength,
    uint32_t DeviceBlockSize,
    uint64_t PartitionBlockCount,
    RC95LDR_NTFS_GEOMETRY *Geometry)
{
    const uint8_t *B = (const uint8_t *)BootSector;
    RC95LDR_NTFS_GEOMETRY G;
    uint32_t BytesPerSector, SectorsPerCluster;
    uint64_t ClusterCount;
    RC95LDR_STATUS Status;
    if (!B || !Geometry || BootSectorLength < 512u ||
        !PartitionBlockCount)
        return Rc95LdrInvalidParameter;
    if (B[510] != 0x55 || B[511] != 0xaa ||
        memcmp(B + 3, "NTFS    ", 8) != 0)
        return Rc95LdrInvalidImage;
    BytesPerSector = Rc95LdrReadU16(B + 11);
    SectorsPerCluster = B[13];
    /* RC95LDR block I/O uses logical device blocks.  Do not accept a
       geometry that would make a sector read span a different block size. */
    if ((DeviceBlockSize != 512u && DeviceBlockSize != 1024u &&
         DeviceBlockSize != 2048u && DeviceBlockSize != 4096u) ||
        (BytesPerSector != 512u && BytesPerSector != 1024u &&
         BytesPerSector != 2048u && BytesPerSector != 4096u) ||
        BytesPerSector != DeviceBlockSize ||
        !NtfsPowerOfTwo(SectorsPerCluster) || SectorsPerCluster > 128u ||
        BytesPerSector > 65536u / SectorsPerCluster)
        return Rc95LdrCorruptData;
    if (BootSectorLength < BytesPerSector)
        return Rc95LdrBufferTooSmall;
    memset(&G, 0, sizeof(G));
    G.BytesPerSector = BytesPerSector;
    G.SectorsPerCluster = SectorsPerCluster;
    G.BytesPerCluster = BytesPerSector * SectorsPerCluster;
    G.TotalSectors = Rc95LdrReadU64(B + 40);
    /* Keep every later sector-to-byte conversion representable.  In
       particular, NTFS index allocation bounds use ClusterCount * BytesPerCluster. */
    if (G.TotalSectors > UINT64_MAX / BytesPerSector)
        return Rc95LdrIntegerOverflow;
    G.MftLcn = Rc95LdrReadU64(B + 48);
    G.MftMirrorLcn = Rc95LdrReadU64(B + 56);
    G.VolumeSerial = Rc95LdrReadU64(B + 72);
    if (!G.TotalSectors || G.TotalSectors > PartitionBlockCount)
        return Rc95LdrCorruptData;
    ClusterCount = G.TotalSectors / SectorsPerCluster;
    if (!ClusterCount || G.MftLcn >= ClusterCount ||
        G.MftMirrorLcn >= ClusterCount)
        return Rc95LdrCorruptData;
    G.ClusterCount = ClusterCount;
    Status = NtfsDecodeRecordSize(B[64], G.BytesPerCluster,
                                  &G.FileRecordBytes);
    if (Status != Rc95LdrSuccess)
        return Status;
    Status = NtfsDecodeRecordSize(B[68], G.BytesPerCluster,
                                  &G.IndexBufferBytes);
    if (Status != Rc95LdrSuccess)
        return Status;

    /* NTFS update-sequence fixups operate on complete logical sectors.
       Reject metadata records that cannot contain at least one sector or
       whose boundaries cannot be mapped to device blocks exactly. */
    if (G.FileRecordBytes < BytesPerSector ||
        G.IndexBufferBytes < BytesPerSector ||
        G.FileRecordBytes % BytesPerSector != 0 ||
        G.IndexBufferBytes % BytesPerSector != 0)
        return Rc95LdrCorruptData;

    /* A cluster count rounded down must still leave both bootstrap metadata
       locations inside the addressable data-cluster range. */
    if (G.ClusterCount < 2u)
        return Rc95LdrCorruptData;
    *Geometry = G;
    return Rc95LdrSuccess;
}
