// raster.cpp : rasterizer implementation
//

#include "raster.h"
#include <iostream>
#include <cassert>
#include <limits>

using namespace std;



Polyline::Segment::Segment(const Point &r_) : r(r_), r2(r_ * r_), er(int64_t(err) * max(absval(r_.x), absval(r_.y)))
{
}

bool Polyline::Segment::subdivide(const Point &p)
{
    int64_t pdr = p * r, pcr = p % r;
    return pdr < -er || pdr > r2 + er || absval(pcr) > er;
}

Polyline::Line::Line(const Point &pt0, const Point &pt1)
{
    Point r = pt1 - pt0;  flags = f_exact_x | f_exact_y;
    if(r.x < 0)flags ^= f_ur_dl;  if(r.y >= 0)flags ^= f_up | f_ur_dl;
    x_min = min(pt0.x, pt1.x);  x_max = max(pt0.x, pt1.x);
    y_min = min(pt0.y, pt1.y);  y_max = max(pt0.y, pt1.y);
    a = r.y;  b = -r.x;  c = a * int64_t(pt0.x) + b * int64_t(pt0.y);

    uint32_t max_ab = max(absval(a), absval(b));
    order = ilog2(uint32_t(max_ab));  max_ab <<= 31 - order;
    scale = (uint64_t(1) << 61) / max_ab;
}

bool Polyline::add_line(const Point &pt0, const Point &pt1)
{
    if(pt0.x == pt1.x && pt0.y == pt1.y)return true;
    x_min = min(x_min, pt0.x);  x_max = max(x_max, pt0.x);
    y_min = min(y_min, pt0.y);  y_max = max(y_max, pt0.y);
    linebuf[0].emplace_back(pt0, pt1);  return true;
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
    linebuf[0].clear();

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


uint8_t Polyline::calc_pixel(std::vector<Line> &line, size_t offs, int winding)
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
                uint8_t hit = uint64_t(line[i].c - line[i].a * int64_t(pt[k].x) - line[i].b * int64_t(pt[k].y)) >> 63;
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
    x_min -= x;  x_max -= x;  c -= a * int64_t(x);
    if(is_split_x() && is_ur_dl())flags &= ~f_exact_y;
}

void Polyline::Line::move_y(int32_t y)
{
    y_min -= y;  y_max -= y;  c -= b * int64_t(y);
    if(is_split_y() && is_ur_dl())flags &= ~f_exact_x;
}

static int32_t div_floor(int64_t a, int32_t b)
{
    return (a < 0) == (b < 0) ? a / b : -(absval(a) + absval(b) - 1) / absval(b);
}

void Polyline::Line::split_horz(int32_t x, Line &next)
{
    assert(x > x_min && x < x_max);  next = *this;  next.c -= a * int64_t(x);
    int32_t y = div_floor(next.c, b), y1 = y;  if(b * int64_t(y) != next.c)y1++;  // TODO: optimize out division
    assert(y <= next.c / double(b) && y1 >= next.c / double(b));
    assert(y >= y_min && y1 <= y_max);

    next.x_min = 0;  next.x_max -= x;  x_max = x;  next.flags |= f_exact_x;
    if(flags & f_ur_dl)
    {
        next.y_min = y;  y_max = y1;  next.flags &= ~f_exact_y;
    }
    else
    {
        y_min = y;  next.y_max = y1;  flags &= ~f_exact_y;
    }
}

void Polyline::Line::split_vert(int32_t y, Line &next)
{
    assert(y > y_min && y < y_max);  next = *this;  next.c -= b * int64_t(y);
    int32_t x = div_floor(next.c, a), x1 = x;  if(a * int64_t(x) != next.c)x1++;  // TODO: optimize out division
    assert(x <= next.c / double(a) && x1 >= next.c / double(a));
    assert(x >= x_min && x1 <= x_max);

    next.y_min = 0;  next.y_max -= y;  y_max = y;  next.flags |= f_exact_y;
    if(flags & f_ur_dl)
    {
        next.x_min = x;  x_max = x1;  next.flags &= ~f_exact_x;
    }
    else
    {
        x_min = x;  next.x_max = x1;  flags &= ~f_exact_x;
    }
}

int Polyline::split_horz(const vector<Line> &src, size_t offs, vector<Line> &dst0, vector<Line> &dst1, int32_t x)
{
    int winding = 0;
    for(size_t i = offs; i < src.size(); i++)
    {
        int delta = src[i].delta_horz();
        if(src[i].x_max <= x)
        {
            winding += delta;  if(src[i].x_min < x)dst0.push_back(src[i]);
        }
        else if(src[i].x_min < x)
        {
            if(src[i].is_ur_dl())winding += delta;
            dst0.push_back(src[i]);  dst1.push_back(Line());
            dst0.rbegin()->split_horz(x, *dst1.rbegin());
        }
        else
        {
            dst1.push_back(src[i]);  dst1.rbegin()->move_x(x);
        }
    }
    return winding;
}

int Polyline::split_vert(const vector<Line> &src, size_t offs, vector<Line> &dst0, vector<Line> &dst1, int32_t y)
{
    int winding = 0;
    for(size_t i = offs; i < src.size(); i++)
    {
        int delta = src[i].delta_vert();
        if(src[i].y_max <= y)
        {
            winding += delta;  if(src[i].y_min < y)dst0.push_back(src[i]);
        }
        else if(src[i].y_min < y)
        {
            if(src[i].is_ur_dl())winding += delta;
            dst0.push_back(src[i]);  dst1.push_back(Line());
            dst0.rbegin()->split_vert(y, *dst1.rbegin());
        }
        else
        {
            dst1.push_back(src[i]);  dst1.rbegin()->move_y(y);
        }
    }
    return winding;
}

void Polyline::rasterize(const Point &orig, int x_ord, int y_ord, int index, size_t offs, int winding)
{
    assert(x_ord >= tile_order && y_ord >= tile_order && unsigned(index) < 3);
    assert(!(orig.x & ((int32_t(1) << x_ord) - 1)) && !(orig.y & ((int32_t(1) << y_ord) - 1)));

    vector<Line> &line = linebuf[index];
    if(line.size() == offs)
    {
        fill_solid(orig, x_ord, y_ord, winding & winding_mask);  return;
    }
    if(line.size() == offs + 1)
    {
        int flag = 0;
        if(line[offs].c < 0)winding++;
        if(winding & winding_mask)flag ^= 1;
        if((winding - 1) & winding_mask)flag ^= 3;
        if(flag & 1)
        {
            int32_t a = line[offs].a_norm(), b = line[offs].b_norm();
            int64_t c = line[offs].c_norm();
            if(flag & 2)
            {
                a = -a;  b = -b;  c = -c;
            }
            fill_halfplane(orig, x_ord, y_ord, a, b, c);
        }
        else fill_solid(orig, x_ord, y_ord, flag & 2);
        line.pop_back();  return;
    }
    if(x_ord == tile_order && y_ord == tile_order)
    {
        fill_generic(orig, x_ord, y_ord, index, offs, winding);  line.resize(offs);  return;
    }

    int dst0 = index == 2 ? 0 : index + 1;
    int dst1 = index == 0 ? 2 : index - 1;
    size_t offs0 = linebuf[dst0].size(), offs1 = linebuf[dst1].size();
    linebuf[dst0].reserve(offs0 + line.size() - offs);
    linebuf[dst1].reserve(offs1 + line.size() - offs);
    int winding1 = winding;  Point orig1 = orig;
    if(x_ord > y_ord)
    {
        int32_t x = int32_t(1) << --x_ord;  orig1.x += x;
        winding1 += split_horz(line, offs, linebuf[dst0], linebuf[dst1], x);
    }
    else
    {
        int32_t y = int32_t(1) << --y_ord;  orig1.y += y;
        winding1 += split_vert(line, offs, linebuf[dst0], linebuf[dst1], y);
    }
    line.resize(offs);

    rasterize(orig,  x_ord, y_ord, dst0, offs0, winding);   assert(linebuf[dst0].size() == offs0);
    rasterize(orig1, x_ord, y_ord, dst1, offs1, winding1);  assert(linebuf[dst1].size() == offs1);
}

void Polyline::rasterize(int x0, int y0, int width, int height)
{
    Point orig(x0, y0);  orig <<= pixel_order;
    width <<= pixel_order;  height <<= pixel_order;

    vector<Line> &line = linebuf[0];  assert(line.size());
    for(size_t i = 0; i < line.size(); i++)
    {
        line[i].x_min -= orig.x;  line[i].x_max -= orig.x;
        line[i].y_min -= orig.y;  line[i].y_max -= orig.y;
        line[i].c -= line[i].a * int64_t(orig.x) + line[i].b * int64_t(orig.y);
    }
    x_min -= orig.x;  x_max -= orig.x;
    y_min -= orig.y;  y_max -= orig.y;

    int x_ord = tile_order, y_ord = tile_order;
    while(int32_t(1) << x_ord < width)x_ord++;
    while(int32_t(1) << y_ord < height)y_ord++;

    stride = size_t(1) << (x_ord - pixel_order);
    size_y = size_t(1) << (y_ord - pixel_order);
    bitmap.resize(stride * size_y);

    int src = 0, dst0 = 1, dst1 = 2, winding = 0;
    assert(!linebuf[dst0].size() && !linebuf[dst1].size());
    if(x_max >= int32_t(1) << x_ord)
    {
        split_horz(linebuf[src], 0, linebuf[dst0], linebuf[dst1], int32_t(1) << x_ord);
        linebuf[src].clear();  linebuf[dst1].clear();  swap(src, dst0);
    }
    if(y_max >= int32_t(1) << y_ord)
    {
        split_vert(linebuf[src], 0, linebuf[dst0], linebuf[dst1], int32_t(1) << y_ord);
        linebuf[src].clear();  linebuf[dst1].clear();  swap(src, dst0);
    }
    if(x_min <= 0)
    {
        split_horz(linebuf[src], 0, linebuf[dst0], linebuf[dst1], 0);
        linebuf[src].clear();  linebuf[dst0].clear();  swap(src, dst1);
    }
    if(y_min <= 0)
    {
        winding = split_vert(linebuf[src], 0, linebuf[dst0], linebuf[dst1], 0);
        linebuf[src].clear();  linebuf[dst0].clear();  swap(src, dst1);
    }
    rasterize(Point(0, 0), x_ord, y_ord, src, 0, winding);
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

void print_bitmap(const uint8_t *image, size_t width, size_t height, ptrdiff_t stride)
{
    for(size_t i = 0; i < height; i++, image += stride)
    {
        for(size_t j = 0; j < width; j++)print_cell(image[j]);
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

    winding_mask = -1;  linebuf[0].clear();
    x_min = y_min = numeric_limits<int32_t>::max();
    x_max = y_max = numeric_limits<int32_t>::min();
    for(size_t i = 1; i < n; i++)add_line(pt[i - 1], pt[i]);
    add_line(pt[n - 1], pt[0]);

    rasterize(0, 0, 64, 64);  print();
}
