cmake_minimum_required(VERSION 2.6)

project(media_server)

set(MA_BUILD_TYPE "debug")

if(${MA_BUILD_TYPE} STREQUAL "debuggs") 
	set(CMAKE_C_COMPILER "/usr/local/gcc-7.3.0/bin/gcc")
	set(CMAKE_CXX_COMPILER "/usr/local/gcc-7.3.0/bin/g++")
	set(CMAKE_CXX_GS_FLAGS "-D__GS__ -DOS_LINUX -DLINUX -DUNIX -D_REENTRANT -D_GNU_SOURCE")
endif()

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

set(MYRTC_CMAKE_CXX_FLAGS "-fexceptions -DWEBRTC_POSIX -DWEBRTC_LINUX -DLINUX -DNOLINUXIF -DNO_REG_RPC=1 -DHAVE_VFPRINTF=1 -DRETSIGTYPE=void -DNEW_STDIO -DHAVE_STRDUP=1 -DHAVE_STRLCPY=1 -DHAVE_LIBM=1 -DHAVE_SYS_TIME_H=1 -DTIME_WITH_SYS_TIME_H=1 -D_LIBCPP_ABI_UNSTABLE")
set(MA_CMAKE_CXX_FLAGS "-D_GNU_SOURCE -g -Wall -std=gnu++17 -fPIC -D__STDC_FORMAT_MACROS ${MYRTC_CMAKE_CXX_FLAGS} ${CMAKE_CXX_GS_FLAGS}")

if(${MA_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-O0 -ggdb ${MA_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${MA_CMAKE_CXX_FLAGS} -DNDEBUG")
endif()

message("Generating ma ${MA_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")


set(MA_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../../../include 
	${CMAKE_CURRENT_SOURCE_DIR}/../../../include/platform
	${CMAKE_CURRENT_SOURCE_DIR}/../../../platform/include
	${CMAKE_CURRENT_SOURCE_DIR}/../../..
	${CMAKE_CURRENT_SOURCE_DIR}/../build/libdeps/build/include
	${CMAKE_CURRENT_SOURCE_DIR}/../3rd/rtc_stack/src
	${CMAKE_CURRENT_SOURCE_DIR}/../3rd/rtc_stack/src/myrtc
	${CMAKE_CURRENT_SOURCE_DIR}/../3rd/rtc_stack/src/owt
	${CMAKE_CURRENT_SOURCE_DIR}/../3rd/rtc_stack/src/3rd/abseil-cpp
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

#subdirs()

