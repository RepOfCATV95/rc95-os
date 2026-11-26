#ifndef _LIBWIM_SHA1_H_
#define _LIBWIM_SHA1_H_

#include <stddef.h>
#include <stdint.h>

typedef struct _WIM_SHA1_CONTEXT {
    uint32_t State[5];
    uint64_t TotalBytes;
    uint8_t Buffer[64];
    size_t BufferUsed;
} WIM_SHA1_CONTEXT;

void WimSha1Initialize(WIM_SHA1_CONTEXT *Context);
void WimSha1Update(WIM_SHA1_CONTEXT *Context, const void *Data, size_t Length);
void WimSha1Finalize(WIM_SHA1_CONTEXT *Context, uint8_t Digest[20]);
void WimSha1(const void *Data, size_t Length, uint8_t Digest[20]);

#endif
