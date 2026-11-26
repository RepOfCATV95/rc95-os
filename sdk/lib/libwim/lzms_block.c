/*
 * Clean-room LZMS raw-block grammar. Decisions are range-coded from the
 * front; adaptive Huffman symbols and verbatim extra bits are read backward.
 * Raw blocks carry no output-size header: WIM resource/chunk metadata does.
 */
#include <string.h>
#include <stdint.h>
#include <libwim/lzms_block.h>
#include <libwim/lzms_bitstream.h>
#include <libwim/lzms_context.h>
#include <libwim/lzms_huffman.h>
#include <libwim/lzms_dictionary.h>
#include <libwim/lzms_token.h>
#include <libwim/lzms_range16.h>
#include <libwim/lzms_slots.h>
static int StreamsOverlap(const LZMS_RANGE16_DECODER *R,const LZMS_REVERSE16_READER *B)
{return LzmsRange16DecoderBytesRead(R)>B->NextWordOffset;}

/* Reject aliased input/output: output is also the sliding dictionary window. */
static int BuffersOverlap(const uint8_t *In,size_t InSize,
                          uint8_t *Out,size_t OutSize)
{
 uintptr_t InStart=(uintptr_t)In,OutStart=(uintptr_t)Out;
 uintptr_t InEnd,OutEnd;
 if(!InSize||!OutSize)return 0;
 if(InSize>UINTPTR_MAX-InStart||OutSize>UINTPTR_MAX-OutStart)return 1;
 InEnd=InStart+InSize;OutEnd=OutStart+OutSize;
 return InStart<OutEnd&&OutStart<InEnd;
}
static int DecodeSym(LZMS_ADAPTIVE_HUFFMAN *H,LZMS_REVERSE16_READER *R,uint16_t *S)
{return LzmsAdaptiveHuffmanDecode(H,R,S)==LZMS_HUFFMAN_OK;}
static int DecodeSlot(LZMS_ADAPTIVE_HUFFMAN *H,LZMS_REVERSE16_READER *R,
 const uint32_t *Base,const uint8_t *Extra,uint16_t Count,uint32_t *V)
{
 uint16_t S;
 uint32_t X = 0;
 if (!DecodeSym(H, R, &S) || S >= Count)
  return 0;
 if (Extra[S] &&
     LzmsReverse16ReaderReadBits(R, Extra[S], &X) != LZMS_BITSTREAM_OK)
  return 0;
 if (Base[S] > UINT32_MAX - X)
  return 0;
 *V = Base[S] + X;
 return 1;
}
LZMS_BLOCK_STATUS
LzmsDecodeRawBlock(const uint8_t *In,size_t InSize,uint8_t *Out,size_t OutSize,size_t *OutUsed)
{
 LZMS_RANGE16_DECODER Range;LZMS_REVERSE16_READER Rev;LZMS_CONTEXT_SET C;
 LZMS_ADAPTIVE_HUFFMAN H[LZMS_HUFFMAN_ALPHABET_COUNT];LZMS_DICTIONARY D;
 LZMS_TOKEN_DECODER T;uint16_t NS,Sym;size_t Pos=0,Used;LZMS_TOKEN Tok;
 if(OutUsed)*OutUsed=0;
 if(!In||!Out||!OutUsed||!OutSize||OutSize>(1u<<30)||(InSize&1)||InSize<4)
  return LZMS_BLOCK_INVALID_ARGUMENT;
 if(BuffersOverlap(In,InSize,Out,OutSize))
  return LZMS_BLOCK_INPUT_OVERLAP;
 NS=LzmsGetOffsetSlotCount((uint32_t)OutSize);
 if(LzmsRange16DecoderInitialize(&Range,In,InSize)!=LZMS_RANGE16_OK||
    LzmsReverse16ReaderInitialize(&Rev,In,InSize)!=LZMS_BITSTREAM_OK||
    LzmsContextSetInitialize(&C)!=LZMS_CONTEXT_OK||
    LzmsAdaptiveHuffmanInitializeLzmsSet(H,NS)!=LZMS_HUFFMAN_OK||
    LzmsDictionaryInitialize(&D,Out,OutSize)!=LZMS_DICTIONARY_OK||
    LzmsTokenDecoderInitialize(&T,&D,OutSize,OutSize)!=LZMS_TOKEN_OK)
  return LZMS_BLOCK_BAD_FRAMING;
 while(Pos<OutSize){
  LZMS_ITEM_CLASS Item;uint32_t Repeat=0,V=0;
  if(LzmsContextDecodeItemClass(&C,&Range,&Item)!=LZMS_CONTEXT_OK)return LZMS_BLOCK_CORRUPT_DATA;
  memset(&Tok,0,sizeof(Tok));
  if(Item==LZMS_ITEM_LITERAL){
   if(!DecodeSym(&H[LZMS_HUFFMAN_LITERAL],&Rev,&Sym))return LZMS_BLOCK_CORRUPT_DATA;
   Tok.Kind=LZMS_TOKEN_LITERAL;Tok.Literal=(uint8_t)Sym;
  }else{
   if(LzmsContextDecodeOffsetMode(&C,&Range,Item,&Repeat)!=LZMS_CONTEXT_OK)return LZMS_BLOCK_CORRUPT_DATA;
   if(Repeat){uint8_t Rank;if(LzmsContextDecodeRepeatRank(&C,&Range,Item,&Rank)!=LZMS_CONTEXT_OK)return LZMS_BLOCK_CORRUPT_DATA;
    Tok.Kind=Item==LZMS_ITEM_LZ_MATCH?LZMS_TOKEN_REP_MATCH:LZMS_TOKEN_REP_DELTA_MATCH;Tok.RepIndex=Rank;
   }else if(Item==LZMS_ITEM_LZ_MATCH){
    if(!DecodeSlot(&H[LZMS_HUFFMAN_LZ_OFFSET],&Rev,LzmsOffsetSlotBase,LzmsExtraOffsetBits,NS,&V))return LZMS_BLOCK_CORRUPT_DATA;
    Tok.Kind=LZMS_TOKEN_MATCH;Tok.Distance=V;
   }else{
    if(!DecodeSym(&H[LZMS_HUFFMAN_DELTA_POWER],&Rev,&Sym)||Sym>7)return LZMS_BLOCK_CORRUPT_DATA;
    Tok.DeltaPower=(uint8_t)Sym;
    if(!DecodeSlot(&H[LZMS_HUFFMAN_DELTA_OFFSET],&Rev,LzmsOffsetSlotBase,LzmsExtraOffsetBits,NS,&V))return LZMS_BLOCK_CORRUPT_DATA;
    Tok.Kind=LZMS_TOKEN_DELTA_MATCH;Tok.Distance=V;
   }
   if(!DecodeSlot(&H[LZMS_HUFFMAN_LENGTH],&Rev,LzmsLengthSlotBase,LzmsExtraLengthBits,LZMS_LENGTH_SLOTS,&V)||!V)return LZMS_BLOCK_CORRUPT_DATA;
   Tok.Length=V;if(V>OutSize-Pos)return LZMS_BLOCK_OUTPUT_LIMIT;
  }
  if(LzmsTokenDecode(&T,&Tok,Out+Pos,OutSize-Pos,&Used)!=LZMS_TOKEN_OK)return LZMS_BLOCK_CORRUPT_DATA;
  Pos+=Used;if(StreamsOverlap(&Range,&Rev))return LZMS_BLOCK_INPUT_OVERLAP;
 }
 *OutUsed=Pos;return LZMS_BLOCK_OK;
}
