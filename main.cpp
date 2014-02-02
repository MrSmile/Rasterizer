// main.cpp -- entry point
//


#include "raster.h"
#include <iostream>
#include FT_OUTLINE_H

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

    cout << "-----------------------------------------" << endl;

    Polyline poly;
    poly.create(face->glyph->outline);
    poly.rasterize(0, 0, 64, 64);  poly.print();
    const uint8_t *cmp = poly.image();

    cout << "-----------------------------------------" << endl;

    FT_Bitmap bm;
    bm.rows = poly.height();  bm.width = bm.pitch = poly.width();
    vector<uint8_t> image(bm.rows * bm.pitch);  bm.buffer = image.data();
    bm.num_grays = 256;  bm.pixel_mode = FT_PIXEL_MODE_GRAY;
    FT_Outline_Get_Bitmap(lib, &face->glyph->outline, &bm);

    print_bitmap(bm.buffer, bm.width, bm.rows, bm.pitch);

    cout << "-----------------------------------------" << endl;

    for(int k = 0; k < bm.rows * bm.pitch; k++)
        bm.buffer[k] = 8 * abs(bm.buffer[k] - cmp[k]);

    print_bitmap(bm.buffer, bm.width, bm.rows, bm.pitch);

    cout << "-----------------------------------------" << endl;


    FT_Done_Face(face);  FT_Done_FreeType(lib);  return 0;
}
