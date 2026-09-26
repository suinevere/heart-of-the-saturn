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

disc_cue.o: ../host/disc_cue.c
	$(CC) $(CFLAGS) -c ../host/disc_cue.c -o disc_cue.o

video_sdl.o: ../host/video_sdl.c
	$(CC) $(CFLAGS) -c ../host/video_sdl.c -o video_sdl.o

input_sdl.o: ../host/input_sdl.c
	$(CC) $(CFLAGS) -c ../host/input_sdl.c -o input_sdl.o

platform_sdl.o: ../host/platform_sdl.c
	$(CC) $(CFLAGS) -c ../host/platform_sdl.c -o platform_sdl.o

clean:
	@rm $(OBJS)
