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
#include <emmintrin.h>
#include <assert.h>


static const int16_t index_x8_internal[][8] __attribute__ ((aligned(16))) =
{
    { 0,  1,  2,  3,  4,  5,  6,  7},
    { 8,  9, 10, 11, 12, 13, 14, 15},
    {16, 17, 18, 19, 20, 21, 22, 23},
    {24, 25, 26, 27, 28, 29, 30, 31},
};

static const __m128i *index_x8 = (const __m128i *)index_x8_internal;


void fill_solid_tile16_sse2(uint8_t *buf, ptrdiff_t stride)
{
    int j;
    __m128i value = _mm_set1_epi8(255);
    for(j = 0; j < 16; j++, buf += stride)
        *(__m128i *)buf = value;
}

void fill_solid_tile32_sse2(uint8_t *buf, ptrdiff_t stride)
{
    int j;
    __m128i value = _mm_set1_epi8(255);
    for(j = 0; j < 16; j++, buf += stride)
    {
        ((__m128i *)buf)[0] = value;
        ((__m128i *)buf)[1] = value;
    }
}


void fill_halfplane_tile16_sse2(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    int16_t aa = (a * (int64_t)scale + ((int64_t)1 << 49)) >> 50;
    int16_t bb = (b * (int64_t)scale + ((int64_t)1 << 49)) >> 50;
    int16_t cc = ((int32_t)(c >> 11) * (int64_t)scale + ((int64_t)1 << 44)) >> 45;
    cc += (1 << 9) - ((int16_t)(aa + bb) >> 1);

    int16_t abs_a = aa < 0 ? -aa : aa, abs_b = bb < 0 ? -bb : bb;
    int16_t delta = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2;

    int i, j;
    __m128i va1[2], va2[2];
    __m128i va = _mm_set1_epi16(aa);
    __m128i vd = _mm_set1_epi16(delta);
    for(i = 0; i < 2; i++)
    {
        __m128i vai = _mm_mullo_epi16(va, index_x8[i]);
        va1[i] = _mm_sub_epi16(vai, vd);
        va2[i] = _mm_add_epi16(vai, vd);
    }

    const __m128i zero = _mm_set1_epi16(0);
    const __m128i full = _mm_set1_epi16(1 << 10);
    for(j = 0; j < 16; j++, buf += stride, cc -= bb)
    {
        __m128i res[2];
        __m128i vc = _mm_set1_epi16(cc);
        for(i = 0; i < 2; i++)
        {
            __m128i c1 = _mm_sub_epi16(vc, va1[i]);
            __m128i c2 = _mm_sub_epi16(vc, va2[i]);
            c1 = _mm_max_epi16(zero, _mm_min_epi16(full, c1));
            c2 = _mm_max_epi16(zero, _mm_min_epi16(full, c2));
            res[i] = _mm_srai_epi16(_mm_add_epi16(c1, c2), 3);
        }
        *(__m128i *)buf = _mm_packus_epi16(res[0], res[1]);
    }
}

void fill_halfplane_tile32_sse2(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    int16_t aa = (a * (int64_t)scale + ((int64_t)1 << 50)) >> 51;
    int16_t bb = (b * (int64_t)scale + ((int64_t)1 << 50)) >> 51;
    int16_t cc = ((int32_t)(c >> 12) * (int64_t)scale + ((int64_t)1 << 44)) >> 45;
    cc += (1 << 8) - ((int16_t)(aa + bb) >> 1);

    int16_t abs_a = aa < 0 ? -aa : aa, abs_b = bb < 0 ? -bb : bb;
    int16_t delta = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2;

    int i, j;
    __m128i va1[4], va2[4];
    __m128i va = _mm_set1_epi16(aa);
    __m128i vd = _mm_set1_epi16(delta);
    for(i = 0; i < 4; i++)
    {
        __m128i vai = _mm_mullo_epi16(va, index_x8[i]);
        va1[i] = _mm_sub_epi16(vai, vd);
        va2[i] = _mm_add_epi16(vai, vd);
    }

    const __m128i zero = _mm_set1_epi16(0);
    const __m128i full = _mm_set1_epi16(1 << 9);
    for(j = 0; j < 32; j++, buf += stride, cc -= bb)
    {
        __m128i res[4], vc = _mm_set1_epi16(cc);
        for(i = 0; i < 4; i++)
        {
            __m128i c1 = _mm_sub_epi16(vc, va1[i]);
            __m128i c2 = _mm_sub_epi16(vc, va2[i]);
            c1 = _mm_max_epi16(zero, _mm_min_epi16(full, c1));
            c2 = _mm_max_epi16(zero, _mm_min_epi16(full, c2));
            res[i] = _mm_srai_epi16(_mm_add_epi16(c1, c2), 2);
        }
        ((__m128i *)buf)[0] = _mm_packus_epi16(res[0], res[1]);
        ((__m128i *)buf)[1] = _mm_packus_epi16(res[2], res[3]);
    }
}


static inline void update_border_line16(__m128i res[2],
    int16_t abs_a, const __m128i va[2], int16_t b, int16_t abs_b, int16_t c, int dn, int up)
{
    int16_t size = up - dn;
    int16_t w = (1 << 10) + (size << 4) - abs_a;
    w = (w < 1 << 10 ? w : 1 << 10) << 3;

    int16_t dc_b = abs_b * (int32_t)size >> 6;
    int16_t dc = (abs_a < dc_b ? abs_a : dc_b) >> 2;

    c -= (int32_t)b * (int16_t)(dn + up) >> 7;
    int16_t offs1 = ((c - dc) * (int32_t)w >> 16) + size;
    int16_t offs2 = ((c + dc) * (int32_t)w >> 16) + size;

    int i;
    __m128i vw = _mm_set1_epi16(w);
    __m128i vc1 = _mm_set1_epi16(offs1);
    __m128i vc2 = _mm_set1_epi16(offs2);
    __m128i vs = _mm_set1_epi16(size << 1);
    const __m128i zero = _mm_set1_epi16(0);
    for(i = 0; i < 2; i++)
    {
        __m128i aw = _mm_mulhi_epi16(va[i], vw);
        __m128i c1 = _mm_sub_epi16(vc1, aw);
        __m128i c2 = _mm_sub_epi16(vc2, aw);
        c1 = _mm_max_epi16(zero, _mm_min_epi16(vs, c1));
        c2 = _mm_max_epi16(zero, _mm_min_epi16(vs, c2));
        res[i] = _mm_add_epi16(res[i], _mm_add_epi16(c1, c2));
    }
}

void fill_generic_tile16_sse2(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding)
{
    int i, j;
    __m128i res[16][2];  int16_t delta[18];
    for(j = 0; j < 16; j++)for(i = 0; i < 2; i++)res[j][i] = _mm_set1_epi16(0);
    for(j = 0; j < 18; j++)delta[j] = 0;

    const __m128i zero = _mm_set1_epi16(0);
    const __m128i full = _mm_set1_epi16(1 << 10);
    const struct Segment *end = line + n_lines;
    for(; line != end; line++)
    {
        assert(line->y_min >= 0 && line->y_min < 1 << 10);
        assert(line->y_max > 0 && line->y_max <= 1 << 10);
        assert(line->y_min <= line->y_max);

        int16_t dn_delta = line->flags & SEGFLAG_UP ? 4 : 0, up_delta = dn_delta;
        if(!line->x_min && (line->flags & SEGFLAG_EXACT_LEFT))up_delta ^= 4;
        if(line->flags & SEGFLAG_UR_DL)
        {
            int16_t tmp = dn_delta;  dn_delta = up_delta;  up_delta = tmp;
        }

        int dn = line->y_min >> 6, up = line->y_max >> 6;
        int16_t dn_pos = line->y_min & 63, dn_delta1 = dn_delta * dn_pos;
        int16_t up_pos = line->y_max & 63, up_delta1 = up_delta * up_pos;
        delta[dn + 1] -= dn_delta1;  delta[dn] -= (dn_delta << 6) - dn_delta1;
        delta[up + 1] += up_delta1;  delta[up] += (up_delta << 6) - up_delta1;
        if(line->y_min == line->y_max)continue;

        int16_t a = (line->a * (int64_t)line->scale + ((int64_t)1 << 49)) >> 50;
        int16_t b = (line->b * (int64_t)line->scale + ((int64_t)1 << 49)) >> 50;
        int16_t c = ((int32_t)(line->c >> 11) * (int64_t)line->scale + ((int64_t)1 << 44)) >> 45;
        c -= (a >> 1) + b * dn;

        __m128i va[2], aa = _mm_set1_epi16(a);
        for(i = 0; i < 2; i++)va[i] = _mm_mullo_epi16(aa, index_x8[i]);
        int16_t abs_a = a < 0 ? -a : a, abs_b = b < 0 ? -b : b;
        int16_t dc = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2, base = (1 << 9) - (b >> 1);
        int16_t dc1 = base + dc, dc2 = base - dc;

        if(dn_pos)
        {
            if(up == dn)
            {
                update_border_line16(res[dn], abs_a, va, b, abs_b, c, dn_pos, up_pos);  continue;
            }
            update_border_line16(res[dn], abs_a, va, b, abs_b, c, dn_pos, 64);
            dn++;  c -= b;
        }
        for(j = dn; j < up; j++, c -= b)
        {
            __m128i vc1 = _mm_set1_epi16(c + dc1);
            __m128i vc2 = _mm_set1_epi16(c + dc2);
            for(i = 0; i < 2; i++)
            {
                __m128i c1 = _mm_sub_epi16(vc1, va[i]);
                __m128i c2 = _mm_sub_epi16(vc2, va[i]);
                c1 = _mm_max_epi16(zero, _mm_min_epi16(full, c1));
                c2 = _mm_max_epi16(zero, _mm_min_epi16(full, c2));
                __m128i c12 = _mm_srai_epi16(_mm_add_epi16(c1, c2), 3);
                res[j][i] = _mm_add_epi16(res[j][i], c12);
            }
        }
        if(up_pos)update_border_line16(res[up], abs_a, va, b, abs_b, c, 0, up_pos);
    }

    int16_t cur = winding << 8;
    for(j = 0; j < 16; j++, buf += stride)
    {
        __m128i line[2], vc = _mm_set1_epi16(cur += delta[j]);
        for(i = 0; i < 2; i++)
        {
            __m128i val = _mm_add_epi16(res[j][i], vc);
            __m128i neg_val = _mm_sub_epi16(zero, val);
            line[i] = _mm_max_epi16(val, neg_val);
        }
        *(__m128i *)buf = _mm_packus_epi16(line[0], line[1]);
    }
}

static inline void update_border_line32(__m128i res[4],
    int16_t abs_a, const __m128i va[4], int16_t b, int16_t abs_b, int16_t c, int dn, int up)
{
    int16_t size = up - dn;
    int16_t w = (1 << 9) + (size << 3) - abs_a;
    w = (w < 1 << 9 ? w : 1 << 9) << 5;

    int16_t dc_b = abs_b * (int32_t)size >> 6;
    int16_t dc = (abs_a < dc_b ? abs_a : dc_b) >> 2;

    c -= (int32_t)b * (int16_t)(dn + up) >> 7;
    int16_t offs1 = ((c - dc) * (int32_t)w >> 16) + size;
    int16_t offs2 = ((c + dc) * (int32_t)w >> 16) + size;

    int i;
    __m128i vw = _mm_set1_epi16(w);
    __m128i vc1 = _mm_set1_epi16(offs1);
    __m128i vc2 = _mm_set1_epi16(offs2);
    __m128i vs = _mm_set1_epi16(size << 1);
    const __m128i zero = _mm_set1_epi16(0);
    for(i = 0; i < 4; i++)
    {
        __m128i aw = _mm_mulhi_epi16(va[i], vw);
        __m128i c1 = _mm_sub_epi16(vc1, aw);
        __m128i c2 = _mm_sub_epi16(vc2, aw);
        c1 = _mm_max_epi16(zero, _mm_min_epi16(vs, c1));
        c2 = _mm_max_epi16(zero, _mm_min_epi16(vs, c2));
        res[i] = _mm_add_epi16(res[i], _mm_add_epi16(c1, c2));
    }
}

void fill_generic_tile32_sse2(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding)
{
    int i, j;
    __m128i res[32][4];  int16_t delta[34];
    for(j = 0; j < 32; j++)for(i = 0; i < 4; i++)res[j][i] = _mm_set1_epi16(0);
    for(j = 0; j < 34; j++)delta[j] = 0;

    const __m128i zero = _mm_set1_epi16(0);
    const __m128i full = _mm_set1_epi16(1 << 9);
    const struct Segment *end = line + n_lines;
    for(; line != end; line++)
    {
        assert(line->y_min >= 0 && line->y_min < 1 << 11);
        assert(line->y_max > 0 && line->y_max <= 1 << 11);
        assert(line->y_min <= line->y_max);

        int16_t dn_delta = line->flags & SEGFLAG_UP ? 4 : 0, up_delta = dn_delta;
        if(!line->x_min && (line->flags & SEGFLAG_EXACT_LEFT))up_delta ^= 4;
        if(line->flags & SEGFLAG_UR_DL)
        {
            int16_t tmp = dn_delta;  dn_delta = up_delta;  up_delta = tmp;
        }

        int dn = line->y_min >> 6, up = line->y_max >> 6;
        int16_t dn_pos = line->y_min & 63, dn_delta1 = dn_delta * dn_pos;
        int16_t up_pos = line->y_max & 63, up_delta1 = up_delta * up_pos;
        delta[dn + 1] -= dn_delta1;  delta[dn] -= (dn_delta << 6) - dn_delta1;
        delta[up + 1] += up_delta1;  delta[up] += (up_delta << 6) - up_delta1;
        if(line->y_min == line->y_max)continue;

        int16_t a = (line->a * (int64_t)line->scale + ((int64_t)1 << 50)) >> 51;
        int16_t b = (line->b * (int64_t)line->scale + ((int64_t)1 << 50)) >> 51;
        int16_t c = ((int32_t)(line->c >> 12) * (int64_t)line->scale + ((int64_t)1 << 44)) >> 45;
        c -= (a >> 1) + b * dn;

        __m128i va[4], aa = _mm_set1_epi16(a);
        for(i = 0; i < 4; i++)va[i] = _mm_mullo_epi16(aa, index_x8[i]);
        int16_t abs_a = a < 0 ? -a : a, abs_b = b < 0 ? -b : b;
        int16_t dc = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2, base = (1 << 8) - (b >> 1);
        int16_t dc1 = base + dc, dc2 = base - dc;

        if(dn_pos)
        {
            if(up == dn)
            {
                update_border_line32(res[dn], abs_a, va, b, abs_b, c, dn_pos, up_pos);  continue;
            }
            update_border_line32(res[dn], abs_a, va, b, abs_b, c, dn_pos, 64);
            dn++;  c -= b;
        }
        for(j = dn; j < up; j++, c -= b)
        {
            __m128i vc1 = _mm_set1_epi16(c + dc1);
            __m128i vc2 = _mm_set1_epi16(c + dc2);
            for(i = 0; i < 4; i++)
            {
                __m128i c1 = _mm_sub_epi16(vc1, va[i]);
                __m128i c2 = _mm_sub_epi16(vc2, va[i]);
                c1 = _mm_max_epi16(zero, _mm_min_epi16(full, c1));
                c2 = _mm_max_epi16(zero, _mm_min_epi16(full, c2));
                __m128i c12 = _mm_srai_epi16(_mm_add_epi16(c1, c2), 2);
                res[j][i] = _mm_add_epi16(res[j][i], c12);
            }
        }
        if(up_pos)update_border_line32(res[up], abs_a, va, b, abs_b, c, 0, up_pos);
    }

    int16_t cur = winding << 8;
    for(j = 0; j < 32; j++, buf += stride)
    {
        __m128i line[4], vc = _mm_set1_epi16(cur += delta[j]);
        for(i = 0; i < 4; i++)
        {
            __m128i val = _mm_add_epi16(res[j][i], vc);
            __m128i neg_val = _mm_sub_epi16(zero, val);
            line[i] = _mm_max_epi16(val, neg_val);
        }
        ((__m128i *)buf)[0] = _mm_packus_epi16(line[0], line[1]);
        ((__m128i *)buf)[1] = _mm_packus_epi16(line[2], line[3]);
    }
}
