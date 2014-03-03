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

#include "ass_rasterizer.h"
#include <assert.h>



static inline int ilog2(uint32_t n)
{
    return __builtin_clz(n) ^ 31;
}


static const int use_sse2 = 1;
static const int use_tile16 = 1;

void fill_solid_tile16_c(uint8_t *buf, ptrdiff_t stride, int set);
void fill_solid_tile32_c(uint8_t *buf, ptrdiff_t stride, int set);
void fill_halfplane_tile16_c(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);
void fill_halfplane_tile32_c(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);
void fill_generic_tile16_c(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding);
void fill_generic_tile32_c(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding);

void fill_solid_tile16_sse2(uint8_t *buf, ptrdiff_t stride, int set);
void fill_solid_tile32_sse2(uint8_t *buf, ptrdiff_t stride, int set);
void fill_halfplane_tile16_sse2(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);
void fill_halfplane_tile32_sse2(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);
void fill_generic_tile16_sse2(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding);
void fill_generic_tile32_sse2(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding);

void rasterizer_init(struct Rasterizer *rst)
{
    if(use_sse2)
    {
        if(use_tile16)
        {
            rst->tile_order = 4;
            rst->fill_solid = fill_solid_tile16_sse2;
            rst->fill_halfplane = fill_halfplane_tile16_sse2;
            rst->fill_generic = fill_generic_tile16_sse2;
        }
        else
        {
            rst->tile_order = 5;
            rst->fill_solid = fill_solid_tile32_sse2;
            rst->fill_halfplane = fill_halfplane_tile32_sse2;
            rst->fill_generic = fill_generic_tile32_sse2;
        }
    }
    else
    {
        if(use_tile16)
        {
            rst->tile_order = 4;
            rst->fill_solid = fill_solid_tile16_c;
            rst->fill_halfplane = fill_halfplane_tile16_c;
            rst->fill_generic = fill_generic_tile16_c;
        }
        else
        {
            rst->tile_order = 5;
            rst->fill_solid = fill_solid_tile32_c;
            rst->fill_halfplane = fill_halfplane_tile32_c;
            rst->fill_generic = fill_generic_tile32_c;
        }
    }

    rst->linebuf[0] = rst->linebuf[1] = NULL;
    rst->size[0] = rst->capacity[0] = 0;
    rst->size[1] = rst->capacity[1] = 0;
}

static inline int check_capacity(struct Rasterizer *rst, int index, size_t delta)
{
    delta += rst->size[index];
    if(rst->capacity[index] >= delta)return 1;

    delta = delta > 64 ? delta : 64;
    size_t capacity = 2 * rst->capacity[index];
    capacity = capacity > delta ? capacity : delta;
    void *ptr = realloc(rst->linebuf[index], capacity * sizeof(struct Segment));
    if(!ptr)return 0;

    rst->linebuf[index] = (struct Segment *)ptr;
    rst->capacity[index] = capacity;
    return 1;
}

void rasterizer_done(struct Rasterizer *rst)
{
    free(rst->linebuf[0]);  free(rst->linebuf[1]);
}


typedef struct
{
    int32_t x, y;
} OutlinePoint;

typedef struct
{
    OutlinePoint r;
    int64_t r2, er;
} OutlineSegment;

static const int outline_error = 16;

static inline void segment_init(OutlineSegment *seg, OutlinePoint beg, OutlinePoint end)
{
    int32_t x = end.x - beg.x, abs_x = x < 0 ? -x : x;
    int32_t y = end.y - beg.y, abs_y = y < 0 ? -y : y;

    seg->r.x = x;  seg->r.y = y;
    seg->r2 = x * (int64_t)x + y * (int64_t)y;
    seg->er = outline_error * (int64_t)(abs_x > abs_y ? abs_x : abs_y);
}

static inline int segment_subdivide(const OutlineSegment *seg, OutlinePoint beg, OutlinePoint pt)
{
    int32_t x = pt.x - beg.x;
    int32_t y = pt.y - beg.y;
    int64_t pdr = seg->r.x * (int64_t)x + seg->r.y * (int64_t)y;
    int64_t pcr = seg->r.x * (int64_t)y - seg->r.y * (int64_t)x;
    return pdr < -seg->er || pdr > seg->r2 + seg->er || (pcr < 0 ? -pcr : pcr) > seg->er;
}

static inline int add_line(struct Rasterizer *rst, OutlinePoint pt0, OutlinePoint pt1)
{
    int32_t x = pt1.x - pt0.x;
    int32_t y = pt1.y - pt0.y;
    if(!x && !y)return 1;

    if(!check_capacity(rst, 0, 1))return 0;
    struct Segment *line = rst->linebuf[0] + rst->size[0]++;
    line->flags = SEGFLAG_EXACT_LEFT | SEGFLAG_EXACT_RIGHT | SEGFLAG_EXACT_BOTTOM | SEGFLAG_EXACT_TOP;
    if(x < 0)line->flags ^= SEGFLAG_UR_DL;  if(y >= 0)line->flags ^= SEGFLAG_UP | SEGFLAG_UR_DL;
    line->x_min = (pt0.x < pt1.x ? pt0.x : pt1.x);  line->x_max = (pt0.x > pt1.x ? pt0.x : pt1.x);
    line->y_min = (pt0.y < pt1.y ? pt0.y : pt1.y);  line->y_max = (pt0.y > pt1.y ? pt0.y : pt1.y);
    line->a = y;  line->b = -x;  line->c = y * (int64_t)pt0.x - x * (int64_t)pt0.y;

    int32_t abs_x = x < 0 ? -x : x;
    int32_t abs_y = y < 0 ? -y : y;
    uint32_t max_ab = (abs_x > abs_y ? abs_x : abs_y);
    int shift = 30 - ilog2(max_ab);  max_ab <<= shift + 1;
    line->a <<= shift;  line->b <<= shift;  line->c <<= shift;
    line->scale = (uint64_t)0x53333333 * (uint32_t)(max_ab * (uint64_t)max_ab >> 32) >> 32;
    line->scale += 0x8810624D - (0xBBC6A7EF * (uint64_t)max_ab >> 32);
    //line->scale = ((uint64_t)1 << 61) / max_ab;
    return 1;
}

static int add_quadratic(struct Rasterizer *rst, OutlinePoint pt0, OutlinePoint pt1, OutlinePoint pt2)
{
    OutlineSegment seg;  segment_init(&seg, pt0, pt2);
    if(!segment_subdivide(&seg, pt0, pt1))return add_line(rst, pt0, pt2);

    OutlinePoint p01, p12, c;  // TODO: overflow?
    p01.x = pt0.x + pt1.x;  p12.x = pt1.x + pt2.x;
    p01.y = pt0.y + pt1.y;  p12.y = pt1.y + pt2.y;
    c.x = (p01.x + p12.x + 2) >> 2;  p01.x >>= 1;  p12.x >>= 1;
    c.y = (p01.y + p12.y + 2) >> 2;  p01.y >>= 1;  p12.y >>= 1;
    return add_quadratic(rst, pt0, p01, c) && add_quadratic(rst, c, p12, pt2);
}

static int add_cubic(struct Rasterizer *rst, OutlinePoint pt0, OutlinePoint pt1, OutlinePoint pt2, OutlinePoint pt3)
{
    OutlineSegment seg;  segment_init(&seg, pt0, pt3);
    if(!segment_subdivide(&seg, pt0, pt1) && !segment_subdivide(&seg, pt0, pt2))return add_line(rst, pt0, pt3);

    OutlinePoint p01, p12, p23, p012, p123, c;  // TODO: overflow?
    p01.x = pt0.x + pt1.x;  p12.x = pt1.x + pt2.x + 2;  p23.x = pt2.x + pt3.x;
    p01.y = pt0.y + pt1.y;  p12.y = pt1.y + pt2.y + 2;  p23.y = pt2.y + pt3.y;
    p012.x = p01.x + p12.x;  p123.x = p12.x + p23.x;  c.x = (p012.x + p123.x - 1) >> 3;
    p012.y = p01.y + p12.y;  p123.y = p12.y + p23.y;  c.y = (p012.y + p123.y - 1) >> 3;
    p01.x >>= 1;  p012.x >>= 2;  p123.x >>= 2;  p23.x >>= 1;
    p01.y >>= 1;  p012.y >>= 2;  p123.y >>= 2;  p23.y >>= 1;
    return add_cubic(rst, pt0, p01, p012, c) && add_cubic(rst, c, p123, p23, pt3);
}


int rasterizer_set_outline(struct Rasterizer *rst, const FT_Outline *path)
{
    enum Status
    {
        S_ON, S_Q, S_C1, S_C2
    };

    int i, j;
    rst->size[0] = 0;
    for(i = 0, j = 0; i < path->n_contours; i++)
    {
        OutlinePoint start, p[4];
        int process_end = 1;
        enum Status st;

        int last = path->contours[i];
        switch(FT_CURVE_TAG(path->tags[j]))
        {
        case FT_CURVE_TAG_ON:
            p[0].x = path->points[j].x;
            p[0].y = path->points[j].y;
            start = p[0];  st = S_ON;  break;

        case FT_CURVE_TAG_CONIC:
            switch(FT_CURVE_TAG(path->tags[last]))
            {
            case FT_CURVE_TAG_ON:
                p[0].x = path->points[last].x;
                p[0].y = path->points[last].y;
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                process_end = 0;  st = S_Q;  break;

            case FT_CURVE_TAG_CONIC:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                p[0].x = (p[1].x + path->points[last].x) >> 1;
                p[0].y = (p[1].y + path->points[last].y) >> 1;
                start = p[0];  st = S_Q;  break;

            default:
                return 0;
            }
            break;

        default:
            return 0;
        }

        for(j++; j <= last; j++)switch(FT_CURVE_TAG(path->tags[j]))
        {
        case FT_CURVE_TAG_ON:
            switch(st)
            {
            case S_ON:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                if(!add_line(rst, p[0], p[1]))return 0;
                p[0] = p[1];  break;

            case S_Q:
                p[2].x = path->points[j].x;
                p[2].y = path->points[j].y;
                if(!add_quadratic(rst, p[0], p[1], p[2]))return 0;
                p[0] = p[2];  st = S_ON;  break;

            case S_C2:
                p[3].x = path->points[j].x;
                p[3].y = path->points[j].y;
                if(!add_cubic(rst, p[0], p[1], p[2], p[3]))return 0;
                p[0] = p[3];  st = S_ON;  break;

            default:
                return 0;
            }
            break;

        case FT_CURVE_TAG_CONIC:
            switch(st)
            {
            case S_ON:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                st = S_Q;  break;

            case S_Q:
                p[3].x = path->points[j].x;
                p[3].y = path->points[j].y;
                p[2].x = (p[1].x + p[3].x) >> 1;
                p[2].y = (p[1].y + p[3].y) >> 1;
                if(!add_quadratic(rst, p[0], p[1], p[2]))return 0;
                p[0] = p[2];  p[1] = p[3];  break;

            default:
                return 0;
            }
            break;

        case FT_CURVE_TAG_CUBIC:
            switch(st)
            {
            case S_ON:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                st = S_C1;  break;

            case S_C1:
                p[2].x = path->points[j].x;
                p[2].y = path->points[j].y;
                st = S_C2;  break;

            default:
                return 0;
            }
            break;

        default:
            return 0;
        }

        if(process_end)switch(st)
        {
        case S_ON:
            if(!add_line(rst, p[0], start))return 0;  break;

        case S_Q:
            if(!add_quadratic(rst, p[0], p[1], start))return 0;  break;

        case S_C2:
            if(!add_cubic(rst, p[0], p[1], p[2], start))return 0;  break;

        default:
            return 0;
        }
    }

    size_t k;
    rst->x_min = rst->y_min = 0x7FFFFFFF;
    rst->x_max = rst->y_max = 0x80000000;
    for(k = 0; k < rst->size[0]; k++)
    {
        rst->x_min = rst->x_min < rst->linebuf[0][k].x_min ? rst->x_min : rst->linebuf[0][k].x_min;
        rst->x_max = rst->x_max > rst->linebuf[0][k].x_max ? rst->x_max : rst->linebuf[0][k].x_max;
        rst->y_min = rst->y_min < rst->linebuf[0][k].y_min ? rst->y_min : rst->linebuf[0][k].y_min;
        rst->y_max = rst->y_max > rst->linebuf[0][k].y_max ? rst->y_max : rst->linebuf[0][k].y_max;
    }
    return 1;
}


static void segment_move_x(struct Segment *line, int32_t x)
{
    line->x_min -= x;  line->x_max -= x;
    line->x_min = line->x_min > 0 ? line->x_min : 0;
    line->c -= line->a * (int64_t)x;

    static const int test = SEGFLAG_EXACT_LEFT | SEGFLAG_UR_DL;
    if(!line->x_min && (line->flags & test) == test)line->flags &= ~SEGFLAG_EXACT_BOTTOM;
}

static void segment_move_y(struct Segment *line, int32_t y)
{
    line->y_min -= y;  line->y_max -= y;
    line->y_min = line->y_min > 0 ? line->y_min : 0;
    line->c -= line->b * (int64_t)y;

    static const int test = SEGFLAG_EXACT_BOTTOM | SEGFLAG_UR_DL;
    if(!line->y_min && (line->flags & test) == test)line->flags &= ~SEGFLAG_EXACT_LEFT;
}

static void segment_split_horz(struct Segment *line, struct Segment *next, int32_t x)
{
    assert(x > line->x_min && x < line->x_max);
    *next = *line;  next->c -= line->a * (int64_t)x;
    next->x_min = 0;  next->x_max -= x;  line->x_max = x;

    line->flags &= ~SEGFLAG_EXACT_BOTTOM;  next->flags &= ~SEGFLAG_EXACT_TOP;
    if(line->flags & SEGFLAG_UR_DL)
    {
        int32_t tmp = line->flags;  line->flags = next->flags;  next->flags = tmp;
    }
    line->flags |= SEGFLAG_EXACT_RIGHT;  next->flags |= SEGFLAG_EXACT_LEFT;
}

static void segment_split_vert(struct Segment *line, struct Segment *next, int32_t y)
{
    assert(y > line->y_min && y < line->y_max);
    *next = *line;  next->c -= line->b * (int64_t)y;
    next->y_min = 0;  next->y_max -= y;  line->y_max = y;

    line->flags &= ~SEGFLAG_EXACT_LEFT;  next->flags &= ~SEGFLAG_EXACT_RIGHT;
    if(line->flags & SEGFLAG_UR_DL)
    {
        int32_t tmp = line->flags;  line->flags = next->flags;  next->flags = tmp;
    }
    line->flags |= SEGFLAG_EXACT_TOP;  next->flags |= SEGFLAG_EXACT_BOTTOM;
}

static inline int segment_check_right(const struct Segment *line, int32_t x)
{
    if(line->flags & SEGFLAG_EXACT_RIGHT)return line->x_max <= x;
    int64_t cc = line->c - line->a * (int64_t)x -
        line->b * (int64_t)(line->flags & SEGFLAG_UR_DL ? line->y_max : line->y_min);
    if(line->a > 0)cc = -cc;  return cc >= 0;
}
static inline int segment_check_left(const struct Segment *line, int32_t x)
{
    if(line->flags & SEGFLAG_EXACT_LEFT)return line->x_min >= x;
    int64_t cc = line->c - line->a * (int64_t)x -
        line->b * (int64_t)(line->flags & SEGFLAG_UR_DL ? line->y_min : line->y_max);
    if(line->a < 0)cc = -cc;  return cc >= 0;
}

static inline int segment_check_top(const struct Segment *line, int32_t y)
{
    if(line->flags & SEGFLAG_EXACT_TOP)return line->y_max <= y;
    int64_t cc = line->c - line->b * (int64_t)y -
        line->a * (int64_t)(line->flags & SEGFLAG_UR_DL ? line->x_max : line->x_min);
    if(line->b > 0)cc = -cc;  return cc >= 0;
}

static inline int segment_check_bottom(const struct Segment *line, int32_t y)
{
    if(line->flags & SEGFLAG_EXACT_BOTTOM)return line->y_min >= y;
    int64_t cc = line->c - line->b * (int64_t)y -
        line->a * (int64_t)(line->flags & SEGFLAG_UR_DL ? line->x_min : line->x_max);
    if(line->b < 0)cc = -cc;  return cc >= 0;
}

static int polyline_split_horz(const struct Segment *src, size_t n_src, struct Segment **dst0, struct Segment **dst1, int32_t x)
{
    int winding = 0;
    const struct Segment *end = src + n_src;
    for(; src != end; src++)
    {
        int delta = 0;
        if(!src->y_min && (src->flags & SEGFLAG_EXACT_BOTTOM))delta = src->a < 0 ? 1 : -1;
        if(segment_check_right(src, x))
        {
            winding += delta;  if(src->x_min >= x)continue;  **dst0 = *src;
            (*dst0)->x_max = (*dst0)->x_max < x ? (*dst0)->x_max : x;
            (*dst0)++;  continue;
        }
        if(segment_check_left(src, x))
        {
            **dst1 = *src;  segment_move_x(*dst1, x);
            (*dst1)++;  continue;
        }
        if(src->flags & SEGFLAG_UR_DL)winding += delta;
        **dst0 = *src;  segment_split_horz(*dst0, *dst1, x);
        (*dst0)++;  (*dst1)++;
    }
    return winding;
}

static int polyline_split_vert(const struct Segment *src, size_t n_src, struct Segment **dst0, struct Segment **dst1, int32_t y)
{
    int winding = 0;
    const struct Segment *end = src + n_src;
    for(; src != end; src++)
    {
        int delta = 0;
        if(!src->x_min && (src->flags & SEGFLAG_EXACT_LEFT))delta = src->b < 0 ? 1 : -1;
        if(segment_check_top(src, y))
        {
            winding += delta;  if(src->y_min >= y)continue;  **dst0 = *src;
            (*dst0)->y_max = (*dst0)->y_max < y ? (*dst0)->y_max : y;
            (*dst0)++;  continue;
        }
        if(segment_check_bottom(src, y))
        {
            **dst1 = *src;  segment_move_y(*dst1, y);
            (*dst1)++;  continue;
        }
        if(src->flags & SEGFLAG_UR_DL)winding += delta;
        **dst0 = *src;  segment_split_vert(*dst0, *dst1, y);
        (*dst0)++;  (*dst1)++;
    }
    return winding;
}


static inline void rasterizer_fill_solid(struct Rasterizer *rst,
    uint8_t *buf, int width, int height, ptrdiff_t stride, int set)
{
    assert(!(width & ((1 << rst->tile_order) - 1)) && !(height & ((1 << rst->tile_order) - 1)));

    int i, j;
    ptrdiff_t step = 1 << rst->tile_order;
    ptrdiff_t tile_stride = stride << rst->tile_order;
    width >>= rst->tile_order;  height >>= rst->tile_order;
    for(j = 0; j < height; j++, buf += tile_stride)for(i = 0; i < width; i++)
        rst->fill_solid(buf + i * step, stride, set);
}

static inline void rasterizer_fill_halfplane(struct Rasterizer *rst,
    uint8_t *buf, int width, int height, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    assert(!(width & ((1 << rst->tile_order) - 1)) && !(height & ((1 << rst->tile_order) - 1)));
    if(width == 1 << rst->tile_order && height == 1 << rst->tile_order)
    {
        rst->fill_halfplane(buf, stride, a, b, c, scale);  return;
    }

    int64_t offs = ((int64_t)a + b) << (rst->tile_order + 5);
    uint32_t abs_a = a < 0 ? -a : a, abs_b = b < 0 ? -b : b;
    int64_t size = (int64_t)(abs_a + abs_b) << (rst->tile_order + 5);

    int i, j;
    ptrdiff_t step = 1 << rst->tile_order;
    ptrdiff_t tile_stride = stride << rst->tile_order;
    width >>= rst->tile_order;  height >>= rst->tile_order;
    for(j = 0; j < height; j++, buf += tile_stride)
        for(i = 0; i < width; i++)
        {
            int64_t cc = c - ((a * (int64_t)i + b * (int64_t)j) << (rst->tile_order + 6));
            int64_t offs_c = offs - cc, abs_cc = offs_c < 0 ? -offs_c : offs_c;
            if(abs_cc < size)rst->fill_halfplane(buf + i * step, stride, a, b, cc, scale);
            else rst->fill_solid(buf + i * step, stride, ((int32_t)(offs_c >> 32) ^ scale) & (1 << 31));
        }
}

static int rasterizer_fill_level(struct Rasterizer *rst,
    uint8_t *buf, int width, int height, ptrdiff_t stride, int index, size_t offs, int winding)
{
    assert(width > 0 && height > 0 && (unsigned)index < 2u && offs <= rst->size[index]);
    assert(!(width & ((1 << rst->tile_order) - 1)) && !(height & ((1 << rst->tile_order) - 1)));

    size_t n = rst->size[index] - offs;
    struct Segment *line = rst->linebuf[index] + offs;
    if(!n)
    {
        rasterizer_fill_solid(rst, buf, width, height, stride, winding);  return 1;
    }
    if(n == 1)
    {
        int flag = 0;  if(line->c < 0)winding++;
        if(winding)flag ^= 1;  if(winding - 1)flag ^= 3;
        if(flag & 1)rasterizer_fill_halfplane(rst, buf, width, height, stride,
            line->a, line->b, line->c, flag & 2 ? -line->scale : line->scale);
        else rasterizer_fill_solid(rst, buf, width, height, stride, flag & 2);
        rst->size[index] = offs;  return 1;
    }
    if(width == 1 << rst->tile_order && height == 1 << rst->tile_order)
    {
        rst->fill_generic(buf, stride, line, rst->size[index] - offs, winding);
        rst->size[index] = offs;  return 1;
    }

    size_t offs1 = rst->size[index ^ 1];
    if(!check_capacity(rst, index ^ 1, n))return 0;
    struct Segment *dst0 = line, *dst1 = rst->linebuf[index ^ 1] + offs1;

    int winding1 = winding;  uint8_t *buf1 = buf;
    int width1 = width, height1 = height;
    if(width > height)
    {
        width = 1 << ilog2(width - 1);  width1 -= width;  buf1 += width;
        winding1 += polyline_split_horz(line, n, &dst0, &dst1, (int32_t)width << 6);
    }
    else
    {
        height = 1 << ilog2(height - 1);  height1 -= height;  buf1 += height * stride;
        winding1 += polyline_split_vert(line, n, &dst0, &dst1, (int32_t)height << 6);
    }
    rst->size[index ^ 0] = dst0 - rst->linebuf[index ^ 0];
    rst->size[index ^ 1] = dst1 - rst->linebuf[index ^ 1];

    if(!rasterizer_fill_level(rst, buf,  width,  height,  stride, index ^ 0, offs,  winding))return 0;
    assert(rst->size[index ^ 0] == offs);
    if(!rasterizer_fill_level(rst, buf1, width1, height1, stride, index ^ 1, offs1, winding1))return 0;
    assert(rst->size[index ^ 1] == offs1);
    return 1;
}

int rasterizer_fill(struct Rasterizer *rst,
    uint8_t *buf, int x0, int y0, int width, int height, ptrdiff_t stride, int vert_flip)
{
    assert(width > 0 && height > 0);
    assert(!(width & ((1 << rst->tile_order) - 1)) && !(height & ((1 << rst->tile_order) - 1)));
    x0 <<= 6;  y0 <<= 6;

    if(vert_flip)
    {
        buf += (height - 1) * stride;  stride = -stride;
    }

    size_t n = rst->size[0];
    struct Segment *line = rst->linebuf[0], *end = line + n;
    for(; line != end; line++)
    {
        line->x_min -= x0;  line->x_max -= x0;
        line->y_min -= y0;  line->y_max -= y0;
        line->c -= line->a * (int64_t)x0 + line->b * (int64_t)y0;
    }
    rst->x_min -= x0;  rst->x_max -= x0;
    rst->y_min -= y0;  rst->y_max -= y0;

    int index = 0, winding = 0;
    if(!check_capacity(rst, 1, rst->size[0]))return 0;
    int32_t size_x = (int32_t)width << 6, size_y = (int32_t)height << 6;
    if(rst->x_max >= size_x)
    {
        struct Segment *dst0 = rst->linebuf[index], *dst1 = rst->linebuf[index ^ 1];
        polyline_split_horz(rst->linebuf[index], n, &dst0, &dst1, size_x);
        n = dst0 - rst->linebuf[index];
    }
    if(rst->y_max >= size_y)
    {
        struct Segment *dst0 = rst->linebuf[index], *dst1 = rst->linebuf[index ^ 1];
        polyline_split_vert(rst->linebuf[index], n, &dst0, &dst1, size_y);
        n = dst0 - rst->linebuf[index];
    }
    if(rst->x_min <= 0)
    {
        struct Segment *dst0 = rst->linebuf[index], *dst1 = rst->linebuf[index ^ 1];
        polyline_split_horz(rst->linebuf[index], n, &dst0, &dst1, 0);
        n = dst1 - rst->linebuf[index ^= 1];
    }
    if(rst->y_min <= 0)
    {
        struct Segment *dst0 = rst->linebuf[index], *dst1 = rst->linebuf[index ^ 1];
        winding = polyline_split_vert(rst->linebuf[index], n, &dst0, &dst1, 0);
        n = dst1 - rst->linebuf[index ^= 1];
    }
    rst->size[index] = n;  rst->size[index ^ 1] = 0;
    return rasterizer_fill_level(rst, buf, width, height, stride, index, 0, winding);
}


int rasterizer_test(struct Rasterizer *rst, uint8_t buf[64 * 64])  // DEBUG
{
    OutlinePoint pt[] =
    {
        {0, 0}, {4096, 999}, {3072, 2048}, {3072, 3072},
        {2000, 4077}, {512, 2048}, {1536, 2048}
    };
    static const size_t n = sizeof(pt) / sizeof(pt[0]);

    for(size_t i = 1; i < n; i++)if(!add_line(rst, pt[i - 1], pt[i]))return 0;
    if(!add_line(rst, pt[n - 1], pt[0]))return 0;

    size_t k;
    rst->x_min = rst->y_min = 0x7FFFFFFF;
    rst->x_max = rst->y_max = 0x80000000;
    for(k = 0; k < rst->size[0]; k++)
    {
        rst->x_min = rst->x_min < rst->linebuf[0][k].x_min ? rst->x_min : rst->linebuf[0][k].x_min;
        rst->x_max = rst->x_max > rst->linebuf[0][k].x_max ? rst->x_max : rst->linebuf[0][k].x_max;
        rst->y_min = rst->y_min < rst->linebuf[0][k].y_min ? rst->y_min : rst->linebuf[0][k].y_min;
        rst->y_max = rst->y_max > rst->linebuf[0][k].y_max ? rst->y_max : rst->linebuf[0][k].y_max;
    }

    memset(buf, 128, 64 * 64);
    return rasterizer_fill(rst, buf, 0, 0, 64, 64, 64, 1);
}
