/* RC95-OS BGFX fixed bitmap font renderer. */
#include <ntoskrnl.h>
#include "../include/bgfx_font.h"
#include "../include/bgfx_framebuffer.h"

typedef struct _BGFX_GLYPH { CHAR Character; UCHAR Rows[7]; } BGFX_GLYPH;
static const BGFX_GLYPH BgfxFont[]={
{' ',{0,0,0,0,0,0,0}},{'!',{4,4,4,4,4,0,4}},{'.',{0,0,0,0,0,6,6}},{',',{0,0,0,0,0,6,4}},{':',{0,6,6,0,6,6,0}},{'-',{0,0,0,31,0,0,0}},{'/',{1,2,2,4,8,8,16}},{'?',{14,17,1,2,4,0,4}},
{'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},{'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
{'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,2,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},{'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},{'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
{'a',{14,1,15,17,17,19,13}},{'b',{16,16,30,17,17,17,30}},{'c',{0,0,14,17,16,17,14}},{'d',{1,1,15,17,17,19,13}},{'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}},{'g',{0,15,17,17,15,1,14}},{'h',{16,16,30,17,17,17,17}},{'i',{4,0,12,4,4,4,14}},{'j',{2,0,6,2,2,18,12}},{'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}},{'m',{0,0,26,21,21,21,21}},{'n',{0,0,30,17,17,17,17}},{'o',{0,0,14,17,17,17,14}},{'p',{0,30,17,17,30,16,16}},{'q',{0,15,17,17,15,1,1}},{'r',{0,0,22,25,16,16,16}},{'s',{0,0,15,16,14,1,30}},{'t',{8,8,28,8,8,9,6}},{'u',{0,0,17,17,17,19,13}},{'v',{0,0,17,17,17,10,4}},{'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}},{'y',{0,17,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}}
};

static const BGFX_GLYPH *BgfxFindGlyph(CHAR Character)
{
    ULONG Index;
    for (Index = 0; Index < RTL_NUMBER_OF(BgfxFont); ++Index)
        if (BgfxFont[Index].Character == Character)
            return &BgfxFont[Index];
    for (Index = 0; Index < RTL_NUMBER_OF(BgfxFont); ++Index)
        if (BgfxFont[Index].Character == '?')
            return &BgfxFont[Index];
    return NULL;
}

NTSTATUS NTAPI BgfxDrawText(ULONG Left, ULONG Top, PCSTR Text,
                             ULONG Foreground, ULONG Background, ULONG Scale)
{
    ULONG OriginX = Left, Width = BgfxGetFramebufferWidth();
    ULONG Height = BgfxGetFramebufferHeight(), Row, Column;
    if (!Text || !Scale || Scale > 8 || !Width || !Height)
        return STATUS_INVALID_PARAMETER;
    for (; *Text; ++Text)
    {
        const BGFX_GLYPH *Glyph;
        if (*Text == '\n') { Left = OriginX; if (Top > MAXULONG - 9 * Scale) return STATUS_INTEGER_OVERFLOW; Top += 9 * Scale; continue; }
        if (*Text == '\r') { Left = OriginX; continue; }
        if (*Text == '\\t') { if (Left > MAXULONG - 24 * Scale) return STATUS_INTEGER_OVERFLOW; Left += 24 * Scale; continue; }
        if (Left > Width || 6 * Scale > Width - Left) break;
        Glyph = BgfxFindGlyph(*Text);
        if (Top < Height)
        for (Row = 0; Row < 7; ++Row)
        {
            UCHAR Bits = Glyph ? Glyph->Rows[Row] : 0;
            for (Column = 0; Column < 5; ++Column)
            {
                ULONG X = Left + Column * Scale, Y = Top + Row * Scale;
                ULONG Color = (Bits & (1u << (4 - Column))) ? Foreground : Background;
                if (X < Width && Y < Height)
                    BgfxFillRectangle(X, Y, min(Scale, Width - X), min(Scale, Height - Y), Color);
            }
        }
        if (Left > MAXULONG - 6 * Scale) return STATUS_INTEGER_OVERFLOW;
        Left += 6 * Scale;
    }
    return STATUS_SUCCESS;
}
