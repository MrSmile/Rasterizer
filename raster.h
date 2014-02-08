// raster.h : rasterizer interface
//

#pragma once

#include "point.h"
#include <algorithm>
#include <vector>



void print_bitmap(const uint8_t *image, size_t width, size_t height, ptrdiff_t stride);


class Polyline
{
public:
    static constexpr int pixel_order = 6;
    static constexpr int tile_order = 6 + 4;  // 16x16 pixels
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
            f_up = 1, f_ur_dl = 2, f_exact_x = 4, f_exact_y = 8
        };

        uint8_t flags;
        int32_t x_min, x_max, y_min, y_max;
        int32_t a, b;
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
            return !x_min && (flags & f_exact_x);
        }

        bool is_split_y() const
        {
            return !y_min && (flags & f_exact_y);
        }

        int delta_horz() const
        {
            return is_split_y() ? 1 - ((2 * flags) & 2) : 0;
        }

        int delta_vert() const
        {
            return is_split_x() ? 1 - ((3 * flags) & 2) : 0;
        }

        void move_x(int32_t x);
        void move_y(int32_t y);
        void split_horz(int32_t x, Line &next);
        void split_vert(int32_t y, Line &next);
    };

    struct ScanSegment
    {
        uint8_t weight;
        uint16_t total;
        int16_t a, b, c;
    };


private:
    int winding_mask;
    std::vector<Line> linebuf[3];
    int32_t x_min, x_max, y_min, y_max;
    std::vector<ScanSegment> scanbuf;
    std::vector<uint8_t> bitmap;
    size_t stride, size_y;


    bool add_line(const Point &pt0, const Point &pt1);
    bool add_quadratic(const Point &pt0, const Point &pt1, const Point &pt2);
    bool add_cubic(const Point &pt0, const Point &pt1, const Point &pt2, const Point &pt3);

    void fill_solid(const Point &orig, int x_ord, int y_ord, bool set);
    void fill_halfplane(const Point &orig, int x_ord, int y_ord, int32_t a, int32_t b, int64_t c);
    void fill_generic(const Point &orig, int x_ord, int y_ord, int index, size_t offs, int winding);
    uint8_t calc_pixel(std::vector<Line> &line, size_t offs, int winding);

    static int split_horz(const std::vector<Line> &src, size_t offs, std::vector<Line> &dst0, std::vector<Line> &dst1, int32_t x);
    static int split_vert(const std::vector<Line> &src, size_t offs, std::vector<Line> &dst0, std::vector<Line> &dst1, int32_t y);
    void rasterize(const Point &orig, int x_ord, int y_ord, int index, size_t offs, int winding);

public:
    bool create(const FT_Outline &path);
    void rasterize(int x0, int y0, int width, int height);

    const uint8_t *image() const
    {
        return bitmap.data();
    }

    const size_t width() const
    {
        return stride;
    }

    const size_t height() const
    {
        return size_y;
    }

    void print() const
    {
        print_bitmap(bitmap.data(), stride, size_y, stride);
    }


    void test();  // DEBUG
};
