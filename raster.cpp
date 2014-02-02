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
    Point r = pt1 - pt0;  flags = f_exact_x | f_exact_y;
    if(r.x < 0)flags ^= f_ur_dl;  if(r.y >= 0)flags ^= f_up | f_ur_dl;
    x_min = min(pt0.x, pt1.x);  x_max = max(pt0.x, pt1.x);
    y_min = min(pt0.y, pt1.y);  y_max = max(pt0.y, pt1.y);
    a = r.y;  b = -r.x;  c = a * pt0.x + b * pt0.y;
}

bool Polyline::add_line(const Point &pt0, const Point &pt1)
{
    if(pt0.x == pt1.x && pt0.y == pt1.y)return true;
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
    c = (c - ((a + b) << (pixel_order - 1))) >> (pixel_order - 1);
    int32_t aa = 2 * a, bb = 2 * b;  a = abs(a);  b = abs(b);
    uint32_t sum = a + b, diff = abs(a - b), base = 2 * (sum - diff);
    uint64_t div = 8 * a * b, offs = div << 7;

    Point r = orig >> pixel_order;
    x_ord -= pixel_order;  y_ord -= pixel_order;
    uint8_t *ptr = bitmap.data() + r.x + r.y * stride;
    for(int32_t j = 0; j < int32_t(1) << y_ord; j++, ptr += stride)
        for(int32_t i = 0; i < int32_t(1) << x_ord; i++)
        {
            int64_t val = c - aa * i - bb * j;
            if(abs(val) < sum)
            {
                int32_t d = max<int32_t>(0, int32_t(abs(val)) - diff);
                int64_t res = base * val + (val < 0 ? d * d : -d * d);
                ptr[i] = (255 * res + offs) / div;  // TODO: overflow
            }
            else ptr[i] = val < 0 ? 0 : 255;
        }
}

uint8_t Polyline::calc_pixel(size_t offs, int winding)
{
    static Point pt[] =
    {
        Point(0x08, 0x08), Point(0x18, 0x08), Point(0x28, 0x08), Point(0x38, 0x08),
        Point(0x08, 0x18), Point(0x18, 0x18), Point(0x28, 0x18), Point(0x38, 0x18),
        Point(0x08, 0x28), Point(0x18, 0x28), Point(0x28, 0x28), Point(0x38, 0x28),
        Point(0x08, 0x38), Point(0x18, 0x38), Point(0x28, 0x38), Point(0x38, 0x38),
    };
    static size_t n = sizeof(pt) / sizeof(pt[0]);

    int8_t wnd[n];
    for(size_t i = offs; i < line.size(); i++)
        winding -= line[i].is_split_x() && line[i].is_ur_dl() ? 1 - 2 * (line[i].flags & 1) : 0;
    for(size_t k = 0; k < n; k++)wnd[k] = winding;
    for(size_t i = offs; i < line.size(); i++)
    {
        int delta = 1 - 2 * (line[i].flags & 1), delta_lim[] = {0, 0};
        if(line[i].is_split_x())delta_lim[line[i].flags >> 1 & 1] = delta;

        for(size_t k = 0; k < n; k++)
            if(pt[k].y < line[i].y_min)wnd[k] += delta_lim[1];
            else if(pt[k].y < line[i].y_max)
            {
                uint8_t hit = uint64_t(line[i].c - line[i].a * pt[k].x - line[i].b * pt[k].y) >> 63;
                if(~(hit ^ line[i].flags) & 1)wnd[k] += delta;
            }
            else wnd[k] += delta_lim[0];
    }

    int res = 0;
    for(size_t k = 0; k < n; k++)if(wnd[k] & winding_mask)res++;
    return (255 * res + n / 2) / n;
}


void Polyline::Line::move_x(int32_t x)
{
    x_min -= x;  x_max -= x;  c -= a * x;
    if(is_split_x() && is_ur_dl())flags &= ~f_exact_y;
}

void Polyline::Line::move_y(int32_t y)
{
    y_min -= y;  y_max -= y;  c -= b * y;
    if(is_split_y() && is_ur_dl())flags &= ~f_exact_x;
}

static int32_t div_floor(int64_t a, int32_t b)
{
    return (a < 0) == (b < 0) ? a / b : -(abs(a) + abs(b) - 1) / abs(b);
}

void Polyline::Line::split_horz(int32_t x, Line &next)
{
    next.a = a;  next.b = b;  next.c = c - a * x;
    int32_t y = div_floor(next.c, b), y1 = y;  if(b * y != next.c)y1++;  // TODO: optimize out division
    assert(y <= next.c / double(b) && y1 >= next.c / double(b));

    next.x_min = 0;  next.x_max = x_max - x;  x_max = x;
    if(flags & f_ur_dl)
    {
        next.flags = flags & ~f_exact_y | f_exact_x;
        next.y_min = y;  next.y_max = y_max;  y_max = y1;
    }
    else
    {
        next.flags = flags | f_exact_x;  flags &= ~f_exact_y;
        next.y_min = y_min;  next.y_max = y1;  y_min = y;
    }
}

void Polyline::Line::split_vert(int32_t y, Line &next)
{
    next.a = a;  next.b = b;  next.c = c - b * y;
    int32_t x = div_floor(next.c, a), x1 = x;  if(a * x != next.c)x1++;  // TODO: optimize out division
    assert(x <= next.c / double(a) && x1 >= next.c / double(a));

    next.y_min = 0;  next.y_max = y_max - y;  y_max = y;
    if(flags & f_ur_dl)
    {
        next.flags = flags & ~f_exact_x | f_exact_y;
        next.x_min = x;  next.x_max = x_max;  x_max = x1;
    }
    else
    {
        next.flags = flags | f_exact_y;  flags &= ~f_exact_x;
        next.x_min = x_min;  next.x_max = x1;  x_min = x;
    }
}

size_t Polyline::split_horz(size_t offs, int &winding, int32_t x)
{
    size_t size = line.size(), last = 0;
    for(size_t i = offs; i < size; i++)
    {
        int delta = line[i].delta_horz();
        if(line[i].x_max <= x)
        {
            winding += delta;  if(line[i].x_min < x)continue;
            if(--size == i)break;  line[i--] = line[size];
        }
        else if(line[i].x_min < x)
        {
            if(line[i].is_ur_dl())winding += delta;  last++;
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
        int delta = line[i].delta_vert();
        if(line[i].y_max <= y)
        {
            winding += delta;  if(line[i].y_min < y)continue;
            if(--size == i)break;  line[i--] = line[size];
        }
        else if(line[i].y_min < y)
        {
            if(line[i].is_ur_dl())winding += delta;  last++;
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
    /*if(line.size() == offs + 1)
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
    }*/
    if(x_ord == pixel_order && y_ord == pixel_order)
    {
        bitmap[(orig.x >> pixel_order) + (orig.y >> pixel_order) * stride] = calc_pixel(offs, winding);
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

    assert(line.size());
    for(size_t i = 0; i < line.size(); i++)
    {
        line[i].x_min -= orig.x;  line[i].x_max -= orig.x;
        line[i].y_min -= orig.y;  line[i].y_max -= orig.y;
        line[i].c -= line[i].a * orig.x + line[i].b * orig.y;
    }
    x_min -= orig.x;  x_max -= orig.x;
    y_min -= orig.y;  y_max -= orig.y;

    int x_ord = pixel_order, y_ord = pixel_order;
    while(int32_t(1) << x_ord < width)x_ord++;
    while(int32_t(1) << y_ord < height)y_ord++;

    stride = size_t(1) << (x_ord - pixel_order);
    bitmap.resize(stride << (y_ord - pixel_order));

    int winding;  size_t offs = 0;
    if(x_max >= int32_t(1) << x_ord)
        line.resize(split_horz(0, winding = 0, int32_t(1) << x_ord));
    if(y_max >= int32_t(1) << y_ord)
        line.resize(split_vert(0, winding = 0, int32_t(1) << y_ord));
    if(x_min <= 0)offs = split_horz(offs, winding = 0, 0);
    if(y_min <= 0)offs = split_vert(offs, winding = 0, 0);
    rasterize(Point(0, 0), x_ord, y_ord, offs, winding);
}


inline void print_cell(uint8_t val)
{
    static const uint8_t palette[] =
        {232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 15};

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
