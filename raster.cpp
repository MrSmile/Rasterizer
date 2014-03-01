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
    Point r = pt1 - pt0;
    flags = SEGFLAG_EXACT_LEFT | SEGFLAG_EXACT_RIGHT | SEGFLAG_EXACT_BOTTOM | SEGFLAG_EXACT_TOP;
    if(r.x < 0)flags ^= SEGFLAG_UR_DL;  if(r.y >= 0)flags ^= SEGFLAG_UP | SEGFLAG_UR_DL;
    x_min = min(pt0.x, pt1.x);  x_max = max(pt0.x, pt1.x);
    y_min = min(pt0.y, pt1.y);  y_max = max(pt0.y, pt1.y);
    a = r.y;  b = -r.x;  c = a * int64_t(pt0.x) + b * int64_t(pt0.y);

    uint32_t max_ab = max(absval(a), absval(b));
    int shift = 30 - ilog2(max_ab);  max_ab <<= shift + 1;
    scale = uint64_t(0x53333333) * uint32_t(max_ab * uint64_t(max_ab) >> 32) >> 32;
    scale += 0x8810624D - (uint64_t(0xBBC6A7EF) * max_ab >> 32);
    //scale = (uint64_t(1) << 61) / max_ab;
    a <<= shift;  b <<= shift;  c <<= shift;
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
    for(size_t k = 0; k < n; k++)if(wnd[k])res++;
    return (255 * res + n / 2) / n;
}


void Polyline::Line::move_x(int32_t x)
{
    x_min = max<int32_t>(0, x_min - x);  x_max -= x;  c -= a * int64_t(x);
    if(is_split_x() && is_ur_dl())flags &= ~SEGFLAG_EXACT_BOTTOM;
}

void Polyline::Line::move_y(int32_t y)
{
    y_min = max<int32_t>(0, y_min - y);  y_max -= y;  c -= b * int64_t(y);
    if(is_split_y() && is_ur_dl())flags &= ~SEGFLAG_EXACT_LEFT;
}

void Polyline::Line::split_horz(int32_t x, Line &next)
{
    assert(x > x_min && x < x_max);
    next = *this;  next.c -= a * int64_t(x);
    next.x_min = 0;  next.x_max -= x;  x_max = x;

    flags &= ~SEGFLAG_EXACT_BOTTOM;  next.flags &= ~SEGFLAG_EXACT_TOP;
    if(flags & SEGFLAG_UR_DL)swap(flags, next.flags);
    flags |= SEGFLAG_EXACT_RIGHT;  next.flags |= SEGFLAG_EXACT_LEFT;
}

void Polyline::Line::split_vert(int32_t y, Line &next)
{
    assert(y > y_min && y < y_max);
    next = *this;  next.c -= b * int64_t(y);
    next.y_min = 0;  next.y_max -= y;  y_max = y;

    flags &= ~SEGFLAG_EXACT_LEFT;  next.flags &= ~SEGFLAG_EXACT_RIGHT;
    if(flags & SEGFLAG_UR_DL)swap(flags, next.flags);
    flags |= SEGFLAG_EXACT_TOP;  next.flags |= SEGFLAG_EXACT_BOTTOM;
}

int Polyline::split_horz(const vector<Line> &src, size_t offs, vector<Line> &dst0, vector<Line> &dst1, int32_t x)
{
    int winding = 0;
    for(size_t i = offs; i < src.size(); i++)
    {
        int delta = src[i].delta_horz();
        if(src[i].check_r(x))
        {
            winding += delta;  if(src[i].x_min >= x)continue;  dst0.push_back(src[i]);
            dst0.rbegin()->x_max = min(x, dst0.rbegin()->x_max);  continue;
        }
        if(src[i].check_l(x))
        {
            dst1.push_back(src[i]);  dst1.rbegin()->move_x(x);  continue;
        }
        if(src[i].is_ur_dl())winding += delta;
        dst0.push_back(src[i]);  dst1.push_back(Line());
        dst0.rbegin()->split_horz(x, *dst1.rbegin());
    }
    return winding;
}

int Polyline::split_vert(const vector<Line> &src, size_t offs, vector<Line> &dst0, vector<Line> &dst1, int32_t y)
{
    int winding = 0;
    for(size_t i = offs; i < src.size(); i++)
    {
        int delta = src[i].delta_vert();
        if(src[i].check_u(y))
        {
            winding += delta;  if(src[i].y_min >= y)continue;  dst0.push_back(src[i]);
            dst0.rbegin()->y_max = min(y, dst0.rbegin()->y_max);  continue;
        }
        if(src[i].check_d(y))
        {
            dst1.push_back(src[i]);  dst1.rbegin()->move_y(y);  continue;
        }
        if(src[i].is_ur_dl())winding += delta;
        dst0.push_back(src[i]);  dst1.push_back(Line());
        dst0.rbegin()->split_vert(y, *dst1.rbegin());
    }
    return winding;
}

void Polyline::rasterize(uint8_t *buf, int width, int height, ptrdiff_t stride, int index, size_t offs, int winding)
{
    assert(width > 0 && !(width & tile_mask) && height > 0 && !(height & tile_mask) && unsigned(index) < 3);

    vector<Line> &line = linebuf[index];
    if(line.size() == offs)
    {
        fill_solid(buf, width, height, stride, winding);  return;
    }
    if(line.size() == offs + 1)
    {
        int flag = 0;  if(line[offs].c < 0)winding++;
        if(winding)flag ^= 1;  if(winding - 1)flag ^= 3;
        if(flag & 1)fill_halfplane(buf, width, height, stride,
            line[offs].a, line[offs].b, line[offs].c, flag & 2 ? -line[offs].scale : line[offs].scale);
        else fill_solid(buf, width, height, stride, flag & 2);
        line.pop_back();  return;
    }
    if(width == tile_mask + 1 && height == tile_mask + 1)
    {
        fill_generic(buf, width, height, stride, line.data() + offs, line.size() - offs, winding);
        line.resize(offs);  return;
    }

    int dst0 = index == 2 ? 0 : index + 1;
    int dst1 = index == 0 ? 2 : index - 1;
    size_t offs0 = linebuf[dst0].size(), offs1 = linebuf[dst1].size();
    linebuf[dst0].reserve(offs0 + line.size() - offs);
    linebuf[dst1].reserve(offs1 + line.size() - offs);
    int winding1 = winding;  uint8_t *buf1 = buf;
    int width1 = width, height1 = height;
    if(width > height)
    {
        width = 1 << ilog2(unsigned(width - 1));  width1 -= width;  buf1 += width;
        winding1 += split_horz(line, offs, linebuf[dst0], linebuf[dst1], int32_t(width) << pixel_order);
    }
    else
    {
        height = 1 << ilog2(unsigned(height - 1));  height1 -= height;  buf1 += height * stride;
        winding1 += split_vert(line, offs, linebuf[dst0], linebuf[dst1], int32_t(height) << pixel_order);
    }
    line.resize(offs);

    rasterize(buf,  width,  height,  stride, dst0, offs0, winding);   assert(linebuf[dst0].size() == offs0);
    rasterize(buf1, width1, height1, stride, dst1, offs1, winding1);  assert(linebuf[dst1].size() == offs1);
}

void Polyline::rasterize(uint8_t *buf, int x0, int y0, int width, int height, ptrdiff_t stride, bool vert_flip)
{
    assert(width && !(width & tile_mask) && height && !(height & tile_mask));
    x0 <<= pixel_order;  y0 <<= pixel_order;

    if(vert_flip)
    {
        buf += (height - 1) * stride;  stride = -stride;
    }

    vector<Line> &line = linebuf[0];  assert(line.size());
    for(size_t i = 0; i < line.size(); i++)
    {
        line[i].x_min -= x0;  line[i].x_max -= x0;
        line[i].y_min -= y0;  line[i].y_max -= y0;
        line[i].c -= line[i].a * int64_t(x0) + line[i].b * int64_t(y0);
    }
    x_min -= x0;  x_max -= x0;
    y_min -= y0;  y_max -= y0;

    int src = 0, dst0 = 1, dst1 = 2, winding = 0;
    assert(!linebuf[dst0].size() && !linebuf[dst1].size());
    int32_t size_x = int32_t(width) << pixel_order;
    int32_t size_y = int32_t(height) << pixel_order;
    if(x_max >= size_x)
    {
        split_horz(linebuf[src], 0, linebuf[dst0], linebuf[dst1], size_x);
        linebuf[src].clear();  linebuf[dst1].clear();  swap(src, dst0);
    }
    if(y_max >= size_y)
    {
        split_vert(linebuf[src], 0, linebuf[dst0], linebuf[dst1], size_y);
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
    rasterize(buf, width, height, stride, src, 0, winding);
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

    linebuf[0].clear();
    x_min = y_min = numeric_limits<int32_t>::max();
    x_max = y_max = numeric_limits<int32_t>::min();
    for(size_t i = 1; i < n; i++)add_line(pt[i - 1], pt[i]);
    add_line(pt[n - 1], pt[0]);

    const int w = 64, h = 64;
    uint8_t bitmap[w * h] alignas(16);
    rasterize(bitmap, 0, 0, w, h, w);
    print_bitmap(bitmap, w, h, w);
}
