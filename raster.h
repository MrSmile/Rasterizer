// raster.h : rasterizer interface
//

#pragma once

#include "point.h"
#include <algorithm>
#include <vector>



class Polyline
{
    static constexpr int pixel_order = 6;
    static constexpr int tile_order = 6 + 3;  // 8x8 pixels
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
            f_mask = 3, f_nosplit = 1, s_horz = 2, s_vert = 4
        };

        uint8_t flags;
        int32_t x_min, x_max, y_min, y_max;
        int32_t a, b;
        int64_t c;

        Line() = default;
        Line(const Point &pt0, const Point &pt1);

        bool ur_dl() const
        {
            return (flags ^ flags >> 1) & 1;
        }

        void move_x(int32_t x);
        void move_y(int32_t y);
        void split_horz(int32_t x, Line &next);
        void split_vert(int32_t y, Line &next);
    };


    int winding_mask;
    std::vector<Line> line;
    int32_t x_min, x_max, y_min, y_max;
    std::vector<uint8_t> bitmap;
    size_t stride;


    bool add_line(const Point &pt0, const Point &pt1);
    bool add_quadratic(const Point &pt0, const Point &pt1, const Point &pt2);
    bool add_cubic(const Point &pt0, const Point &pt1, const Point &pt2, const Point &pt3);

    void fill_solid(const Point &orig, int x_ord, int y_ord, uint8_t value);
    void fill_halfplane(const Point &orig, int x_ord, int y_ord, int32_t a, int32_t b, int64_t c);
    uint8_t calc_pixel(size_t offs, int winding);

    size_t split_horz(size_t offs, int &winding, int32_t x);
    size_t split_vert(size_t offs, int &winding, int32_t y);
    void rasterize(const Point &orig, int x_ord, int y_ord, size_t offs, int winding);

public:
    bool create(const FT_Outline &path);
    void rasterize(int x0, int y0, int width, int height);
    void print();

    void test();  // DEBUG
};
