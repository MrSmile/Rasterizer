
SOURCE = raster.cpp fill.cpp main.cpp
HEADER = point.h raster.h
FLAGS = -std=c++11 -I/usr/include/freetype2 -fno-exceptions -Wall -Wno-parentheses
LIBS = -lfreetype
PROGRAM = raster


debug: $(SOURCE) $(HEADER)
	g++ -g -O0 -DDEBUG $(FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

release: $(SOURCE) $(HEADER)
	g++ -g -Ofast -flto -fno-omit-frame-pointer -mtune=native -DNDEBUG $(FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

clean:
	rm $(PROGRAM)
