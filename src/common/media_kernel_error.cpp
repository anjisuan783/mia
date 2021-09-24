//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include "common/media_kernel_error.h"

#include <errno.h>
#include <sstream>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>

#define gettid() syscall(__NR_gettid)

bool srs_is_system_control_error(srs_error_t err)
{
  int error_code = srs_error_code(err);
  return error_code == ERROR_CONTROL_RTMP_CLOSE
      || error_code == ERROR_CONTROL_REPUBLISH
      || error_code == ERROR_CONTROL_REDIRECT;
}

bool srs_is_client_gracefully_close(srs_error_t err)
{
  int error_code = srs_error_code(err);
  return error_code == ERROR_SOCKET_READ
      || error_code == ERROR_SOCKET_READ_FULLY
      || error_code == ERROR_SOCKET_WRITE;
}

bool srs_is_server_gracefully_close(srs_error_t err)
{
  int code = srs_error_code(err);
  return code == ERROR_HTTP_STREAM_EOF;
}

SrsCplxError::SrsCplxError()
{
  code = ERROR_SUCCESS;
  wrapped = NULL;
  rerrno = line = 0;
}

SrsCplxError::~SrsCplxError()
{
  srs_freep(wrapped);
}

std::string SrsCplxError::description() {
  if (desc.empty()) {
    std::stringstream ss;
    ss << "code=" << code;

    SrsCplxError* next = this;
    while (next) {
      ss << " : " << next->msg;
      next = next->wrapped;
    }
    ss << std::endl;

    next = this;
    while (next) {
      ss /*<< "thread [" << getpid() << "][" << next->tid << "]: "*/
      << next->func << "() [" << next->file << ":" << next->line << "]"
      << "[errno=" << next->rerrno << "]";

      next = next->wrapped;

      if (next) {
        ss << std::endl;
      }
    }

    desc = ss.str();
  }

  return desc;
}

std::string SrsCplxError::summary() {
  if (_summary.empty()) {
    std::stringstream ss;

    SrsCplxError* next = this;
    while (next) {
        ss << " : " << next->msg;
        next = next->wrapped;
    }

    _summary = ss.str();
  }

  return _summary;
}

SrsCplxError* SrsCplxError::create(const char* func, 
                                   const char* file, 
                                   int line, 
                                   int code, 
                                   const char* fmt, ...) {
  int rerrno = (int)errno;

  va_list ap;
  va_start(ap, fmt);
  static char buffer[4096];
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);
  
  SrsCplxError* err = new SrsCplxError();
  
  err->func = func;
  err->file = file;
  err->line = line;
  err->code = code;
  err->rerrno = rerrno;
  err->msg = buffer;
  err->wrapped = NULL;
  err->tid = gettid();
  return err;
}

SrsCplxError* SrsCplxError::wrap(const char* func, 
                                 const char* file, 
                                 int line, 
                                 SrsCplxError* v, 
                                 const char* fmt, ...) {
  int rerrno = (int)errno;
  
  va_list ap;
  va_start(ap, fmt);
  static char buffer[4096];
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);
  
  SrsCplxError* err = new SrsCplxError();
  
  err->func = func;
  err->file = file;
  err->line = line;
  if (v) {
      err->code = v->code;
  }
  err->rerrno = rerrno;
  err->msg = buffer;
  err->wrapped = v;
  err->tid = gettid();
  return err;
}

SrsCplxError* SrsCplxError::success() {
  return NULL;
}

SrsCplxError* SrsCplxError::copy(SrsCplxError* from)
{
  if (from == srs_success) {
    return srs_success;
  }
  
  SrsCplxError* err = new SrsCplxError();
  
  err->code = from->code;
  err->wrapped = srs_error_copy(from->wrapped);
  err->msg = from->msg;
  err->func = from->func;
  err->file = from->file;
  err->line = from->line;
  err->tid = from->tid;
  err->rerrno = from->rerrno;
  err->desc = from->desc;
  
  return err;
}

std::string SrsCplxError::description(SrsCplxError* err)
{
  return err? err->description() : "Success";
}

std::string SrsCplxError::summary(SrsCplxError* err)
{
  return err? err->summary() : "Success";
}

int SrsCplxError::error_code(SrsCplxError* err)
{
  return err? err->code : ERROR_SUCCESS;
}

