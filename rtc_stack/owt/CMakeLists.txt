cmake_minimum_required(VERSION 2.6)

project (OWT)

set(OWT_CMAKE_CXX_FLAGS "${WA_CMAKE_CXX_FLAGS} -fexceptions -DWEBRTC_POSIX -DWEBRTC_LINUX -DLINUX -DNOLINUXIF -DNO_REG_RPC=1 -DHAVE_VFPRINTF=1 -DRETSIGTYPE=void -DNEW_STDIO -DHAVE_STRDUP=1 -DHAVE_STRLCPY=1 -DHAVE_LIBM=1 -DHAVE_SYS_TIME_H=1 -DTIME_WITH_SYS_TIME_H=1 -D_LIBCPP_ABI_UNSTABLE")

if(${OWT_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-O0 -ggdb ${OWT_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${OWT_CMAKE_CXX_FLAGS}")  
endif()

message("Generating OWT ${OWT_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/../myrtc/"
	"${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/"
	"${CMAKE_CURRENT_SOURCE_DIR}/../erizo/src/erizo/"
)

#get_property(dirs DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
#message("owt >>> include_dirs=${dirs}")

set(OWT_BASE_SOURCE
	${OWT_SOURCE_DIR}/common/JobTimer.cpp
	${OWT_SOURCE_DIR}/owt_base/AudioFrameConstructor.cpp
	${OWT_SOURCE_DIR}/owt_base/AudioFramePacketizer.cpp
	${OWT_SOURCE_DIR}/owt_base/AudioUtilitiesNew.cpp
	${OWT_SOURCE_DIR}/owt_base/MediaFramePipeline.cpp
	${OWT_SOURCE_DIR}/owt_base/VideoFrameConstructor.cpp
	${OWT_SOURCE_DIR}/owt_base/VideoFramePacketizer.cpp
	${OWT_SOURCE_DIR}/owt_base/SsrcGenerator.cc
	${OWT_SOURCE_DIR}/owt_base/TaskRunnerPool.cpp
)

set(OWT_ADAPTER_SOURCE
	${OWT_SOURCE_DIR}/rtc_adapter/AudioSendAdapter.cc
	${OWT_SOURCE_DIR}/rtc_adapter/RtcAdapter.cc
	${OWT_SOURCE_DIR}/rtc_adapter/VideoReceiveAdapter.cc
	${OWT_SOURCE_DIR}/rtc_adapter/VideoSendAdapter.cc
	${OWT_SOURCE_DIR}/rtc_adapter/thread/StaticTaskQueueFactory.cc
)

add_library(owt STATIC ${OWT_BASE_SOURCE} ${OWT_ADAPTER_SOURCE})
#add_library(owt SHARED ${OWT_BASE_SOURCE} ${OWT_ADAPTER_SOURCE})

#target_link_libraries(owt erizo myrtc)

