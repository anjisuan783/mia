#include "handler/media_rtc_handler.h"

#include <optional>

#include "h/rtc_return_value.h"

#include "common/media_log.h"
#include "utils/json.h"
#include "http/h/http_message.h"
#include "connection/h/conn_interface.h"
#include "media_server.h"
#include "handler/media_rtc_source.h"
#include "handler/media_404_handler.h"

namespace ma {

#define RTC_PUBLISH_HANDLER 0
#define RTC_PLAY_HANDLER 1

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("MediaHttpRtcServer");

class MediaHttpPlayHandler : public IMediaHttpHandler {
 public: 
  MediaHttpPlayHandler(wa::rtc_api* p) : rtc_api_(p){
  }
 private:
  void conn_destroy(std::shared_ptr<IMediaConnection>) override { }
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter>, 
                         std::shared_ptr<ISrsHttpMessage>) override;
 private:
  wa::rtc_api* rtc_api_;
};

srs_error_t MediaHttpPlayHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> writer, std::shared_ptr<ISrsHttpMessage>) {
  srs_error_t err = srs_success;
  return err;
}

//MediaHttpPublishHandler
class MediaHttpPublishHandler : public IMediaHttpHandler {
 public:
  MediaHttpPublishHandler(wa::rtc_api* p) : rtc_api_{p} {
  }
 private:
  void conn_destroy(std::shared_ptr<IMediaConnection> conn) override { }
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter>, 
                         std::shared_ptr<ISrsHttpMessage>) override;

 private:
  bool IsExisted(const std::string& id);

 private:
  std::mutex source_lock_;
  std::map<std::string, std::shared_ptr<MediaRtcSource>> sources_;

  wa::rtc_api* rtc_api_;
};

bool MediaHttpPublishHandler::IsExisted(const std::string& id) {
  bool found = true;
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    if(sources_.find(id) == sources_.end()) {
      found = false;
    }
  }

  return found;
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

srs_error_t MediaHttpPublishHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> writer, std::shared_ptr<ISrsHttpMessage> msg) {
  assert(msg->get_body().length() == msg->content_length());
  assert(msg->is_http_post());
  assert(msg->path() == RTC_PUBLISH_PREFIX);
  assert(msg->is_body_eof());

  static HttpForbiddonHandler s_handler_403;
  static HttpNotFoundHandler  s_handler_404;

  srs_error_t err = srs_success;
  
  json::Object jobj = json::Deserialize(msg->get_body());

  std::string streamurl = std::move((std::string)jobj["streamurl"]);
  //std::string clientip = std::move((std::string)jobj["clientip"]);

  size_t pos = streamurl.rfind("/");
  if (pos == std::string::npos) {
    return s_handler_404.serve_http(writer, msg);
  }
  std::string stream_id = streamurl.substr(pos + 1);

  bool found = IsExisted(stream_id);

  if (found) {
    return s_handler_403.serve_http(writer, msg);
  }

  auto ms = std::make_shared<MediaRtcSource>(stream_id);

  std::string sdp = std::move((std::string)jobj["sdp"]);
  if ((err = ms->Init(rtc_api_, writer, sdp, streamurl)) != srs_success) {
    return srs_error_wrap(err, "rtc resource init failed.");
  }
 
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    sources_.emplace(stream_id, std::move(ms));
  }
  
  return srs_success;
}

//MediaHttpRtcServeMux
MediaHttpRtcServeMux::MediaHttpRtcServeMux()
  : api_(std::move(wa::AgentFactory().create_agent())) {
  handlers_.resize(2);
  handlers_[RTC_PUBLISH_HANDLER].reset(new MediaHttpPublishHandler(api_.get()));
  handlers_[RTC_PLAY_HANDLER].reset(new MediaHttpPlayHandler(api_.get()));
}

MediaHttpRtcServeMux::~MediaHttpRtcServeMux() = default;

srs_error_t MediaHttpRtcServeMux::init() {
  int ret = api_->initiate(g_server_.config_.rtc_workers_,
                           g_server_.config_.rtc_addr_,
                           g_server_.config_.rtc_stun_addr_);

  srs_error_t err = srs_success;
  if (ret != wa::wa_ok) {
    err = srs_error_wrap(err, "wa init failed. %d", ret); 
  }

  return err;
}

srs_error_t MediaHttpRtcServeMux::serve_http(
    std::shared_ptr<IHttpResponseWriter> w, std::shared_ptr<ISrsHttpMessage> m) {
  srs_error_t result = srs_success;
  std::string path = m->path();

  if (path == RTC_PUBLISH_PREFIX) {
    return handlers_[RTC_PUBLISH_HANDLER]->serve_http(w, m);
  } else {
    static HttpNotFoundHandler s_hangler_404;
    return s_hangler_404.serve_http(w, m);
  }

  return result;
}

void MediaHttpRtcServeMux::conn_destroy(std::shared_ptr<IMediaConnection>) {
}

}
