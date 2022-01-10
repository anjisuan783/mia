#
# Copyright 2017 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Most widely used distributions have cmake 3.5 or greater available as of March
# 2019.  A notable exception is RHEL-7 (CentOS7).  You can install a current
# version of CMake by first installing Extra Packages for Enterprise Linux
# (https://fedoraproject.org/wiki/EPEL#Extra_Packages_for_Enterprise_Linux_.28EPEL.29)
# and then issuing `yum install cmake3` on the command line.
cmake_minimum_required(VERSION 2.6)

# Compiler id for Apple Clang is now AppleClang.
#cmake_policy(SET CMP0025 NEW)

# if command can use IN_LIST
#cmake_policy(SET CMP0057 NEW)

# Project version variables are the empty std::string if version is unspecified
#cmake_policy(SET CMP0048 NEW)

project(absl CXX)

# when absl is included as subproject (i.e. using add_subdirectory(abseil-cpp))
# in the source tree of a project that uses it, install rules are disabled.
if(NOT "^${CMAKE_SOURCE_DIR}$" STREQUAL "^${PROJECT_SOURCE_DIR}$")
  set(ABSL_ENABLE_INSTALL FALSE)
else()
  set(ABSL_ENABLE_INSTALL TRUE)
endif()

set(ABSL_CMAKE_CXX_FLAGS "${WA_CMAKE_CXX_FLAGS} -DABSL_ALLOCATOR_NOTHROW=1")

if(${WA_BUILD_TYPE} STREQUAL "debug")
  set(CMAKE_CXX_FLAGS "-O0 -ggdb ${ABSL_CMAKE_CXX_FLAGS}")
else()
  set(CMAKE_CXX_FLAGS "-O3 ${ABSL_CMAKE_CXX_FLAGS}")
endif()
message("Generating absl ${MYRTC_BUILD_TYPE} project with ${CMAKE_CXX_FLAGS}")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

#add_subdirectory(absl)

set(ABSL_SOURCE_LIST 
	${CMAKE_CURRENT_SOURCE_DIR}/absl/numeric/int128.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/types/bad_variant_access.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/types/bad_optional_access.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/spinlock_wait.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/cycleclock.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/raw_logging.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/spinlock.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/sysinfo.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/thread_identity.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/unscaledcycleclock.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/dynamic_annotations.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/internal/throw_delegate.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/base/log_severity.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/internal/utf8.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/internal/ostringstream.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/ascii.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/charconv.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/escaping.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/internal/memutil.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/match.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/internal/charconv_bigint.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/internal/charconv_parse.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/numbers.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/str_cat.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/str_replace.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/str_split.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/string_view.cc
	${CMAKE_CURRENT_SOURCE_DIR}/absl/strings/substitute.cc
)

add_library(absl STATIC ${ABSL_SOURCE_LIST})

