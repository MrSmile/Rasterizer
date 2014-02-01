// raster.cpp : rasterizer implementation
//

#include "raster.h"
#include <iostream>

using namespace std;



bool Polyline::add_line(const Point &pt0, const Point &pt1)
{
    cout << pt1.x << ' ' << pt1.y << endl;  return true;  // TODO
}

bool Polyline::add_quadratic(const Point &pt0, const Point &pt1, const Point &pt2)
{
    Segment seg(pt2 - pt0);
    if(!seg.subdivide(pt1 - pt0))return add_line(pt0, pt2);

    Point p01 = pt0 + pt1, p12 = pt1 + pt2, c = (p01 + p12 + Point(2, 2)) >> 2;  // TODO: overflow?
    return add_quadratic(pt0, p01 >> 1, c) && add_quadratic(c, p12 >> 1, pt2);
}

bool Polyline::add_cubic(const Point &pt0, const Point &pt1, const Point &pt2, const Point &pt3)
{
    Segment seg(pt3 - pt0);
    if(!seg.subdivide(pt1 - pt0) && !seg.subdivide(pt2 - pt0))return add_line(pt0, pt3);

    Point p01 = pt0 + pt1, p12 = pt1 + pt2 + Point(2, 2), p23 = pt2 + pt3;
    Point p012 = p01 + p12, p123 = p12 + p23, c = (p012 + p123 - Point(1, 1)) >> 3;  // TODO: overflow?
    return add_cubic(pt0, p01 >> 1, p012 >> 2, c) && add_cubic(c, p123 >> 2, p23 >> 1, pt3);
}

bool Polyline::create(const FT_Outline &path)
{
    enum Status
    {
        s_on, s_q, s_c1, s_c2
    };

    for(int i = 0, j = 0; i < path.n_contours; i++)
    {
        Point start, p[4];
        bool skip_end = false;
        Status st;

        int last = path.contours[i];
        switch(FT_CURVE_TAG(path.tags[j]))
        {
        case FT_CURVE_TAG_ON:
            start = p[0] = path.points[j];  st = s_on;  break;

        case FT_CURVE_TAG_CONIC:
            switch(FT_CURVE_TAG(path.tags[last]))
            {
            case FT_CURVE_TAG_ON:
                p[0] = path.points[last];  p[1] = path.points[j];
                skip_end = true;  st = s_q;  break;

            case FT_CURVE_TAG_CONIC:
                start = p[0] = ((p[1] = path.points[j]) + path.points[last]) >> 1;
                st = s_q;  break;

            default:
                return false;
            }
            break;

        default:
            return false;
        }

        for(j++; j <= last; j++)switch(FT_CURVE_TAG(path.tags[j]))
        {
        case FT_CURVE_TAG_ON:
            switch(st)
            {
            case s_on:
                if(!add_line(p[0], p[1] = path.points[j]))return false;
                p[0] = p[1];  break;

            case s_q:
                if(!add_quadratic(p[0], p[1], p[2] = path.points[j]))return false;
                p[0] = p[2];  st = s_on;  break;

            case s_c2:
                if(!add_cubic(p[0], p[1], p[2], p[3] = path.points[j]))return false;
                p[0] = p[3];  st = s_on;  break;

            default:
                return false;
            }
            break;

        case FT_CURVE_TAG_CONIC:
            switch(st)
            {
            case s_on:
                p[1] = path.points[j];  st = s_q;  break;

            case s_q:
                if(!add_quadratic(p[0], p[1], p[2] = (p[1] + (p[3] = path.points[j])) >> 1))return false;
                p[0] = p[2];  p[1] = p[3];  break;

            default:
                return false;
            }
            break;

        case FT_CURVE_TAG_CUBIC:
            switch(st)
            {
            case s_on:
                p[1] = path.points[j];  st = s_c1;  break;

            case s_c1:
                p[2] = path.points[j];  st = s_c2;  break;

            default:
                return false;
            }
            break;

        default:
            return false;
        }

        if(!skip_end)switch(st)
        {
        case s_on:
            if(!add_line(p[0], start))return false;  break;

        case s_q:
            if(!add_quadratic(p[0], p[1], start))return false;  break;

        case s_c2:
            if(!add_cubic(p[0], p[1], p[2], start))return false;  break;

        default:
            return false;
        }
    }
    return true;
}
