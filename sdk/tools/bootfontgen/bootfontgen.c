/*
 * bootfontgen - deterministic host-side TrueType to BGFX bitmap-font converter.
 *
 * Output format (all integers little-endian):
 *   Header: "BFNT", u16 version, u16 glyph count, u16 pixel height, u16 reserved
 *   Repeated glyph records:
 *     u32 codepoint, u16 width, u16 rows, i16 bitmap_left,
 *     i16 bitmap_top, i16 advance_x_26_6, followed by width*rows
 *     8-bit coverage bytes (top-to-bottom, left-to-right).
 *
 * This program is a build tool. FreeType is not needed by the target runtime.
 */
#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_CODEPOINT 32u
#define LAST_CODEPOINT 126u
#define DEFAULT_PIXEL_HEIGHT 16u

static int
WriteU16(FILE *File, uint16_t Value)
{
    return fputc((int)(Value & 0xff), File) != EOF &&
           fputc((int)(Value >> 8), File) != EOF;
}

static int
WriteU32(FILE *File, uint32_t Value)
{
    return WriteU16(File, (uint16_t)Value) &&
           WriteU16(File, (uint16_t)(Value >> 16));
}

static int
WriteI16(FILE *File, int16_t Value)
{
    return WriteU16(File, (uint16_t)Value);
}

static int
WriteHeader(FILE *File, uint16_t PixelHeight)
{
    return fwrite("BFNT", 1, 4, File) == 4 &&
           WriteU16(File, 1) &&
           WriteU16(File, (uint16_t)(LAST_CODEPOINT - FIRST_CODEPOINT + 1)) &&
           WriteU16(File, PixelHeight) &&
           WriteU16(File, 0);
}

static int
WriteGlyph(FILE *File, FT_Face Face, uint32_t Codepoint)
{
    FT_GlyphSlot Slot;
    FT_Bitmap *Bitmap;
    unsigned int Y, X;

    if (FT_Load_Char(Face, (FT_ULong)Codepoint,
                     FT_LOAD_DEFAULT | FT_LOAD_RENDER) != 0)
    {
        fprintf(stderr, "bootfontgen: cannot rasterize U+%04X\n",
                (unsigned int)Codepoint);
        return 0;
    }

    Slot = Face->glyph;
    Bitmap = &Slot->bitmap;
    if (Bitmap->width > UINT16_MAX || Bitmap->rows > UINT16_MAX ||
        Slot->bitmap_left < INT16_MIN || Slot->bitmap_left > INT16_MAX ||
        Slot->bitmap_top < INT16_MIN || Slot->bitmap_top > INT16_MAX ||
        Slot->advance.x < INT16_MIN || Slot->advance.x > INT16_MAX)
    {
        fprintf(stderr, "bootfontgen: glyph U+%04X exceeds format limits\n",
                (unsigned int)Codepoint);
        return 0;
    }

    if (!WriteU32(File, Codepoint) ||
        !WriteU16(File, (uint16_t)Bitmap->width) ||
        !WriteU16(File, (uint16_t)Bitmap->rows) ||
        !WriteI16(File, (int16_t)Slot->bitmap_left) ||
        !WriteI16(File, (int16_t)Slot->bitmap_top) ||
        !WriteI16(File, (int16_t)Slot->advance.x))
        return 0;

    for (Y = 0; Y < Bitmap->rows; ++Y)
    {
        const unsigned char *Row = Bitmap->buffer +
            (Bitmap->pitch >= 0 ? Y : Bitmap->rows - 1u - Y) *
            (unsigned int)(Bitmap->pitch >= 0 ? Bitmap->pitch : -Bitmap->pitch);

        for (X = 0; X < Bitmap->width; ++X)
        {
            unsigned char Coverage;
            if (Bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                Coverage = Row[X];
                if (Bitmap->num_grays > 1 && Bitmap->num_grays != 256)
                    Coverage = (unsigned char)((Coverage * 255u) /
                                               (Bitmap->num_grays - 1u));
            }
            else if (Bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
            {
                Coverage = (Row[X >> 3] & (0x80u >> (X & 7))) ? 255u : 0u;
            }
            else
            {
                fprintf(stderr, "bootfontgen: unsupported pixel mode %u\n",
                        (unsigned int)Bitmap->pixel_mode);
                return 0;
            }

            if (fputc(Coverage, File) == EOF)
                return 0;
        }
    }
    return 1;
}

int
main(int argc, char **argv)
{
    FT_Library Library = NULL;
    FT_Face Face = NULL;
    FILE *Output = NULL;
    unsigned long PixelHeight = DEFAULT_PIXEL_HEIGHT;
    unsigned int Codepoint;
    int Result = EXIT_FAILURE;

    if (argc != 3 && argc != 4)
    {
        fprintf(stderr, "usage: %s input.ttf output.bfnt [pixel-height]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 4)
    {
        char *End = NULL;
        PixelHeight = strtoul(argv[3], &End, 10);
        if (!End || *End || PixelHeight == 0 || PixelHeight > UINT16_MAX)
        {
            fprintf(stderr, "bootfontgen: invalid pixel height '%s'\n", argv[3]);
            return EXIT_FAILURE;
        }
    }

    if (FT_Init_FreeType(&Library) != 0 ||
        FT_New_Face(Library, argv[1], 0, &Face) != 0)
    {
        fprintf(stderr, "bootfontgen: unable to initialize FreeType or open '%s'\n",
                argv[1]);
        goto Cleanup;
    }

    if (FT_Set_Pixel_Sizes(Face, 0, (FT_UInt)PixelHeight) != 0)
    {
        fprintf(stderr, "bootfontgen: cannot set pixel height to %lu\n",
                PixelHeight);
        goto Cleanup;
    }

    Output = fopen(argv[2], "wb");
    if (!Output)
    {
        fprintf(stderr, "bootfontgen: cannot create '%s'\n", argv[2]);
        goto Cleanup;
    }

    if (!WriteHeader(Output, (uint16_t)PixelHeight))
    {
        fprintf(stderr, "bootfontgen: failed writing output header\n");
        goto Cleanup;
    }

    for (Codepoint = FIRST_CODEPOINT; Codepoint <= LAST_CODEPOINT; ++Codepoint)
    {
        if (!WriteGlyph(Output, Face, Codepoint))
        {
            fprintf(stderr, "bootfontgen: failed writing output glyph U+%04X\n",
                    Codepoint);
            goto Cleanup;
        }
    }

    if (fflush(Output) != 0 || ferror(Output))
    {
        fprintf(stderr, "bootfontgen: output write failed\n");
        goto Cleanup;
    }

    Result = EXIT_SUCCESS;

Cleanup:
    if (Output && fclose(Output) != 0)
        Result = EXIT_FAILURE;
    if (Face)
        FT_Done_Face(Face);
    if (Library)
        FT_Done_FreeType(Library);
    return Result;
}
