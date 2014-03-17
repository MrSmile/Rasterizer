/* debug test function */

#include "ass_rasterizer.c"


int rasterizer_test(ASS_Rasterizer *rst, uint8_t buf[64 * 64])  // DEBUG
{
    OutlinePoint pt[] =
    {
        {0, 0}, {4096, 999}, {3072, 2048}, {3072, 3072},
        {2000, 4077}, {512, 2048}, {1536, 2048}
    };
    static const size_t n = sizeof(pt) / sizeof(pt[0]);

    for(size_t i = 1; i < n; i++)if(!add_line(rst, pt[i - 1], pt[i]))return 0;
    if(!add_line(rst, pt[n - 1], pt[0]))return 0;

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

    memset(buf, 0, 64 * 64);
    return rasterizer_fill(rst, buf, 0, 0, 64, 64, 64, 1);
}
