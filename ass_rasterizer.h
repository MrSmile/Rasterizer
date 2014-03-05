/*
 * Copyright (C) 2014 Vabishchevich Nikolay <vabnick@gmail.com>
 *
 * This file is part of libass.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIBASS_RASTERIZER_H
#define LIBASS_RASTERIZER_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stddef.h>
#include <stdint.h>


enum
{
    SEGFLAG_UP = 1, SEGFLAG_UR_DL = 2,
    SEGFLAG_EXACT_LEFT = 4, SEGFLAG_EXACT_RIGHT = 8,
    SEGFLAG_EXACT_BOTTOM = 16, SEGFLAG_EXACT_TOP = 32,
};

struct Segment
{
    int32_t x_min, x_max, y_min, y_max;
    int32_t a, b, scale, flags;
    int64_t c;
};


typedef void (*FillSolidTileFunc)(uint8_t *buf, ptrdiff_t stride);
typedef void (*FillHalfplaneTileFunc)(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);
typedef void (*FillGenericTileFunc)(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding);

struct Rasterizer
{
    int tile_order;
    FillSolidTileFunc fill_solid;
    FillHalfplaneTileFunc fill_halfplane;
    FillGenericTileFunc fill_generic;

    int32_t x_min, x_max, y_min, y_max;
    struct Segment *linebuf[2];
    size_t size[2], capacity[2];
};

void rasterizer_init(struct Rasterizer *rst);
void rasterizer_done(struct Rasterizer *rst);
int rasterizer_set_outline(struct Rasterizer *rst, const FT_Outline *path);
int rasterizer_fill(struct Rasterizer *rst,
    uint8_t *buf, int x0, int y0, int width, int height, ptrdiff_t stride, int vert_flip);

int rasterizer_test(struct Rasterizer *rst, uint8_t buf[64 * 64]);  // DEBUG

#endif                          /* LIBASS_RASTERIZER_H */
