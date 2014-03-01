// raster.c : rasterizer implementation
//

#include "fill.h"
#include <stdlib.h>



static inline int ilog2(uint32_t n)
{
    return __builtin_clz(n) ^ 31;
}


static inline int check_capacity(struct Rasterizer *rst, int index, size_t delta)
{
    delta += rst->size[index];
    if(rst->capacity[index] >= delta)return 1;

    delta = delta > 64 ? delta : 64;
    size_t capacity = 2 * rst->capacity[index];
    capacity = capacity > delta ? capacity : delta;
    void *ptr = realloc(rst->linebuf[index], capacity * sizeof(struct Segment));
    if(!ptr)return 0;

    rst->linebuf[index] = (struct Segment *)ptr;
    rst->capacity[index] = capacity;
    return 1;
}

void rasterizer_init(struct Rasterizer *rst)
{
    rst->linebuf[0] = rst->linebuf[1] = NULL;
    rst->size[0] = rst->capacity[0] = 0;
    rst->size[1] = rst->capacity[1] = 0;
}

void rasterizer_done(struct Rasterizer *rst)
{
    free(rst->linebuf[0]);  free(rst->linebuf[1]);
}


typedef struct
{
    int32_t x, y;
} OutlinePoint;

typedef struct
{
    OutlinePoint r;
    int64_t r2, er;
} OutlineSegment;

const int outline_error = 16;

static inline void segment_init(OutlineSegment *seg, OutlinePoint beg, OutlinePoint end)
{
    int32_t x = end.x - beg.x, abs_x = x < 0 ? -x : x;
    int32_t y = end.y - beg.y, abs_y = y < 0 ? -y : y;

    seg->r.x = x;  seg->r.y = y;
    seg->r2 = x * (int64_t)x + y * (int64_t)y;
    seg->er = outline_error * (int64_t)(abs_x > abs_y ? abs_x : abs_y);
}

static inline int segment_subdivide(const OutlineSegment *seg, OutlinePoint beg, OutlinePoint pt)
{
    int32_t x = pt.x - beg.x;
    int32_t y = pt.y - beg.y;
    int64_t pdr = seg->r.x * (int64_t)x + seg->r.y * (int64_t)y;
    int64_t pcr = seg->r.x * (int64_t)y - seg->r.y * (int64_t)x;
    return pdr < -seg->er || pdr > seg->r2 + seg->er || (pcr < 0 ? -pcr : pcr) > seg->er;
}

static inline int add_line(struct Rasterizer *rst, OutlinePoint pt0, OutlinePoint pt1)
{
    int32_t x = pt1.x - pt0.x;
    int32_t y = pt1.y - pt0.y;
    if(!x && !y)return 1;

    if(!check_capacity(rst, 0, 1))return 0;
    struct Segment *line = rst->linebuf[0] + rst->size[0]++;
    line->flags = SEGFLAG_EXACT_LEFT | SEGFLAG_EXACT_RIGHT | SEGFLAG_EXACT_BOTTOM | SEGFLAG_EXACT_TOP;
    if(x < 0)line->flags ^= SEGFLAG_UR_DL;  if(y >= 0)line->flags ^= SEGFLAG_UP | SEGFLAG_UR_DL;
    line->x_min = (pt0.x < pt1.x ? pt0.x : pt1.x);  line->x_max = (pt0.x > pt1.x ? pt0.x : pt1.x);
    line->y_min = (pt0.y < pt1.y ? pt0.y : pt1.y);  line->y_max = (pt0.y > pt1.y ? pt0.y : pt1.y);
    line->a = y;  line->b = -x;  line->c = y * (int64_t)pt0.x - x * (int64_t)pt0.y;

    int32_t abs_x = x < 0 ? -x : x;
    int32_t abs_y = y < 0 ? -y : y;
    uint32_t max_ab = (abs_x > abs_y ? abs_x : abs_y);
    int shift = 30 - ilog2(max_ab);  max_ab <<= shift + 1;
    line->a <<= shift;  line->b <<= shift;  line->c <<= shift;
    line->scale = (uint64_t)0x53333333 * (uint32_t)(max_ab * (uint64_t)max_ab >> 32) >> 32;
    line->scale += 0x8810624D - (0xBBC6A7EF * (uint64_t)max_ab >> 32);
    //line->scale = ((uint64_t)1 << 61) / max_ab;
    return 1;
}

static int add_quadratic(struct Rasterizer *rst, OutlinePoint pt0, OutlinePoint pt1, OutlinePoint pt2)
{
    OutlineSegment seg;  segment_init(&seg, pt0, pt2);
    if(!segment_subdivide(&seg, pt0, pt1))return add_line(rst, pt0, pt2);

    OutlinePoint p01, p12, c;  // TODO: overflow?
    p01.x = pt0.x + pt1.x;  p12.x = pt1.x + pt2.x;
    p01.y = pt0.y + pt1.y;  p12.y = pt1.y + pt2.y;
    c.x = (p01.x + p12.x + 2) >> 2;  p01.x >>= 1;  p12.x >>= 1;
    c.y = (p01.y + p12.y + 2) >> 2;  p01.y >>= 1;  p12.y >>= 1;
    return add_quadratic(rst, pt0, p01, c) && add_quadratic(rst, c, p12, pt2);
}

static int add_cubic(struct Rasterizer *rst, OutlinePoint pt0, OutlinePoint pt1, OutlinePoint pt2, OutlinePoint pt3)
{
    OutlineSegment seg;  segment_init(&seg, pt0, pt3);
    if(!segment_subdivide(&seg, pt0, pt1) && !segment_subdivide(&seg, pt0, pt2))return add_line(rst, pt0, pt3);

    OutlinePoint p01, p12, p23, p012, p123, c;  // TODO: overflow?
    p01.x = pt0.x + pt1.x;  p12.x = pt1.x + pt2.x + 2;  p23.x = pt2.x + pt3.x;
    p01.y = pt0.y + pt1.y;  p12.y = pt1.y + pt2.y + 2;  p23.y = pt2.y + pt3.y;
    p012.x = p01.x + p12.x;  p123.x = p12.x + p23.x;  c.x = (p012.x + p123.x - 1) >> 3;
    p012.y = p01.y + p12.y;  p123.y = p12.y + p23.y;  c.y = (p012.y + p123.y - 1) >> 3;
    p01.x >>= 1;  p012.x >>= 2;  p123.x >>= 2;  p23.x >>= 1;
    p01.y >>= 1;  p012.y >>= 2;  p123.y >>= 2;  p23.y >>= 1;
    return add_cubic(rst, pt0, p01, p012, c) && add_cubic(rst, c, p123, p23, pt3);
}


int rasterizer_set_outline(struct Rasterizer *rst, const FT_Outline *path)
{
    enum Status
    {
        S_ON, S_Q, S_C1, S_C2
    };

    int i, j;
    rst->size[0] = 0;
    for(i = 0, j = 0; i < path->n_contours; i++)
    {
        OutlinePoint start, p[4];
        int process_end = 1;
        enum Status st;

        int last = path->contours[i];
        switch(FT_CURVE_TAG(path->tags[j]))
        {
        case FT_CURVE_TAG_ON:
            p[0].x = path->points[j].x;
            p[0].y = path->points[j].y;
            start = p[0];  st = S_ON;  break;

        case FT_CURVE_TAG_CONIC:
            switch(FT_CURVE_TAG(path->tags[last]))
            {
            case FT_CURVE_TAG_ON:
                p[0].x = path->points[last].x;
                p[0].y = path->points[last].y;
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                process_end = 0;  st = S_Q;  break;

            case FT_CURVE_TAG_CONIC:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                p[0].x = (p[1].x + path->points[last].x) >> 1;
                p[0].y = (p[1].y + path->points[last].y) >> 1;
                start = p[0];  st = S_Q;  break;

            default:
                return 0;
            }
            break;

        default:
            return 0;
        }

        for(j++; j <= last; j++)switch(FT_CURVE_TAG(path->tags[j]))
        {
        case FT_CURVE_TAG_ON:
            switch(st)
            {
            case S_ON:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                if(!add_line(rst, p[0], p[1]))return 0;
                p[0] = p[1];  break;

            case S_Q:
                p[2].x = path->points[j].x;
                p[2].y = path->points[j].y;
                if(!add_quadratic(rst, p[0], p[1], p[2]))return 0;
                p[0] = p[2];  st = S_ON;  break;

            case S_C2:
                p[3].x = path->points[j].x;
                p[3].y = path->points[j].y;
                if(!add_cubic(rst, p[0], p[1], p[2], p[3]))return 0;
                p[0] = p[3];  st = S_ON;  break;

            default:
                return 0;
            }
            break;

        case FT_CURVE_TAG_CONIC:
            switch(st)
            {
            case S_ON:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                st = S_Q;  break;

            case S_Q:
                p[3].x = path->points[j].x;
                p[3].y = path->points[j].y;
                p[2].x = (p[1].x + p[3].x) >> 1;
                p[2].y = (p[1].y + p[3].y) >> 1;
                if(!add_quadratic(rst, p[0], p[1], p[2]))return 0;
                p[0] = p[2];  p[1] = p[3];  break;

            default:
                return 0;
            }
            break;

        case FT_CURVE_TAG_CUBIC:
            switch(st)
            {
            case S_ON:
                p[1].x = path->points[j].x;
                p[1].y = path->points[j].y;
                st = S_C1;  break;

            case S_C1:
                p[2].x = path->points[j].x;
                p[2].y = path->points[j].y;
                st = S_C2;  break;

            default:
                return 0;
            }
            break;

        default:
            return 0;
        }

        if(process_end)switch(st)
        {
        case S_ON:
            if(!add_line(rst, p[0], start))return 0;  break;

        case S_Q:
            if(!add_quadratic(rst, p[0], p[1], start))return 0;  break;

        case S_C2:
            if(!add_cubic(rst, p[0], p[1], p[2], start))return 0;  break;

        default:
            return 0;
        }
    }

    size_t k;
    rst->x_min = rst->y_min = 0x7FFFFFFF;
    rst->x_max = rst->y_max = 0x80000000;
    for(k = 0; k < rst->size[0]; k++)
    {
        rst->x_min = rst->x_min < rst->linebuf[0][k].x_min ? rst->x_min : rst->linebuf[0][k].x_min;
        rst->x_max = rst->x_max > rst->linebuf[0][k].x_max ? rst->x_max : rst->linebuf[0][k].x_max;
        rst->y_min = rst->y_min < rst->linebuf[0][k].y_min ? rst->y_min : rst->linebuf[0][k].y_min;
        rst->y_max = rst->y_max > rst->linebuf[0][k].y_max ? rst->y_max : rst->linebuf[0][k].y_max;
    }
    return 1;
}
