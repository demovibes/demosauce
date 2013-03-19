/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*
*   this file handles dynamic linking to libbass.so
*   it was nessessary because the system's dynamic 
*   linking isn't flexible enough
*/

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <bass.h>
#include "util.h"

// I had a lot of preprocessor hackery here to detect void functions. 
// it almost worked but void* made a mess of thins :(
// instead i added an extra column that holds the number of arguments, 
// or 'V' for a void function. more verbose, but much cleaner to
// implement

#define JOIN(a,b)           JOIN2(a,b)
#define JOIN2(a,b)          a##b

#define ARG_V(type)         type
#define ARG_0()             
#define ARG_1(type)         type a1
#define ARG_2(type,...)     type a2,ARG_1(__VA_ARGS__)
#define ARG_3(type,...)     type a3,ARG_2(__VA_ARGS__)
#define ARG_4(type,...)     type a4,ARG_3(__VA_ARGS__)
#define ARG_5(type,...)     type a5,ARG_4(__VA_ARGS__)
#define ARG_6(type,...)     type a6,ARG_5(__VA_ARGS__)
#define ARG_DECL(n,...)     JOIN(ARG_,n)(__VA_ARGS__)
#define ARG_CALL(n)         JOIN(ARG_,n)()

// x macros for bass functions
#define FUNCTION_DEFS                                                                   \
    X(BOOL,         Init,                   5,int,DWORD,DWORD,void*,void*)              \
    X(int,          ErrorGetCode,           0)                                          \
    X(BOOL,         SetConfig,              2,DWORD,DWORD)                              \
    X(HSTREAM,      StreamCreateFile,       5,BOOL,const void*,QWORD,QWORD,DWORD)       \
    X(BOOL,         StreamFree,             1,HSTREAM)                                  \
    X(QWORD,        StreamGetFilePosition,  2,HSTREAM,DWORD)                            \
    X(HMUSIC,       MusicLoad,              6,BOOL,const void*,QWORD,DWORD,DWORD,DWORD) \
    X(BOOL,         MusicFree,              1,HMUSIC)                                   \
    X(DWORD,        ChannelFlags,           3,DWORD,DWORD,DWORD)                        \
    X(BOOL,         ChannelGetInfo,         2,DWORD,BASS_CHANNELINFO*)                  \
    X(DWORD,        ChannelGetData,         3,DWORD,void*,DWORD)                        \
    X(QWORD,        ChannelGetPosition,     2,DWORD,DWORD)                              \
    X(QWORD,        ChannelGetLength,       2,DWORD,DWORD)                              \
    X(const char*,  ChannelGetTags,         2,DWORD,DWORD)                              \
    X(BOOL,         ChannelSetPosition,     3,DWORD,QWORD,DWORD)

#define X(ret,name,args,...) static ret (*name)(__VA_ARGS__);
FUNCTION_DEFS
#undef X

#define X(ret,name,args,...) ret BASSDEF(BASS_##name)(ARG_DECL(args,__VA_ARGS__)){return name(ARG_CALL(args));}
FUNCTION_DEFS
#undef X

static void* handle;
static bool  error;

static void* bind(const char* symbol)
{
    void* sym = dlsym(handle, symbol);
    if (!sym) {
        error = true;
        printf("libbass.so missing symbol: %s\n", symbol);
    }
    return sym;
}

static bool load(const char* file)
{
    handle = dlopen(file, RTLD_LAZY);
    if (!handle)
        return false;
    
    #define X(ret,name,agrs,...) name=(ret(*)(__VA_ARGS__))bind(#name);
    FUNCTION_DEFS
    #undef X

    return error ? false : true;
}

bool bass_loadso(char** argv)
{
    char path[4096];
    if (strlen(argv[0]) < sizeof(path) - 32) {
        strcpy(path, argv[0]);
        char* path_end = strrchr(path, '/') + 1;
        strcpy(path_end, "libbass.so");
        if (load(path))
            return true;
        strcpy(path_end, "bass/libbass.so");
        if (load(path))
            return true;
    }
    if (load("/usr/local/lib/libbass.so"))
        return true;
    if (load("/usr/lib/libbass.so"))
        return true;
    puts("can't locate libbass.so");
    return false;
}

