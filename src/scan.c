/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <replay_gain.h>
#include "bassdecoder.h"
#include "ffdecoder.h"
#include "effects.h"
#include "util.h"

#define MAX_LENGTH      3600     // abort scan if track is too long, in seconds
#define SAMPLERATE      44100
#define HELP_MESSAGE    "demosauce scan tool 0.4.0"ID_STR"\n"
                        "syntax: scan [options] file\n\t"
                            "-h: print help\n\t"
                            "-r: disable replaygain analysis\n\t"
                            "-o file.wav or stdout: write to wav or stdout\n\t\t"
                                "format is 32 bit float, 44.1 khz, stereo"

// for some formats avcodec fails to provide a bitrate so I just
// make an educated guess. if the file contains large amounts of 
// other data besides music, this will be completely wrong.
static float fake_bitrate(const char* path, float duration)
{
    long bytes = util_filesize(path);
    long kbit = (bytes * 8) / 1000;
    return (float)kbit / duration;
}

void die(const char* msg)
{
    puts(msg);
    exit(EXIT_FAILURE);
}

static void mwav_write_int(FILE* f, int v, int size)
{
    unsigned char buf[4] = {v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255};
    fwrite(buf, 1, size, f);
}

static FILE* mwav_open_writer(const char* path, int channels, int samplerate, int samplesize)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return NULL;
    fwrite("RIFF", 1, 4, f);
    mwav_write_int(f, 0, 4);            /* filled by close */
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    mwav_write_int(f, 16, 4);
    mwav_write_int(f, samplesize == 4 ? 3: 1, 2); 
    mwav_write_int(f, channels, 2);
    mwav_write_int(f, samplerate, 4); 
    mwav_write_int(f, channels * samplerate * samplesize, 4);
    mwav_write_int(f, channels * samplesize, 2);
    mwav_write_int(f, samplesize * 8, 2);
    fwrite("data", 1, 4, f);
    mwav_write_int(f, 0, 4);            /* filled by close */
    if (ftell(f) == 44)
        return f;
    fclose(f);
    return NULL;
}

static void mwav_close_writer(FILE* f)
{
    if (!f)
        return;
    int size = (int)ftell(f);
    if (size < 0) size = 0x7fffffff;
    fseek(f, 4, SEEK_SET);
    mwav_write_int(f, size - 8, 4);
    fseek(f, 40, SEEK_SET);
    mwav_write_int(f, size - 44, 4);
    fclose(f);
}

static void write_wav(FILE* f, struct stream* s)
{
    float tmp[128];
    int frames = 0;

    while (frames < s->frames) {
        int process_frames = CLAMP(0, s->frames - frames, COUNT(tmp) / 2);
        const float* left  = s->buffer[0] + frames;
        const float* right = s->buffer[s->channels == 1 ? 0 : 1] + frames;
        for (int i = 0; i < process_frames; i++) {
            tmp[i * 2]     = left[i];
            tmp[i * 2 + 1] = right[i];
        }
        fwrite(tmp, sizeof(float), process_frames, f);
        frames += process_frames;
    }
}

int main(int argc, char** argv)
{
    const char*     path        = NULL;
    bool            analyze     = true;
    bool            loaded      = false;
    struct info     info        = {0};
    struct decoder  decoder     = {0};
    void*           resampler   = NULL;
    struct stream   stream0     = {{0}};
    struct stream   stream1     = {{0}};
    struct stream*  stream      = &stream0;
    FILE*           output      = NULL;

#ifdef ENABLE_BASS
    if (!bass_loadso(argv))
        die("failed to load libbass.so");
#endif
    if (argc <= 1) 
        die(HELP_MESSAGE);
    
    char c = 0;
    while ((c = getopt(argc, argv, "hro:")) != -1) {
        switch (c) {
        default:
        case '?':
            die(HELP_MESSAGE);
        case 'h':
            puts(HELP_MESSAGE);
            return EXIT_SUCCESS;
        case 'r':
            analyze = false;
            break;
        case 'o':
            if (!strcmp(optarg, "stdout")) {
                output = stdout;
                analyze = false;
            } else {
                output = mwav_open_writer(optarg, 2, SAMPLERATE, 4);
            }
            break;
        };
    }
    path = argv[optind];

#ifdef ENABLE_BASS
    loaded = bass_load(&decoder, path, "bass_prescan=true", SAMPLERATE);
#endif
    if (!loaded)
        loaded = ff_load(&decoder, path);

    if (!loaded) 
        die("unknown format");
        
    decoder.info(&decoder, &info);

    if (info.samplerate <= 0) 
        die("improper samplerate");

    if (info.channels < 1 || info.channels > 2) 
        die("improper channel number");
    
    if (info.samplerate != SAMPLERATE) {
        resampler = fx_resample_init(info.channels, info.samplerate, SAMPLERATE);
        if (!resampler)
            die("failed to init resampler");      
        stream = &stream1; 
    }

    struct rg_context* ctx = rg_new(SAMPLERATE, RG_FLOAT32, info.channels, false);

    // avcodec is unreliable when it comes to length, so the only way to be 
    // absolutely accurate is to decode the whole stream
    long frames = 0;
    if (analyze || output || (info.flags & INFO_FFMPEG)) {
        while (!stream->end_of_stream) {
            decoder.decode(&decoder, &stream0, SAMPLERATE);
            // TODO disable resampler if rg is disabled
            if (resampler)
                fx_resample(resampler, &stream0, &stream1);
            float* buff[2] = {stream->buffer[0], stream->buffer[1]};
            // there is a strange bug in the replaygain code that can cause it to report the wrong
            // value if the input buffer has an odd lengh, until the root of the cause is found,
            // this will have to do :(
            if (analyze) 
                rg_analyze(ctx, buff, stream->frames & -2);
            if (output)
                write_wav(output, stream);
            frames += stream->frames;
            if (frames > MAX_LENGTH * SAMPLERATE) 
                die("exceeded maxium length");
        }
    }

    if (output == stdout)
        return EXIT_SUCCESS;
    if (output)
        mwav_close_writer(output);

    char* str = NULL;
    str = decoder.metadata(&decoder, "artist");
    if (str)
        printf("artist:%s\n", str);
    str = decoder.metadata(&decoder, "title");
    if (str)
        printf("title:%s\n", str);
    printf("type:%s\n", info.codec);
    // ffmpeg's length is not reliable
    float duration = (info.flags & INFO_FFMPEG) ?
        (float)frames / SAMPLERATE :
        (float)info.frames / info.samplerate;
    printf("length:%f\n", duration);    
    if (analyze)
        printf("replaygain:%f\n", rg_title_gain(ctx));
#ifdef ENABLE_BASS
    if ((info.flags & INFO_BASS) && (info.flags & INFO_MOD))
        printf("loopiness:%f\n", bass_loopiness(path));
#endif
    if (info.bitrate)
        printf("bitrate:%f\n", info.bitrate);
    else if (info.flags & INFO_FFMPEG)
        printf("bitrate:%f\n", fake_bitrate(path, frames * SAMPLERATE));
    if (!(info.flags & INFO_MOD) && info.samplerate)
        printf("samplerate:%d\n", info.samplerate);
    
    return EXIT_SUCCESS;
}

