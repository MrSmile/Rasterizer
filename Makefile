
SOURCE = raster.cpp fill.cpp main.cpp
HEADER = point.h raster.h
FLAGS = -std=c++11 -I/usr/include/freetype2 -fno-exceptions -Wall -Wno-parentheses
LIBS = -lfreetype -lpnglite -lz
PROGRAM = raster


debug: $(SOURCE) $(HEADER)
	gcc -g -O0 -DDEBUG -Wall fill.c -c -o fill.o
	g++ -g -O0 -DDEBUG $(FLAGS) $(SOURCE) fill.o $(LIBS) -o $(PROGRAM)

release: $(SOURCE) $(HEADER)
	gcc -g -Ofast -flto -fno-omit-frame-pointer -mtune=native -DNDEBUG -Wall fill.c -c -o fill.o
	g++ -g -Ofast -flto -fno-omit-frame-pointer -mtune=native -DNDEBUG $(FLAGS) $(SOURCE) fill.o $(LIBS) -o $(PROGRAM)

clean:
	rm $(PROGRAM) fill.o
