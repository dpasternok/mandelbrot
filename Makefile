# Makefile for mandelbrot

CC = gcc
CFLAGS = -Wall -O3 -march=native -ffast-math -mavx512f -mavx512dq -mavx2 -mfma
LDFLAGS = -lm -lpthread
SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LDFLAGS = $(shell sdl2-config --libs) -lpthread -lm
GL_LDFLAGS = -lGL -lGLEW $(SDL_LDFLAGS)

# Main target
all: mandel mandel2 mandel3 mandel4

# mandel - original version (requires mandel.sh to run)
mandel: mandel.c
	$(CC) $(CFLAGS) -o mandel mandel.c $(LDFLAGS)

# mandel2 - optimized terminal version with multithreading
mandel2: mandel2.c
	$(CC) $(CFLAGS) -o mandel2 mandel2.c $(LDFLAGS)

# mandel3 - GUI version with SDL2 + AVX-512
mandel3: mandel3.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o mandel3 mandel3.c $(SDL_LDFLAGS)

# mandel4 - GPU version with OpenGL compute shaders
mandel4: mandel4.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o mandel4 mandel4.c $(GL_LDFLAGS)

# Debug versions (no optimizations, with debug symbols)
debug: CFLAGS = -Wall -g -O0
debug: mandel

debug2: CFLAGS = -Wall -g -O0
debug2: mandel2

debug3: CFLAGS = -Wall -g -O0
debug3: mandel3

debug4: CFLAGS = -Wall -g -O0
debug4: mandel4

# Clean compiled files
clean:
	rm -f mandel mandel2 mandel3 mandel4

# Clean and rebuild from scratch
rebuild: clean all

# Run original version (bash script wrapper)
run1: mandel
	./mandel.sh

# Run mandel2 (terminal version)
run2: mandel2
	./mandel2

# Run mandel3 (CPU AVX-512 version)
run3: mandel3
	./mandel3

# Run mandel4 (GPU version)
run4: mandel4
	./mandel4

# Default run target (GPU version)
run: run4

# Help
help:
	@echo "Available targets:"
	@echo "  make          - compile all versions"
	@echo "  make mandel   - compile original renderer (use with ./mandel.sh)"
	@echo "  make mandel2  - compile terminal version (standalone)"
	@echo "  make mandel3  - compile CPU AVX-512 version (SDL2)"
	@echo "  make mandel4  - compile GPU OpenGL version (SDL2+OpenGL)"
	@echo "  make debug/2/3/4 - compile with debug symbols"
	@echo "  make clean    - remove compiled files"
	@echo "  make rebuild  - clean and compile from scratch"
	@echo ""
	@echo "Run targets:"
	@echo "  make run      - run mandel4 (GPU, default)"
	@echo "  make run4     - run mandel4 (GPU)"
	@echo "  make run3     - run mandel3 (CPU AVX-512)"
	@echo "  make run2     - run mandel2 (terminal standalone)"
	@echo "  make run1     - run mandel.sh (original bash version)"
	@echo ""
	@echo "  make help     - display this help"

.PHONY: all debug debug2 debug3 debug4 clean rebuild run run1 run2 run3 run4 help
