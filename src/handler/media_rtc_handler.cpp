#include "handler/media_rtc_handler.h"

#include "common/media_log.h"
#include "utils/json.h"
#include "http/h/http_message.h"
#include "connection/h/conn_interface.h"

namespace ma {

class GsHttpPlayHandler : public IMediaHttpHandler {
 public: 
  GsHttpPlayHandler() { }
 private:
  void conn_destroy(std::shared_ptr<IMediaConnection>) override { }
  srs_error_t serve_http(IHttpResponseWriter*, 
                         ISrsHttpMessage*) override;
};

class GsHttpPublishHandler : public IMediaHttpHandler {
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

MediaHttpRtcServeMux::MediaHttpRtcServeMux()
//  : api_(wa::AgentFactory().create_agent()) 
{
  
}

MediaHttpRtcServeMux::~MediaHttpRtcServeMux() {
}

srs_error_t MediaHttpRtcServeMux::serve_http(IHttpResponseWriter*, ISrsHttpMessage*) {
  srs_error_t result = srs_success;
  return result;
}

void MediaHttpRtcServeMux::conn_destroy(std::shared_ptr<IMediaConnection>) {
}

}

