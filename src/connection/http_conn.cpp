#include "connection/http_conn.h"

#include "h/media_return_code.h"
#include "http/http_consts.h"
#include "http/http_stack.h"
#include "http/h/http_message.h"
#include "handler/h/media_handler.h"
#include "connection/h/media_conn_mgr.h"

namespace ma {

// The filter http mux, directly serve the http CORS requests,
// while proxy to the worker mux for services.
class MediaHttpCorsMux : public IMediaHttpHandler
{
public:
  void initialize(IMediaHttpHandler* worker, bool cros_enabled);

  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter> w, 
                         std::shared_ptr<ISrsHttpMessage> r) override;
  void conn_destroy(std::shared_ptr<IMediaConnection>) override {}
private:
  bool required{false};
  bool enabled{false};
  IMediaHttpHandler* next{nullptr};
};


void MediaHttpCorsMux::initialize(IMediaHttpHandler* mux, bool cros_enabled) {
  next = mux;
  enabled = cros_enabled;
}

srs_error_t MediaHttpCorsMux::serve_http(
    std::shared_ptr<IHttpResponseWriter> w, std::shared_ptr<ISrsHttpMessage> r) {
  
  // If CORS enabled, and there is a "Origin" header, it's CORS.
  if (enabled) {
    SrsHttpHeader& h = r->header();
    required = !h.get("Origin").empty();
  }
  
  // When CORS required, set the CORS headers.
  if (required) {
    SrsHttpHeader* h = w->header();
    h->set("Access-Control-Allow-Origin", "*");
    h->set("Access-Control-Allow-Methods", "GET, POST, HEAD, PUT, DELETE, OPTIONS");
    h->set("Access-Control-Expose-Headers", "Server,range,Content-Length,Content-Range");
    h->set("Access-Control-Allow-Headers", 
           "origin,range,accept-encoding,referer,Cache-Control,"
           "X-Proxy-Authorization,X-Requested-With,Content-Type");
  }
  
  // handle the http options.
  if (r->is_http_options()) {
    w->header()->set_content_length(0);
    if (enabled) {
        w->write_header(SRS_CONSTS_HTTP_OK);
    } else {
        w->write_header(SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    return w->final_request();
  }
  
  assert(next);
  return next->serve_http(w, r);  
}

MDEFINE_LOGGER(MediaHttpConn, "MediaHttpConn");

MediaHttpConn::MediaHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, 
                       IMediaHttpHandler* mux)
    : reader_{std::move(fac->CreateRequestReader(this))},
      parser_{std::move(fac->CreateMessageParser())},
      http_mux_{mux},
      cors_{std::make_unique<MediaHttpCorsMux>()},
      factory_{std::move(fac)} {
  MLOG_TRACE("");
  parser_->initialize(HTTP_REQUEST);
}

MediaHttpConn::~MediaHttpConn() {
  MLOG_TRACE("");
}

void MediaHttpConn::Start() {
  this->set_crossdomain_enabled(true);
  reader_->open();
}

srs_error_t MediaHttpConn::set_crossdomain_enabled(bool v) {
  srs_error_t err = srs_success;

  // initialize the cors, which will proxy to mux.
  cors_->initialize(http_mux_, v);
  
  return err;
}

srs_error_t MediaHttpConn::process_request(std::string_view req) {
  MLOG_TRACE("");
  srs_error_t err = srs_success;
  std::shared_ptr<ISrsHttpMessage> msg;
  if((err = parser_->parse_message(req, msg)) != srs_success) {
    return srs_error_wrap(err, "parse_message failed");
  }

  auto writer = factory_->CreateResponseWriter(false);
  writer->open();
  if (msg) {
    msg->connection(shared_from_this());
    if ((err = cors_->serve_http(writer, msg)) != srs_success) {
      return srs_error_wrap(err, "mux serve");
    }
  }

  return err;
}

void MediaHttpConn::on_disconnect() {
  MLOG_TRACE("");
  g_conn_mgr_.RemoveConnection(shared_from_this());
}

void MediaHttpConn::Disconnect() {
  if(reader_){
    reader_->disconnect();
    reader_ = nullptr;
  }

  g_conn_mgr_.RemoveConnection(shared_from_this());
}

MDEFINE_LOGGER(MediaResponseOnlyHttpConn, "MediaResponseOnlyHttpConn");

MediaResponseOnlyHttpConn::MediaResponseOnlyHttpConn(
    std::unique_ptr<IHttpProtocalFactory> fac, IMediaHttpHandler* m)
    : MediaHttpConn(std::move(fac), m) {
  MLOG_TRACE("");
}

MediaResponseOnlyHttpConn::~MediaResponseOnlyHttpConn() {
  MLOG_TRACE("");
}

srs_error_t MediaResponseOnlyHttpConn::process_request(std::string_view req) {
  MLOG_TRACE("");
  srs_error_t err = srs_success;
  std::shared_ptr<ISrsHttpMessage> msg;
  if((err = parser_->parse_message(req, msg)) != srs_success) {
    return srs_error_wrap(err, "parse_message failed");
  }

  auto writer = factory_->CreateResponseWriter(true);
  if (msg) {
    msg->connection(shared_from_this());
    if ((err = cors_->serve_http(writer, msg)) != srs_success) {
      return srs_error_wrap(err, "mux serve");
    }
  }

  return err;
}

}

