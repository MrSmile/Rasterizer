// fill.c : low-level rasterization
//

#include "fill.h"
#include <assert.h>



void fill_solid_tile16(uint8_t *buf, ptrdiff_t stride, int set)
{
    int i, j;
    int8_t value = set ? 255 : 0;
    for(j = 0; j < 16; j++, buf += stride)
        for(i = 0; i < 16; i++)buf[i] = value;
}

void fill_solid_tile32(uint8_t *buf, ptrdiff_t stride, int set)
{
    int i, j;
    int8_t value = set ? 255 : 0;
    for(j = 0; j < 32; j++, buf += stride)
        for(i = 0; i < 32; i++)buf[i] = value;
}


void fill_halfplane_tile16(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    int16_t aa = (a * (int64_t)scale + ((int64_t)1 << 49)) >> 50;
    int16_t bb = (b * (int64_t)scale + ((int64_t)1 << 49)) >> 50;
    int16_t cc = ((int32_t)(c >> 11) * (int64_t)scale + ((int64_t)1 << 44)) >> 45;
    cc += (1 << 9) - ((int16_t)(aa + bb) >> 1);

    int16_t abs_a = aa < 0 ? -aa : aa, abs_b = bb < 0 ? -bb : bb;
    int16_t delta = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2;

    int i, j;
    int16_t va1[16], va2[16];
    for(i = 0; i < 16; i++)
    {
        va1[i] = aa * i - delta;  va2[i] = aa * i + delta;
    }

    static const int16_t full = (1 << 10) - 1;
    for(j = 0; j < 16; j++, buf += stride, cc -= bb)for(i = 0; i < 16; i++)
    {
        int16_t c1 = cc - va1[i], c2 = cc - va2[i];
        c1 = (c1 > 0 ? c1 : 0);  c1 = (c1 < full ? c1 : full);
        c2 = (c2 > 0 ? c2 : 0);  c2 = (c2 < full ? c2 : full);
        buf[i] = (c1 + c2) >> 3;
    }
}

void fill_halfplane_tile32(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    int16_t aa = (a * (int64_t)scale + ((int64_t)1 << 50)) >> 51;
    int16_t bb = (b * (int64_t)scale + ((int64_t)1 << 50)) >> 51;
    int16_t cc = ((int32_t)(c >> 12) * (int64_t)scale + ((int64_t)1 << 44)) >> 45;
    cc += (1 << 8) - ((int16_t)(aa + bb) >> 1);

    int16_t abs_a = aa < 0 ? -aa : aa, abs_b = bb < 0 ? -bb : bb;
    int16_t delta = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2;

    int i, j;
    int16_t va1[32], va2[32];
    for(i = 0; i < 32; i++)
    {
        va1[i] = aa * i - delta;  va2[i] = aa * i + delta;
    }

    static const int16_t full = (1 << 9) - 1;
    for(j = 0; j < 32; j++, buf += stride, cc -= bb)for(i = 0; i < 32; i++)
    {
        int16_t c1 = cc - va1[i], c2 = cc - va2[i];
        c1 = (c1 > 0 ? c1 : 0);  c1 = (c1 < full ? c1 : full);
        c2 = (c2 > 0 ? c2 : 0);  c2 = (c2 < full ? c2 : full);
        buf[i] = (c1 + c2) >> 2;
    }
}


static inline void update_border_line16(int16_t res[16], int16_t a, int16_t abs_a, const int16_t va[16], int16_t b, int16_t abs_b, int16_t c, int dn, int up)
{
    int16_t size = (up - dn) << 1, offs = size >> 1;
    int16_t w = (1 << 13) + (size << 6) - (abs_a << 3);
    w = (w < 1 << 13 ? w : 1 << 13);

    int32_t dc_a = (int32_t)abs_a << 7, dc_b = abs_b * (int32_t)size;
    int16_t dc = (dc_a < dc_b ? dc_a : dc_b) >> 9;

    c -= (int32_t)b * (int16_t)(dn + up) >> 7;
    int16_t offs1 = ((c - dc) * (int32_t)w >> 16) + offs;
    int16_t offs2 = ((c + dc) * (int32_t)w >> 16) + offs;

    int i;
    for(i = 0; i < 16; i++)
    {
        int16_t aw = va[i] * (int32_t)w >> 16;
        int16_t c1 = offs1 - aw, c2 = offs2 - aw;
        c1 = (c1 > 0 ? c1 : 0);  c1 = (c1 < size ? c1 : size);
        c2 = (c2 > 0 ? c2 : 0);  c2 = (c2 < size ? c2 : size);
        res[i] += c1 + c2;
    }
}

void fill_generic_tile16(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding)
{
    int i, j;
    int16_t res[16][16], delta[18];
    for(j = 0; j < 16; j++)for(i = 0; i < 16; i++)res[j][i] = 0;
    for(j = 0; j < 18; j++)delta[j] = 0;

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

        int16_t va[16];
        for(i = 0; i < 16; i++)va[i] = a * i;
        int16_t abs_a = a < 0 ? -a : a, abs_b = b < 0 ? -b : b;
        int16_t dc = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2, base = (1 << 9) - (b >> 1);
        int16_t dc1 = base + dc, dc2 = base - dc;

        if(dn_pos)
        {
            if(up == dn)
            {
                update_border_line16(res[dn], a, abs_a, va, b, abs_b, c, dn_pos, up_pos);  continue;
            }
            update_border_line16(res[dn], a, abs_a, va, b, abs_b, c, dn_pos, 64);
            dn++;  c -= b;
        }
        for(j = dn; j < up; j++, c -= b)
        {
            static const int16_t full = 1 << 10;
            for(i = 0; i < 16; i++)
            {
                int16_t c1 = c - va[i] + dc1, c2 = c - va[i] + dc2;
                c1 = (c1 > 0 ? c1 : 0);  c1 = (c1 < full ? c1 : full);
                c2 = (c2 > 0 ? c2 : 0);  c2 = (c2 < full ? c2 : full);
                res[j][i] += (c1 + c2) >> 3;
            }
        }
        if(up_pos)update_border_line16(res[up], a, abs_a, va, b, abs_b, c, 0, up_pos);
    }

    int16_t cur = winding << 8;
    for(j = 0; j < 16; j++, buf += stride)
    {
        cur += delta[j];
        for(i = 0; i < 16; i++)
        {
            int16_t val = res[j][i] + cur, neg_val = -val;
            val = (val > neg_val ? val : neg_val);
            buf[i] = (val < 255 ? val : 255);
        }
    }
}

static inline void update_border_line32(int16_t res[32], int16_t a, int16_t abs_a, const int16_t va[32], int16_t b, int16_t abs_b, int16_t c, int dn, int up)
{
    int16_t size = (up - dn) << 1, offs = size >> 1;
    int16_t w = (1 << 14) + (size << 7) - (abs_a << 5);
    w = (w < 1 << 14 ? w : 1 << 14);

    int32_t dc_a = (int32_t)abs_a << 7, dc_b = abs_b * (int32_t)size;
    int16_t dc = (dc_a < dc_b ? dc_a : dc_b) >> 9;

    c -= (int32_t)b * (int16_t)(dn + up) >> 7;
    int16_t offs1 = ((c - dc) * (int32_t)w >> 16) + offs;
    int16_t offs2 = ((c + dc) * (int32_t)w >> 16) + offs;

    int i;
    for(i = 0; i < 32; i++)
    {
        int16_t aw = va[i] * (int32_t)w >> 16;
        int16_t c1 = offs1 - aw, c2 = offs2 - aw;
        c1 = (c1 > 0 ? c1 : 0);  c1 = (c1 < size ? c1 : size);
        c2 = (c2 > 0 ? c2 : 0);  c2 = (c2 < size ? c2 : size);
        res[i] += c1 + c2;
    }
}

void fill_generic_tile32(uint8_t *buf, ptrdiff_t stride, const struct Segment *line, size_t n_lines, int winding)
{
    int i, j;
    int16_t res[32][32], delta[34];
    for(j = 0; j < 32; j++)for(i = 0; i < 32; i++)res[j][i] = 0;
    for(j = 0; j < 34; j++)delta[j] = 0;

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

        int16_t va[32];
        for(i = 0; i < 32; i++)va[i] = a * i;
        int16_t abs_a = a < 0 ? -a : a, abs_b = b < 0 ? -b : b;
        int16_t dc = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2, base = (1 << 8) - (b >> 1);
        int16_t dc1 = base + dc, dc2 = base - dc;

        if(dn_pos)
        {
            if(up == dn)
            {
                update_border_line32(res[dn], a, abs_a, va, b, abs_b, c, dn_pos, up_pos);  continue;
            }
            update_border_line32(res[dn], a, abs_a, va, b, abs_b, c, dn_pos, 64);
            dn++;  c -= b;
        }
        for(j = dn; j < up; j++, c -= b)
        {
            static const int16_t full = 1 << 9;
            for(i = 0; i < 32; i++)
            {
                int16_t c1 = c - va[i] + dc1, c2 = c - va[i] + dc2;
                c1 = (c1 > 0 ? c1 : 0);  c1 = (c1 < full ? c1 : full);
                c2 = (c2 > 0 ? c2 : 0);  c2 = (c2 < full ? c2 : full);
                res[j][i] += (c1 + c2) >> 2;
            }
        }
        if(up_pos)update_border_line32(res[up], a, abs_a, va, b, abs_b, c, 0, up_pos);
    }

    int16_t cur = winding << 8;
    for(j = 0; j < 32; j++, buf += stride)
    {
        cur += delta[j];
        for(i = 0; i < 32; i++)
        {
            int16_t val = res[j][i] + cur, neg_val = -val;
            val = (val > neg_val ? val : neg_val);
            buf[i] = (val < 255 ? val : 255);
        }
    }
}
