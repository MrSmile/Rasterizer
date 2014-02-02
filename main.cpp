// main.cpp -- entry point
//


#include "raster.h"
#include <iostream>

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

    print_outline(face->glyph->outline);

    Polyline poly;
    if(poly.create(face->glyph->outline))
        cout << "-------------------------------\nSUCCESS" << endl;
    else cout << "-------------------------------\nFAIL" << endl;

    poly.rasterize(0, 0, 64, 64);  poly.print();

    FT_Done_Face(face);  FT_Done_FreeType(lib);  return 0;
}
