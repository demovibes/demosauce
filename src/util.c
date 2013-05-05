/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include "util.h"
#include "log.h"
#include "effects.h"

#define MEM_ALIGN (sizeof(void*) * 4)

void* util_malloc(size_t size)
{
    void* ptr = NULL;
    int err = posix_memalign(&ptr, MEM_ALIGN, size);
    return err ? NULL : ptr;
}    

void* util_realloc(void* ptr, size_t size)
{
    if (ptr) {
        ptr = realloc(ptr, size);
        if ((size_t)ptr % MEM_ALIGN != 0) {
            void* tmp_ptr = util_malloc(size);
            memmove(tmp_ptr, ptr, size);
            free(ptr);
            ptr = tmp_ptr;
        }
    } else {
        ptr = util_malloc(size);
    }
    return ptr;
}

void util_free(void* ptr)
{
    free(ptr);
}

//-----------------------------------------------------------------------------

bool util_isfile(const char* path)
{
    struct stat buf = {0};
    int err = stat(path, &buf);
    bool isfile = !err && S_ISREG(buf.st_mode);
    LOG_DEBUG("[util_isfile] '%s' %s", path, BOOL_STR(isfile));
    return isfile;
}

long util_filesize(const char* path)
{
    struct stat buf = {0};
    int err = stat(path, &buf);
    long size = err ? -1 : buf.st_size;
    LOG_DEBUG("[util_filesize] '%s' %ld", path, size);
    return size;
}

//-----------------------------------------------------------------------------

char* util_strdup(const char* str)
{
    if (!str)
        return NULL;
    char* s = util_malloc(strlen(str) + 1);
    strcpy(s, str);
    return s;
}

char* util_trim(char* str)
{
    if (!str)
        return NULL;
    char* tmp = str;
    while (isspace(*str))
        str++;
    memmove(str, tmp, strlen(tmp));
    tmp += strlen(tmp);
    while (tmp > str && isspace(tmp[-1]))
        tmp--;
    *tmp = 0;
    return str;
}

static const char* skip_line(const char* str)
{
    str = strchr(str, '\n');
    return str ? str + 1 : NULL;
}

char* keyval_str(char* out, int size, const char* heap, const char* key, const char* fallback)
{
    const char* tmp         = heap;
    size_t      span        = 0;
    bool        have_key    = false;

    while (tmp && *tmp) {
        tmp += strspn(tmp, " \t");                  // skip space before key
        if (strncmp(tmp, key, strlen(key))) {       // see if matches key
            tmp = skip_line(tmp);
            continue;
        }
        tmp += strlen(key);
        tmp += strspn(tmp, " \t");                  // skip space after key 
        if (!*tmp || *tmp != '=') {                 // check for =
            tmp = skip_line(tmp);
            continue;
        }
        have_key = true;
        tmp += strspn(tmp + 1, " \t") + 1;          // skip space before value
        span = strcspn(tmp, "\n");                  // add # for line comments
        while (span && isspace(tmp[span - 1]))      // remove tailing whitespace
            span--;
        break;
    }

    if (have_key) {
        if (!out || span < size) {
            char* value = out ? out : util_malloc(span + 1);
            memmove(value, tmp, span);
            value[span] = 0;
            LOG_DEBUG("[keyval_str] '%s' = '%s'", key, value);
            return value;
        } else {
            LOG_WARN("[keyval_str] buffer too small for value '%s'", key);
        }
    }

    LOG_DEBUG("[keyval_str] '%s' = '%s' (fallback)", key, fallback);    
    if (!out && fallback) {
        return util_strdup(fallback);
    } else if (out && fallback && strlen(fallback) < size) {
        return strcpy(out, fallback);
    } else if (out && size) {
        LOG_WARN("[keyval_str] buffer too small for fallback (%s, %s)", key, fallback);
        return strcpy(out, "");
    } else {
        return NULL;
    }
}

int keyval_int(const char* heap, const char* key, int fallback)
{
    char tmp[16] = {0};
    keyval_str(tmp, 16, heap, key, NULL);
    return strlen(tmp) ? atoi(tmp) : fallback;
}

double keyval_real(const char* heap, const char* key, double fallback)
{
    char tmp[16] = {0};
    keyval_str(tmp, 16, heap, key, NULL);
    return strlen(tmp) ? atof(tmp) : fallback;
}
  
bool keyval_bool(const char* heap, const char* key, bool fallback)
{
    char tmp[8] = {0};
    keyval_str(tmp, 8, heap, key, NULL);
    return strlen(tmp) ? !strcasecmp(tmp, "true") : fallback;
}

//-----------------------------------------------------------------------------

int socket_open(const char* host, int port)
{
    int fd = -1;
    char portstr[10] = {0};
    struct addrinfo* info = NULL;
    struct addrinfo hints = {0};
    
    LOG_DEBUG("[socket] opening %s:%d", host, port);
    if (snprintf(portstr, sizeof(portstr), "%d", port) < 0)
        goto error;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &info))
        goto error;

    for (struct addrinfo* i = info; i; i = i->ai_next) {
        fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (fd < 0)
            continue;   // error
        if (connect(fd, info->ai_addr, info->ai_addrlen) == 0)
            break;      // success
        close(fd);
        fd = -1;
    }

    freeaddrinfo(info);
    if (fd < 0)
        return fd;

error:
    LOG_DEBUG("[socket] failed to open %s:%d", host, port);
    return -1;
}

bool socket_read(int socket, struct buffer* buffer)
{
    ssize_t bytes = 0;
    char buff[4096];
    if (send(socket, "NEXTSONG", 8, 0) == -1)
        goto error;
    bytes = recv(socket, buff, sizeof(buff) - 1, 0);
    if (bytes < 0)
       goto error;
    buffer_resize(buffer, bytes + 1);
    memmove(buffer->data, buff, bytes);
    ((char*)buffer->data)[bytes] = 0;
    LOG_DEBUG("[socket] read %d byets", bytes);
    return true;

error:
    LOG_DEBUG("[socket] read failed");
    return false;
}

void socket_close(int socket)
{
    close(socket);
}

//-----------------------------------------------------------------------------

void buffer_resize(struct buffer* buf, size_t size) 
{
    if (buf->size < size) {
        buf->data = util_realloc(buf->data, size);
        buf->size = size;
    }
}

void buffer_zero(struct buffer* buf)
{
    memset(buf->data, 0, buf->size);
}

void buffer_free(struct buffer* buf)
{
    util_free(buf->data);
    memset(buf, 0, sizeof(struct buffer));
}

//-----------------------------------------------------------------------------

void stream_free(struct stream* s)
{
    for (int i = 0; i < MAX_CHANNELS; i++)
        util_free(s->buffer[i]);
}

void stream_resize(struct stream* s, int frames)
{
    assert(s->channels >= 1 && s->channels <= MAX_CHANNELS);
    if (s->max_frames >= frames)
        return;
    for (int ch = 0; ch < s->channels; ch++) 
        s->buffer[ch] = util_realloc(s->buffer[ch], frames * sizeof(float));
    s->max_frames = frames;
}

void stream_append(struct stream* s, struct stream* source, int frames)
{
    frames = CLAMP(0, frames, source->frames);
    s->channels = source->channels;
    s->frames += frames;
    stream_resize(s, s->frames);
    for (int ch = 0; ch < s->channels; ch++)
        memmove(s->buffer[ch], source->buffer[ch], frames * sizeof(float));
}

void stream_append_convert(struct stream* s, void** source, int type, int frames, int channels)
{
    assert(channels >= 1 && channels <= MAX_CHANNELS);
    float* buffs[MAX_CHANNELS] = {0};
    s->channels = channels;
    stream_resize(s, s->frames + frames);
    for (int ch = 0; ch < MAX_CHANNELS; ch++)
        buffs[ch] = s->buffer[ch] + s->frames;
    fx_convert_to_float(source, buffs, type, frames, channels);
    s->frames += frames;
}

void stream_drop(struct stream* s, int frames)
{
    frames = CLAMP(0, frames, s->frames);
    s->frames -= frames;
    if (s->frames > 0)
        for (int ch = 0; ch < s->channels; ch++)
            memmove(s->buffer[ch], s->buffer[ch] + frames, s->frames * sizeof(float));
}

void stream_zero(struct stream* s, int offset, int frames)
{
    // TODO replace assert with MIN
    assert(offset + frames <= s->max_frames);
    for (int i = 0; i < s->channels; i++)
        memset(s->buffer[i] + offset, 0, s->frames * sizeof(float));
    s->frames = offset + frames;
}

