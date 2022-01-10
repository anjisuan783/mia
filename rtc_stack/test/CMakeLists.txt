set(CMAKE_CXX_FLAGS ${WA_CMAKE_CXX_FLAGS})

# gtest
find_library(LIBGTEST NAMES gtest gmock HINTS ${THIRD_PARTY_LIB} REQUIRED)

# gtestmain
find_library(LIBGTESTMAIN NAMES gtest_main HINTS ${THIRD_PARTY_LIB} REQUIRED)

#message("test case Libs ${LIBGTEST}")

include_directories(
	${THIRD_PARTY_INCLUDE}
	../h
	..
)

set(
	SOURCE_FILES
	sdp_processor_ut.cpp
)

set(
	HEADER_FILES
)

add_executable(test_rtc ${SOURCE_FILES} ${HEADER_FILES})

target_link_libraries(
	test_rtc
	wa
	absl
	${GLIB}
	${LIBS}
	${LOG}
	${GTHREAD}
	gthread-2.0 
	gio-2.0
	gobject-2.0
	glib-2.0
	pthread
	${THIRD_PARTY_LIB}/libgtest.a
	${THIRD_PARTY_LIB}/libgtest_main.a
)
