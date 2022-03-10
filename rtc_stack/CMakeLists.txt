cmake_minimum_required(VERSION 2.8)

project(webrtc_agent)

find_package(PkgConfig)

function(test_lib LIB_NAME)
  if (${LIB_NAME} MATCHES "^.*-NOTFOUND")
    message(FATAL_ERROR "lib not found: " ${LIB_NAME} " check README")
    return()
  endif(${LIB_NAME} MATCHES "^.*-NOTFOUND")
endfunction(test_lib)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
    OUTPUT_VARIABLE GCC_VERSION)
  if (GCC_VERSION VERSION_LESS 7)
    message(FATAL_ERROR "gcc version must be >= 7 and is ${GCC_VERSION}")
  endif()

  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
    OUTPUT_VARIABLE CXX_VERSION)
  if (CXX_VERSION VERSION_LESS 7)
    message(FATAL_ERROR "g++ version must be >= 7 and is ${CXX_VERSION}")
  endif()
endif()

set(WA_CMAKE_CXX_FLAGS "-g ${WA_DWARF_TYPE} -std=gnu++17 -fPIC -Wall -DDCHECK_ALWAYS_ON -Wno-unused-variable -Wno-unused-but-set-variable")
set(WA_CMAKE_C_FLAGS "-g ${WA_DWARF_TYPE} -Wall -fPIC -Wno-unused-variable -Wno-unused-but-set-variable")

set(MYRTC_CMAKE_CXX_FLAGS "${WA_CMAKE_CXX_FLAGS} -fexceptions -DWEBRTC_POSIX -DWEBRTC_LINUX -DLINUX -DHAVE_STRLCPY=1 -DHAVE_SYS_TIME_H=1 -DTIME_WITH_SYS_TIME_H=1 -D_LIBCPP_ABI_UNSTABLE -DWEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS")

if(${WA_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-O0 -ggdb ${WA_CMAKE_CXX_FLAGS} ${MYRTC_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${WA_CMAKE_CXX_FLAGS} ${MYRTC_CMAKE_CXX_FLAGS} -DNDEBUG")
endif()

message("Generating wa ${WA_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")

#global variable
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/")
set(THIRD_PARTY_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../build/libdeps/build/include/")
set(THIRD_PARTY_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../build/libdeps/build/lib/")


## Depencencies

# GLIB
find_package(Glib REQUIRED)
include_directories(${GLIB_INCLUDE_DIRS})

# BOOST
#set (BOOST_LIBS thread regex system)
#find_package(Boost COMPONENTS ${BOOST_LIBS} REQUIRED)

# GTHREAD
find_library(GTHREAD gthread-2.0 HINTS "${THIRD_PARTY_LIB}")
test_lib(${GTHREAD})

# SRTP
find_library(SRTP libsrtp2.a HINTS "${THIRD_PARTY_LIB}")
test_lib(${SRTP})

# OPENSSL
find_library(SSL libssl.a HINTS "${THIRD_PARTY_LIB}")
test_lib(${SSL})
find_library(CRYPTO libcrypto.a HINTS "${THIRD_PARTY_LIB}")
test_lib(${CRYPTO})

# LOG4CXX
find_library(LOG log4cxx)
test_lib(${LOG})

# NICE
find_library(NICE libnice.a HINTS "${THIRD_PARTY_LIB}")
test_lib(${NICE})

set(WA_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/
	${CMAKE_CURRENT_SOURCE_DIR}/3rd/
	${CMAKE_CURRENT_SOURCE_DIR}/myrtc/
	${CMAKE_CURRENT_SOURCE_DIR}/owt/
	${CMAKE_CURRENT_SOURCE_DIR}/3rd/abseil-cpp/
	${CMAKE_CURRENT_SOURCE_DIR}/3rd/libevent/
	${CMAKE_CURRENT_SOURCE_DIR}/3rd/abseil-cpp/
	#${CMAKE_CURRENT_SOURCE_DIR}/erizo/
	${GLIB_INCLUDE_DIRS}
	${THIRD_PARTY_INCLUDE}
)

set(
	HEADER_FILES
	h/rtc_stack_api.h
	h/rtc_return_value.h
)

set(
	UTIL_SOURCES
	utils/IOWorker.cpp
	utils/Worker.cpp
	utils/Clock.cpp
)

set(WA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wa")

file(GLOB_RECURSE WA_SOURCE_FILE 
	"${WA_SOURCE_DIR}/*.h" 
	"${WA_SOURCE_DIR}/*.hpp" 
	"${WA_SOURCE_DIR}/*.cpp" 
	"${WA_SOURCE_DIR}/*.cc" 
	"${WA_SOURCE_DIR}/*.c"
)

set(ERIZO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/erizo")

file(GLOB_RECURSE ERIZO_SOURCES "${ERIZO_SOURCE_DIR}/*.h" "${ERIZO_SOURCE_DIR}/*.hpp" "${ERIZO_SOURCE_DIR}/*.cpp" "${ERIZO_SOURCE_DIR}/*.cc" "${ERIZO_SOURCE_DIR}/*.c")

set(OWT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/owt")

file(GLOB_RECURSE OWT_SOURCES "${OWT_SOURCE_DIR}/*.h" "${OWT_SOURCE_DIR}/*.hpp" "${OWT_SOURCE_DIR}/*.cpp" "${OWT_SOURCE_DIR}/*.cc" "${OWT_SOURCE_DIR}/*.c")

set(MYRTC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/myrtc")

file(
	GLOB_RECURSE MYRTC_SOURCES
	"${MYRTC_SOURCE_DIR}/api/*.h" "${MYRTC_SOURCE_DIR}/api/*.cc"
	"${MYRTC_SOURCE_DIR}/audio/*.h" "${MYRTC_SOURCE_DIR}/audio/*.cc"
	"${MYRTC_SOURCE_DIR}/call/*.h" "${MYRTC_SOURCE_DIR}/call/*.cc"
	"${MYRTC_SOURCE_DIR}/cc/*.h" "${MYRTC_SOURCE_DIR}/cc/*.cc"
	"${MYRTC_SOURCE_DIR}/logging/*.h" "${MYRTC_SOURCE_DIR}/logging/*.cc"
	"${MYRTC_SOURCE_DIR}/media/*.h" "${MYRTC_SOURCE_DIR}/media/*.cc"
	"${MYRTC_SOURCE_DIR}/module/*.h" "${MYRTC_SOURCE_DIR}/module/*.cc"
	"${MYRTC_SOURCE_DIR}/pacing/*.h" "${MYRTC_SOURCE_DIR}/pacing/*.cc"
	"${MYRTC_SOURCE_DIR}/rbe/*.h" "${MYRTC_SOURCE_DIR}/rbe/*.cc"
	"${MYRTC_SOURCE_DIR}/utility/*.h" "${MYRTC_SOURCE_DIR}/utility/*.cc"
	"${MYRTC_SOURCE_DIR}/rtc_base/*.h" "${MYRTC_SOURCE_DIR}/rtc_base/*.cc"
	"${MYRTC_SOURCE_DIR}/rtp_rtcp/*.h" "${MYRTC_SOURCE_DIR}/rtp_rtcp/*.cc"
	"${MYRTC_SOURCE_DIR}/utility/*.h" "${MYRTC_SOURCE_DIR}/utility/*.cc"
	"${MYRTC_SOURCE_DIR}/video/*.h" "${MYRTC_SOURCE_DIR}/video/*.cc"
	"${MYRTC_SOURCE_DIR}/network/*.h" "${MYRTC_SOURCE_DIR}/network/*.cc"
)

#message("wa source ${WA_SOURCE_FILE}")

add_library(wa STATIC ${UTIL_SOURCES} ${WA_SOURCE_FILE} ${ERIZO_SOURCES} ${OWT_SOURCES} ${MYRTC_SOURCES} ${HEADER_FILES})
#add_library(wa SHARED ${UTIL_SOURCES} ${WA_SOURCES} ${ERIZO_SOURCES} ${OWT_SOURCES} ${MYRTC_SOURCES} ${HEADER_FILES})

subdirs(
	3rd/libsdptransform
	3rd/abseil-cpp
	3rd/libevent
	test
	#${CMAKE_CURRENT_SOURCE_DIR}/example
)

#message("Libs ${SRTP} ${NICE} ${GTHREAD} ${SSL} ${CRYPTO} ${LIBS} ${LOG}")

target_link_libraries(
	wa
	${${MODULE_NAME}_EXTRA_LDFLAGS}
	"-Wl,--whole-archive"
	sdptransform
	"-Wl,--no-whole-archive"
	${Boost_LIBRARIES}
	${LIBS}
	${LOG}
)

install(TARGETS wa DESTINATION lib)
install(FILES ${HEADER_FILES} DESTINATION include/h)
