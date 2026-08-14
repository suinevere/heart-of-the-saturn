all: alien

CC=gcc
SDL_CFLAGS = `sdl2-config --cflags`
SDL_LIBS = `sdl2-config --libs`
CFLAGS=-ggdb -DENABLE_DEBUG -O2 -Wall -I. -Ihost $(SDL_CFLAGS)
OBJS=\
	client.o common.o vm.o      sprites.o decode.o   animation.o   \
	rooms.o  video_sdl.o main.o debug.o    lzss.o                  \
	sound.o  screen.o scale2x.o scale3x.o game2bin.o discfmt.o     \
	disc_cue.o input_sdl.o platform_sdl.o fadecalc.o

LIBS=$(SDL_LIBS) -lSDL2_mixer

alien: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

# disc_cue.o lives under ../host/ -- the .c.o suffix rule above only looks in
# the current directory, so a plain OBJS entry for it would silently never
# compile. Explicit rule beats the implicit suffix rule for the same target.
# It sits outside src/ because saturn/makefile globs `find src/ -name '*.c'`
# for the SH-2 build, which would otherwise compile this SDL file for Saturn.
disc_cue.o: ../host/disc_cue.c
	$(CC) $(CFLAGS) -c ../host/disc_cue.c -o disc_cue.o

# video_sdl.o lives under ../host/ for the same reason disc_cue.o does -- the
# .c.o suffix rule above only looks in the current directory, and this SDL
# file must stay out of saturn/makefile's `find src/ -name '*.c'` glob for
# the SH-2 build.
video_sdl.o: ../host/video_sdl.c
	$(CC) $(CFLAGS) -c ../host/video_sdl.c -o video_sdl.o

# input_sdl.o lives under ../host/ for the same reason disc_cue.o does -- the
# .c.o suffix rule above only looks in the current directory, and this SDL
# file must stay out of saturn/makefile's `find src/ -name '*.c'` glob for
# the SH-2 build.
input_sdl.o: ../host/input_sdl.c
	$(CC) $(CFLAGS) -c ../host/input_sdl.c -o input_sdl.o

# platform_sdl.o lives under ../host/ for the same reason disc_cue.o does --
# the .c.o suffix rule above only looks in the current directory, and this
# SDL file must stay out of saturn/makefile's `find src/ -name '*.c'` glob
# for the SH-2 build.
platform_sdl.o: ../host/platform_sdl.c
	$(CC) $(CFLAGS) -c ../host/platform_sdl.c -o platform_sdl.o

clean:
	@rm $(OBJS)
