cmake_minimum_required(VERSION 2.6)

project (MYRTC)

set(MYRTC_COMMON_CMAKE_CXX_FLAGS "${WA_CMAKE_CXX_FLAGS} -fexceptions -DWEBRTC_POSIX -DWEBRTC_LINUX -DLINUX -DNOLINUXIF -DNO_REG_RPC=1 -DHAVE_VFPRINTF=1 -DRETSIGTYPE=void -DNEW_STDIO -DHAVE_STRDUP=1 -DHAVE_STRLCPY=1 -DHAVE_LIBM=1 -DHAVE_SYS_TIME_H=1 -DTIME_WITH_SYS_TIME_H=1 -D_LIBCPP_ABI_UNSTABLE")
set(MYRTC_DEBUG_CMAKE_CXX_FLAGS "${WA_CMAKE_CXX_FLAGS} ${MYRTC_COMMON_CMAKE_CXX_FLAGS}")
set(MYRTC_RELEASE_CMAKE_CXX_FLAGS "${WA_CMAKE_CXX_FLAGS} ${MYRTC_COMMON_CMAKE_CXX_FLAGS} -DNDEBUG")

if(${MYRTC_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-O0 -ggdb ${MYRTC_DEBUG_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${MYRTC_RELEASE_CMAKE_CXX_FLAGS}")  
endif()

message("Generating MYRTC ${MYRTC_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	"${CMAKE_CURRENT_SOURCE_DIR}/../libevent/"
	"${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp/"
)

file(
	GLOB_RECURSE MYRTC_SOURCES
	"${MYRTC_SOURCE_DIR}/api/*.h" "${MYRTC_SOURCE_DIR}/api/*.cc"
	"${MYRTC_SOURCE_DIR}/audio/*.h" "${MYRTC_SOURCE_DIR}/audio/*.cc"
	"${MYRTC_SOURCE_DIR}/call/*.h" "${MYRTC_SOURCE_DIR}/call/*.cc"
	"${MYRTC_SOURCE_DIR}/congestion_controller/*.h" "${MYRTC_SOURCE_DIR}/congestion_controller/*.cc"
	"${MYRTC_SOURCE_DIR}/logging/*.h" "${MYRTC_SOURCE_DIR}/logging/*.cc"
	"${MYRTC_SOURCE_DIR}/media/*.h" "${MYRTC_SOURCE_DIR}/media/*.cc"
	"${MYRTC_SOURCE_DIR}/module/*.h" "${MYRTC_SOURCE_DIR}/module/*.cc"
	"${MYRTC_SOURCE_DIR}/pacing/*.h" "${MYRTC_SOURCE_DIR}/pacing/*.cc"
	"${MYRTC_SOURCE_DIR}/remote_bitrate_estimator/*.h" "${MYRTC_SOURCE_DIR}/remote_bitrate_estimator/*.cc"
	"${MYRTC_SOURCE_DIR}/utility/*.h" "${MYRTC_SOURCE_DIR}/utility/*.cc"
	"${MYRTC_SOURCE_DIR}/rtc_base/*.h" "${MYRTC_SOURCE_DIR}/rtc_base/*.cc"
	"${MYRTC_SOURCE_DIR}/rtp_rtcp/*.h" "${MYRTC_SOURCE_DIR}/rtp_rtcp/*.cc"
	"${MYRTC_SOURCE_DIR}/utility/*.h" "${MYRTC_SOURCE_DIR}/utility/*.cc"
	"${MYRTC_SOURCE_DIR}/video/*.h" "${MYRTC_SOURCE_DIR}/video/*.cc"
	"${MYRTC_SOURCE_DIR}/network/*.h" "${MYRTC_SOURCE_DIR}/network/*.cc"
)

add_library(myrtc STATIC ${MYRTC_SOURCES})
#add_library(myrtc SHARED ${MYRTC_SOURCES})

target_link_libraries(
	myrtc
	"-Wl,--whole-archive"
	event
	absl
	"-Wl,--no-whole-archive"
)

