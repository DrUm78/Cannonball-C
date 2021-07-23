CC = gcc

FFLAGS = -I./ -I./engine/audio -I./src -I./hwvideo -I./engine -I./directx -I./frontend -I./hwaudio -I./sdl
CFLAGS = -O2 -march=native -g3 -Wall $(FFLAGS) `sdl-config --cflags --libs` -DSDL -DHOME_SUPPORT -DCOMPILE_SOUND_CODE
LDFLAGS = -Wl,--as-needed `sdl-config --libs` -lm
OUTPUT = cannonball.elf

SOURCES = main.c sdl/music.c xmlutils.c romloader.c roms.c trackloader.c utils.c video.c thirdparty/crc/crc.c thirdparty/sxmlc/sxmlc.c thirdparty/sxmlc/sxmlsearch.c
SOURCES += engine/oentry.c engine/oanimseq.c engine/oattractai.c engine/obonus.c engine/ocrash.c engine/oferrari.c engine/ohiscore.c engine/ohud.c engine/oinitengine.c engine/oinputs.c engine/olevelobjs.c engine/ologo.c engine/omap.c engine/omusic.c
SOURCES += engine/ooutputs.c engine/opalette.c engine/oroad.c engine/osmoke.c engine/osprite.c engine/osprites.c engine/ostats.c engine/otiles.c
SOURCES += engine/otraffic.c engine/outils.c engine/outrun.c engine/audio/osound.c engine/audio/osoundint.c frontend/config.c font/font_drawing.c
SOURCES += frontend/menu.c frontend/ttrial.c
SOURCES += hwaudio/segapcm.c hwaudio/ym2151.c
SOURCES += hwvideo/hwroad.c hwvideo/hwsprites.c hwvideo/hwtiles.c cannonboard/interface.c cannonboard/asyncserial.c
SOURCES += sdl/audio.c sdl/input.c sdl/rendersw.c sdl/timer.c
OBJS = ${SOURCES:.c=.o}

all: cannonball.elf

%.o:	%.c
		$(CC) ${CFLAGS} -c -o $@ $?
		
cannonball.elf:	${OBJS}
		$(CC) -o $@ $+ ${LDFLAGS}
	
clean:
	rm *.o src/*.o engine/audio/*.o engine/*.o hwvideo/*.o engine/*.o directx/*.o frontend/*.o hwaudio/*.o sdl/*.o sdl/*.o thirdparty/crc/*.o thirdparty/sxmlc/*.o cannonboard/*.o font/*.o
	rm ${OUTPUT}
