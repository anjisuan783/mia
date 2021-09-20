
#!/bin/bash/sh

CUR_DIR=`pwd`
source scl_source enable devtoolset-7

cd 3rd/rtc_stack/scripts && ./buildProject.sh && cd $CUR_DIR

cd ./script && ./build_3rd.sh && cd $CUR_DIR

cmake ./src -Bbuild && cd build && make -j4 && cd $CUR_DIR

rm -f media_server

g++ -o media_server -g3 -ggdb -std=gnu++17 ./demo/media_server_main.cpp  -I./src \
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
	-llog4cxx -lpthread -ldl -lboost_system -lboost_thread -lgthread-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0

echo "build media server done"

