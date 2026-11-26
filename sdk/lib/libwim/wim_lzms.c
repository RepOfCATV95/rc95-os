#include <string.h>
#include <libwim/wim_codec.h>
#include <libwim/lzms_block.h>

typedef struct _WIM_LZMS_STATE {
    WIM_CODEC_ALLOCATOR Allocator;
    uint64_t ExpectedOutputSize;
    uint64_t MaximumOutputSize;
    uint8_t Completed;
} WIM_LZMS_STATE;

static WIM_CODEC_STATUS
WimLzmsCreate(const WIM_CODEC_OPTIONS *Options,
              WIM_CODEC_DIRECTION Direction, void **State)
{
    WIM_LZMS_STATE *S;
    if (!Options || !State || Direction!=WIM_CODEC_DECODE ||
        !Options->ExpectedOutputSize ||
        Options->ExpectedOutputSize>(1u<<30) ||
        (Options->MaximumOutputSize &&
         Options->ExpectedOutputSize>Options->MaximumOutputSize))
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    *State=NULL;
    S=(WIM_LZMS_STATE *)Options->Allocator.Allocate(
        Options->Allocator.Context,sizeof(*S));
    if(!S)return WIM_CODEC_STATUS_OUT_OF_MEMORY;
    memset(S,0,sizeof(*S));
    S->Allocator=Options->Allocator;
    S->ExpectedOutputSize=Options->ExpectedOutputSize;
    S->MaximumOutputSize=Options->MaximumOutputSize;
    *State=S;
    return WIM_CODEC_STATUS_OK;
}

static void
WimLzmsDestroy(void *State)
{
    WIM_LZMS_STATE *S=(WIM_LZMS_STATE *)State;
    WIM_CODEC_ALLOCATOR A;
    if(!S)return;
    A=S->Allocator;
    memset(S,0,sizeof(*S));
    A.Free(A.Context,S);
}

static WIM_CODEC_STATUS
WimLzmsReset(void *State)
{
    WIM_LZMS_STATE *S=(WIM_LZMS_STATE *)State;
    if(!S)return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    S->Completed=0;
    return WIM_CODEC_STATUS_OK;
}

static WIM_CODEC_STATUS
WimLzmsProcess(void *State,const uint8_t *Input,size_t InputSize,
               size_t *InputUsed,uint8_t *Output,size_t OutputSize,
               size_t *OutputUsed,uint32_t Flags)
{
    WIM_LZMS_STATE *S=(WIM_LZMS_STATE *)State;
    LZMS_BLOCK_STATUS Status;
    size_t Produced=0;
    if(InputUsed)*InputUsed=0;
    if(OutputUsed)*OutputUsed=0;
    if(!S||!InputUsed||!OutputUsed||(InputSize&&!Input)||
       (OutputSize&&!Output)||(Flags&~WIM_CODEC_PROCESS_FINAL))
        return WIM_CODEC_STATUS_INVALID_ARGUMENT;
    if(S->Completed)return WIM_CODEC_STATUS_STREAM_END;
    if(!(Flags&WIM_CODEC_PROCESS_FINAL))return WIM_CODEC_STATUS_NEED_INPUT;
    if(S->ExpectedOutputSize>OutputSize)return WIM_CODEC_STATUS_NEED_OUTPUT;
    if(S->ExpectedOutputSize>S->MaximumOutputSize &&
       S->MaximumOutputSize!=0)return WIM_CODEC_STATUS_BUFFER_ERROR;
    Status=LzmsDecodeRawBlock(Input,InputSize,Output,
                              (size_t)S->ExpectedOutputSize,&Produced);
    if(Status!=LZMS_BLOCK_OK)
        return Status==LZMS_BLOCK_OUTPUT_LIMIT
            ? WIM_CODEC_STATUS_BUFFER_ERROR : WIM_CODEC_STATUS_CORRUPT_DATA;
    *InputUsed=InputSize;
    *OutputUsed=Produced;
    S->Completed=1;
    return WIM_CODEC_STATUS_STREAM_END;
}

static const WIM_CODEC_PROVIDER WimLzmsProvider = {
    WIM_CODEC_ABI_VERSION,sizeof(WIM_CODEC_PROVIDER),WIM_CODEC_LZMS,
    WIM_CODEC_CAP_DECODE|WIM_CODEC_CAP_WIM_CHUNKS,
    "LZMS raw-block decoder",
    WimLzmsCreate,WimLzmsDestroy,WimLzmsProcess,WimLzmsReset,NULL,NULL
};

WIM_CODEC_STATUS
WimCodecRegisterLzmsProvider(void)
{
    return WimCodecRegisterProvider(&WimLzmsProvider);
}
