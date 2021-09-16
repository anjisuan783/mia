
#!/bin/bash/sh

source scl_source enable devtoolset-7

cmake ./src -Bbuild

cd build

make -j4

cd ..

g++ -o media_server -g3 -ggdb -std=gnu++17 ./demo/media_server_main.cpp  -I./src \
	./build/libma.a ../rtc_stack/build/debug/libwa.a \
	../rtc_stack/build/debug/3rd/libsdptransform/libsdptransform.a \
	../rtc_stack/build/libdeps/build/lib/libsrtp2.a \
	../rtc_stack/build/debug/3rd/abseil-cpp/libabsl.a \
	../rtc_stack/build/libdeps/build/lib64/libnice.a \
	../rtc_stack/build/debug/3rd/libevent/libevent.a \
	../rtc_stack/build/libdeps/build/lib/libssl.a \
	../rtc_stack/build/libdeps/build/lib/libcrypto.a \
	-llog4cxx -lpthread -ldl -lboost_system -lboost_thread -lgthread-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0  
	
