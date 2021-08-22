#include "media_404_handler.h"

#include "common/media_log.h"
#include "http/http_consts.h"
#include "http/http_stack.h"
#include "http/h/http_protocal.h"

namespace ma {

srs_error_t HttpNotFoundHandler::serve_http(IHttpResponseWriter* w, ISrsHttpMessage*) {
  return srs_go_http_error(w, SRS_CONSTS_HTTP_NotFound); 
}

srs_error_t srs_go_http_error(IHttpResponseWriter* w, int code) {
  std::string_view error = generate_http_status_text(code);

  MLOG_TRACE(error);
  w->header()->set_content_type("text/plain; charset=utf-8");
  w->header()->set_content_length(error.length());
  w->write_header(code);

  srs_error_t err = srs_success;
  
  if ((err = w->write((char*)error.data(), (int)error.length())) != srs_success) {
    return srs_error_wrap(err, "http write");
  }

  //return w->final_request();
  
  return err;
}

}