#cmake_minimum_required(VERSION 3.0)

project(libevent)

# VERSION 1.4.15

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
  if (GCC_VERSION VERSION_LESS 6.1)
    message(FATAL_ERROR "gcc version must be >= 6.1 and is ${GCC_VERSION}")
  endif()

  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE CXX_VERSION)
  if (CXX_VERSION VERSION_LESS 6.1)
    message(FATAL_ERROR "g++ version must be >= 6.1 and is ${CXX_VERSION}")
  endif()
endif()

set(EVENT_CMAKE_C_FLAGS "${WA_CMAKE_C_FLAGS} -DHAVE_CONFIG_H -D__linux__")

if(${WA_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_C_FLAGS "-O0 -ggdb ${EVENT_CMAKE_C_FLAGS}")
  message("Generating event DEBUG project with ${EVENT_CMAKE_C_FLAGS}")
else()
  set(CMAKE_C_FLAGS "-O3 ${EVENT_CMAKE_C_FLAGS}")
  message("Generating event RELEASE project with ${EVENT_CMAKE_C_FLAGS}")
endif()

set(EVENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(
	${EVENT_SOURCE_DIR}
	${EVENT_SOURCE_DIR}/linux
)

set(
	SOURCE_FILES
    buffer.c
    evbuffer.c
    evdns.c
    event.c
    event_tagging.c
    evrpc.c
    evutil.c
    http.c
    log.c
    poll.c
    select.c
    signal.c
    strlcpy.c
    epoll.c
)

set(
	HEADER_FILES
	linux/config.h
    linux/event-config.h
	strlcpy-internal.h
    min_heap.h
    http-internal.h
    evutil.h
    evsignal.h
    evrpc-internal.h
    evdns.h
    event-config.h
    event-internal.h
    evhttp.h
    evrpc.h
    event.h
    log.h
)

add_library(event STATIC ${SOURCE_FILES} ${HEADER_FILES})

