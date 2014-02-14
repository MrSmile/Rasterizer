// fill.cpp : low-lever rasterization
//

#include "raster.h"
#include <iostream>  // DEBUG
#include <cassert>
#include <limits>

using namespace std;



typedef char char8x16_t __attribute__ ((vector_size (16)));
typedef uint8_t uint8x16_t __attribute__ ((vector_size (16)));
typedef int16_t int16x8_t __attribute__ ((vector_size (16)));
typedef uint16_t uint16x8_t __attribute__ ((vector_size (16)));

inline constexpr uint8x16_t uint8x16(uint8_t val)
{
    return uint8x16_t{val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val};
}

inline constexpr int16x8_t int16x8(int16_t val)
{
    return int16x8_t{val, val, val, val, val, val, val, val};
}


inline int16_t limit(int16_t value, int16_t top)
{
    return min(top, max(int16_t(0), value));
}

inline int16x8_t limit(int16x8_t value, int16x8_t top)
{
    constexpr int16x8_t zero = int16x8(0);
    return __builtin_ia32_pminsw128(top, __builtin_ia32_pmaxsw128(zero, value));
}

inline int16_t mul_high(int16_t a, int16_t b)
{
    return a * int32_t(b) >> 16;
}

inline int16x8_t mul_high(int16x8_t a, int16x8_t b)
{
    return __builtin_ia32_pmulhw128(a, b);
}

inline int16x8_t absval(int16x8_t value)
{
    int16x8_t flag = value >> 15;
    return (value ^ flag) + (flag & 1);
}


void fill_solid_line16(uint8_t *buf, ptrdiff_t stride, int32_t width, int32_t height, bool set)
{
    assert(!(reinterpret_cast<uintptr_t>(buf) & 15) && !(stride & 15));
    uint8x16_t *ptr = reinterpret_cast<uint8x16_t *>(__builtin_assume_aligned(buf, 16));
    stride >>= 4;

    uint8x16_t value = set ? uint8x16(255) : uint8x16(0);
    for(int32_t j = 0; j < height; j++, ptr += stride)
        for(int32_t i = 0; i < width; i++)ptr[i] = value;
}

template<int x_ord, int y_ord> void fill_solid(uint8_t *buf, ptrdiff_t stride, bool set)
{
    if(x_ord >= 4)
    {
        fill_solid_line16(buf, stride, int32_t(1) << (x_ord - 4),  int32_t(1) << y_ord, set);  return;
    }

    uint8_t value = set ? 255 : 0;
    for(int j = 0; j < 1 << y_ord; j++, buf += stride)
        for(int i = 0; i < 1 << x_ord; i++)buf[i] = value;
}

void Polyline::fill_solid(uint8_t *buf, ptrdiff_t stride, int x_ord, int y_ord, bool set)
{
    x_ord -= pixel_order;  y_ord -= pixel_order;
#define LINE(x, y)  case x | y << 8:  ::fill_solid<x, y>(buf, stride, set);  return;
    switch(x_ord | y_ord << 8)
    {
        LINE(0, 0)  LINE(1, 0)  LINE(1, 1)  LINE(2, 1)  LINE(2, 2)  LINE(3, 2)  LINE(3, 3)
        default:  break;
    }
#undef LINE
    fill_solid_line16(buf, stride, int32_t(1) << (x_ord - 4), int32_t(1) << y_ord, set);

    /*
    uint8_t value = set ? 255 : 0;
    uint8_t *ptr = reinterpret_cast<uint8_t *>(__builtin_assume_aligned(buf, 16));
    for(int32_t j = 0; j < int32_t(1) << y_ord; j++, ptr += stride)
        for(int32_t i = 0; i < int32_t(1) << x_ord; i++)ptr[i] = value;
    */
}


template<int x_ord, int y_ord, int res_ord = (x_ord > y_ord ? x_ord : y_ord) + 2>
    void fill_halfplane(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c)
{
    static_assert(res_ord > x_ord + 1 && res_ord > y_ord + 1, "int16_t overflow!");

    int16_t aa = rounded_shift(a, res_ord + 14);
    int16_t bb = rounded_shift(b, res_ord + 14);
    int16_t cc = rounded_shift(2 * c - a - b, res_ord + 15) + (int16_t(1) << (15 - res_ord));

    int16_t va[1 << x_ord];
    for(int i = 0; i < 1 << x_ord; i++)va[i] = aa * i;
    for(int j = 0; j < 1 << y_ord; j++, buf += stride, cc -= bb)
        for(int i = 0; i < 1 << x_ord; i++)
            buf[i] = limit((cc - va[i]) >> (8 - res_ord), 255);
}

template<> void fill_halfplane<4, 4, 6>(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c)
{
    constexpr int res_ord = 6;
    assert(!(reinterpret_cast<uintptr_t>(buf) & 15) && !(stride & 15));
    char8x16_t *ptr = reinterpret_cast<char8x16_t *>(__builtin_assume_aligned(buf, 16));
    stride >>= 4;

    int16_t aa = rounded_shift(a, res_ord + 14);
    int16_t bb = rounded_shift(b, res_ord + 14);
    int16_t cc = rounded_shift(2 * c - a - b, res_ord + 15) + (int16_t(1) << (15 - res_ord));

    int16x8_t va[2] = {{0, 1, 2, 3, 4, 5, 6, 7}, {8, 9, 10, 11, 12, 13, 14, 15}};
    for(int i = 0; i < 2; i++)va[i] *= aa;
    for(int j = 0; j < 16; j++, ptr += stride, cc -= bb)
    {
        int16x8_t res[2];
        for(int i = 0; i < 2; i++)res[i] = (cc - va[i]) >> (8 - res_ord);
        *ptr = __builtin_ia32_packuswb128(res[0], res[1]);
    }
}

void Polyline::fill_halfplane(uint8_t *buf, ptrdiff_t stride, int x_ord, int y_ord, int32_t a, int32_t b, int64_t c)
{
    x_ord -= pixel_order;  y_ord -= pixel_order;
#define LINE(x, y)  case x | y << 8:  ::fill_halfplane<x, y>(buf, stride, a, b, c);  return;
    switch(x_ord | y_ord << 8)
    {
        LINE(0, 0)  LINE(1, 0)  LINE(1, 1)  LINE(2, 1)  LINE(2, 2)  LINE(3, 2)  LINE(3, 3)  LINE(4, 3)  LINE(4, 4)
        default:  break;
    }
#undef LINE

    constexpr int n = 4;
    int64_t offs = (int64_t(a) + int64_t(b)) << (n - 1);
    int64_t size = int64_t(uint32_t(absval(a)) + uint32_t(absval(b))) << (n - 1);
    const ptrdiff_t step = 1 << n;  x_ord -= n;  y_ord -= n;
    for(int j = 0; j < 1 << y_ord; j++, buf += step * stride)
        for(int i = 0; i < 1 << x_ord; i++)
        {
            int64_t cc = c - a * int64_t(step * i) - b * int64_t(step * j);
            if(absval(offs - cc) < size)::fill_halfplane<n, n>(buf + i * step, stride, a, b, cc);
            else ::fill_solid<n, n>(buf + i * step, stride, offs < cc);
        }
}


template<int x_ord, int y_ord, int res_ord> void fill_line(uint8_t *buf, int16_t base, Polyline::ScanSegment *seg, int n_seg)
{
    int16_t res[1 << x_ord];
    for(int i = 0; i < 1 << x_ord; i++)res[i] = base;
    for(int k = 0; k < n_seg; k++)
    {
        int16_t top = min<int16_t>(1 << Polyline::pixel_order, seg[k].total);
        int16_t size = (top - seg[k].cur) << (8 - Polyline::pixel_order), offs = size >> 1;
        //int16_t w = min<int32_t>(1 << (res_ord + 8), (int32_t(size) << 16) / absval(seg[k].a));
        int16_t w = min<int16_t>(0, (size << res_ord) - (absval(seg[k].a) << (2 * res_ord - 8))) + (1 << (res_ord + 8));
        int16_t c = seg[k].c - ((int32_t(seg[k].b) * int16_t(top + seg[k].cur)) >> (Polyline::pixel_order + 1));

        for(int i = 0; i < 1 << x_ord; i++)
            res[i] += limit(mul_high(c - seg[k].a * i, w) + offs, size);
    }
    for(int i = 0; i < 1 << x_ord; i++)buf[i] = min<int16_t>(255, absval(res[i]));
}

template<> void fill_line<4, 4, 6>(uint8_t *buf, int16_t base, Polyline::ScanSegment *seg, int n_seg)
{
    constexpr int res_ord = 6;
    static const int16x8_t vi[2] = {{0, 1, 2, 3, 4, 5, 6, 7}, {8, 9, 10, 11, 12, 13, 14, 15}};

    assert(!(reinterpret_cast<uintptr_t>(buf) & 15));
    char8x16_t *ptr = reinterpret_cast<char8x16_t *>(__builtin_assume_aligned(buf, 16));

    int16x8_t res[2] = {int16x8(base), int16x8(base)};
    for(int k = 0; k < n_seg; k++)
    {
        int16_t top = min<int16_t>(1 << Polyline::pixel_order, seg[k].total);
        int16_t size = (top - seg[k].cur) << (8 - Polyline::pixel_order), offs = size >> 1;
        //int16_t w = min<int32_t>(1 << (res_ord + 8), (int32_t(size) << 16) / absval(seg[k].a));
        int16_t w = min<int16_t>(0, (size << res_ord) - (absval(seg[k].a) << (2 * res_ord - 8))) + (1 << (res_ord + 8));
        int16_t c = seg[k].c - ((int32_t(seg[k].b) * int16_t(top + seg[k].cur)) >> (Polyline::pixel_order + 1));

        for(int i = 0; i < 2; i++)
            res[i] += limit(mul_high(c - seg[k].a * vi[i], int16x8(w)) + offs, int16x8(size));
    }
    for(int i = 0; i < 2; i++)res[i] = absval(res[i]);
    *ptr = __builtin_ia32_packuswb128(res[0], res[1]);
}

template<int x_ord, int y_ord, int res_ord = (x_ord > y_ord ? x_ord : y_ord) + 2>
    void fill_generic(uint8_t *buf, ptrdiff_t stride,
        const Polyline::Line *line, size_t n_lines, int winding, Polyline::ScanSegment *seg)
{
    static_assert(res_ord > x_ord + 1 && res_ord > y_ord + 1, "int16_t overflow!");

    size_t count[1 << y_ord];  memset(count, 0, sizeof(count));
    for(size_t i = 0; i < n_lines; i++)
    {
        assert(line[i].y_min >= 0 && line[i].y_min < int32_t(1) << (y_ord + Polyline::pixel_order));
        assert(line[i].y_max > 0 && line[i].y_max <= int32_t(1) << (y_ord + Polyline::pixel_order));
        if(line[i].y_min < line[i].y_max)count[line[i].y_min >> Polyline::pixel_order]++;
    }

    size_t index[1 << y_ord], prev = 0;
    for(int i = 0; i < 1 << y_ord; i++)index[i] = (prev += count[i]);

    int16_t delta[(1 << y_ord) + 2];  memset(delta, 0, sizeof(delta));
    constexpr int16_t pixel_mask = (1 << Polyline::pixel_order) - 1;
    for(size_t i = 0; i < n_lines; i++)
    {
        int16_t dn_delta = line[i].is_up() ? 256 >> Polyline::pixel_order : 0;
        int16_t up_delta = line[i].is_split_x() == line[i].is_up() ? 0 : 256 >> Polyline::pixel_order;
        if(line[i].is_ur_dl())swap(dn_delta, up_delta);

        int dn = line[i].y_min >> Polyline::pixel_order;
        int up = line[i].y_max >> Polyline::pixel_order;
        int16_t dn_pos = line[i].y_min & pixel_mask, dn_delta1 = dn_delta * dn_pos;
        int16_t up_pos = line[i].y_max & pixel_mask, up_delta1 = up_delta * up_pos;
        delta[dn + 1] -= dn_delta1;  delta[dn] -= (dn_delta << Polyline::pixel_order) - dn_delta1;
        delta[up + 1] += up_delta1;  delta[up] += (up_delta << Polyline::pixel_order) - up_delta1;
        if(line[i].y_min == line[i].y_max)continue;

        size_t pos = --index[dn];  seg[pos].cur = dn_pos;
        seg[pos].total = line[i].y_max - (dn << Polyline::pixel_order);
        assert(seg[pos].total - seg[pos].cur == line[i].y_max - line[i].y_min);

        seg[pos].a = line[i].a_norm(res_ord + 14);  seg[pos].b = line[i].b_norm(res_ord + 14);
        seg[pos].c = line[i].c_norm(res_ord + 14) - (seg[pos].a >> 1) - seg[pos].b * dn;
    }

    int16_t cur = 256 * winding;  int beg = 0, end = 0;
    for(int j = 0; j < 1 << y_ord; j++, buf += stride)
    {
        int pos = end;
        for(int k = end - 1; k != beg - 1; k--)if(seg[k].total > 1 << Polyline::pixel_order)
        {
            pos--;  seg[pos].cur = 0;
            seg[pos].total = seg[k].total - (1 << Polyline::pixel_order);
            seg[pos].a = seg[k].a;  seg[pos].b = seg[k].b;
            seg[pos].c = seg[k].c - seg[pos].b;
        }
        beg = pos;  end += count[j];

        fill_line<x_ord, y_ord, res_ord>(buf, cur += delta[j], seg + beg, end - beg);  // unsupported winding_mask!
    }
}

void Polyline::fill_generic(uint8_t *buf, ptrdiff_t stride, int x_ord, int y_ord, int index, size_t offs, int winding)
{
    Line *line = linebuf[index].data() + offs;
    size_t size = linebuf[index].size() - offs;
    scanbuf.resize(size);

    x_ord -= pixel_order;  y_ord -= pixel_order;
#define LINE(x, y)  case x | y << 8:  ::fill_generic<x, y>(buf, stride, line, size, winding, scanbuf.data());  return;
    switch(x_ord | y_ord << 8)
    {
        LINE(0, 0)  LINE(1, 0)  LINE(1, 1)  LINE(2, 1)  LINE(2, 2)  LINE(3, 2)  LINE(3, 3)  LINE(4, 3)  LINE(4, 4)
        default:  assert(false);
    }
#undef LINE
}
