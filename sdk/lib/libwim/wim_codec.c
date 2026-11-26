#include <string.h>
#include <libwim/wim_codec.h>

static const WIM_CODEC_PROVIDER *WimCodecProviders[WIM_CODEC_MAX_PROVIDERS];
static uint32_t WimCodecProviderCount;
static int WimCodecBuiltinsRegistered;

static int
WimCodecProviderIsValid(const WIM_CODEC_PROVIDER *Provider)
{
    if (Provider == NULL ||
        Provider->AbiVersion != WIM_CODEC_ABI_VERSION ||
        Provider->StructureSize < sizeof(WIM_CODEC_PROVIDER) ||
        Provider->Algorithm < WIM_CODEC_XPRESS ||
        Provider->Algorithm > WIM_CODEC_LZMS ||
        Provider->Name == NULL || Provider->Create == NULL ||
        Provider->Destroy == NULL || Provider->Process == NULL)
        return 0;

    /* Zero capabilities is valid for a registered, not-yet-implemented codec. */
    return (Provider->Capabilities &
            ~(WIM_CODEC_CAP_DECODE | WIM_CODEC_CAP_ENCODE |
              WIM_CODEC_CAP_STREAMING | WIM_CODEC_CAP_WIM_CHUNKS)) == 0;
}

WIM_CODEC_STATUS
WimCodecRegisterProvider(const WIM_CODEC_PROVIDER *Provider)
{
    uint32_t Index;

    if (!WimCodecProviderIsValid(Provider))
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;

    for (Index = 0; Index < WimCodecProviderCount; ++Index) {
        if (WimCodecProviders[Index]->Algorithm == Provider->Algorithm)
            return WimCodecProviders[Index] == Provider
                ? WIM_CODEC_STATUS_OK : WIM_CODEC_STATUS_INVALID_ARGUMENT;
    }

    if (WimCodecProviderCount == WIM_CODEC_MAX_PROVIDERS)
        return WIM_CODEC_STATUS_BUFFER_ERROR;

    WimCodecProviders[WimCodecProviderCount++] = Provider;
    return WIM_CODEC_STATUS_OK;
}

const WIM_CODEC_PROVIDER *
WimCodecFindProvider(WIM_CODEC_ALGORITHM Algorithm)
{
    uint32_t Index;

    /* Lazy registration keeps built-in codecs discoverable without relying
       on compiler-specific constructors or platform startup hooks. */
    if (!WimCodecBuiltinsRegistered)
        (void)WimCodecRegisterBuiltins();
    for (Index = 0; Index < WimCodecProviderCount; ++Index)
        if (WimCodecProviders[Index]->Algorithm == Algorithm)
            return WimCodecProviders[Index];
    return NULL;
}

uint32_t
WimCodecGetCapabilities(WIM_CODEC_ALGORITHM Algorithm)
{
    const WIM_CODEC_PROVIDER *Provider = WimCodecFindProvider(Algorithm);
    return Provider != NULL ? Provider->Capabilities : 0;
}

WIM_CODEC_STATUS
WimCodecOpen(
    WIM_CODEC_INSTANCE **Instance,
    WIM_CODEC_ALGORITHM Algorithm,
    WIM_CODEC_DIRECTION Direction,
    const WIM_CODEC_OPTIONS *Options)
{
    const WIM_CODEC_PROVIDER *Provider;
    WIM_CODEC_INSTANCE *NewInstance;
    WIM_CODEC_STATUS Status;
    uint32_t RequiredCapability;

    if (Instance == NULL)
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    *Instance = NULL;

    if (Options == NULL ||
        Options->StructureSize < sizeof(WIM_CODEC_OPTIONS) ||
        (Options->MaximumOutputSize != 0 &&
         Options->ExpectedOutputSize > Options->MaximumOutputSize) ||
        Options->Allocator.Allocate == NULL ||
        Options->Allocator.Free == NULL ||
        (Direction != WIM_CODEC_DECODE && Direction != WIM_CODEC_ENCODE))
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;

    Provider = WimCodecFindProvider(Algorithm);
    if (Provider == NULL)
        return WIM_CODEC_STATUS_UNSUPPORTED;

    RequiredCapability = Direction == WIM_CODEC_DECODE
        ? WIM_CODEC_CAP_DECODE : WIM_CODEC_CAP_ENCODE;
    if ((Provider->Capabilities & RequiredCapability) == 0)
        return WIM_CODEC_STATUS_UNSUPPORTED;

    NewInstance = (WIM_CODEC_INSTANCE *)Options->Allocator.Allocate(
        Options->Allocator.Context, sizeof(*NewInstance));
    if (NewInstance == NULL)
        return WIM_CODEC_STATUS_OUT_OF_MEMORY;

    memset(NewInstance, 0, sizeof(*NewInstance));
    NewInstance->Provider = Provider;
    NewInstance->Allocator = Options->Allocator;
    NewInstance->Direction = Direction;
    Status = Provider->Create(Options, Direction, &NewInstance->State);
    if (Status != WIM_CODEC_STATUS_OK || NewInstance->State == NULL) {
        /* A provider that fails after allocating state must not leak it. */
        if (NewInstance->State != NULL)
            Provider->Destroy(NewInstance->State);
        Options->Allocator.Free(Options->Allocator.Context, NewInstance);
        return Status == WIM_CODEC_STATUS_OK
            ? WIM_CODEC_STATUS_INTERNAL_ERROR : Status;
    }

    *Instance = NewInstance;
    return WIM_CODEC_STATUS_OK;
}

void
WimCodecClose(WIM_CODEC_INSTANCE *Instance)
{
    WIM_CODEC_ALLOCATOR Allocator;
    if (Instance == NULL)
        return;
    Allocator = Instance->Allocator;
    if (Instance->Provider != NULL && Instance->State != NULL)
        Instance->Provider->Destroy(Instance->State);
    memset(Instance, 0, sizeof(*Instance));
    Allocator.Free(Allocator.Context, Instance);
}

WIM_CODEC_STATUS
WimCodecProcess(
    WIM_CODEC_INSTANCE *Instance,
    const uint8_t *Input,
    size_t InputSize,
    size_t *InputUsed,
    uint8_t *Output,
    size_t OutputSize,
    size_t *OutputUsed,
    uint32_t ProcessFlags)
{
    uint32_t RequiredCapability;
    WIM_CODEC_STATUS Status;

    if (InputUsed != NULL) *InputUsed = 0;
    if (OutputUsed != NULL) *OutputUsed = 0;
    if (Instance == NULL || Instance->Provider == NULL ||
        Instance->State == NULL || InputUsed == NULL || OutputUsed == NULL ||
        (InputSize != 0 && Input == NULL) ||
        (OutputSize != 0 && Output == NULL) ||
        (ProcessFlags & ~WIM_CODEC_PROCESS_FINAL) != 0)
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;

    RequiredCapability = Instance->Direction == WIM_CODEC_DECODE
        ? WIM_CODEC_CAP_DECODE : WIM_CODEC_CAP_ENCODE;
    if ((Instance->Provider->Capabilities & RequiredCapability) == 0)
        return WIM_CODEC_STATUS_UNSUPPORTED;

    Status = Instance->Provider->Process(
        Instance->State, Input, InputSize, InputUsed,
        Output, OutputSize, OutputUsed, ProcessFlags);

    /*
     * Provider callbacks are an ABI boundary. Reject impossible accounting
     * rather than allowing a faulty codec to make callers advance beyond
     * their input or output buffers.
     */
    if (*InputUsed > InputSize || *OutputUsed > OutputSize) {
        *InputUsed = 0;
        *OutputUsed = 0;
        return WIM_CODEC_STATUS_INTERNAL_ERROR;
    }

    return Status;
}

WIM_CODEC_STATUS
WimCodecReset(WIM_CODEC_INSTANCE *Instance)
{
    if (Instance == NULL || Instance->Provider == NULL ||
        Instance->State == NULL)
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    return Instance->Provider->Reset != NULL
        ? Instance->Provider->Reset(Instance->State)
        : WIM_CODEC_STATUS_UNSUPPORTED;
}

size_t
WimCodecMaximumCompressedSize(
    WIM_CODEC_ALGORITHM Algorithm, size_t InputSize, uint32_t ChunkSize)
{
    const WIM_CODEC_PROVIDER *Provider = WimCodecFindProvider(Algorithm);
    return Provider != NULL && Provider->MaximumCompressedSize != NULL
        ? Provider->MaximumCompressedSize(InputSize, ChunkSize) : 0;
}

size_t
WimCodecMaximumDecompressedSize(
    WIM_CODEC_ALGORITHM Algorithm, size_t InputSize, uint32_t ChunkSize)
{
    const WIM_CODEC_PROVIDER *Provider = WimCodecFindProvider(Algorithm);
    return Provider != NULL && Provider->MaximumDecompressedSize != NULL
        ? Provider->MaximumDecompressedSize(InputSize, ChunkSize) : 0;
}

WIM_CODEC_STATUS
WimCodecRegisterBuiltins(void)
{
    WIM_CODEC_STATUS Status;

    if (WimCodecBuiltinsRegistered)
        return WIM_CODEC_STATUS_OK;

    Status = WimCodecRegisterXpressProvider();
    if (Status != WIM_CODEC_STATUS_OK)
        return Status;

    Status = WimCodecRegisterXpressHuffmanProvider();
    if (Status != WIM_CODEC_STATUS_OK)
        return Status;

    Status = WimCodecRegisterLzxProvider();
    if (Status != WIM_CODEC_STATUS_OK)
        return Status;

    Status = WimCodecRegisterLzmsProvider();
    if (Status == WIM_CODEC_STATUS_OK)
        WimCodecBuiltinsRegistered = 1;
    return Status;
}
