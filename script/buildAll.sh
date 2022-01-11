
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

INCR_INSTALL=true
DEBUG_MODE=release

if [ "$*" ]; then
    for input in $@
    do
        if [ $input = "--rebuild" ]; then INCR_INSTALL=false; fi
		if [ $input = "--debug" ]; then DEBUG_MODE=debug; fi
    done
fi

lib_all_path="build/libma.a \
	build/libwa.a \
	build/3rd/libsdptransform/libsdptransform.a \
	build/libdeps/build/lib/libsrtp2.a \
	build/3rd/abseil-cpp/libabsl.a \
	build/3rd/libevent/libevent.a \
	build/libdeps/build/lib64/libnice.a \
	build/libdeps/build/lib/libssl.a \
	build/libdeps/build/lib/libcrypto.a"

gen_lib() {
	rm -f ../libwa.a ../libma.a
	mkdir ./build/o -p
	cp $lib_all_path ./build/o
	pushd $BUILD_DIR/o >/dev/null
	all_a=`find . -name "*.a"`

	for i in $all_a
	do
	  echo "process $i"
	  ar x $i
	  all_o=`find . -name "*.o"`
	  if [[ -n $all_o ]];
	  then
	  	if [ -f "../libma.a" ];
	  	then
	  	  echo "ar q ../libma.a"
	      ar q ../libma.a $all_o
	    else
	      echo "ar cq ../libma.a"
	      ar cq ../libma.a $all_o
	    fi
	    rm -f *.o
	  fi
	done
	
	pushd
}


if [[ $EUID -ne 0 ]]; then
  SUDO="sudo -E"
fi

OS=`$PATHNAME/detectOS.sh | awk '{print tolower($0)}'`
echo $OS

if [[ "$OS" =~ .*centos.* ]]
then
  . ./script/installCentOSDeps.sh
  read -p "Add EPEL repository to yum? [Yes/no]" yn
  case $yn in
    [Nn]* ) ;;
    [Yy]* ) installRepo;;
    * ) installRepo;;
  esac

  read -p "Installing deps via yum [Yes/no]" yn
  case $yn in
    [Nn]* ) ;;
    [Yy]* ) installYumDeps;;
    * ) installYumDeps;;
  esac
  source scl_source enable devtoolset-7
elif [[ "$OS" =~ .*ubuntu.* ]]
then
  echo "support only centos7"
  exit 1
fi

. ./script/installCommonDeps.sh

install_opus
install_ffmpeg
install_openssl
install_libsrtp2
install_libnice
install_gtest

. ./script/installLibrary.sh
generateVersion $DEBUG_MODE

buildLibrary $*

cd $CURRENT_DIR

rm -f mia

g++ -o mia -g3 -ggdb -std=gnu++17 ./demo/media_server_main.cpp  -I./src \
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

