#!/bin/bash

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

DEBUG_MODE=release

if [ "$*" ]; then
    for input in $@
    do
		if [ $input = "--debug" ]; then DEBUG_MODE=debug; fi
    done
fi

source scl_source enable devtoolset-7

. ./script/installLibrary.sh

buildLibrary $*

cd $CURRENT_DIR

rm -f mia

TEST_PATH=$CURRENT_DIR/test
SRC_FILES="$TEST_PATH/msg_queue.cpp"

DEPS_LIBS="./build/ma/libma.a \
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
	-llog4cxx -lpthread -ldl -lgthread-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lconfig"

DEPS_INCLUDE="-I./src -I./rtc_stack -I./rtc_stack/myrtc"

COMMON_FLAG="-std=gnu++17 -DWEBRTC_POSIX -DWEBRTC_LINUX -Wall \
		-DDCHECK_ALWAYS_ON"

DEBUG_FLAG="-O0 -g -DNDEBUG"

g++ -o test_msg $DEBUG_FLAG $COMMON_FLAG $SRC_FILES $DEPS_INCLUDE $DEPS_LIBS

echo "build test done"
