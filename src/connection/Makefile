SRC_PATH = ../../..
BIN_PATH = ../../../
USER_DEPTH = $(SRC_PATH)
THIRD_PART=$(USER_DEPTH)/opensource

include $(SRC_PATH)/config/makeinclude/env.make
include $(SRC_PATH)/config/makeinclude/platform.make

USER_LIB = rtcapi

USER_SRCS = \
	json.cpp \
	srs_protocol_utility.cpp \
	srs_kernel_error.cpp \
	srs_kernel_consts.cpp \
	http_stack.cpp \
	http_message.cpp \
	http_conn.cpp \
	rtc_server.cpp

USER_INCLDIRS = -I$(SRC_PATH)/include \
  -I$(SRC_PATH)/include/platform \
  -I$(SRC_PATH)/platform/include \
  -I$(THIRD_PART)/include \
  -I$(SRC_PATH)/opensource/abseil \
  -I./h/

USER_DEFFLAGS =

USER_LINK_LIBS =

USER_OBJ_DIR = $(BIN_PATH)/bin/rtc_api

USER_LIB_DIR = $(BIN_PATH)/bin

include $(SRC_PATH)/config/makeinclude/all_in_one.make
