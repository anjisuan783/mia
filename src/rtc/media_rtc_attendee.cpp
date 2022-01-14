//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "rtc/media_rtc_attendee.h"

#include "utils/Worker.h"
#include "h/rtc_return_value.h"
#include "common/media_log.h"
#include "utils/json.h"
#include "utils/protocol_utility.h"
#include "http/h/http_message.h"
#include "http/h/http_protocal.h"
#include "media_server.h"
#include "rtmp/media_req.h"
#include "rtc/media_rtc_source.h"

namespace ma {

namespace {

// one m=audio, one m=video sdp 
// unified plan only
void FillTrack(const std::string& sdp, 
    std::vector<wa::TTrackInfo>& tracks, bool push) {
  size_t found = 0;

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
      tracks.emplace_back(track);
    }
  }

  found = 0;
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
      track.preference_.profile_ = "42e01f";
      track.request_keyframe_period_ = 
          g_server_.config_.request_keyframe_interval;
      tracks.emplace_back(track);
    }
  }

  for (auto& i : tracks) {
   if (push) {
      i.direction_ = "sendonly";
    }
    else {
      i.direction_ = "recvonly";
    }
  }
}

} //namespace

static log4cxx::LoggerPtr logger = 
    log4cxx::Logger::getLogger("MediaRtcAttendee");

//MediaRtcAttendeeBase
srs_error_t MediaRtcAttendeeBase::Responese(int code, const std::string& sdp) {
  srs_error_t err = srs_success;
  writer_->header()->set("Connection", "Close");

  json::Object jroot;
  jroot["code"] = code;
  jroot["server"] = "mia rtc";
  jroot["sdp"] = sdp; 
  jroot["sessionid"] = pc_id_;
  
  std::string jsonStr = std::move(json::Serialize(jroot));

  if((err = writer_->write(jsonStr.c_str(), jsonStr.length())) != srs_success){
    return srs_error_wrap(err, "Responese");
  }

  if((err = writer_->final_request()) != srs_success){
    return srs_error_wrap(err, "final_request failed");
  }

  return err;
}

void MediaRtcAttendeeBase::onCandidate(const std::string&) {
  MLOG_TRACE("");
}

void MediaRtcAttendeeBase::onStat() {
  MLOG_TRACE("");
}

void MediaRtcAttendeeBase::post(Task t) {
  worker_->task(t);
}

//MediaRtcPublisher
srs_error_t MediaRtcPublisher::Open(
    wa::rtc_api* rtc, 
    std::shared_ptr<IHttpResponseWriter> writer, 
    const std::string& stream_id,
    const std::string& isdp,
    wa::Worker* worker,
    const std::string&) {
    
  MLOG_INFO(pc_id_ << ", offer:" << isdp);
  writer_ = std::move(writer);
  rtc_ = rtc;
  worker_ = worker;
  
  std::string sdp = srs_string_replace(isdp, "\\r\\n", "\r\n");
    
  wa::TOption  t;
  t.connectId_ = pc_id_;
  t.stream_name_ = stream_id;

  // TODO parse sdp here
  FillTrack(sdp, t.tracks_, true);

  t.call_back_ = shared_from_this();
  async_callback_ = true;

  int rv = rtc_->CreatePeer(t, sdp);

  srs_error_t err = srs_success;
  if (rv == wa::wa_e_found) {
    err = srs_error_wrap(err, "rtc publish failed, code:%d", rv);
  }

  pc_in_ = true;

  return err;
}

void MediaRtcPublisher::Close() {
  if (pc_in_) {
    int rv = rtc_->DestroyPeer(pc_id_);

    pc_in_ = false;
    if (rv != wa::wa_ok) {
      MLOG_ERROR("pc[" << pc_id_ << "] not found");
    }
  }
}

void MediaRtcPublisher::onAnswer(const std::string& sdp) {
  srs_error_t err = srs_success;
  std::string answer_sdp = std::move(srs_string_replace(sdp, "\r\n", "\\r\\n"));
  MLOG_INFO(pc_id_ << ", answer:" << answer_sdp);
  if((err = this->Responese(0, answer_sdp)) != srs_success){
    MLOG_ERROR("send rtc answer failed, desc" << srs_error_desc(err));
    delete err;

    // try again ?

    int rv = rtc_->DestroyPeer(pc_id_);
    if (rv != wa::wa_ok) {
      MLOG_ERROR("pc[" << pc_id_ << "] not found");
    }

    pc_in_ = false;
  }
}

void MediaRtcPublisher::onReady() {
  MLOG_TRACE(pc_id_);
  signal_join_ok_(
      std::dynamic_pointer_cast<MediaRtcAttendeeBase>(shared_from_this()));
}

void MediaRtcPublisher::onFailed(const std::string& /*remoted_sdp*/) {
  MLOG_CINFO("pc[%s] disconnected", pc_id_);
  int rv = rtc_->DestroyPeer(pc_id_);

  pc_in_ = false;

  if (rv != wa::wa_ok) {
    MLOG_ERROR("pc[" << pc_id_ << "] not found");
  }

  signal_left_(
      std::dynamic_pointer_cast<MediaRtcAttendeeBase>(shared_from_this()));
}

void MediaRtcPublisher::onFrame(const owt_base::Frame& frm) {
  if (!first_packet_) {
    first_packet_ = true;
    signal_first_packet_(
        std::dynamic_pointer_cast<MediaRtcAttendeeBase>(shared_from_this()));
  }

  if (sink_) {
    sink_->OnMediaFrame(frm);
  }
}

//MediaRtcPublisher
srs_error_t MediaRtcSubscriber::Open(
    wa::rtc_api* rtc, 
    std::shared_ptr<IHttpResponseWriter> writer,
    const std::string& stream_id,
    const std::string& isdp,
    wa::Worker* worker,
    const std::string& publisher_id) {
    
  MLOG_INFO(pc_id_ << ", offer:" << isdp);
  writer_ = std::move(writer);
  rtc_ = rtc;
  worker_ = worker;
  publisher_id_ = publisher_id;
  std::string sdp = srs_string_replace(isdp, "\\r\\n", "\r\n");
    
  wa::TOption  t;
  t.connectId_ = pc_id_;
  t.stream_name_ = stream_id;

  FillTrack(sdp, t.tracks_, false);

  t.call_back_ = shared_from_this();

  int rv = rtc_->CreatePeer(t, sdp);

  srs_error_t err = srs_success;
  if (rv == wa::wa_e_found) {
    err = srs_error_wrap(err, "rtc publish failed, code:%d", rv);
  }

  pc_in_ = true;

  return err;
}

void MediaRtcSubscriber::Close() {
  if (pc_in_) {
    int rv = rtc_->DestroyPeer(pc_id_);

    pc_in_ = false;
    if (rv != wa::wa_ok) {
      MLOG_ERROR("pc[" << pc_id_ << "] not found");
    }
  }
}

void MediaRtcSubscriber::onAnswer(const std::string& sdp) {
  srs_error_t err = srs_success;
  std::string answer_sdp = std::move(srs_string_replace(sdp, "\r\n", "\\r\\n"));
  MLOG_INFO(pc_id_ << ", answer:" << answer_sdp);
  if((err = this->Responese(0, answer_sdp)) != srs_success){
    MLOG_ERROR("send rtc answer failed, desc" << srs_error_desc(err));
    delete err;

    // try again ?
    int rv = rtc_->DestroyPeer(pc_id_);
    if (rv != wa::wa_ok) {
      MLOG_ERROR("pc[" << pc_id_ << "] not found");
    }

    pc_in_ = false;
  }
}

void MediaRtcSubscriber::onReady() {
  MLOG_TRACE(pc_id_);
  signal_join_ok_(
      std::dynamic_pointer_cast<MediaRtcAttendeeBase>(shared_from_this()));
  
  if (publisher_id_.empty()) {
    return ;
  }
  
  int rv = rtc_->Subscribe(publisher_id_, pc_id_);
  if (rv != wa::wa_ok) {
    MLOG_ERROR(pc_id_ << " subscribe " << 
               publisher_id_ << " failed code:" << rv);
    return ;
  }

  linked_ = true;
}

void MediaRtcSubscriber::onFailed(const std::string& /*remoted_sdp*/) {
  MLOG_CINFO("pc[%s] disconnected", pc_id_);

  int rv = rtc_->Unsubscribe(publisher_id_, pc_id_);
  if (rv != wa::wa_ok) {
    MLOG_CERROR("pc[%s] unsub failed, publisher[%s] not found", publisher_id_);
  }
  
  rv = rtc_->DestroyPeer(pc_id_);

  pc_in_ = false;

  if (rv != wa::wa_ok) {
    MLOG_CERROR("pc[%s] unpublish failed, code:%d",  pc_id_, rv);
  }

  signal_left_(
      std::dynamic_pointer_cast<MediaRtcAttendeeBase>(shared_from_this()));
}


void MediaRtcSubscriber::OnPublisherJoin(const std::string& id) {
  if (linked_) {
    return;
  }

  publisher_id_ = id;
  int rv = rtc_->Subscribe(publisher_id_, pc_id_);
  if (rv != wa::wa_ok) {
    MLOG_ERROR(pc_id_ << " subscribe " << publisher_id_ << 
               " failed code:" << rv);
    return ;
  }

  linked_ = true;
}

void MediaRtcSubscriber::OnPublisherLeft(const std::string& id) {
  if (id != publisher_id_) {
    MLOG_CFATAL("unexpected publisher:%s[%s]",id.c_str(),publisher_id_.c_str());
  }

  if (!linked_) {
    return;
  }
  
  int rv = rtc_->Unsubscribe(publisher_id_, pc_id_);
  if (rv != wa::wa_ok) {
    MLOG_ERROR(publisher_id_ << " cutoff  " << pc_id_ << " failed code:" << rv);
  }
  publisher_id_ = "";
  linked_ = false;
}

void MediaRtcSubscriber::OnPublisherChange(const std::string& id) {
  if (publisher_id_ == id){
    return;
  }

  if (!linked_) {
    return;
  }
  
  publisher_id_ = id;
  int rv = rtc_->Unsubscribe(publisher_id_, pc_id_);
  if (rv != wa::wa_ok) {
    MLOG_CERROR("pc[%s] unsub failed, publisher[%s] not found", publisher_id_);
  }

  rv = rtc_->Subscribe(publisher_id_, pc_id_);
  if (rv != wa::wa_ok) {
    MLOG_ERROR(pc_id_ << " subscribe " << publisher_id_ << 
               " failed code:" << rv);
    return ;
  }

  linked_ = true;
}

} //namespace ma

