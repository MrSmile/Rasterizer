// raster.h : rasterizer interface
//

#pragma once

#include "point.h"
#include <algorithm>
#include <vector>



template<typename T> constexpr T absval(T value)
{
    return value < 0 ? -value : value;
}

template<typename T> T rounded_shift(T value, int shift)
{
    return (value + (T(1) << (shift - 1))) >> shift;
}

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


void print_bitmap(const uint8_t *image, size_t width, size_t height, ptrdiff_t stride);


class Polyline
{
public:
    static constexpr int pixel_order = 6, tile_order = 4;  // 16x16 pixels
    static constexpr int tile_mask = (uint32_t(1) << tile_order) - 1;
    static constexpr int err = 16, e2 = err * err;


    struct Segment
    {
        Point r;
        int64_t r2, er;

        Segment(const Point &r_);
        bool subdivide(const Point &p);
    };

    struct Line
    {
        enum Flags  // TODO: better enum
        {
            f_up = 1, f_ur_dl = 2, f_exact_l = 4, f_exact_r = 8, f_exact_d = 16, f_exact_u = 32
        };

        uint8_t flags;
        int32_t x_min, x_max, y_min, y_max;
        int32_t a, b, scale;
        int64_t c;

        Line() = default;
        Line(const Point &pt0, const Point &pt1);

        bool is_up() const
        {
            return flags & f_up;
        }

        bool is_ur_dl() const
        {
            return flags & f_ur_dl;
        }

        bool is_split_x() const
        {
            return !x_min && (flags & f_exact_l);
        }

        bool is_split_y() const
        {
            return !y_min && (flags & f_exact_d);
        }

        int delta_horz() const
        {
            return is_split_y() ? 1 - ((2 * flags) & 2) : 0;
        }

        int delta_vert() const
        {
            return is_split_x() ? 1 - ((3 * flags) & 2) : 0;
        }

        bool check_l(int32_t x) const
        {
            if(flags & f_exact_l)return x_min >= x;
            int64_t cc = c - a * int64_t(x) - b * int64_t(flags & f_ur_dl ? y_min : y_max);
            if(a < 0)cc = -cc;  return cc >= 0;
        }

        bool check_r(int32_t x) const
        {
            if(flags & f_exact_r)return x_max <= x;
            int64_t cc = c - a * int64_t(x) - b * int64_t(flags & f_ur_dl ? y_max : y_min);
            if(a > 0)cc = -cc;  return cc >= 0;
        }

        bool check_d(int32_t y) const
        {
            if(flags & f_exact_d)return y_min >= y;
            int64_t cc = c - b * int64_t(y) - a * int64_t(flags & f_ur_dl ? x_min : x_max);
            if(b < 0)cc = -cc;  return cc >= 0;
        }

        bool check_u(int32_t y) const
        {
            if(flags & f_exact_u)return y_max <= y;
            int64_t cc = c - b * int64_t(y) - a * int64_t(flags & f_ur_dl ? x_max : x_min);
            if(b > 0)cc = -cc;  return cc >= 0;
        }

        void move_x(int32_t x);
        void move_y(int32_t y);
        void split_horz(int32_t x, Line &next);
        void split_vert(int32_t y, Line &next);
    };


private:
    std::vector<Line> linebuf[3];
    int32_t x_min, x_max, y_min, y_max;


    bool add_line(const Point &pt0, const Point &pt1);
    bool add_quadratic(const Point &pt0, const Point &pt1, const Point &pt2);
    bool add_cubic(const Point &pt0, const Point &pt1, const Point &pt2, const Point &pt3);

    void fill_solid(uint8_t *buf, int width, int height, ptrdiff_t stride, bool set);
    void fill_halfplane(uint8_t *buf, int width, int height, ptrdiff_t stride, int32_t a, int32_t b, int64_t c, int32_t scale);
    void fill_generic(uint8_t *buf, int width, int height, ptrdiff_t stride, Line *line, size_t size, int winding);
    uint8_t calc_pixel(std::vector<Line> &line, size_t offs, int winding);

    static int split_horz(const std::vector<Line> &src, size_t offs, std::vector<Line> &dst0, std::vector<Line> &dst1, int32_t x);
    static int split_vert(const std::vector<Line> &src, size_t offs, std::vector<Line> &dst0, std::vector<Line> &dst1, int32_t y);
    void rasterize(uint8_t *buf, int width, int height, ptrdiff_t stride, int index, size_t offs, int winding);

public:
    bool create(const FT_Outline &path);
    void rasterize(uint8_t *buf, int x0, int y0, int width, int height, ptrdiff_t stride, bool vert_flip = true);


    void test();  // DEBUG
};
