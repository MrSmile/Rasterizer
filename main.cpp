// main.cpp -- entry point
//


#include "raster.h"
#include <pnglite.h>
#include <iostream>
#include FT_OUTLINE_H
#include <ctime>

using namespace std;



void print_outline(const FT_Outline &path)
{
    cout << "Outline info: n_contours = " << path.n_contours << ", n_points = " << path.n_points << endl;
    cout << "--------------------------" << endl;
    for(int i = 0, j = 0; i < path.n_points; i++)
    {
        cout << path.points[i].x << ' ' << path.points[i].y;
        switch(FT_CURVE_TAG(path.tags[i]))
        {
        case FT_CURVE_TAG_ON:  break;
        case FT_CURVE_TAG_CONIC:  cout << " quadratic";  break;
        case FT_CURVE_TAG_CUBIC:  cout << " cubic";  break;
        default:  cout << " unknown";
        }
        cout << endl;  if(i != path.contours[j])continue;
        cout << "--------------------------" << endl;  j++;
    }
    cout << endl;
}

bool write_image(const char *file, const uint8_t *img, unsigned width, unsigned height)
{
    png_t png;
    if(png_open_file_write(&png, file))return false;
    bool res = !png_set_data(&png, width, height, 8, PNG_GREYSCALE, const_cast<unsigned char *>(img));
    return !png_close_file(&png) && res;
}

void compare_results(FT_Library lib, FT_Outline *outline, size_t n_outlines, int width, int height)
{
    /*
    Rasterizer rst;
    rasterizer_init(&rst);
    for(size_t i = 0; i < n_outlines; i++)
    {
        rasterizer_set_outline(&rst, &outline[i]);
    }
    rasterizer_done(&rst);
    */

    ptrdiff_t stride = width * n_outlines;
    vector<uint8_t> image(3 * height * stride);

    Polyline poly;  uint8_t *buf = image.data();
    for(size_t i = 0; i < n_outlines; i++, buf += width)
    {
        poly.create(outline[i]);
        poly.rasterize(buf, 0, 0, width, height, stride);
    }

    FT_Bitmap bm;  bm.rows = height;  bm.width = width;
    bm.pitch = stride;  bm.buffer = image.data() + height * stride;
    bm.num_grays = 256;  bm.pixel_mode = FT_PIXEL_MODE_GRAY;
    for(size_t i = 0; i < n_outlines; i++, bm.buffer += width)
        FT_Outline_Get_Bitmap(lib, &outline[i], &bm);

    uint8_t *src1 = image.data() + 0 * height * stride;
    uint8_t *src2 = image.data() + 1 * height * stride;
    uint8_t *dst  = image.data() + 2 * height * stride;
    for(int i = 0; i < height * stride; i++)
        dst[i] = max(0, min(255, 4 * (src1[i] - src2[i]) + 127));

    write_image("output.png", image.data(), stride, 3 * height);
}

void benchmark(FT_Library lib, FT_Outline *outline, size_t n_outlines, int width, int height, int repeat)
{
    vector<uint8_t> image(width * height);

    clock_t tm0 = clock();

    Polyline poly;
    for(int k = 0; k < repeat; k++)for(size_t i = 0; i < n_outlines; i++)
    {
        poly.create(outline[i]);
        poly.rasterize(image.data(), 0, 0, width, height, width);
    }

    clock_t tm1 = clock();

    FT_Bitmap bm;  bm.rows = height;  bm.width = bm.pitch = width;
    bm.buffer = image.data();  bm.num_grays = 256;  bm.pixel_mode = FT_PIXEL_MODE_GRAY;

    for(int k = 0; k < repeat; k++)for(size_t i = 0; i < n_outlines; i++)
        FT_Outline_Get_Bitmap(lib, &outline[i], &bm);

    clock_t tm2 = clock();

    cout << "Benchmark (" << n_outlines << " outlines, " << repeat << " cycles) result:" << endl;
    cout << "QuadTree: " << double(tm1 - tm0) / CLOCKS_PER_SEC << endl;
    cout << "FreeType: " << double(tm2 - tm1) / CLOCKS_PER_SEC << endl;
}

int main()
{
    //Polyline().test();  return 0;

    if(png_init(0, 0))
    {
        cerr << "pnglite init failed!" << endl;  return -1;
    }

    FT_Library lib;
    if(FT_Init_FreeType(&lib))
    {
        cerr << "FreeType init failed!" << endl;  return -1;
    }

    FT_Face face;
    if(FT_New_Face(lib, "test.ttf", 0, &face))
    //if(FT_New_Face(lib, "test.pfb", 0, &face))
    {
        cerr << "Cannot load font face!" << endl;
        FT_Done_FreeType(lib);  return -1;
    }

    const int size = 64;
    if(FT_Set_Pixel_Sizes(face, size, 0))
    {
        cerr << "Cannot set pixel size!" << endl;
        FT_Done_Face(face);  FT_Done_FreeType(lib);  return -1;
    }

    const int n = 26;
    FT_Outline outline[n];
    for(int i = 0; i < n; i++)
        if(FT_Load_Char(face, 'A' + i, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) ||
            face->glyph->format != FT_GLYPH_FORMAT_OUTLINE ||
            FT_Outline_New(lib, face->glyph->outline.n_points, face->glyph->outline.n_contours, &outline[i]) ||
            FT_Outline_Copy(&face->glyph->outline, &outline[i]))
        {
            cerr << "Cannot load char!" << endl;  exit(-1);
        }

    compare_results(lib, outline, n, size, size);
    benchmark(lib, outline, n, size, size, 9999);

    for(int i = 0; i < n; i++)FT_Outline_Done(lib, &outline[i]);
    FT_Done_Face(face);  FT_Done_FreeType(lib);  return 0;
}
