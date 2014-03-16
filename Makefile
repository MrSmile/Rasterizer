
SOURCE = raster.cpp fill.cpp main.cpp
HEADER = point.h raster.h
FLAGS = -I/usr/include/freetype2 -Wall -mpreferred-stack-boundary=5
#FLAGS += -m32 -msse2
CFLAGS = -std=c99 $(FLAGS)
CXXFLAGS = -std=c++11 -fno-exceptions -Wno-parentheses $(FLAGS)
DFLAGS = -g -O0 -DDEBUG
RFLAGS = -g -Ofast -flto -fno-omit-frame-pointer -mtune=native -DNDEBUG
LIBS = -lfreetype -lpnglite -lz
PROGRAM = raster

YASM = yasm -DARCH_X86_64=1 -m amd64 -f elf -DHAVE_ALIGNED_STACK=1
#YASM = yasm -DARCH_X86_64=0 -m x86 -f elf -DHAVE_ALIGNED_STACK=1


debug: $(SOURCE) $(HEADER)
	$(YASM) rasterizer.asm -o rasterizer.o
	gcc $(DFLAGS) $(CFLAGS) ass_rasterizer.c -c -o ass_rasterizer.o
	gcc $(DFLAGS) $(CFLAGS) ass_rasterizer_c.c -c -o ass_rasterizer_c.o
	gcc $(DFLAGS) $(CFLAGS) ass_rasterizer_sse2.c -c -o ass_rasterizer_sse2.o
	g++ $(DFLAGS) $(CXXFLAGS) $(SOURCE) ass_rasterizer.o ass_rasterizer_c.o ass_rasterizer_sse2.o rasterizer.o $(LIBS) -o $(PROGRAM)

release: $(SOURCE) $(HEADER)
	$(YASM) rasterizer.asm -o rasterizer.o
	gcc $(RFLAGS) $(CFLAGS) ass_rasterizer.c -c -o ass_rasterizer.o
	gcc $(RFLAGS) $(CFLAGS) ass_rasterizer_c.c -c -o ass_rasterizer_c.o
	gcc $(RFLAGS) $(CFLAGS) ass_rasterizer_sse2.c -c -o ass_rasterizer_sse2.o
	g++ $(RFLAGS) $(CXXFLAGS) $(SOURCE) ass_rasterizer.o ass_rasterizer_c.o ass_rasterizer_sse2.o rasterizer.o $(LIBS) -o $(PROGRAM)

clean:
	rm $(PROGRAM) *.o
