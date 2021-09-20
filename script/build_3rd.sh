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

check_meson(){
  local installed=`rpm -qa | grep meson`
  if [[ "$installed" == meson* ]];then
    echo "$installed installed."
  else
  	echo "install meson for libnice0118"
    yum install meson.noarch -y
  fi
}

install_openssl(){
  local LIST_LIBS=`ls ${PREFIX_DIR}/lib/libssl* 2>/dev/null`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "openssl already installed." && return 0

  if [ -d $LIB_DIR ]; then
    local SSL_BASE_VERSION="1.1.1"
    local SSL_VERSION="1.1.1h"
    cd $LIB_DIR
    rm -f ./build/lib/libssl.*
    rm -f ./build/lib/libcrypto.*
    rm -rf openssl-1*

    #wget -c https://www.openssl.org/source/openssl-${SSL_VERSION}.tar.gz
    cp ${THIRD_PARTY_DEPTH}/openssl-${SSL_VERSION}.tar.gz ./
    tar xf openssl-${SSL_VERSION}.tar.gz
    cd openssl-${SSL_VERSION}
    ./config no-ssl3 --prefix=$PREFIX_DIR -fPIC
    make depend
    make -s V=0 -j4
    make install > /dev/null

    cd $CURRENT_DIR
  else
    mkdir -p $LIB_DIR
    install_openssl
  fi
}

#libnice depends on zlib
install_libnice014(){
  local LIST_LIBS=`ls ${PREFIX_DIR}/lib/libnice* 2>/dev/null`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "libnice already installed." && return 0

  if [ -d $LIB_DIR ]; then
    cd $LIB_DIR
    rm -f ./build/lib/libnice.*
    rm -rf libnice-0.1.*
    #wget -c http://nice.freedesktop.org/releases/libnice-0.1.4.tar.gz
    cp ${THIRD_PARTY_DEPTH}/libnice-0.1.4.tar.gz ./
    tar -zxvf libnice-0.1.4.tar.gz
    cd libnice-0.1.4
    patch -p1 < $PATHNAME/patches/libnice014-agentlock.patch
    patch -p1 < $PATHNAME/patches/libnice014-agentlock-plus.patch
    patch -p1 < $PATHNAME/patches/libnice014-removecandidate.patch
    patch -p1 < $PATHNAME/patches/libnice014-keepalive.patch
    patch -p1 < $PATHNAME/patches/libnice014-startcheck.patch
    patch -p1 < $PATHNAME/patches/libnice014-closelock.patch
    ./configure --prefix=$PREFIX_DIR && make -s V= && make install
    cd $CURRENT_DIR
  else
    mkdir -p $LIB_DIR
    install_libnice014
  fi
}

#libnice depends on zlib
install_libnice0118(){
  check_meson

  local LIST_LIBS=`ls ${PREFIX_DIR}/lib64/libnice* 2>/dev/null`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "libnice already installed." && return 0

  if [ -d $LIB_DIR ]; then
    cd $LIB_DIR
    rm -f ./build/lib64/libnice.*
    rm -rf libnice-0.1.*
    cp ${THIRD_PARTY_DEPTH}/libnice-0.1.18.tar.gz ./
    tar -zxvf libnice-0.1.18.tar.gz
    cd libnice-0.1.18
    meson builddir -Dprefix=$PREFIX_DIR -Drelease=true -Dc_args=$1 && ninja -C builddir && ninja -C builddir install
    cd $CURRENT_DIR
  else
    mkdir -p $LIB_DIR
    install_libnice0118
  fi
}

install_libsrtp2(){
  local LIST_LIBS=`ls ${PREFIX_DIR}/lib/libsrtp2* 2>/dev/null`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "libsrtp2 already installed." && return 0

  if [ -d $LIB_DIR ]; then
    cd $LIB_DIR
    rm -rf libsrtp-2.1.0
    #curl -o libsrtp-2.1.0.tar.gz https://codeload.github.com/cisco/libsrtp/tar.gz/v2.1.0
    cp ${THIRD_PARTY_DEPTH}/libsrtp-2.1.0.tar.gz ./
    tar -zxvf libsrtp-2.1.0.tar.gz
    cd libsrtp-2.1.0
    CFLAGS="-fPIC" ./configure --enable-openssl --prefix=$PREFIX_DIR --with-openssl-dir=$PREFIX_DIR
    make $FAST_MAKE -s V=0 -j4 && make uninstall && make install
    cd $CURRENT_DIR
  else
    mkdir -p $LIB_DIR
    install_libsrtp2
  fi
}

install_gtest(){
  local LIST_LIBS=`ls ${PREFIX_DIR}/lib/libgtest* 2>/dev/null`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "gtest already installed." && return 0

  if [ -d $LIB_DIR ]; then
    cd $LIB_DIR
    rm -rf gtest
    cp ${THIRD_PARTY_DEPTH}/googletest.tar.gz ./
    tar -zxvf googletest.tar.gz
    cd googletest
    cmake . -Bbuild -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR
    cd build
    make $FAST_MAKE -s V=0 -j4 && make install
    cd $CURRENT_DIR
  else
    mkdir -p $LIB_DIR
    install_gtest
  fi
}

instlall_ninja(){
  local LIST_LIBS=`ls ${PREFIX_DIR}/ninja/*`
  $INCR_INSTALL && [[ ! -z $LIST_LIBS ]] && echo "ninja already installed." && return 0

  if [ -d PREFIX_DIR ]; then
    cd $PREFIX_DIR
    cp ${THIRD_PARTY_DEPTH}/ninja.tar.gz ./
    tar -zxvf ninja.tar.gz
    cd $CURRENT_DIR
  else
    mkdir -p PREFIX_DIR
    instlall_ninja
  fi
}

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

export PKG_CONFIG_PATH

#install_openssl
#install_libsrtp2
#install_libnice014
#install_libnice0118 $1
#install_gtest
#instlall_ninja
install_opus
install_ffmpeg

cd $ROOT_DIR
