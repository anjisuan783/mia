#include "handler/media_mux.h"

#include "common/srs_kernel_error.h"
#include "common/media_log.h"
#include "utils/json.h"
#include "datapackage.h"
#include "connection/http_conn.h"
#include "http/http_stack.h"
#include "http/h/http_message.h"
#include "handler/media_flv_handler.h"
#include "connection/h/media_conn_mgr.h"
#include "handler/media_404_handler.h"
#include "rtmp/media_req.h"

namespace ma {

class GsHttpPlayHandler : public IGsHttpHandler {
 public: 
  GsHttpPlayHandler() { }
 private:
  void conn_destroy(std::shared_ptr<IMediaConnection>) override { }
  srs_error_t serve_http(IHttpResponseWriter*, 
                         ISrsHttpMessage*) override;
};

class GsHttpPublishHandler : public IGsHttpHandler {
 public:
  GsHttpPublishHandler() { }
 private:
  void conn_destroy(std::shared_ptr<IMediaConnection> conn) override { }
  srs_error_t serve_http(IHttpResponseWriter*, 
                         ISrsHttpMessage*) override;
};

srs_error_t GsHttpPlayHandler::serve_http(
    IHttpResponseWriter* writer, ISrsHttpMessage*) {
  srs_error_t err = srs_success;
  return err;
}

/*
request
{
  streamurl: 'webrtc://domain/app/stream',
  sdp: string,  // offer sdp
  clientip: string 
}

response
{
  code:int,
  msg:string,
  data:{
    sdp:string,   // answer sdp 
    sessionid:string // 该路推流的唯一id
  }
}
rtc_server.h

@see https://github.com/rtcdn/rtcdn-draft
*/
srs_error_t GsHttpPublishHandler::serve_http(
    IHttpResponseWriter* writer, ISrsHttpMessage* msg) {
  assert(msg->is_http_post());
  assert(msg->path() == RTC_PUBLISH_PREFIX);

  json::Object jobj = json::Deserialize(msg->get_body());

  std::string streamurl = std::move((std::string)jobj["streamurl"]);
  std::string sdp = std::move((std::string)jobj["sdp"]);
  std::string clientip = std::move((std::string)jobj["clientip"]);

  return srs_success;
}

GsHttpServeMux::GsHttpServeMux() {
  entry_[RTC_PALY_PREFIX] = new GsHttpPlayHandler;
  entry_[RTC_PUBLISH_PREFIX] = new GsHttpPublishHandler;
  flv_sevice_ = std::move(std::make_unique<GsFlvPlayHandler>());

  g_conn_mgr_.signal_destroy_conn_.connect(this, &GsHttpServeMux::conn_destroy);
}

GsHttpServeMux::~GsHttpServeMux() {
  for(auto& x : entry_){
    delete x.second;
  }
}

srs_error_t GsHttpServeMux::serve_http(
    IHttpResponseWriter* writer, ISrsHttpMessage* msg) {
  MLOG_TRACE(msg->path());
/*  
  auto found = entry_.find(msg->path());
  if(found == entry_.end()){
    static HttpNotFoundHandler s_hangler_404;
    return s_hangler_404.serve_http(writer, msg);
  }
  return found->second->serve_http(writer, msg);
*/
  return flv_sevice_->serve_http(writer, msg);
}

srs_error_t GsHttpServeMux::mount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {
  return flv_sevice_->mount_service(s, r);
}

void GsHttpServeMux::unmount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {
  flv_sevice_->unmount_service(s, r);
}

void GsHttpServeMux::conn_destroy(std::shared_ptr<IMediaConnection> conn) {
  //entry_[LIVE_PLAY_PREFIX]->conn_destroy(conn);
  flv_sevice_->conn_destroy(conn);
}

std::unique_ptr<IGsHttpHandler> ServerHandlerFactor::Create() {
  return std::make_unique<GsHttpServeMux>();
}

}
