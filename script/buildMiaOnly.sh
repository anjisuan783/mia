
#!/bin/bash/sh

SCRIPT=`pwd`/$0
FILENAME=`basename $SCRIPT`
PATHNAME=`dirname $SCRIPT`
ROOT_DIR=$PATHNAME/..
BUILD_DIR=$ROOT_DIR/build
LIB_DIR=$BUILD_DIR/libdeps
PREFIX_DIR=$LIB_DIR/build
PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$PREFIX_DIR"/lib/pkgconfig":$PREFIX_DIR"/lib64/pkgconfig"
CURRENT_DIR=`pwd`
THIRD_PARTY_DEPTH=$ROOT_DIR/3rd
SUDO=""

export PKG_CONFIG_PATH

source scl_source enable devtoolset-7

. ./script/installLibrary.sh

buildLibrary $*

cd $CURRENT_DIR

rm -f mia

g++ -o mia -g3 -ggdb -std=gnu++17 -DWEBRTC_POSIX -DWEBRTC_LINUX -DDCHECK_ALWAYS_ON \
	./demo/media_server_main.cpp  -I./src -I./rtc_stack/myrtc \
	./build/ma/libma.a \
	$PREFIX_DIR/lib/libavcodec.a \
	$PREFIX_DIR/lib/libswresample.a \
	$PREFIX_DIR/lib/libavutil.a \
	$PREFIX_DIR/lib/libopus.a \
	./build/wa/libwa.a \
	./build/wa/3rd/libsdptransform/libsdptransform.a \
	./build/wa/3rd/abseil-cpp/libabsl.a \
	./build/wa/3rd/libevent/libevent.a \
	$PREFIX_DIR/lib/libsrtp2.a \
	$PREFIX_DIR/lib64/libnice.a \
	$PREFIX_DIR/lib/libssl.a \
	$PREFIX_DIR/lib/libcrypto.a \
	-llog4cxx -lpthread -ldl -lgthread-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lconfig

echo "build mia done"

