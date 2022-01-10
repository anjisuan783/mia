cmake_minimum_required(VERSION 2.6)

project(sdptransform)

if(${WA_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -O0 -ggdb ${WA_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -O3 ${WA_CMAKE_CXX_FLAGS}")
endif()

message("Generating libsdptransform ${WA_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")

#link_libraries("-lboost_regex")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(
  SOURCE_FILES
  src/grammar.cpp
  src/parser.cpp
  src/writer.cpp
)

set(
  HEADER_FILES
  include/sdptransform.hpp
  include/json.hpp
)

add_library(sdptransform STATIC ${SOURCE_FILES} ${HEADER_FILES})

#install(TARGETS sdptransform DESTINATION lib)
#install(FILES ${HEADER_FILES} DESTINATION include/sdptransform)
