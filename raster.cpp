// raster.cpp : rasterizer implementation
//

#include "raster.h"
#include <iostream>
#include <cassert>
#include <limits>

using namespace std;



Polyline::Segment::Segment(const Point &r_) : r(r_), r2(r_ * r_), er(int64_t(err) * max(abs(r_.x), abs(r_.y)))
{
}

bool Polyline::Segment::subdivide(const Point &p)
{
    int64_t pdr = p * r, pcr = p % r;
    return pdr < -er || pdr > r2 + er || abs(pcr) > er;
}

Polyline::Line::Line(const Point &pt0, const Point &pt1)
{
    Point r = pt1 - pt0;
    a = r.y;  b = -r.x;  c = a * pt0.x + b * pt0.y;
    x_min = min(pt0.x, pt1.x);  x_max = max(pt0.x, pt1.x);
    y_min = min(pt0.y, pt1.y);  y_max = max(pt0.y, pt1.y);
    flags = f_nosplit << s_horz | f_nosplit << s_vert;
    if(r.x >= 0)flags |= 2;  if(r.y < 0)flags |= 1;
}

bool Polyline::add_line(const Point &pt0, const Point &pt1)
{
    x_min = min(x_min, pt0.x);  x_max = max(x_max, pt0.x);
    y_min = min(y_min, pt0.y);  y_max = max(y_max, pt0.y);
    line.emplace_back(pt0, pt1);  return true;
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
    winding_mask = path.flags & FT_OUTLINE_EVEN_ODD_FILL ? 1 : -1;
    x_min = y_min = numeric_limits<int32_t>::max();
    x_max = y_max = numeric_limits<int32_t>::min();
    line.clear();

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


void Polyline::fill_solid(const Point &orig, int x_ord, int y_ord, uint8_t value)
{
    Point r = orig >> pixel_order;
    x_ord -= pixel_order;  y_ord -= pixel_order;
    uint8_t *ptr = bitmap.data() + r.x + r.y * stride;
    for(int32_t j = 0; j < int32_t(1) << y_ord; j++, ptr += stride)
        for(int32_t i = 0; i < int32_t(1) << x_ord; i++)ptr[i] = value;
}

void Polyline::fill_halfplane(const Point &orig, int x_ord, int y_ord, int32_t a, int32_t b, int64_t c)
{
    Point r = orig >> pixel_order;
    x_ord -= pixel_order;  y_ord -= pixel_order;
    uint8_t *ptr = bitmap.data() + r.x + r.y * stride;
    c = (c - ((a + b) << (pixel_order - 1))) >> pixel_order;
    for(int32_t j = 0; j < int32_t(1) << y_ord; j++, ptr += stride)
        for(int32_t i = 0; i < int32_t(1) << x_ord; i++)
            ptr[i] = a * i + b * j <= c ? 255 : 0;  // TODO: AA
}

uint8_t Polyline::calc_pixel(size_t offs, int winding)
{
    return 0;  // TODO
}


void Polyline::Line::move_x(int32_t x)
{
    x_min -= x;  x_max -= x;  c -= a * x;
    if(x_min || !(flags & 1 << s_vert))return;
    flags ^= flags << (s_vert + 0) & 2 << s_vert | 1 << s_vert;
}

void Polyline::Line::move_y(int32_t y)
{
    y_min -= y;  y_max -= y;  c -= b * y;
    if(y_min || !(flags & 1 << s_horz))return;
    flags ^= flags << (s_horz + 1) & 2 << s_horz | 1 << s_horz;
}

void Polyline::Line::split_horz(int32_t x, Line &next)
{
    next.a = a;  next.b = b;  next.c = c - a * x;
    int32_t y = next.c / b, y1 = y;  if(b * y != next.c)y1++;  // TODO: optimize out division
    assert(y <= next.c / double(b) && y1 >= next.c / double(b));

    next.x_min = 0;  next.x_max = x_max - x;  x_max = x;
    if(ur_dl())
    {
        next.flags = flags & f_mask | f_nosplit << s_horz;
        next.y_min = y;  next.y_max = y_max;  y_max = y1;
    }
    else
    {
        next.flags = flags & (f_mask | f_mask << s_horz);
        flags = flags & ~(f_mask << s_horz) | f_nosplit << s_horz;
        next.y_min = y_min;  next.y_max = y1;  y_min = y;
    }
    next.flags |= flags << (s_vert + 0) & 2 << s_vert;
}

void Polyline::Line::split_vert(int32_t y, Line &next)
{
    next.a = a;  next.b = b;  next.c = c - b * y;
    int32_t x = next.c / a, x1 = x;  if(a * x != next.c)x1++;  // TODO: optimize out division
    assert(x <= next.c / double(a) && x1 >= next.c / double(a));

    next.y_min = 0;  next.y_max = y_max - y;  y_max = y;
    if(ur_dl())
    {
        next.flags = flags & f_mask | f_nosplit << s_vert;
        next.x_min = x;  next.x_max = x_max;  x_max = x1;
    }
    else
    {
        next.flags = flags & (f_mask | f_mask << s_vert);
        flags = flags & ~(f_mask << s_vert) | f_nosplit << s_vert;
        next.x_min = x_min;  next.x_max = x1;  x_min = x;
    }
    next.flags |= flags << (s_horz + 1) & 2 << s_horz;
}

size_t Polyline::split_horz(size_t offs, int &winding, int32_t x)
{
    size_t size = line.size(), last = 0;
    for(size_t i = offs; i < size; i++)
    {
        int delta = (line[i].flags >> Line::s_horz & Line::f_mask) - 1;
        if(line[i].x_max <= x)
        {
            winding += delta;  if(line[i].x_min < x)continue;
            if(--size == i)break;  line[i--] = line[size];
        }
        else if(line[i].x_min < x)
        {
            if(line[i].ur_dl())winding += delta;  last++;
        }
    }

    line.resize(size + last);  last = size;
    for(size_t i = offs; i < size; i++)
    {
        if(line[i].x_max <= x)continue;
        if(line[i].x_min < x)
        {
            line[i].split_horz(x, line[last++]);  continue;
        }
        for(line[i].move_x(x);; line[size].move_x(x))
        {
            if(--size == i)return size;
            if(line[size].x_max <= x || line[size].x_min < x)break;
        }
        swap(line[i--], line[size]);
    }
    return size;
}

size_t Polyline::split_vert(size_t offs, int &winding, int32_t y)
{
    size_t size = line.size(), last = 0;
    for(size_t i = offs; i < size; i++)
    {
        int delta = (line[i].flags >> Line::s_vert & Line::f_mask) - 1;
        if(line[i].y_max <= y)
        {
            winding += delta;  if(line[i].y_min < y)continue;
            if(--size == i)break;  line[i--] = line[size];
        }
        else if(line[i].y_min < y)
        {
            if(line[i].ur_dl())winding += delta;  last++;
        }
    }

    line.resize(size + last);  last = size;
    for(size_t i = offs; i < size; i++)
    {
        if(line[i].y_max <= y)continue;
        if(line[i].y_min < y)
        {
            line[i].split_vert(y, line[last++]);  continue;
        }
        for(line[i].move_y(y);; line[size].move_y(y))
        {
            if(--size == i)return size;
            if(line[size].y_max <= y || line[size].y_min < y)break;
        }
        swap(line[i--], line[size]);
    }
    return size;
}

void Polyline::rasterize(const Point &orig, int x_ord, int y_ord, size_t offs, int winding)
{
    assert(x_ord >= pixel_order && y_ord >= pixel_order);
    assert(!(orig.x & ((int32_t(1) << x_ord) - 1)) && !(orig.y & ((int32_t(1) << y_ord) - 1)));

    if(line.size() == offs)
    {
        fill_solid(orig, x_ord, y_ord, winding & winding_mask ? 255 : 0);  return;
    }
    if(line.size() == offs + 1)
    {
        int flag = 0;
        if(line[offs].c < 0)winding++;
        if(winding & winding_mask)flag |= 1;
        if((winding - 1) & winding_mask)flag |= 2;
        switch(flag)
        {
        case 0:  fill_solid(orig, x_ord, y_ord, 0);  break;
        case 1:  fill_halfplane(orig, x_ord, y_ord, line[offs].a, line[offs].b, line[offs].c);  break;
        case 2:  fill_halfplane(orig, x_ord, y_ord, -line[offs].a, -line[offs].b, -line[offs].c);  break;
        default:  fill_solid(orig, x_ord, y_ord, 255);  break;
        }
        line.pop_back();  return;
    }
    if(x_ord == pixel_order && y_ord == pixel_order)
    {
        bitmap[(orig.x >> pixel_order) + (orig.x >> pixel_order) * stride] = calc_pixel(offs, winding);
        line.resize(offs);  return;
    }

    int winding1 = winding;
    size_t offs1;  Point orig1 = orig;
    if(x_ord > y_ord)
    {
        int32_t x = int32_t(1) << --x_ord;  orig1.x += x;
        offs1 = split_horz(offs, winding1, x);
    }
    else
    {
        int32_t y = int32_t(1) << --y_ord;  orig1.y += y;
        offs1 = split_vert(offs, winding1, y);
    }
    rasterize(orig1, x_ord, y_ord, offs1, winding1);  assert(line.size() == offs1);
    rasterize(orig, x_ord, y_ord, offs, winding);
}

void Polyline::rasterize(int x0, int y0, int width, int height)
{
    Point orig(x0, y0);  orig <<= pixel_order;
    width <<= pixel_order;  height <<= pixel_order;
    assert(orig.x <= x_min && orig.x + width >= x_max);
    assert(orig.y <= y_min && orig.y + height >= y_max);

    assert(line.size());
    for(size_t i = 0; i < line.size(); i++)
    {
        line[i].x_min -= orig.x;  line[i].x_max -= orig.x;
        line[i].y_min -= orig.y;  line[i].y_max -= orig.y;
        line[i].c += line[i].a * orig.x + line[i].b * orig.y;
    }

    int x_ord = pixel_order, y_ord = pixel_order;
    while(int32_t(1) << x_ord < width)x_ord++;
    while(int32_t(1) << y_ord < height)y_ord++;

    stride = size_t(1) << (max(int(tile_order), x_ord) - pixel_order);
    bitmap.resize(stride << (max(int(tile_order), y_ord) - pixel_order));
    rasterize(orig, x_ord, y_ord, 0, 0);
}


inline void print_cell(uint8_t val)
{
    static const uint8_t palette[] =
        {0, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 15};

    cout << "\x1B[48;5;" << int(palette[(val * (sizeof(palette) - 1) + 127) / 255]) << 'm';
    cout << "  ";  return;

    static const char hex[] = "0123456789ABCDEF";
    cout << hex[(val >> 4) & 15] << hex[val & 15];
}

void Polyline::print()
{
    for(size_t pos = bitmap.size() - stride; pos != -stride; pos -= stride)
    {
        for(size_t i = 0; i < stride; i++)print_cell(bitmap[pos + i]);
        cout << "\x1B[0m\n";
    }
    cout.flush();
}

void Polyline::test()
{
    static Point pt[] =
    {
        Point(0, 0), Point(4096, 999), Point(3072, 2048), Point(3072, 3072),
        Point(2000, 4077), Point(512, 2048), Point(1536, 2048)
    };
    static const size_t n = sizeof(pt) / sizeof(pt[0]);

    winding_mask = -1;  line.clear();
    x_min = y_min = numeric_limits<int32_t>::max();
    x_max = y_max = numeric_limits<int32_t>::min();
    for(size_t i = 1; i < n; i++)add_line(pt[i - 1], pt[i]);
    add_line(pt[n - 1], pt[0]);

    rasterize(0, 0, 64, 64);  print();
}
