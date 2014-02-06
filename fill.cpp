// fill.cpp : low-lever rasterization
//

#include "raster.h"
#include <iostream>  // DEBUG
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



void Polyline::fill_halfplane(const Point &orig, int x_ord, int y_ord, int32_t a, int32_t b, int64_t c)
{
    uint32_t scale = max(abs(a), abs(b));
    int order = ilog2(scale);  scale <<= 31 - order;
    scale = (uint64_t(1) << 61) / scale;

    a = rounded_shift(int64_t(a) * scale, order);
    b = rounded_shift(int64_t(b) * scale, order);
    int c_ord = ilog2(uint64_t(abs(c)));  c = rounded_shift(c << (62 - c_ord), 32);
    c = rounded_shift(c * scale, order - c_ord + pixel_order + 30);

    Point r = orig >> pixel_order;
    x_ord -= pixel_order;  y_ord -= pixel_order;
    uint8_t *ptr = bitmap.data() + r.x + (size_y - r.y - 1) * stride;
    switch(x_ord | y_ord << 8)
    {
        case 0 | 0 << 8:  ::fill_halfplane<0, 0>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 1 | 0 << 8:  ::fill_halfplane<1, 0>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 1 | 1 << 8:  ::fill_halfplane<1, 1>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 2 | 1 << 8:  ::fill_halfplane<2, 1>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 2 | 2 << 8:  ::fill_halfplane<2, 2>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 3 | 2 << 8:  ::fill_halfplane<3, 2>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 3 | 3 << 8:  ::fill_halfplane<3, 3>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 4 | 3 << 8:  ::fill_halfplane<4, 3>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        case 4 | 4 << 8:  ::fill_halfplane<4, 4>(ptr, -ptrdiff_t(stride), a, b, c);  return;
        default:  break;
    }

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
