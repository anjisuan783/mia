#include "handler/media_rtc_source.h"

#include "h/rtc_return_value.h"
#include "utils/json.h"
#include "utils/protocol_utility.h"
#include "http/h/http_message.h"
#include "http/h/http_protocal.h"
#include "media_source.h"
#include "media_source_mgr.h"
#include "media_server.h"
#include "rtmp/media_req.h"

namespace ma {

MDEFINE_LOGGER(MediaRtcSource, "MediaRtcSource");

MediaRtcSource::MediaRtcSource(const std::string& id) 
  : stream_id_{id} {
  MLOG_TRACE(stream_id_);
}

MediaRtcSource::~MediaRtcSource() {
  MLOG_TRACE(stream_id_);
}

srs_error_t MediaRtcSource::Init(wa::rtc_api* rtc, 
                                 std::shared_ptr<IHttpResponseWriter> w, 
                                 const std::string& sdp, 
                                 const std::string& url) {
  writer_ = std::move(w);
  stream_url_ = url;
  task_queue_ = this;
  rtc_ = rtc;
  return Init_i(sdp);
}

srs_error_t MediaRtcSource::Init(wa::rtc_api* rtc, 
                                std::shared_ptr<IHttpResponseWriter> w, 
                                std::shared_ptr<ISrsHttpMessage> msg, 
                                const std::string& url) {                             
  writer_ = std::move(w);
  stream_url_ = url;
  task_queue_ = this; 
  rtc_ = rtc;
  msg->SignalOnBody_.connect(this, &MediaRtcSource::OnBody);

  return srs_success;
}

srs_error_t MediaRtcSource::Init_i(const std::string& isdp) {
  std::string sdp = srs_string_replace(isdp, "\\r\\n", "\r\n");

  MLOG_DEBUG("sdp:" << sdp);
  
  worker_ = g_source_mgr_.GetWorker();
  
  wa::TOption  t;
  t.connectId_ = "1";

  size_t found = 0;
  do {
    found = sdp.find("m=audio", found);
    if (found != std::string::npos) {
      found = sdp.find("a=mid:", found);
      if (found != std::string::npos) {
        wa::TTrackInfo track;
        found += 6;
        size_t found_end = sdp.find("\r\n", found);
        track.mid_ = sdp.substr(found, found_end-found);
        track.type_ = wa::media_audio;
        track.preference_.format_ = wa::EFormatPreference::p_opus;
        t.tracks_.emplace_back(track);
      }
    }

    found = sdp.find("m=video", found);
    if (found != std::string::npos) {
      found = sdp.find("a=mid:", found);
      if (found != std::string::npos) {
        found += 6;
        size_t found_end = sdp.find("\r\n", found);
        wa::TTrackInfo track;
        track.mid_ = sdp.substr(found, found_end-found);
        track.type_ = wa::media_video;
        track.preference_.format_ = wa::EFormatPreference::p_h264;
        track.preference_.profile_ = "42001f";
        t.tracks_.emplace_back(track);
      }
    }
  } while(found!=std::string::npos);

  t.call_back_ = shared_from_this();

  int ret = rtc_->publish(t, sdp);

  srs_error_t err = srs_success;
  if (ret != wa::wa_ok) {
    err = srs_error_wrap(err, "rtc publish failed, code:%d", ret);
  }

  return err;
}

void MediaRtcSource::OnBody(const std::string& body) {
  json::Object jobj = json::Deserialize(body);
  std::string sdp = std::move((std::string)jobj["sdp"]);
  srs_error_t err = this->Init_i(sdp);

  if (err != srs_success) {
    MLOG_CERROR("rtc source internal init failed, code:%d, desc:%s", 
        srs_error_code(err), srs_error_desc(err).c_str());
    delete err;
  }
}

void MediaRtcSource::onFailed(const std::string&) {
  MLOG_DEBUG("");
}

void MediaRtcSource::onCandidate(const std::string&) {
  MLOG_DEBUG("");
}

void MediaRtcSource::onReady() {
  MLOG_DEBUG("");
}

void MediaRtcSource::onAnswer(const std::string& sdp) {
  MLOG_DEBUG(sdp);
  writer_->header()->set("Connection", "Close");

  json::Object jroot;
  jroot["code"] = 200;
  jroot["server"] = "ly rtc";
  jroot["sdp"] = std::move(srs_string_replace(sdp, "\r\n", "\\r\\n"));
  //jroot["sessionid"] = msid;
  
  std::string jsonStr = std::move(json::Serialize(jroot));

  srs_error_t err = writer_->write(jsonStr.c_str(), jsonStr.length());

  if(err != srs_success){
    MLOG_CERROR("send rtc answer failed, code=%d, %s", 
        srs_error_code(err), srs_error_desc(err).c_str());
    delete err;

    return;
  }

  err = writer_->final_request();

  if(err != srs_success){
    MLOG_CERROR("final_request failed, code=%d, %s", 
        srs_error_code(err), srs_error_desc(err).c_str());
    delete err;

    return;
  }

  OnPublish();
}

void MediaRtcSource::onFrame(const owt_base::Frame& frm) {
  if (owt_base::isVideoFrame(frm) ) {
    
  } else if (owt_base::isAudioFrame(frm)) {
    
  }
}

void MediaRtcSource::onStat() {
  MLOG_DEBUG("");
}

void MediaRtcSource::OnPublish() {
  auto req = std::make_shared<MediaRequest>();
  req->tcUrl = stream_url_;
  req->stream = stream_id_;

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

  auto result = g_source_mgr_.FetchOrCreateSource(req->stream);

  if (!*result) {
    MLOG_ERROR("create source failed url:" << req->stream);
    return;
  }
  source_ = *result;

  g_server_.on_publish(result.value(), req);
  
  source_->on_publish();

  req_ = req;
}

void MediaRtcSource::post(Task t) {
  worker_->task(t);
}

} //namespace ma

