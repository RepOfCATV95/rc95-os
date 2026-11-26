#ifndef _LIBWIM_WIM_CODEC_H_
#define _LIBWIM_WIM_CODEC_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIM_CODEC_ABI_VERSION 2u
#define WIM_CODEC_MAX_PROVIDERS 8u

typedef enum _WIM_CODEC_ALGORITHM {
    WIM_CODEC_XPRESS = 1,
    WIM_CODEC_XPRESS_HUFFMAN = 2,
    WIM_CODEC_LZX = 3,
    WIM_CODEC_LZMS = 4
} WIM_CODEC_ALGORITHM;

typedef enum _WIM_CODEC_DIRECTION {
    WIM_CODEC_DECODE = 1,
    WIM_CODEC_ENCODE = 2
} WIM_CODEC_DIRECTION;

typedef enum _WIM_CODEC_STATUS {
    WIM_CODEC_STATUS_OK = 0,
    WIM_CODEC_STATUS_NEED_INPUT,
    WIM_CODEC_STATUS_NEED_OUTPUT,
    WIM_CODEC_STATUS_STREAM_END,
    WIM_CODEC_STATUS_INVALID_ARGUMENT,
    WIM_CODEC_STATUS_UNSUPPORTED,
    WIM_CODEC_STATUS_OUT_OF_MEMORY,
    WIM_CODEC_STATUS_CORRUPT_DATA,
    WIM_CODEC_STATUS_BUFFER_ERROR,
    WIM_CODEC_STATUS_INTERNAL_ERROR
} WIM_CODEC_STATUS;

#define WIM_CODEC_CAP_DECODE       0x00000001u
#define WIM_CODEC_CAP_ENCODE       0x00000002u
#define WIM_CODEC_CAP_STREAMING    0x00000004u
#define WIM_CODEC_CAP_WIM_CHUNKS   0x00000008u
#define WIM_CODEC_PROCESS_FINAL    0x00000001u

typedef void *(*WIM_CODEC_ALLOCATE)(void *Context, size_t Size);
typedef void (*WIM_CODEC_FREE)(void *Context, void *Allocation);

typedef struct _WIM_CODEC_ALLOCATOR {
    void *Context;
    WIM_CODEC_ALLOCATE Allocate;
    WIM_CODEC_FREE Free;
} WIM_CODEC_ALLOCATOR;

typedef struct _WIM_CODEC_OPTIONS {
    uint32_t StructureSize;
    uint32_t Flags;
    uint32_t CompressionLevel;
    uint32_t WindowBits;
    uint32_t ChunkSize;
    /*
     * Decompressed-size contract for the operation. ExpectedOutputSize is
     * exact when nonzero; zero means the final size is not known in advance.
     * MaximumOutputSize is a hard upper bound; zero means no caller-supplied
     * limit. Decoder providers MUST enforce a nonzero maximum before writing
     * output and MUST reject output beyond ExpectedOutputSize when it is set.
     */
    uint64_t ExpectedOutputSize;
    uint64_t MaximumOutputSize;
    WIM_CODEC_ALLOCATOR Allocator;
} WIM_CODEC_OPTIONS;

typedef struct _WIM_CODEC_PROVIDER WIM_CODEC_PROVIDER;
typedef struct _WIM_CODEC_INSTANCE WIM_CODEC_INSTANCE;

typedef WIM_CODEC_STATUS (*WIM_CODEC_CREATE)(
    const WIM_CODEC_OPTIONS *Options,
    WIM_CODEC_DIRECTION Direction,
    void **State);
typedef void (*WIM_CODEC_DESTROY)(void *State);
typedef WIM_CODEC_STATUS (*WIM_CODEC_PROCESS)(
    void *State,
    const uint8_t *Input,
    size_t InputSize,
    size_t *InputUsed,
    uint8_t *Output,
    size_t OutputSize,
    size_t *OutputUsed,
    uint32_t ProcessFlags);
typedef WIM_CODEC_STATUS (*WIM_CODEC_RESET)(void *State);
typedef size_t (*WIM_CODEC_BOUND)(size_t InputSize, uint32_t ChunkSize);

struct _WIM_CODEC_PROVIDER {
    uint32_t AbiVersion;
    uint32_t StructureSize;
    WIM_CODEC_ALGORITHM Algorithm;
    uint32_t Capabilities;
    const char *Name;
    WIM_CODEC_CREATE Create;
    WIM_CODEC_DESTROY Destroy;
    WIM_CODEC_PROCESS Process;
    WIM_CODEC_RESET Reset;
    WIM_CODEC_BOUND MaximumCompressedSize;
    WIM_CODEC_BOUND MaximumDecompressedSize;
};

struct _WIM_CODEC_INSTANCE {
    const WIM_CODEC_PROVIDER *Provider;
    void *State;
    WIM_CODEC_ALLOCATOR Allocator;
    WIM_CODEC_DIRECTION Direction;
};

WIM_CODEC_STATUS WimCodecRegisterProvider(const WIM_CODEC_PROVIDER *Provider);
const WIM_CODEC_PROVIDER *WimCodecFindProvider(WIM_CODEC_ALGORITHM Algorithm);
uint32_t WimCodecGetCapabilities(WIM_CODEC_ALGORITHM Algorithm);

/* Registers built-in providers. Safe to call repeatedly during initialization. */
WIM_CODEC_STATUS WimCodecRegisterBuiltins(void);
WIM_CODEC_STATUS WimCodecRegisterXpressProvider(void);
WIM_CODEC_STATUS WimCodecRegisterXpressHuffmanProvider(void);
WIM_CODEC_STATUS WimCodecRegisterLzxProvider(void);
WIM_CODEC_STATUS WimCodecRegisterLzmsProvider(void);

WIM_CODEC_STATUS WimCodecOpen(
    WIM_CODEC_INSTANCE **Instance,
    WIM_CODEC_ALGORITHM Algorithm,
    WIM_CODEC_DIRECTION Direction,
    const WIM_CODEC_OPTIONS *Options);
void WimCodecClose(WIM_CODEC_INSTANCE *Instance);

WIM_CODEC_STATUS WimCodecProcess(
    WIM_CODEC_INSTANCE *Instance,
    const uint8_t *Input,
    size_t InputSize,
    size_t *InputUsed,
    uint8_t *Output,
    size_t OutputSize,
    size_t *OutputUsed,
    uint32_t ProcessFlags);
WIM_CODEC_STATUS WimCodecReset(WIM_CODEC_INSTANCE *Instance);

size_t WimCodecMaximumCompressedSize(
    WIM_CODEC_ALGORITHM Algorithm, size_t InputSize, uint32_t ChunkSize);
size_t WimCodecMaximumDecompressedSize(
    WIM_CODEC_ALGORITHM Algorithm, size_t InputSize, uint32_t ChunkSize);

#ifdef __cplusplus
}
#endif
#endif
