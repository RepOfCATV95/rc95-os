/*
 * PROJECT: RC95-OS libwim
 * LICENSE: GPL-3.0-or-later
 * PURPOSE: Filesystem-to-WIM capture API.
 */
#ifndef _LIBWIM_WIM_CAPTURE_H_
#define _LIBWIM_WIM_CAPTURE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _WIM_CAPTURE_STATUS {
    WIM_CAPTURE_OK = 0,
    WIM_CAPTURE_INVALID_ARGUMENT,
    WIM_CAPTURE_OUT_OF_MEMORY,
    WIM_CAPTURE_IO_ERROR,
    WIM_CAPTURE_LIMIT_EXCEEDED,
    WIM_CAPTURE_UNSUPPORTED,
    WIM_CAPTURE_FAILED
} WIM_CAPTURE_STATUS;

typedef struct _WIM_CAPTURE_OPTIONS {
    uint32_t StructureSize;
    const char *ImageName;
    const char *Description;
    uint64_t MaximumTotalBytes; /* Zero selects the 1 GiB default. */
} WIM_CAPTURE_OPTIONS;

/*
 * Capture regular files and directories recursively into an XPRESS-compressed,
 * single-part WIM. The source must be a directory and the output is created or
 * truncated. Keep the output outside the source tree.
 *
 * Names are stored as UTF-16LE in WIM metadata. On Windows, the current ANSI
 * code page is used to enumerate names before conversion to UTF-8; POSIX names
 * must be valid UTF-8. On Windows, owner/group/DACL security descriptors,
 * NTFS named data streams, and NTFS hard-link group identifiers are captured.
 * Sparse-file attributes and logical contents are retained; extraction recreates
 * holes from long zero runs in sparse-marked files.
 * Symbolic links, reparse points, devices, sockets, and SACLs are not captured.
 * Identical non-empty stream contents are deduplicated by SHA-1 followed by a
 * byte-for-byte comparison.
 *
 * Options may be NULL. Otherwise StructureSize must be sizeof the structure.
 * A zero MaximumTotalBytes selects the 1 GiB default. A successful call
 * returns WIM_CAPTURE_OK; failures are reported using WIM_CAPTURE_STATUS.
 */
WIM_CAPTURE_STATUS
WimCaptureDirectory(const char *SourceDirectory,
                   const char *OutputWim,
                   const WIM_CAPTURE_OPTIONS *Options);

#ifdef __cplusplus
}
#endif
#endif
