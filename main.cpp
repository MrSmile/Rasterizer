// main.cpp -- entry point
//


#include <ft2build.h>
#include FT_FREETYPE_H
#include <iostream>

using namespace std;


int main()
{
    FT_Library lib;
    if(FT_Init_FreeType(&lib))
    {
        cerr << "FreeType init failed!" << endl;  return -1;
    }

    FT_Face face;
    if(FT_New_Face(lib, "test.ttf", 0, &face))
    {
        cerr << "Cannot load font face!" << endl;
        FT_Done_FreeType(lib);  return -1;
    }

    if(FT_Set_Pixel_Sizes(face, 64, 0))
    {
        cerr << "Cannot set pixel size!" << endl;
        FT_Done_Face(face);  FT_Done_FreeType(lib);  return -1;
    }

    if(FT_Load_Char(face, 'A', FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) ||
        face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    {
        cerr << "Cannot load char!" << endl;
        FT_Done_Face(face);  FT_Done_FreeType(lib);  return -1;
    }

    cout << "Outline info: n_contours = " << face->glyph->outline.n_contours <<
        ", n_points = " << face->glyph->outline.n_points << endl;

    FT_Done_Face(face);  FT_Done_FreeType(lib);  return 0;
}
