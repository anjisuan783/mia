#!/bin/bash

SCRIPT=`pwd`/$0
FILENAME=`basename $SCRIPT`
PATHNAME=`dirname $SCRIPT`
ROOT_DIR=$PATHNAME/..
BUILD_DIR=$ROOT_DIR/build
CURRENT_DIR=`pwd`

THIRD_PARTY_DEPTH=$ROOT_DIR/3rd

LIB_DIR=$BUILD_DIR/libdeps
PREFIX_DIR=$LIB_DIR/build/
PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$PREFIX_DIR"/lib/pkgconfig":$PREFIX_DIR"/lib64/pkgconfig"

INCR_INSTALL=true
DEBUG_MODE=false

if [ "$*" ]; then
    for input in $@
    do
        if [ $input = "--rebuild" ]; then INCR_INSTALL=false; fi
		if [ $input = "--debug" ]; then DEBUG_MODE=true; fi
    done
fi

install_ffmpeg(){
  local LIST_LIBS=`ls ${PREFIX_DIR}/lib/libavcodec.a 2>/dev/null`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "ffmpeg already installed." && return 0

  if [ -d $LIB_DIR ]; then
    cd $LIB_DIR
    rm -rf ffmpeg-4-fit
    cp ${THIRD_PARTY_DEPTH}/ffmpeg-4-fit.tar.gz ./
    tar -zxvf ffmpeg-4-fit.tar.gz
    cd ffmpeg-4-fit
    FFMPEG_OPTIONS="--disable-asm --disable-x86asm --disable-inline-asm"
    ./configure --prefix=$PREFIX_DIR \
      --pkg-config-flags="--static" --extra-libs="-lpthread" --extra-libs="-lm" ${FFMPEG_OPTIONS} \
      --disable-programs --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
      --disable-avdevice --disable-avformat --disable-swscale --disable-postproc --disable-avfilter --disable-network \
      --disable-dct --disable-dwt --disable-error-resilience --disable-lsp --disable-lzo --disable-faan --disable-pixelutils \
      --disable-hwaccels --disable-devices --disable-audiotoolbox --disable-videotoolbox  --disable-cuvid \
      --disable-d3d11va --disable-dxva2 --disable-ffnvcodec --disable-nvdec --disable-nvenc --disable-v4l2-m2m --disable-vaapi \
      --disable-vdpau --disable-appkit --disable-coreimage --disable-avfoundation --disable-securetransport --disable-iconv \
      --disable-lzma --disable-sdl2 --disable-everything --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm \
      --enable-decoder=libopus --enable-encoder=aac --enable-encoder=opus --enable-encoder=libopus --enable-libopus
    make -j4 && make install
    cd $CURRENT_DIR
  else
    mkdir -p $LIB_DIR
    install_ffmpeg
  fi
}

install_opus(){
  local LIST_LIBS=`ls ${PREFIX_DIR}/lib/libopus.a 2>/dev/null`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "opus already installed." && return 0

  if [ -d $LIB_DIR ]; then
    cd $LIB_DIR
    rm -rf opus-1.3.1
    cp ${THIRD_PARTY_DEPTH}/opus-1.3.1.tar.gz ./
    tar -zxvf opus-1.3.1.tar.gz
    cd opus-1.3.1
    ./configure --prefix=$PREFIX_DIR --enable-static --disable-shared
    make -j4 && make install
    cd $CURRENT_DIR
  else
    mkdir -p $LIB_DIR
    install_opus
  fi
}

install_rtc_stack(){
  cd $ROOT_DIR/3rd/
  git clone https://github.com/anjisuan783/rtc_stack.git
  cd rtc_stack/scripts && ./buildProject.sh && cd $CUR_DIR
  cd ./script && ./build_3rd.sh && cd $ROOT_DIR
}

export PKG_CONFIG_PATH

install_rtc_stack
install_opus
install_ffmpeg

cd $ROOT_DIR
