//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "media_error_handler.h"

#include "http/http_consts.h"
#include "http/http_stack.h"
#include "http/h/http_protocal.h"

namespace ma {

srs_error_t HttpForbiddonHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> w, std::shared_ptr<ISrsHttpMessage>) {
  return srs_go_http_error(w.get(), SRS_CONSTS_HTTP_Forbidden); 
}

srs_error_t HttpNotFoundHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> w, std::shared_ptr<ISrsHttpMessage>) {
  return srs_go_http_error(w.get(), SRS_CONSTS_HTTP_NotFound); 
}

srs_error_t srs_go_http_error(IHttpResponseWriter* w, int code) {
  std::string_view error = generate_http_status_text(code);

  w->header()->set_content_type("text/plain; charset=utf-8");
  w->header()->set_content_length(error.length());
  w->write_header(code);

  srs_error_t err = srs_success;
  
  if ((err = w->write((char*)error.data(), (int)error.length())) != srs_success) {
    return srs_error_wrap(err, "http write");
  }

  w->final_request();
  return err;
}

} //namespace ma

