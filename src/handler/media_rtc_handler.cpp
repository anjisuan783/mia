#include "handler/media_rtc_handler.h"

#include "common/media_log.h"
#include "utils/json.h"
#include "http/h/http_message.h"
#include "connection/h/conn_interface.h"

namespace ma {

#if 0
GsRtcCode MediaHttpConn::response(int code, const std::string& msg,
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

