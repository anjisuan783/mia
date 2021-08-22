#include "connection/http_conn.h"

#include "common/media_log.h"
#include "h/media_return_code.h"
#include "http/http_consts.h"
#include "http/http_stack.h"
#include "http/h/http_message.h"
#include "networkbase.h"
#include "datapackage.h"
#include "handler/h/media_handler.h"
#include "connection/h/media_conn_mgr.h"

namespace ma {

// The filter http mux, directly serve the http CORS requests,
// while proxy to the worker mux for services.
class GsHttpCorsMux : public IGsHttpHandler
{
public:
  void initialize(IGsHttpHandler* worker, bool cros_enabled);

  srs_error_t serve_http(IHttpResponseWriter* w, ISrsHttpMessage* r) override;
  void conn_destroy(std::shared_ptr<IMediaConnection>) override {}
private:
  bool required{false};
  bool enabled{false};
  IGsHttpHandler* next{nullptr};
};


void GsHttpCorsMux::initialize(IGsHttpHandler* mux, bool cros_enabled) {
  next = mux;
  enabled = cros_enabled;
}

srs_error_t GsHttpCorsMux::serve_http(IHttpResponseWriter* w, ISrsHttpMessage* r) {
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

GsHttpConn::GsHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, 
                       IGsHttpHandler* mux)
    : reader_{std::move(fac->CreateReader(this))},
      writer_{std::move(fac->CreateWriter(false))},
      parser_{std::move(fac->CreateParser())},
      http_mux_{mux},
      cors_{std::make_unique<GsHttpCorsMux>()} {
  MLOG_TRACE("");
}

GsHttpConn::~GsHttpConn() {
  MLOG_TRACE("");
}

void GsHttpConn::Start() {
  MLOG_TRACE("");
  this->set_crossdomain_enabled(true);
  reader_->open();
}

srs_error_t GsHttpConn::set_crossdomain_enabled(bool v) {
  srs_error_t err = srs_success;

  // initialize the cors, which will proxy to mux.
  cors_->initialize(http_mux_, v);
  
  return err;
}

srs_error_t GsHttpConn::process_request(const std::string& body) {
  std::optional<ISrsHttpMessage*> result = parser_->parse_message(body);

  if(!result){
    return srs_error_new(kRtc_url_parse_failed, "parse_message failed");
  }

  std::unique_ptr<ISrsHttpMessage> object(*result);

  object->connection(shared_from_this());

  OS_INFO_TRACE_THIS("url:" << object->uri());

  srs_error_t err = srs_success;
  if ((err = cors_->serve_http(writer_.get(), result.value())) != srs_success) {
    return srs_error_wrap(err, "mux serve");
  }

  return err;
}

void GsHttpConn::on_disconnect() {
  MLOG_TRACE("");
  g_conn_mgr_.RemoveConnection(shared_from_this());
}

#if 0
GsRtcCode GsHttpConn::response(int code, const std::string& msg,
    const std::string& sdp, const std::string& msid) {
  header_.set("Connection", "Close");

  json::Object jroot;
  jroot["code"] = code;
  jroot["server"] = "ly rtc";
  //jroot["msg"] = msg;
  jroot["sdp"] = sdp;
  jroot["sessionid"] = msid;
  
  std::string jsonStr = json::Serialize(jroot);

  srs_error_t err = this->write(jsonStr.c_str(), jsonStr.length());

  int ret = kRtc_ok;
  if(err != srs_success){
    OS_ERROR_TRACE_THIS("http: multiple write_header calls, code=" << srs_error_desc(err));
    ret = srs_error_code(err);
    delete err;
  }

  assert(srs_success == final_request());

  return ret;
}
#endif

void GsHttpConn::Disconnect() {
  if(reader_){
    reader_->disconnect();
    reader_ = nullptr;
  }

  g_conn_mgr_.RemoveConnection(shared_from_this());
}

GsResponseOnlyHttpConn::GsResponseOnlyHttpConn(
    std::unique_ptr<IHttpProtocalFactory> fac, IGsHttpHandler* m)
    : GsHttpConn() {
  MLOG_TRACE("");
  reader_ = std::move(fac->CreateReader(this));
  writer_ = std::move(fac->CreateWriter(true));
  parser_ = std::move(fac->CreateParser());
  http_mux_ = m;
  cors_ = std::make_unique<GsHttpCorsMux>();
}

GsResponseOnlyHttpConn::~GsResponseOnlyHttpConn() {
  MLOG_TRACE("");
}


}

