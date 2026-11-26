/*
 * PROJECT: RC95-OS LZMS codec
 * PURPOSE: Canonical Huffman construction and adaptive frequency trees
 *
 * The adaptive builder uses a deterministic binary Huffman tree over the
 * current symbol frequencies, then derives canonical MSB-first codewords.
 */
#include <string.h>
#include <limits.h>
#include <libwim/lzms_huffman.h>

LZMS_HUFFMAN_STATUS
LzmsHuffmanBuildCanonical(LZMS_HUFFMAN_ALPHABET *A, const uint8_t *Lengths,
                          uint16_t N, uint8_t MaxBits, int AllowIncomplete)
{
    uint32_t Left=1, Code=0;
    uint16_t S, Offset=0, NonZero=0;
    uint8_t B;
    if (!A || !Lengths || !N || N>LZMS_HUFFMAN_MAX_SYMBOLS ||
        !MaxBits || MaxBits>LZMS_HUFFMAN_MAX_BITS)
        return LZMS_HUFFMAN_INVALID_ARGUMENT;
    memset(A,0,sizeof(*A));
    A->SymbolCount=N; A->MaximumBits=MaxBits;
    for(S=0;S<N;S++) {
        if(Lengths[S]>MaxBits) return LZMS_HUFFMAN_INVALID_ARGUMENT;
        A->Lengths[S]=Lengths[S];
        if(Lengths[S]) {
            A->Counts[Lengths[S]]++;
            NonZero++;
        }
    }
    if(NonZero==0) return LZMS_HUFFMAN_INCOMPLETE;
    for(B=1;B<=MaxBits;B++) {
        Left <<= 1;
        if(A->Counts[B]>Left) return LZMS_HUFFMAN_OVERSUBSCRIBED;
        Left -= A->Counts[B];
    }
    if(Left && !AllowIncomplete) return LZMS_HUFFMAN_INCOMPLETE;
    for(B=1;B<=MaxBits;B++) {
        if(B>1) Code=(Code+A->Counts[B-1])<<1;
        A->FirstCode[B]=Code;
        A->FirstSymbol[B]=Offset;
        Offset=(uint16_t)(Offset+A->Counts[B]);
    }
    Offset=0;
    for(B=1;B<=MaxBits;B++)
        for(S=0;S<N;S++)
            if(Lengths[S]==B) A->SortedSymbols[Offset++]=S;
    return LZMS_HUFFMAN_OK;
}

static LZMS_HUFFMAN_STATUS
DecodeBits(const LZMS_HUFFMAN_ALPHABET *A, LZMS_REVERSE16_READER *R,
           uint16_t *Symbol)
{
    uint32_t Code=0, Bit, Index;
    uint8_t B;
    for(B=1;B<=A->MaximumBits;B++) {
        if(LzmsReverse16ReaderReadBits(R,1,&Bit)!=LZMS_BITSTREAM_OK)
            return LZMS_HUFFMAN_BITSTREAM_ERROR;
        Code=(Code<<1)|Bit;
        if(Code>=A->FirstCode[B] &&
           Code-A->FirstCode[B]<A->Counts[B]) {
            Index=A->FirstSymbol[B]+Code-A->FirstCode[B];
            if(Index>=A->SymbolCount) return LZMS_HUFFMAN_INVALID_CODE;
            *Symbol=A->SortedSymbols[Index];
            return LZMS_HUFFMAN_OK;
        }
    }
    return LZMS_HUFFMAN_INVALID_CODE;
}

LZMS_HUFFMAN_STATUS
LzmsHuffmanDecodeReverse16(const LZMS_HUFFMAN_ALPHABET *A,
                           LZMS_REVERSE16_READER *R,uint16_t *Symbol)
{
    if(!A||!R||!Symbol||!A->SymbolCount||!A->MaximumBits)
        return LZMS_HUFFMAN_INVALID_ARGUMENT;
    return DecodeBits(A,R,Symbol);
}

LZMS_HUFFMAN_STATUS
LzmsHuffmanDecode(const LZMS_HUFFMAN_ALPHABET *A,LZMS_BIT_READER *R,
                  uint16_t *Symbol)
{
    uint32_t Code=0,Bit,Index;
    uint8_t B;
    if(!A||!R||!Symbol||!A->SymbolCount||!A->MaximumBits)
        return LZMS_HUFFMAN_INVALID_ARGUMENT;
    for(B=1;B<=A->MaximumBits;B++) {
        if(LzmsBitReaderReadBits(R,1,&Bit)!=LZMS_BITSTREAM_OK)
            return LZMS_HUFFMAN_BITSTREAM_ERROR;
        Code=(Code<<1)|Bit;
        if(Code>=A->FirstCode[B] && Code-A->FirstCode[B]<A->Counts[B]) {
            Index=A->FirstSymbol[B]+Code-A->FirstCode[B];
            if(Index>=A->SymbolCount) return LZMS_HUFFMAN_INVALID_CODE;
            *Symbol=A->SortedSymbols[Index];
            return LZMS_HUFFMAN_OK;
        }
    }
    return LZMS_HUFFMAN_INVALID_CODE;
}

LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanRebuild(LZMS_ADAPTIVE_HUFFMAN *H)
{
    uint64_t Weight[2*LZMS_HUFFMAN_MAX_SYMBOLS-1];
    uint8_t Lengths[LZMS_HUFFMAN_MAX_SYMBOLS];
    uint16_t LengthCounts[LZMS_HUFFMAN_MAX_BITS+1]={0};
    uint16_t Order[LZMS_HUFFMAN_MAX_SYMBOLS];
    uint16_t I,NodeCount;
    uint32_t KraftUnits,Capacity;
    if(!H||!H->SymbolCount||H->SymbolCount>LZMS_HUFFMAN_MAX_SYMBOLS||
       !H->MaximumBits||H->MaximumBits>LZMS_HUFFMAN_MAX_BITS)
        return LZMS_HUFFMAN_INVALID_ARGUMENT;
    NodeCount=(uint16_t)(2*H->SymbolCount-1);
    if(H->SymbolCount==1) {
        Lengths[0]=1;
    } else {
        /*
         * Build an unconstrained deterministic Huffman tree.  Depth is kept
         * wider than the format limit so pathological skew cannot wrap and
         * accidentally produce a short code.
         */
        {
            uint16_t Left[2*LZMS_HUFFMAN_MAX_SYMBOLS-1];
            uint16_t Right[2*LZMS_HUFFMAN_MAX_SYMBOLS-1];
            uint8_t Used[2*LZMS_HUFFMAN_MAX_SYMBOLS-1];
            memset(Used,0,sizeof(Used));
            for(I=0;I<NodeCount;I++) Weight[I]=UINT64_MAX;
            for(I=0;I<H->SymbolCount;I++) Weight[I]=H->Frequencies[I];
            for(I=H->SymbolCount;I<NodeCount;I++) {
                uint16_t X,Y,J,Best;
                uint64_t WX,WY;
                Best=UINT16_MAX; WX=UINT64_MAX;
                for(J=0;J<I;J++) if(!Used[J] &&
                    (Weight[J]<WX || (Weight[J]==WX&&J<Best))) {
                    Best=J;WX=Weight[J];
                }
                if(Best==UINT16_MAX)return LZMS_HUFFMAN_INVALID_ARGUMENT;
                X=Best;Used[X]=1;
                Best=UINT16_MAX;WY=UINT64_MAX;
                for(J=0;J<I;J++) if(!Used[J] &&
                    (Weight[J]<WY || (Weight[J]==WY&&J<Best))) {
                    Best=J;WY=Weight[J];
                }
                if(Best==UINT16_MAX)return LZMS_HUFFMAN_INVALID_ARGUMENT;
                Y=Best;Used[Y]=1;
                if(UINT64_MAX-WX<WY)return LZMS_HUFFMAN_FREQUENCY_OVERFLOW;
                Left[I]=X;Right[I]=Y;Weight[I]=WX+WY;
            }
            memset(Lengths,0,sizeof(Lengths));
            for(I=0;I<H->SymbolCount;I++) {
                uint16_t N=I,Depth=0;
                while(N!=NodeCount-1) {
                    uint16_t J;int Found=0;
                    for(J=H->SymbolCount;J<NodeCount;J++)
                        if(Left[J]==N||Right[J]==N) {
                            N=J;Depth++;Found=1;break;
                        }
                    if(!Found)return LZMS_HUFFMAN_INVALID_ARGUMENT;
                }
                if(Depth>H->MaximumBits)
                    Depth=H->MaximumBits;
                Lengths[I]=(uint8_t)Depth;
                LengthCounts[Depth]++;
                Order[I]=I;
            }
        }

        /*
         * Apply the standard bit-count redistribution for a length-limited
         * binary Huffman tree.  Longest lengths are then assigned to the
         * least frequent symbols, preserving a valid prefix code at MaxBits.
         */
        Capacity=UINT32_C(1)<<H->MaximumBits;
        KraftUnits=0;
        {
            uint16_t Bits;
            for(Bits=1;Bits<=H->MaximumBits;Bits++)
                KraftUnits+=(uint32_t)LengthCounts[Bits] <<
                            (H->MaximumBits-Bits);
        }
        /*
         * Clipping deep leaves can over-subscribe the code space. Each
         * redistribution splits one shorter leaf and removes one max-depth
         * leaf, reducing the Kraft sum by exactly one max-depth unit.
         */
        while(KraftUnits>Capacity) {
            int Bits;
            for(Bits=(int)H->MaximumBits-1;Bits>0;--Bits)
                if(LengthCounts[Bits])break;
            if(Bits==0||LengthCounts[H->MaximumBits]==0)
                return LZMS_HUFFMAN_INVALID_ARGUMENT;
            LengthCounts[Bits]--;
            LengthCounts[Bits+1]=(uint16_t)(LengthCounts[Bits+1]+2);
            LengthCounts[H->MaximumBits]--;
            --KraftUnits;
        }

        /* Stable insertion sort: low frequencies receive the longest codes. */
        for(I=1;I<H->SymbolCount;I++) {
            uint16_t Symbol=Order[I],J=I;
            while(J>0 &&
                  (H->Frequencies[Order[J-1]]>H->Frequencies[Symbol] ||
                   (H->Frequencies[Order[J-1]]==H->Frequencies[Symbol] &&
                    Order[J-1]>Symbol))) {
                Order[J]=Order[J-1];
                --J;
            }
            Order[J]=Symbol;
        }
        {
            uint16_t Next=0;
            int Bits;
            for(Bits=(int)H->MaximumBits;Bits>0;--Bits) {
                uint16_t N;
                for(N=0;N<LengthCounts[Bits];N++) {
                    if(Next>=H->SymbolCount)
                        return LZMS_HUFFMAN_INVALID_ARGUMENT;
                    Lengths[Order[Next++]]=(uint8_t)Bits;
                }
            }
            if(Next!=H->SymbolCount)
                return LZMS_HUFFMAN_INVALID_ARGUMENT;
        }
    }
    {
        LZMS_HUFFMAN_STATUS S=LzmsHuffmanBuildCanonical(
            &H->Code,Lengths,H->SymbolCount,H->MaximumBits,1);
        if(S!=LZMS_HUFFMAN_OK)return S;
    }
    return LZMS_HUFFMAN_OK;
}

LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanInitialize(LZMS_ADAPTIVE_HUFFMAN *H,uint16_t N,
                              uint16_t RebuildFrequency,uint8_t MaxBits)
{
    uint16_t I;
    if(!H||!N||N>LZMS_HUFFMAN_MAX_SYMBOLS||!RebuildFrequency||
       !MaxBits||MaxBits>LZMS_HUFFMAN_MAX_BITS)
        return LZMS_HUFFMAN_INVALID_ARGUMENT;
    memset(H,0,sizeof(*H));
    H->SymbolCount=N;H->RebuildFrequency=RebuildFrequency;
    H->SymbolsUntilRebuild=RebuildFrequency;H->MaximumBits=MaxBits;
    for(I=0;I<N;I++)H->Frequencies[I]=1;
    return LzmsAdaptiveHuffmanRebuild(H);
}

LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanDecode(LZMS_ADAPTIVE_HUFFMAN *H,
                          LZMS_REVERSE16_READER *R,uint16_t *Symbol)
{
    LZMS_HUFFMAN_STATUS S;
    if(!H||!R||!Symbol)return LZMS_HUFFMAN_INVALID_ARGUMENT;
    S=LzmsHuffmanDecodeReverse16(&H->Code,R,Symbol);
    if(S!=LZMS_HUFFMAN_OK)return S;
    if(*Symbol>=H->SymbolCount)return LZMS_HUFFMAN_INVALID_CODE;
    if(H->Frequencies[*Symbol]==UINT32_MAX)return LZMS_HUFFMAN_FREQUENCY_OVERFLOW;
    H->Frequencies[*Symbol]++;
    if(--H->SymbolsUntilRebuild==0) {
        uint16_t I;
        /*
         * LZMS rebuilds the current code from the accumulated frequencies,
         * then dilutes those frequencies for the next rebuild interval.
         * The active code therefore intentionally reflects the pre-dilution
         * counts until the next rebuild threshold is reached.
         */
        S=LzmsAdaptiveHuffmanRebuild(H);
        if(S!=LZMS_HUFFMAN_OK)return S;
        for(I=0;I<H->SymbolCount;I++)
            H->Frequencies[I]=(H->Frequencies[I]>>1)+1;
        H->SymbolsUntilRebuild=H->RebuildFrequency;
    }
    return LZMS_HUFFMAN_OK;
}

LZMS_HUFFMAN_STATUS
LzmsAdaptiveHuffmanInitializeLzmsSet(
    LZMS_ADAPTIVE_HUFFMAN H[LZMS_HUFFMAN_ALPHABET_COUNT],
    uint16_t NumOffsetSymbols)
{
    static const uint16_t Counts[LZMS_HUFFMAN_ALPHABET_COUNT]={256,799,54,799,8};
    static const uint16_t Rebuild[LZMS_HUFFMAN_ALPHABET_COUNT]={1024,1024,512,1024,512};
    uint16_t I;
    if(!H||NumOffsetSymbols>799)
        return LZMS_HUFFMAN_INVALID_ARGUMENT;
    for(I=0;I<LZMS_HUFFMAN_ALPHABET_COUNT;I++) {
        uint16_t N=(I==LZMS_HUFFMAN_LZ_OFFSET||I==LZMS_HUFFMAN_DELTA_OFFSET)
                   ?(NumOffsetSymbols ? NumOffsetSymbols : 1):Counts[I];
        LZMS_HUFFMAN_STATUS S=LzmsAdaptiveHuffmanInitialize(
            &H[I],N,Rebuild[I],15);
        if(S!=LZMS_HUFFMAN_OK)return S;
    }
    return LZMS_HUFFMAN_OK;
}
