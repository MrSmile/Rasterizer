// raster.h : rasterizer interface
//

#pragma once

#include "point.h"
#include <algorithm>



class Polyline
{
    static const int err = 16, e2 = err * err;


    struct Segment
    {
        Point r;
        int64_t r2, er;

        Segment(const Point &r_) : r(r_), r2(r_ * r_), er(int64_t(err) * std::max(std::abs(r_.x), std::abs(r_.y)))
        {
        }

        bool subdivide(const Point &p)
        {
            int64_t pdr = p * r, pcr = p % r;
            return pdr < -er || pdr > r2 + er || std::abs(pcr) > er;
        }
    };


    bool add_line(const Point &pt0, const Point &pt1);
    bool add_quadratic(const Point &pt0, const Point &pt1, const Point &pt2);
    bool add_cubic(const Point &pt0, const Point &pt1, const Point &pt2, const Point &pt3);

public:
    bool create(const FT_Outline &path);
};
