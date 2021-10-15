#include "handler/media_rtc_handler.h"

#include <optional>

#include "h/rtc_return_value.h"

#include "common/media_log.h"
#include "utils/json.h"
#include "utils/protocol_utility.h"
#include "http/h/http_message.h"
#include "connection/h/conn_interface.h"
#include "media_server.h"
#include "rtc/media_rtc_source.h"
#include "rtmp/media_req.h"
#include "media_source_mgr.h"
#include "media_source.h"

namespace ma {

#define RTC_PUBLISH_HANDLER 0
#define RTC_PLAY_HANDLER 1

static log4cxx::LoggerPtr logger = 
    log4cxx::Logger::getLogger("MediaHttpRtcServer");

class MediaHttpPlayHandler : public IMediaHttpHandler {
 public: 
  MediaHttpPlayHandler(wa::rtc_api* p) : rtc_api_(p){
  }
 private:
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter>, 
                         std::shared_ptr<ISrsHttpMessage>) override;
  void conn_destroy(std::shared_ptr<IMediaConnection>) override { }
  
  srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                            std::shared_ptr<MediaRequest> r) override;
  void unmount_service(std::shared_ptr<MediaSource> s, 
                       std::shared_ptr<MediaRequest> r) override;
 private:
  wa::rtc_api* rtc_api_;
  std::mutex stream_lock_;
  std::map<std::string, std::shared_ptr<MediaSource>> source_map_;
};

/*
1.拉流URL
schema://domain:port/rtc/v1/play

request
{
  streamurl: 'webrtc://domain/app/stream',
  sdp: string,  // offer sdp
  clientip: string // 可选项， 在实际接入过程中，拉流请求有可能是服务端发起，
                      为了更好的做就近调度，可以把客户端的ip地址当做参数，
                      如果没有此clientip参数，CDN放可以用请求方的ip来做就近接入。
}

response
{
  code: int,
  msg:  string,
  data: {
    sdp:string,   // answer sdp 
    sessionid:string // 该路下行的唯一id
  }
}

HTTP响应code码
200:  正常影响
400:  请求不正确，URL 或者 参数不正确
403:  鉴权失败
404:  该流不存在
500:  服务内部异常 


2.停止拉流URL

schema://domain:port/rtc/v1/unplay

schema: http或者https
method: POST
content-type: json

request
{
  code:int,
  msg:string,
  data:{
    streamurl: 'webrtc://domain/app/stream',
    sessionid:string // 拉流时返回的唯一id
  }
}

HTTP响应code码

200:  正常影响
400:  请求不正确，URL 或者 参数不正确
403:  鉴权失败
404:  该流不存在
500:  服务内部异常  

*/

srs_error_t MediaHttpPlayHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> writer, 
    std::shared_ptr<ISrsHttpMessage> msg) {
  srs_error_t err = srs_success;
  json::Object jobj = json::Deserialize(msg->get_body());

  std::string streamurl = std::move((std::string)jobj["streamurl"]);
  std::string sdp = std::move((std::string)jobj["sdp"]);

  std::string tcUrl;
  std::string stream_id;
  srs_parse_rtmp_url(streamurl, tcUrl, stream_id);

  std::string subscriber_id;
  auto result = g_source_mgr_.FetchSource(stream_id);
  if (*result) {  
    err = (*result)->Subscribe(sdp, std::move(writer), subscriber_id);
  }
  return err;
}

srs_error_t MediaHttpPlayHandler::mount_service(std::shared_ptr<MediaSource> s, 
                                      std::shared_ptr<MediaRequest> r)  {
  std::lock_guard<std::mutex> guard(stream_lock_);
  source_map_.emplace(r->stream, std::move(s));
  return srs_success;
}

void MediaHttpPlayHandler::unmount_service(std::shared_ptr<MediaSource> s, 
                             std::shared_ptr<MediaRequest> r) {
  std::lock_guard<std::mutex> guard(stream_lock_);
  source_map_.erase(r->stream);
}

//MediaHttpPublishHandler
class MediaHttpPublishHandler : public IMediaHttpHandler {
 public:
  MediaHttpPublishHandler(wa::rtc_api* p) : rtc_api_{p} {
  }
 private:
  
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter>, 
                         std::shared_ptr<ISrsHttpMessage>) override;
  srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                            std::shared_ptr<MediaRequest> r) override {
    return srs_success;
  }
  
  void unmount_service(std::shared_ptr<MediaSource> s, 
                       std::shared_ptr<MediaRequest> r) override { }
  void conn_destroy(std::shared_ptr<IMediaConnection> conn) override { }

 private:
  wa::rtc_api* rtc_api_;
};

/*
1.推流URL
schema://domain:port/rtc/v1/publish

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

HTTP响应code码
200:  正常影响
400:  请求不正确，URL 或者 参数不正确
403:  鉴权失败
404:  该流不存在
500:  服务内部异常 

2.停止推流URL
schema://domain:port/rtc/v1/unpublish

schema: http或者https
method: POST
content-type: json

request
{
  streamurl: 'webrtc://domain/app/stream',
  sessionid:string // 推流时返回的唯一id
}

response
{
  code:int,
  msg:string
}

HTTP响应code码

200:  正常影响
400:  请求不正确，URL 或者 参数不正确
403:  鉴权失败
404:  该流不存在
500:  服务内部异常  


@see https://github.com/rtcdn/rtcdn-draft
*/

srs_error_t MediaHttpPublishHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> writer, 
    std::shared_ptr<ISrsHttpMessage> msg) {
  assert(msg->get_body().length() == (size_t)msg->content_length());
  assert(msg->is_http_post());
  assert(msg->path() == RTC_PUBLISH_PREFIX);
  assert(msg->is_body_eof());

  srs_error_t err = srs_success;
  
  json::Object jobj = json::Deserialize(msg->get_body());

  std::string streamurl = std::move((std::string)jobj["streamurl"]);
  //std::string clientip = std::move((std::string)jobj["clientip"]);
  std::string sdp = std::move((std::string)jobj["sdp"]);

  auto req = std::make_shared<MediaRequest>();
  srs_parse_rtmp_url(streamurl, req->tcUrl, req->stream);

  //req->stream = stream_id_;
  srs_discovery_tc_url(req->tcUrl, 
                       req->schema,
                       req->host, 
                       req->vhost, 
                       req->app, 
                       req->stream, 
                       req->port, 
                       req->param);

  MLOG_INFO("schema:" << req->schema << 
            ", host:" << req->host <<
            ", vhost:" << req->vhost << 
            ", app:" << req->app << 
            ", stream:" << req->stream << 
            ", port:" << req->port << 
            ", param:" << req->param);
  
  MediaSource::Config cfg{std::move(g_source_mgr_.GetWorker()), 
                          g_server_.config_.enable_gop_,
                          g_server_.config_.enable_atc_,
                          JitterAlgorithmZERO,
                          rtc_api_};
  auto ms = g_source_mgr_.FetchOrCreateSource(cfg, req);

  std::string publisher_id;
  err = ms->Publish(sdp, std::move(writer), publisher_id);

  //TODO double publisher  
  return err;
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
                           g_server_.config_.candidates_,
                           g_server_.config_.stun_addr_);

  srs_error_t err = srs_success;
  if (ret != wa::wa_ok) {
    err = srs_error_wrap(err, "wa init failed. %d", ret); 
  }

  return err;
}

int tcUrl2Handle(const std::string& tcurl) {
  if (tcurl == RTC_PUBLISH_PREFIX) {
    return RTC_PUBLISH_HANDLER;
  }

  return RTC_PLAY_HANDLER;
}

srs_error_t MediaHttpRtcServeMux::serve_http(
    std::shared_ptr<IHttpResponseWriter> w, 
    std::shared_ptr<ISrsHttpMessage> m) {
  std::string path = m->path();
  return handlers_[tcUrl2Handle(path)]->serve_http(std::move(w), m);
}

void MediaHttpRtcServeMux::conn_destroy(std::shared_ptr<IMediaConnection>) {
}

srs_error_t MediaHttpRtcServeMux::mount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {
  return handlers_[RTC_PUBLISH_HANDLER]->mount_service(std::move(s), 
                                                       std::move(r));
}

void MediaHttpRtcServeMux::unmount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {
  handlers_[RTC_PLAY_HANDLER]->unmount_service(std::move(s),
                                               std::move(r));
}

}

