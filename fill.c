// fill.c : low-level rasterization
//

#include <stddef.h>
#include <stdint.h>


void fill_halfplane_tile16(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    int16_t aa = (a * (int64_t)scale + ((int64_t)1 << 49)) >> 50;
    int16_t bb = (b * (int64_t)scale + ((int64_t)1 << 49)) >> 50;
    int16_t cc = ((int32_t)(c >> 11) * (int64_t)scale + ((int64_t)1 << 44)) >> 45;
    cc += (1 << 9) - ((int16_t)(aa + bb) >> 1);

    int16_t abs_a = aa < 0 ? -aa : aa, abs_b = bb < 0 ? -bb : bb;
    int16_t delta = ((abs_a < abs_b ? abs_a : abs_b) + 2) >> 2;

    int16_t va1[16], va2[16], i, j;
    for(i = 0; i < 16; i++)
    {
        va1[i] = aa * i - delta;  va2[i] = aa * i + delta;
    }

    static const int16_t full = (256 << 2) - 1;
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

    int16_t va1[32], va2[32], i, j;
    for(i = 0; i < 32; i++)
    {
        va1[i] = aa * i - delta;  va2[i] = aa * i + delta;
    }

    static const int16_t full = (256 << 1) - 1;
    for(j = 0; j < 32; j++, buf += stride, cc -= bb)for(i = 0; i < 32; i++)
    {
        int16_t c1 = cc - va1[i], c2 = cc - va2[i];
        c1 = (c1 > 0 ? c1 : 0);  c1 = (c1 < full ? c1 : full);
        c2 = (c2 > 0 ? c2 : 0);  c2 = (c2 < full ? c2 : full);
        buf[i] = (c1 + c2) >> 2;
    }
}
