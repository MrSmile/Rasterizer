
SOURCE = raster.cpp fill.cpp main.cpp
HEADER = point.h raster.h
FLAGS = -I/usr/include/freetype2 -Wall
CFLAGS = -std=c99 $(FLAGS)
CXXFLAGS = -std=c++11 -fno-exceptions -Wno-parentheses $(FLAGS)
DFLAGS = -g -O0 -DDEBUG
RFLAGS = -g -Ofast -flto -fno-omit-frame-pointer -mtune=native -DNDEBUG
LIBS = -lfreetype -lpnglite -lz
PROGRAM = raster


debug: $(SOURCE) $(HEADER)
	gcc $(DFLAGS) $(CFLAGS) fill.c -c -o fill.o
	gcc $(DFLAGS) $(CFLAGS) raster.c -c -o raster.o
	g++ $(DFLAGS) $(CXXFLAGS) $(SOURCE) raster.o fill.o $(LIBS) -o $(PROGRAM)

release: $(SOURCE) $(HEADER)
	gcc $(RFLAGS) $(CFLAGS) fill.c -c -o fill.o
	gcc $(RFLAGS) $(CFLAGS) raster.c -c -o raster.o
	g++ $(RFLAGS) $(CXXFLAGS) $(SOURCE) raster.o fill.o $(LIBS) -o $(PROGRAM)

clean:
	rm $(PROGRAM) fill.o raster.o
