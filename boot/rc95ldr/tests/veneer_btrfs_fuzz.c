#include "veneer.h"
#include <stddef.h>
#include <stdint.h>

/* libFuzzer entry point. The validator must safely reject arbitrary byte
 * strings, including truncated superblocks and checksum-valid corruption. */
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    RC95LDR_BTRFS_SUPERBLOCK Info;
    if (!Data && Size) return 0;
    (void)Rc95LdrVeneerValidateSuperblock(
        Data, Size, RC95LDR_BTRFS_SUPERBLOCK_OFFSET, &Info);
    return 0;
}
