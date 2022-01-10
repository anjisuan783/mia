add_compile_options(-std=c++11)

set(WA_CMAKE_CXX_FLAGS "")

if(${WA_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-g -Wall -std=c++11 ${WA_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-g -Wall -O3 -std=c++11 ${WA_CMAKE_CXX_FLAGS}")
endif()

include_directories(
	../h
	..
)

set(
	SOURCE_FILES
	main.cpp
)

set(
	HEADER_FILES
)

add_executable(wa_example ${SOURCE_FILES} ${HEADER_FILES})

target_link_libraries(wa_example webrtc_agent  sdptransform erizo ${GLIB_LIBRARIES} ${Boost_LIBRARIES} ${SRTP} ${NICE} ${GTHREAD} ${SSL} ${CRYPTO} ${LIBS} ${LOG})

