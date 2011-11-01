#i!/bin/sh
CXX=g++
CFLAGS="-Wall -O2"
#remove old ouput files
rm -f config.h
rm -f makebelieve.sh

check_header() {
    echo -n "checking for $1 ... "
    echo "#include $1" | $CXX -E -xc++ -o /dev/null - > /dev/null 2> /dev/null
    if test $? -ne 0; then
        echo "no"
        return 1
    fi
    echo "yes"
    return 0
}

check_file() {
    echo -n "checking for $1 ... "
    if test -e "$1"; then
        echo "yes"
        return 0
    fi
    echo "no"
    return 1
}

check_exe() {
    echo -n "checking for $1 ... "
    which $1 > /dev/null
    if test $? -ne 0; then
        echo "no"
        return 1
    fi
    echo "yes"
    return 0
}

ask() {
    while true; do
        read -p "$1 [y/n] " reply
        case "$reply" in 
            Y|y) return 0;;
            N|n) return 1;;
        esac
    done
}

run_script() {
    old_dir=`pwd`
    if test -n "$2"; then cd $2; fi     
    ./$1
    result=$?
    cd $old_dir
    return $result
}

build() {
    BUILD="${BUILD}compile \$CFLAGS $1\n"
}

# requirements
if ! check_header "<shout/shout.h>"; then echo 'libshout missing'; exit 1; fi
if ! check_header "<unicode/ucnv.h>"; then echo 'libicu missing'; exit 1; fi
if ! check_header "<boost/version.hpp>"; then echo 'libboost missing'; exit 1; fi

build '-c logror.cpp'

# ladspa
if check_header '<ladspa.h>'; then
    CFLAGS="$CFLAGS -DENABLE_LADSPA"
    LADSPAO="ladspahost.o"
    build '-c ladspahost.cpp'
    build '-o ladspainfo ladspainfo.cpp logror.o ladspahost.o -ldl -lboost_filesystem-mt -lboost_date_time-mt'
fi

# bass
check_bass() {
    if check_header '"bass/bass.h"' && check_file "bass/libbass.so"; then
        if ! check_header "<id3tag.h>"; then echo 'libid3tag missing'; exit 1; fi
        CFLAGS="$CFLAGS -DENABLE_BASS"
        BASSO="libbass.o basssource.o"
        BASSL="-ldl -lid3tag -lz"
        build '-c libbass.c'
        build '-c basssource.cpp'
        return 0
    fi
    return 1
}

if ! check_bass; then
    if ask "download BASS for mod playback?"; then
        run_script getbass.sh bass
        if ! check_bass; then exit 1; fi
    fi 
fi

echo "due to problems with libavcodec on some distros you can build a custom"
echo "version. in general, the distro's libavcodec should be preferable, but"
echo "might be incompatible with demosauce. you'll need the 'yasm' assember"
if ask "use custom libavcodec?"; then
    run_script build.sh ffmpeg
    if test $? -ne 0; then echo 'error while building libavcodec'; exit 1; fi
    AVCODECL="-Lffmpeg -lavformat -lavcodec -lavutil"
    build '-Iffmpeg -c avsource.cpp'
else
    if ! check_header '<libavcodec/avcodec.h>'; then echo 'libavcodec missing'; exit 1; fi
    if ! check_header '<libavformat/avformat.h>'; then echo 'libaformat missing'; exit 1; fi
    AVCODECL="-lavformat -lavcodec" 
    build "-c avsource.cpp"
fi

# replaygain
if ! check_file 'libreplaygain/libreplaygain.a'; then
    run_script build.sh libreplaygain
fi

if check_exe 'ccache'; then
    CXX="ccache $CXX"
fi

# other build steps
build '-c demosauce.cpp'
build '-I. -c shoutcast.cpp'
build '-c scan.cpp'
build '-c settings.cpp'
build '-c convert.cpp'
build '-c effects.cpp'
build '-c sockets.cpp'

INPUT="scan.o avsource.o effects.o logror.o convert.o $BASSO libreplaygain/libreplaygain.a"
LIBS="-lsamplerate -lboost_system-mt"
build "-o scan $INPUT $LIBS $BASSL $AVCODECL"

INPUT="settings.o demosauce.o avsource.o convert.o effects.o logror.o sockets.o shoutcast.o $BASSO $LADSPAO"
LIBS="-lshout -lsamplerate -lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt -lboost_program_options-mt"
build "-o demosauce $INPUT $LIBS $BASSL $AVCODECL `icu-config --ldflags`"

# generate build script
printf "#!/bin/sh\n#generated build script\nCFLAGS='$CFLAGS'\n" >> makebelieve.sh
printf "compile(){\n\techo $CXX \$@\n\tif ! $CXX \$@; then exit 1; fi\n}\n" >> makebelieve.sh
printf "$BUILD\nrm -f *.o" >> makebelieve.sh
chmod a+x makebelieve.sh

echo "run ./makebelieve to build demosauce"
