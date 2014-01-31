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
        cerr << "FreeType Init failed!" << endl;  return -1;
    }

    FT_Done_FreeType(lib);  return 0;
}
