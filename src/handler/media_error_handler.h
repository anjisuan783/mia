//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_ERROR_HANDLER_H__
#define __MEDIA_ERROR_HANDLER_H__

#include "common/media_kernel_error.h"
#include "handler/h/media_handler.h"

namespace ma {

// Forbiddon replies to the request with an HTTP 403  error.
class HttpForbiddonHandler : public IMediaHttpHandler {
 public:
  HttpForbiddonHandler() = default;
  ~HttpForbiddonHandler() override = default;
  
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter> writer, 
                         std::shared_ptr<ISrsHttpMessage> msg) override;
 private:
  srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                            std::shared_ptr<MediaRequest> r) override { 
    return srs_success;
  }
  
  void unmount_service(std::shared_ptr<MediaSource> s, 
                       std::shared_ptr<MediaRequest> r) override { }
  void conn_destroy(std::shared_ptr<IMediaConnection>) override { }
};

// NotFound replies to the request with an HTTP 404 not found error.
class HttpNotFoundHandler : public IMediaHttpHandler {
 public:
  HttpNotFoundHandler() = default;
  ~HttpNotFoundHandler() override = default;
  
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter> writer, 
                         std::shared_ptr<ISrsHttpMessage> msg) override;
 private:
  srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                            std::shared_ptr<MediaRequest> r) override {
    return srs_success;
  }
  
  void unmount_service(std::shared_ptr<MediaSource> s, 
                       std::shared_ptr<MediaRequest> r) override { }
  void conn_destroy(std::shared_ptr<IMediaConnection>) override { }  
};

srs_error_t srs_go_http_error(IHttpResponseWriter* w, int code);

} //namespace ma

#endif //!__MEDIA_ERROR_HANDLER_H__

