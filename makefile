include config.mk

INPUT_DEMOSAUCE = $(BASSOURCE) cast.o demosauce.o effects.o ffdecoder.o log.o settings.o util.o
LINK_DEMOSAUCE = -lm -lmp3lame $(shell pkg-config --libs shout samplerate) $(LINK_FFMPEG) $(LINK_BASS)

INPUT_SCAN = $(BASSOURCE) ffdecoder.o log.o scan.o util.o effects.o
LINK_SCAN = -lm $(shell pkg-config --libs samplerate) $(LINK_FFMPEG) $(LINK_BASS) replaygain/libreplaygain.a

# The reason I clean before the build is because I'm too lazy to check for dependencies.
# If you build the binary just once this if of no concern. If you recompile often install ccache.
all: clean demosauce scan
	rm -f *.o

demosauce: $(INPUT_DEMOSAUCE)
	$(CC) $(LDFLAGS) $(INPUT_DEMOSAUCE) $(LINK_DEMOSAUCE) -o demosauce

scan: $(INPUT_SCAN)
	$(CC) $(LDFLAGS) $(INPUT_SCAN) $(LINK_SCAN) -o scan

%.o: src/%.c
	$(CC) -Wall $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f demosauce scan
	rm -f *.o

