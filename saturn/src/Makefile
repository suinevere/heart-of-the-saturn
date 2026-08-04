all: alien

CC=gcc
SDL_CFLAGS = `sdl2-config --cflags`
SDL_LIBS = `sdl2-config --libs`
CFLAGS=-ggdb -DENABLE_DEBUG -O2 -Wall -I. -Ihost $(SDL_CFLAGS)
OBJS=\
	client.o common.o vm.o      sprites.o decode.o   animation.o   \
	rooms.o  render.o main.o    debug.o    lzss.o                  \
	sound.o  screen.o scale2x.o scale3x.o game2bin.o discfmt.o     \
	disc_cue.o

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

clean:
	@rm $(OBJS)
