//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.


#ifndef __MEDIA_NOT_FOUND_HANDLER_H__
#define __MEDIA_NOT_FOUND_HANDLER_H__

#include "common/srs_kernel_error.h"
#include "handler/h/media_handler.h"

namespace ma {

class IHttpResponseWriter;
class ISrsHttpMessage;

srs_error_t srs_go_http_error(IHttpResponseWriter* w, int code);


// NotFound replies to the request with an HTTP 404 not found error.
class HttpNotFoundHandler : public IGsHttpHandler {
 public:
  HttpNotFoundHandler() = default;
  virtual ~HttpNotFoundHandler() = default;

  srs_error_t serve_http(IHttpResponseWriter*, ISrsHttpMessage*) override;
 private:
  void conn_destroy(std::shared_ptr<IMediaConnection>) override { }
};

}

#endif //!__MEDIA_NOT_FOUND_HANDLER_H__

