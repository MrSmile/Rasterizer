// fill.h : low-level interface functions
//

#pragma once

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

struct Rasterizer
{
    int32_t x_min, x_max, y_min, y_max;
    struct Segment *linebuf[2];
    size_t size[2], capacity[2];
};


void fill_solid_tile16(uint8_t *buf, ptrdiff_t stride, int set);
void fill_solid_tile32(uint8_t *buf, ptrdiff_t stride, int set);

void fill_halfplane_tile16(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);
void fill_halfplane_tile32(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);

void fill_generic_tile16(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding);
void fill_generic_tile32(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding);

void rasterizer_init(struct Rasterizer *rst);
void rasterizer_done(struct Rasterizer *rst);
int rasterizer_set_outline(struct Rasterizer *rst, const FT_Outline *path);