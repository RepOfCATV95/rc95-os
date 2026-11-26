/*
 * PROJECT: RC95-OS clean-room LZMS decoder
 * PURPOSE: Independent adaptive binary contexts for range-coded decisions
 *
 * Context widths and decision topology are based on independently published
 * LZMS behavior documented by wimlib (src/lzms_decompress.c and
 * include/wimlib/lzms_constants.h).  This file is independently authored.
 */
#include <string.h>
#include <libwim/lzms_context.h>
LZMS_CONTEXT_STATUS
LzmsBinaryContextInitialize(LZMS_BINARY_CONTEXT *C, uint8_t Bits)
{
    unsigned I;
    if (!C || Bits < 1 || Bits > 6) return LZMS_CONTEXT_INVALID_ARGUMENT;
    memset(C, 0, sizeof(*C));
    C->StateBits=Bits;
    C->StateCount=(uint8_t)(1u << Bits);
    for (I=0; I<C->StateCount; ++I)
        LzmsProbabilityModelInitialize(&C->Models[I]);
    return LZMS_CONTEXT_OK;
}
LZMS_CONTEXT_STATUS
LzmsBinaryContextDecode(LZMS_BINARY_CONTEXT *C,
                        LZMS_RANGE16_DECODER *R, uint32_t *Bit)
{
    uint32_t B, P;
    if (!C || !R || !Bit || !C->StateCount ||
        C->State >= C->StateCount) return LZMS_CONTEXT_INVALID_ARGUMENT;
    P=LzmsProbabilityModelGetZeroCount(&C->Models[C->State]);
    if (LzmsRange16DecodeBit(R, P, &B)!=LZMS_RANGE16_OK)
        return LZMS_CONTEXT_RANGE_ERROR;
    if (LzmsProbabilityModelUpdate(&C->Models[C->State], B)!=LZMS_PROBABILITY_OK)
        return LZMS_CONTEXT_INVALID_ARGUMENT;
    C->State=((C->State << 1) | B) & (C->StateCount-1);
    *Bit=B;
    return LZMS_CONTEXT_OK;
}
LZMS_CONTEXT_STATUS
LzmsContextSetInitialize(LZMS_CONTEXT_SET *S)
{
    unsigned I;
    if (!S) return LZMS_CONTEXT_INVALID_ARGUMENT;
    memset(S,0,sizeof(*S));
    if (LzmsBinaryContextInitialize(&S->Main,4)!=LZMS_CONTEXT_OK ||
        LzmsBinaryContextInitialize(&S->Match,5)!=LZMS_CONTEXT_OK ||
        LzmsBinaryContextInitialize(&S->Lz,6)!=LZMS_CONTEXT_OK ||
        LzmsBinaryContextInitialize(&S->Delta,6)!=LZMS_CONTEXT_OK)
        return LZMS_CONTEXT_INVALID_ARGUMENT;
    for (I=0; I<2; ++I)
        if (LzmsBinaryContextInitialize(&S->LzRepeat[I],6)!=LZMS_CONTEXT_OK ||
            LzmsBinaryContextInitialize(&S->DeltaRepeat[I],6)!=LZMS_CONTEXT_OK)
            return LZMS_CONTEXT_INVALID_ARGUMENT;
    return LZMS_CONTEXT_OK;
}
LZMS_CONTEXT_STATUS
LzmsContextDecodeItemClass(LZMS_CONTEXT_SET *S, LZMS_RANGE16_DECODER *R,
                           LZMS_ITEM_CLASS *Item)
{
    uint32_t B;
    if (!S || !R || !Item) return LZMS_CONTEXT_INVALID_ARGUMENT;
    if (LzmsBinaryContextDecode(&S->Main,R,&B)!=LZMS_CONTEXT_OK)
        return LZMS_CONTEXT_RANGE_ERROR;
    if (!B) { *Item=LZMS_ITEM_LITERAL; return LZMS_CONTEXT_OK; }
    if (LzmsBinaryContextDecode(&S->Match,R,&B)!=LZMS_CONTEXT_OK)
        return LZMS_CONTEXT_RANGE_ERROR;
    if (!B) { *Item=LZMS_ITEM_LZ_MATCH; return LZMS_CONTEXT_OK; }
    /* A set match decision selects delta directly; Delta is then used
       separately to distinguish its explicit and repeated source. */
    *Item=LZMS_ITEM_DELTA_MATCH;
    return LZMS_CONTEXT_OK;
}
LZMS_CONTEXT_STATUS
LzmsContextDecodeOffsetMode(LZMS_CONTEXT_SET *S, LZMS_RANGE16_DECODER *R,
                            LZMS_ITEM_CLASS Item, uint32_t *IsRepeat)
{
    LZMS_BINARY_CONTEXT *C;
    if (!S || !R || !IsRepeat ||
        (Item!=LZMS_ITEM_LZ_MATCH && Item!=LZMS_ITEM_DELTA_MATCH))
        return LZMS_CONTEXT_INVALID_ARGUMENT;
    C=(Item==LZMS_ITEM_LZ_MATCH)?&S->Lz:&S->Delta;
    if (LzmsBinaryContextDecode(C,R,IsRepeat)!=LZMS_CONTEXT_OK)
        return LZMS_CONTEXT_RANGE_ERROR;
    return LZMS_CONTEXT_OK;
}
LZMS_CONTEXT_STATUS
LzmsContextDecodeRepeatRank(LZMS_CONTEXT_SET *S, LZMS_RANGE16_DECODER *R,
                            LZMS_ITEM_CLASS Item, uint8_t *Rank)
{
    LZMS_BINARY_CONTEXT *C;
    uint32_t B;
    if (!S || !R || !Rank ||
        (Item!=LZMS_ITEM_LZ_MATCH && Item!=LZMS_ITEM_DELTA_MATCH))
        return LZMS_CONTEXT_INVALID_ARGUMENT;
    C=(Item==LZMS_ITEM_LZ_MATCH)?S->LzRepeat:S->DeltaRepeat;
    if (LzmsBinaryContextDecode(&C[0],R,&B)!=LZMS_CONTEXT_OK)
        return LZMS_CONTEXT_RANGE_ERROR;
    if (!B) { *Rank=0; return LZMS_CONTEXT_OK; }
    if (LzmsBinaryContextDecode(&C[1],R,&B)!=LZMS_CONTEXT_OK)
        return LZMS_CONTEXT_RANGE_ERROR;
    *Rank=(uint8_t)(B ? 2 : 1);
    return LZMS_CONTEXT_OK;
}
