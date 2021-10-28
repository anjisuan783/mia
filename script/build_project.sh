
#!/bin/bash/sh

SCRIPT=`pwd`/$0
FILENAME=`basename $SCRIPT`
PATHNAME=`dirname $SCRIPT`
ROOT=$PATHNAME/..
BUILD_DIR=$ROOT/build
CUR_DIR=`pwd`
SUDO=""

if [[ $EUID -ne 0 ]]; then
  SUDO="sudo -E"
fi

OS=`$PATHNAME/detectOS.sh | awk '{print tolower($0)}'`
echo $OS

installYumDeps(){
  ${SUDO} yum groupinstall " Development Tools" "Development Libraries " -y
  ${SUDO} yum install zlib-devel pkgconfig git log4cxx-devel gcc gcc-c++ bzip2 bzip2-devel bzip2-libs python-devel nasm yasm cmake -y
  ${SUDO} yum install h libtool -y
  ${SUDO} yum install glib2-devel -y
  ${SUDO} yum install centos-release-scl -y
  ${SUDO} yum install devtoolset-7-gcc* -y
}

if [[ "$OS" =~ .*centos.* ]]
then
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

pushd $PATHNAME
./build_3rd.sh
popd

cmake ./src -Bbuild && cd build && make -j4 && cd $CUR_DIR

rm -f mia

g++ -o mia -g3 -ggdb -std=gnu++17 ./demo/media_server_main.cpp  -I./src \
	./build/libma.a \
	./build/libdeps/build/lib/libavcodec.a \
	./build/libdeps/build/lib/libswresample.a \
	./build/libdeps/build/lib/libavutil.a \
	./build/libdeps/build/lib/libopus.a \
	./3rd/rtc_stack/build/debug/libwa.a \
	./3rd/rtc_stack/build/debug/3rd/libsdptransform/libsdptransform.a \
	./3rd/rtc_stack/build/libdeps/build/lib/libsrtp2.a \
	./3rd/rtc_stack/build/debug/3rd/abseil-cpp/libabsl.a \
	./3rd/rtc_stack/build/libdeps/build/lib64/libnice.a \
	./3rd/rtc_stack/build/debug/3rd/libevent/libevent.a \
	./3rd/rtc_stack/build/libdeps/build/lib/libssl.a \
	./3rd/rtc_stack/build/libdeps/build/lib/libcrypto.a \
	-llog4cxx -lpthread -ldl -lgthread-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0

echo "build mia done"

