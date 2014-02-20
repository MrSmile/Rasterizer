// fill.cpp : low-lever rasterization
//

#include "raster.h"
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

constexpr int16x8_t vec_index[] =
{
    { 0,  1,  2,  3,  4,  5,  6,  7},
    { 8,  9, 10, 11, 12, 13, 14, 15},
    {16, 17, 18, 19, 20, 21, 22, 23},
    {24, 25, 26, 27, 28, 29, 30, 31},
    {32, 33, 34, 35, 36, 37, 38, 39},
    {40, 41, 42, 43, 44, 45, 46, 47},
    {48, 49, 50, 51, 52, 53, 54, 55},
    {56, 57, 58, 59, 60, 61, 62, 63},
};



inline int16_t limit(int16_t value, int16_t top)
{
    return min(top, max(int16_t(0), value));
}

inline int16x8_t limit(int16x8_t value, int16x8_t top)
{
    static constexpr int16x8_t zero = int16x8(0);
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
    return __builtin_ia32_pmaxsw128(value, -value);
}


void fill_solid_line16(uint8_t *buf, int width, int height, ptrdiff_t stride, bool set)
{
    assert(!(reinterpret_cast<uintptr_t>(buf) & 15) && !(stride & 15));
    uint8x16_t *ptr = reinterpret_cast<uint8x16_t *>(__builtin_assume_aligned(buf, 16));
    stride >>= 4;

    uint8x16_t value = set ? uint8x16(255) : uint8x16(0);
    for(int j = 0; j < height; j++, ptr += stride)
        for(int i = 0; i < width; i++)ptr[i] = value;
}

template<int x_ord, int y_ord> void fill_solid(uint8_t *buf, ptrdiff_t stride, bool set)
{
    if(x_ord >= 4)
    {
        fill_solid_line16(buf, int32_t(1) << (x_ord - 4), int32_t(1) << y_ord, stride, set);  return;
    }

    uint8_t value = set ? 255 : 0;
    for(int j = 0; j < 1 << y_ord; j++, buf += stride)
        for(int i = 0; i < 1 << x_ord; i++)buf[i] = value;
}

void Polyline::fill_solid(uint8_t *buf, int width, int height, ptrdiff_t stride, bool set)
{
    assert(width > 0 && !(width & 15) && height > 0);
    fill_solid_line16(buf, width >> 4, height, stride, set);

    /*
    uint8_t value = set ? 255 : 0;
    uint8_t *ptr = reinterpret_cast<uint8_t *>(__builtin_assume_aligned(buf, 16));
    for(int j = 0; j < height; j++, ptr += stride)
        for(int i = 0; i < width; i++)ptr[i] = value;
    */
}


template<int width, int res_ord> struct HalfplaneFillerGeneric
{
    int16_t va1[width], va2[width];

    void init(int16_t a, int16_t delta)
    {
        for(int i = 0; i < width; i++)
        {
            va1[i] = a * i - delta;  va2[i] = a * i + delta;
        }
    }

    void fill_line(uint8_t *buf, int16_t c)
    {
        static constexpr int16_t full = (256 << (8 - res_ord)) - 1;

        for(int i = 0; i < width; i++)
            buf[i] = (limit(c - va1[i], full) + limit(c - va2[i], full)) >> (9 - res_ord);
    }
};

template<int width, int res_ord> struct HalfplaneFillerSSE
{
    int16x8_t va1[width], va2[width];

    void init(int16_t a, int16_t delta)
    {
        for(int i = 0; i < width; i++)
        {
            va1[i] = a * vec_index[i] - delta;  va2[i] = a * vec_index[i] + delta;
        }
    }

    void fill_line(uint8_t *buf, int16_t c)
    {
        static constexpr int16x8_t full = int16x8((256 << (8 - res_ord)) - 1);

        assert(!(reinterpret_cast<uintptr_t>(buf) & 15));
        char8x16_t *ptr = reinterpret_cast<char8x16_t *>(__builtin_assume_aligned(buf, 16));

        int16x8_t res[width];
        for(int i = 0; i < width; i++)res[i] = (limit(c - va1[i], full) + limit(c - va2[i], full)) >> (9 - res_ord);
        for(int i = 0; i < width / 2; i++)ptr[i] = __builtin_ia32_packuswb128(res[2 * i], res[2 * i + 1]);
    }
};

template<int x_ord, int res_ord> struct HalfplaneFiller : public HalfplaneFillerGeneric<1 << x_ord, res_ord> { };
template<int res_ord> struct HalfplaneFiller<4, res_ord> : public HalfplaneFillerSSE<2, res_ord> { };
template<int res_ord> struct HalfplaneFiller<5, res_ord> : public HalfplaneFillerSSE<4, res_ord> { };

template<int x_ord, int y_ord, int res_ord = (x_ord > y_ord ? x_ord : y_ord) + 2>
    void fill_halfplane(uint8_t *buf, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    constexpr int max_ord = (x_ord > y_ord ? x_ord : y_ord) + 1;
    static_assert(res_ord > max_ord, "int16_t overflow!");

    int16_t aa = rounded_shift(a * int64_t(scale), res_ord + 44);
    int16_t bb = rounded_shift(b * int64_t(scale), res_ord + 44);
    assert(!(absval(c >> (Polyline::pixel_order + max_ord)) & 0xFFFFFFFF80000000));
    int16_t cc = rounded_shift(int32_t(c >> (Polyline::pixel_order + max_ord)) * int64_t(scale), res_ord - max_ord + 44);
    cc += (int16_t(1) << (15 - res_ord)) - (int16_t(aa + bb) >> 1);
    int16_t delta = rounded_shift(min(absval(aa), absval(bb)), 2);

    HalfplaneFiller<x_ord, res_ord> filler;  filler.init(aa, delta);
    for(int j = 0; j < 1 << y_ord; j++, buf += stride, cc -= bb)filler.fill_line(buf, cc);
}

void Polyline::fill_halfplane(uint8_t *buf, int width, int height, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale)
{
    assert(width > 0 && !(width & tile_mask) && height > 0 && !(height & tile_mask));
    if(width == tile_mask + 1 && height == tile_mask + 1)
    {
        ::fill_halfplane<tile_order, tile_order>(buf, stride, a, b, c, scale);  return;
    }

    int64_t offs = (int64_t(a) + int64_t(b)) << (pixel_order + tile_order - 1);
    int64_t size = int64_t(uint32_t(absval(a)) + uint32_t(absval(b))) << (pixel_order + tile_order - 1);
    constexpr ptrdiff_t step = 1 << tile_order;  width >>= tile_order;  height >>= tile_order;
    for(int j = 0; j < height; j++, buf += step * stride)
        for(int i = 0; i < width; i++)
        {
            int64_t cc = c - ((a * int64_t(i) + b * int64_t(j)) << (pixel_order + tile_order));
            if(absval(offs - cc) < size)::fill_halfplane<tile_order, tile_order>(buf + i * step, stride, a, b, cc, scale);
            else ::fill_solid<tile_order, tile_order>(buf + i * step, stride, offs < cc);
        }
}


template<int width> struct ScanLineGeneric
{
    int16_t res[width];

    ScanLineGeneric()
    {
        for(int i = 0; i < width; i++)res[i] = 0;
    }

    void fill_line(uint8_t *buf, int16_t offs)
    {
        for(int i = 0; i < width; i++)buf[i] = min<int16_t>(255, absval(res[i] + offs));
    }
};

template<int width> struct ScanLineSSE
{
    int16x8_t res[width];

    ScanLineSSE()
    {
        for(int i = 0; i < width; i++)res[i] = int16x8(0);
    }

    void fill_line(uint8_t *buf, int16_t offs)
    {
        assert(!(reinterpret_cast<uintptr_t>(buf) & 15));
        char8x16_t *ptr = reinterpret_cast<char8x16_t *>(__builtin_assume_aligned(buf, 16));

        for(int i = 0; i < width; i++)res[i] = absval(res[i] + offs);
        for(int i = 0; i < width / 2; i++)ptr[i] = __builtin_ia32_packuswb128(res[2 * i], res[2 * i + 1]);
    }
};

template<int x_ord> struct ScanLine : public ScanLineGeneric<1 << x_ord> { };
template<> struct ScanLine<4> : public ScanLineSSE<2> { };
template<> struct ScanLine<5> : public ScanLineSSE<4> { };


template<int width, int res_ord> struct ScanLineFillerGeneric
{
    int16_t a;

    void init(int16_t a_)
    {
        a = a_;
    }

    void add_line(ScanLineGeneric<width> &line, int16_t c1, int16_t c2)
    {
        static constexpr int16_t full = 256 << (8 - res_ord);

        for(int i = 0; i < width; i++)
            line.res[i] += (limit(c1 - a * i, full) + limit(c2 - a * i, full)) >> (9 - res_ord);
    }

    void add_line(ScanLineGeneric<width> &line, int16_t w, int16_t offs1, int16_t offs2, int16_t size)
    {
        for(int i = 0; i < width; i++)
        {
            int16_t aw = mul_high(a * i, w);
            line.res[i] += limit(offs1 - aw, size) + limit(offs2 - aw, size);
        }
    }
};

template<int width, int res_ord> struct ScanLineFillerSSE
{
    int16x8_t va[width];

    void init(int16_t a)
    {
        for(int i = 0; i < width; i++)va[i] = a * vec_index[i];
    }

    void add_line(ScanLineSSE<width> &line, int16_t c1, int16_t c2)
    {
        static constexpr int16x8_t full = int16x8(256 << (8 - res_ord));

        for(int i = 0; i < width; i++)
            line.res[i] += (limit(c1 - va[i], full) + limit(c2 - va[i], full)) >> (9 - res_ord);
    }

    void add_line(ScanLineSSE<width> &line, int16_t w, int16_t offs1, int16_t offs2, int16_t size)
    {
        for(int i = 0; i < width; i++)
        {
            int16x8_t aw = mul_high(va[i], int16x8(w));
            line.res[i] += limit(offs1 - aw, int16x8(size)) + limit(offs2 - aw, int16x8(size));
        }
    }
};

template<int x_ord, int res_ord> struct ScanLineFillerBase : public ScanLineFillerGeneric<1 << x_ord, res_ord> { };
template<int res_ord> struct ScanLineFillerBase<4, res_ord> : public ScanLineFillerSSE<2, res_ord> { };
template<int res_ord> struct ScanLineFillerBase<5, res_ord> : public ScanLineFillerSSE<4, res_ord> { };


template<int x_ord, int res_ord> struct ScanLineFiller : public ScanLineFillerBase<x_ord, res_ord>
{
    typedef ScanLineFillerBase<x_ord, res_ord> Base;

    int16_t a_abs, b, dc1, dc2;

    void init(int16_t a_, int16_t b_)
    {
        Base::init(a_);  a_abs = absval(a_);  b = b_;
        int16_t delta = rounded_shift(min(a_abs, absval(b_)), 2);
        int16_t base = (int16_t(1) << (15 - res_ord)) - (b >> 1);
        dc1 = base + delta;  dc2 = base - delta;
    }

    void update_line(ScanLine<x_ord> &line, int16_t c, int dn, int up)
    {
        int16_t size = (up - dn) << (7 - Polyline::pixel_order), offs = size >> 1;
        //int16_t w = min<int32_t>(1 << (res_ord + 7), (int32_t(size) << 16) / max<int16_t>(1, a_abs));
        int16_t w = min<int16_t>(0, (size << res_ord) - (a_abs << (2 * res_ord - 9))) + (1 << (res_ord + 7));
        c -= int32_t(b) * int16_t(dn + up) >> (Polyline::pixel_order + 1);
        int16_t dc = min(int32_t(a_abs) << 7, absval(b) * int32_t(size)) >> 9;
        int16_t offs1 = mul_high(c - dc, w) + offs, offs2 = mul_high(c + dc, w) + offs;
        Base::add_line(line, w, offs1, offs2, size);
    }

    void update_line(ScanLine<x_ord> &line, int16_t c)
    {
        //update_line(line, c, 0, 1 << Polyline::pixel_order);
        Base::add_line(line, c + dc1, c + dc2);
    }
};

template<int x_ord, int y_ord, int res_ord = (x_ord > y_ord ? x_ord : y_ord) + 2>
    void fill_generic(uint8_t *buf, ptrdiff_t stride, const Polyline::Line *line, size_t n_lines, int winding)
{
    constexpr int max_ord = (x_ord > y_ord ? x_ord : y_ord) + 1;
    static_assert(res_ord > max_ord, "int16_t overflow!");

    ScanLine<x_ord> res[1 << y_ord];
    ScanLineFiller<x_ord, res_ord> filler;
    int16_t delta[(1 << y_ord) + 2];  memset(delta, 0, sizeof(delta));
    constexpr int16_t pixel_mask = (1 << Polyline::pixel_order) - 1;
    for(size_t i = 0; i < n_lines; i++)
    {
        assert(line[i].y_min >= 0 && line[i].y_min < int32_t(1) << (y_ord + Polyline::pixel_order));
        assert(line[i].y_max > 0 && line[i].y_max <= int32_t(1) << (y_ord + Polyline::pixel_order));
        assert(line[i].y_min <= line[i].y_max);

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

        int16_t a = rounded_shift(line[i].a * int64_t(line[i].scale), res_ord + 44);
        int16_t b = rounded_shift(line[i].b * int64_t(line[i].scale), res_ord + 44);
        assert(!(absval(line[i].c >> (Polyline::pixel_order + max_ord)) & 0xFFFFFFFF80000000));
        int16_t c = rounded_shift(int32_t(line[i].c >> (Polyline::pixel_order + max_ord)) * int64_t(line[i].scale), res_ord - max_ord + 44);
        c -= (a >> 1) + b * dn;  filler.init(a, b);

        if(dn_pos)
        {
            if(up == dn)
            {
                filler.update_line(res[dn], c, dn_pos, up_pos);  continue;
            }
            filler.update_line(res[dn], c, dn_pos, 1 << Polyline::pixel_order);
            dn++;  c -= b;
        }
        for(int j = dn; j < up; j++, c -= b)filler.update_line(res[j], c);
        if(up_pos)filler.update_line(res[up], c, 0, up_pos);
    }

    int16_t cur = 256 * winding;
    for(int j = 0; j < 1 << y_ord; j++, buf += stride)
        res[j].fill_line(buf, cur += delta[j]);
}

void Polyline::fill_generic(uint8_t *buf, int width, int height, ptrdiff_t stride, Line *line, size_t size, int winding)
{
    assert(width == tile_mask + 1 && height == tile_mask + 1);
    ::fill_generic<tile_order, tile_order>(buf, stride, line, size, winding);
}
