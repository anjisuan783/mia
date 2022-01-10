cmake_minimum_required(VERSION 2.6)

project (ERIZO)

if(${ERIZO_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-O0 -ggdb ${WA_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${WA_CMAKE_CXX_FLAGS}")
endif()

message("Generating erizo ${ERIZO_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")

#set(EXECUTABLE_OUTPUT_PATH "")

include_directories(AFTER
	${GLIB_INCLUDE_DIRS}
	${THIRD_PARTY_INCLUDE}
	${CMAKE_CURRENT_SOURCE_DIR}
)

#get_property(dirs DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
#message("erizo >>> include_dirs=${dirs}")

#add_library(erizo SHARED ${ERIZO_SOURCES_FILES})

add_library(erizo STATIC ${ERIZO_SOURCES_FILES})

#message("Libs ${SRTP} ${NICE} ${GTHREAD} ${SSL} ${CRYPTO} ${LIBS} ${LOG}")

target_link_libraries(
	erizo
	"-Wl,--whole-archive"
	${NICE}
	${SRTP}
	${SSL} 
	${CRYPTO}
	"-Wl,--no-whole-archive"
	${GLIB_LIBRARIES} 
	${Boost_LIBRARIES} 
	${GTHREAD}
	${LIBS} 
	${LOG}
)

