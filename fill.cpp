// fill.cpp : low-lever rasterization
//

#include "raster.h"
#include <cassert>
#include <limits>

using namespace std;



inline int ilog2(unsigned n)
{
    return (std::numeric_limits<unsigned>::digits - 1) ^ __builtin_clz(n);
}

inline int ilog2(unsigned long n)
{
    return (std::numeric_limits<unsigned long>::digits - 1) ^ __builtin_clzl(n);
}

inline int ilog2(unsigned long long n)
{
    return (std::numeric_limits<unsigned long long>::digits - 1) ^ __builtin_clzll(n);
}


template<typename T> T rounded_shift(T value, int shift)
{
    return (value + (T(1) << (shift - 1))) >> shift;
}

inline int16_t max0(int16_t value)
{
    return value & ~(value >> 15);
}

template<int x_ord, int y_ord, int res_ord = (x_ord > y_ord ? x_ord : y_ord) + 2>
    void fill_halfplane(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c)
{
    static_assert(res_ord > x_ord + 1 && res_ord > y_ord + 1, "int16_t overflow!");

    int16_t aa = rounded_shift(a, res_ord + 14);
    int16_t bb = rounded_shift(b, res_ord + 14);
    const int16_t full = (int16_t(1) << (16 - res_ord)) - 1;
    int16_t cc = rounded_shift(2 * c - a - b, res_ord + 15) - full / 2;

    int16_t va[1 << x_ord];
    for(int i = 0; i < 1 << x_ord; i++)va[i] = aa * i;
    for(int j = 0; j < 1 << y_ord; j++, buf += stride, cc -= bb)
        for(int i = 0; i < 1 << x_ord; i++)
            buf[i] = max0(full - max0(va[i] - cc)) >> (8 - res_ord);
}

template<int x_ord, int y_ord> void fill_solid(uint8_t *buf, ptrdiff_t stride, uint8_t value)
{
    for(int j = 0; j < 1 << y_ord; j++, buf += stride)
        for(int i = 0; i < 1 << x_ord; i++)buf[i] = value;
}


inline void normalize_halfplane(int32_t &a, int32_t &b, int64_t &c)
{
    uint32_t scale = max(abs(a), abs(b));
    int order = ilog2(scale);  scale <<= 31 - order;
    scale = (uint64_t(1) << 61) / scale;

    a = rounded_shift(int64_t(a) * scale, order);
    b = rounded_shift(int64_t(b) * scale, order);
    int c_ord = ilog2(uint64_t(abs(c)));  c = rounded_shift(c << (62 - c_ord), 32);
    c = rounded_shift(c * scale, order - c_ord + Polyline::pixel_order + 30);
}

void Polyline::fill_halfplane(const Point &orig, int x_ord, int y_ord, int32_t a, int32_t b, int64_t c)
{
    normalize_halfplane(a, b, c);
    Point r = orig >> pixel_order;
    x_ord -= pixel_order;  y_ord -= pixel_order;
    uint8_t *ptr = bitmap.data() + r.x + (size_y - r.y - 1) * stride;
#define LINE(x, y)  case x | y << 8:  ::fill_halfplane<x, y>(ptr, -ptrdiff_t(stride), a, b, c);  return;
    switch(x_ord | y_ord << 8)
    {
        LINE(0, 0)  LINE(1, 0)  LINE(1, 1)  LINE(2, 1)  LINE(2, 2)  LINE(3, 2)  LINE(3, 3)  LINE(4, 3)  LINE(4, 4)
        default:  break;
    }
#undef LINE

    constexpr int n = 4;
    int64_t offs = (int64_t(a) + int64_t(b)) << (n - 1);
    int64_t size = int64_t(uint32_t(abs(a)) + uint32_t(abs(b))) << (n - 1);
    const ptrdiff_t step = 1 << n;  x_ord -= n;  y_ord -= n;
    for(int j = 0; j < 1 << y_ord; j++, ptr -= step * stride)
        for(int i = 0; i < 1 << x_ord; i++)
        {
            int64_t cc = c - a * int64_t(step * i) - b * int64_t(step * j);
            if(abs(offs - cc) < size)
                ::fill_halfplane<n, n>(ptr + i * step, -ptrdiff_t(stride), a, b, cc);
            else ::fill_solid<n, n>(ptr + i * step, -ptrdiff_t(stride), (offs - cc) >> 63);
        }
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
    constexpr int16_t full = (int16_t(1) << (16 - res_ord)) - 1;
    for(size_t i = 0; i < n_lines; i++)
    {
        int16_t dn_delta = line[i].is_up() ? 255 : 0;
        int16_t up_delta = line[i].is_split_x() == line[i].is_up() ? 0 : 255;
        if(line[i].is_ur_dl())swap(dn_delta, up_delta);

        int dn = line[i].y_min >> Polyline::pixel_order;
        int up = line[i].y_max >> Polyline::pixel_order;
        int16_t dn_pos = line[i].y_min & pixel_mask, dn_delta1 = dn_delta * dn_pos;
        int16_t up_pos = line[i].y_max & pixel_mask, up_delta1 = up_delta * up_pos;
        delta[dn + 1] -= dn_delta1;  delta[dn] -= (dn_delta << Polyline::pixel_order) - dn_delta1;
        delta[up + 1] += up_delta1;  delta[up] += (up_delta << Polyline::pixel_order) - up_delta1;
        if(line[i].y_min == line[i].y_max)continue;

        size_t pos = --index[dn];
        seg[pos].total = line[i].y_max - (dn << Polyline::pixel_order);
        seg[pos].weight = min<int16_t>(seg[pos].total, 1 << Polyline::pixel_order);
        seg[pos].total -= seg[pos].weight;  seg[pos].weight -= dn_pos;
        assert(seg[pos].weight > 0 && seg[pos].weight + seg[pos].total == line[i].y_max - line[i].y_min);

        int32_t a = line[i].a, b = line[i].b;  int64_t c = line[i].c;
        normalize_halfplane(a, b, c);  c -= b * int64_t(dn);
        seg[pos].a = rounded_shift(a, res_ord + 14);
        seg[pos].b = rounded_shift(b, res_ord + 14);
        seg[pos].c = rounded_shift(2 * c - a - b, res_ord + 15) - full / 2;
    }

    int beg = 0, end = 0;
    int16_t cur = (255 << Polyline::pixel_order) * winding;
    for(int j = 0; j < 1 << y_ord; j++, buf += stride)
    {
        int pos = end;
        for(int k = end - 1; k != beg - 1; k--)if(seg[k].total)
        {
            pos--;
            seg[pos].weight = min<int16_t>(seg[k].total, 1 << Polyline::pixel_order);
            seg[pos].total = seg[k].total - seg[pos].weight;
            seg[pos].a = seg[k].a;  seg[pos].b = seg[k].b;
            seg[pos].c = seg[k].c - seg[pos].b;
        }
        beg = pos;  end += count[j];

        cur += delta[j];  int16_t res[1 << x_ord];
        for(int i = 0; i < 1 << x_ord; i++)res[i] = cur;
        for(int k = beg; k < end; k++)for(int i = 0; i < 1 << x_ord; i++)
            res[i] += seg[k].weight * (max0(full - max0(seg[k].a * i - seg[k].c)) >> (8 - res_ord));
        for(int i = 0; i < 1 << x_ord; i++)buf[i] = min<int16_t>(255, abs(res[i] >> Polyline::pixel_order));  // unsupported winding_mask!
    }
}

void Polyline::fill_generic(const Point &orig, int x_ord, int y_ord, int index, size_t offs, int winding)
{
    Line *buf = linebuf[index].data() + offs;
    size_t size = linebuf[index].size() - offs;
    scanbuf.resize(size);

    Point r = orig >> pixel_order;
    x_ord -= pixel_order;  y_ord -= pixel_order;
    uint8_t *ptr = bitmap.data() + r.x + (size_y - r.y - 1) * stride;
#define LINE(x, y)  case x | y << 8:  ::fill_generic<x, y>(ptr, -ptrdiff_t(stride), buf, size, winding, scanbuf.data());  return;
    switch(x_ord | y_ord << 8)
    {
        LINE(0, 0)  LINE(1, 0)  LINE(1, 1)  LINE(2, 1)  LINE(2, 2)  LINE(3, 2)  LINE(3, 3)  LINE(4, 3)  LINE(4, 4)
        default:  assert(false);
    }
#undef LINE
}