cmake_minimum_required(VERSION 2.8)

project(libma)

#set(CMAKE_C_COMPILER "/opt/rh/devtoolset-7/root/usr/bin/gcc")
#set(CMAKE_CXX_COMPILER "/opt/rh/devtoolset-7/root/usr/bin/g++")

#check compiler version
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
    OUTPUT_VARIABLE GCC_VERSION)
  if (GCC_VERSION VERSION_LESS 7.0.0)
    message(FATAL_ERROR "gcc version must be >= 7.0.0 and is ${GCC_VERSION}")
  endif()

  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
    OUTPUT_VARIABLE CXX_VERSION)
  if (CXX_VERSION VERSION_LESS 7.0.0)
    message(FATAL_ERROR "g++ version must be >= 7.0.0 and is ${CXX_VERSION}")
  endif()
endif()

set(COMMON_CMAKE_CXX_FLAGS "-D_GNU_SOURCE -g -std=gnu++17 -fPIC -Wall -DDCHECK_ALWAYS_ON -Wno-unused-variable")
set(COMMON_CMAKE_C_FLAGS "-g -Wall -fPIC -Wno-unused-variable")
set(MYRTC_CMAKE_CXX_FLAGS "-fexceptions -DWEBRTC_POSIX -DWEBRTC_LINUX -DLINUX -DNOLINUXIF -DNO_REG_RPC=1 -DHAVE_VFPRINTF=1 -DRETSIGTYPE=void -DNEW_STDIO -DHAVE_STRDUP=1 -DHAVE_STRLCPY=1 -DHAVE_LIBM=1 -DHAVE_SYS_TIME_H=1 -DTIME_WITH_SYS_TIME_H=1 -D_LIBCPP_ABI_UNSTABLE -DWEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS")

set(MA_CMAKE_CXX_FLAGS "-D__STDC_FORMAT_MACROS ${COMMON_CMAKE_CXX_FLAGS} ${MYRTC_CMAKE_CXX_FLAGS}")

if(${MA_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-O0 -ggdb ${MA_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${MA_CMAKE_CXX_FLAGS} -DNDEBUG")
endif()

message("Generating ma ${MA_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")

set(MA_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

set(WA_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../rtc_stack")

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${WA_SOURCE_PATH}/
	${WA_SOURCE_PATH}/myrtc
	${WA_SOURCE_PATH}/owt
	${WA_SOURCE_PATH}/3rd/abseil-cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../build/libdeps/build/include
)

set(
	HEADER_FILES
)

set(MA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE MA_SOURCE_FILE 
	"${MA_SOURCE_DIR}/*.h" 
	"${MA_SOURCE_DIR}/*.hpp" 
	"${MA_SOURCE_DIR}/*.cpp" 
	"${MA_SOURCE_DIR}/*.cc" 
	"${MA_SOURCE_DIR}/*.c"
)

add_library(ma STATIC ${MA_SOURCE_FILE} ${HEADER_FILES})

#subdirs(
#	../rtcstack
#)

