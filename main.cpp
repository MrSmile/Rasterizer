// main.cpp -- entry point
//


#include "raster.h"
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

void compare_results(FT_Library lib, FT_Outline &outline, int width, int height)
{
    cout << "-----------------------------------------" << endl;

    Polyline poly;  poly.create(outline);
    poly.rasterize(0, 0, width, height);  poly.print();
    const uint8_t *cmp = poly.image();

    cout << "-----------------------------------------" << endl;

    FT_Bitmap bm;  bm.rows = poly.height();  bm.width = bm.pitch = poly.width();
    vector<uint8_t> image(bm.rows * bm.pitch);  bm.buffer = image.data();
    bm.num_grays = 256;  bm.pixel_mode = FT_PIXEL_MODE_GRAY;
    FT_Outline_Get_Bitmap(lib, &outline, &bm);

    print_bitmap(bm.buffer, bm.width, bm.rows, bm.pitch);

    cout << "-----------------------------------------" << endl;

    for(int k = 0; k < bm.rows * bm.pitch; k++)
        bm.buffer[k] = 8 * abs(bm.buffer[k] - cmp[k]);

    print_bitmap(bm.buffer, bm.width, bm.rows, bm.pitch);

    cout << "-----------------------------------------" << endl;
}

void benchmark(FT_Library lib, FT_Outline *outline, size_t n_outlines, int width, int height, int repeat)
{
    clock_t tm0 = clock();

    Polyline poly;
    for(int k = 0; k < repeat; k++)for(size_t i = 0; i < n_outlines; i++)
    {
        poly.create(outline[i]);
        poly.rasterize(0, 0, width, height);
    }

    clock_t tm1 = clock();

    FT_Bitmap bm;  bm.rows = height;  bm.width = bm.pitch = width;
    vector<uint8_t> image(bm.rows * bm.pitch);  bm.buffer = image.data();
    bm.num_grays = 256;  bm.pixel_mode = FT_PIXEL_MODE_GRAY;

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

    if(FT_Set_Pixel_Sizes(face, 64, 0))
    {
        cerr << "Cannot set pixel size!" << endl;
        FT_Done_Face(face);  FT_Done_FreeType(lib);  return -1;
    }


    if(FT_Load_Char(face, 'B', FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) ||
        face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    {
        cerr << "Cannot load char!" << endl;
        FT_Done_Face(face);  FT_Done_FreeType(lib);  return -1;
    }
    compare_results(lib, face->glyph->outline, 64, 64);


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

    benchmark(lib, outline, n, 64, 64, 999);

    for(int i = 0; i < n; i++)FT_Outline_Done(lib, &outline[i]);


    FT_Done_Face(face);  FT_Done_FreeType(lib);  return 0;
}
