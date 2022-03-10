//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "rtc/media_rtc_source.h"

#include <string_view>
#include <random>

#include "h/rtc_return_value.h"
#include "rtc/media_rtc_attendee.h"
#include "rtmp/media_req.h"
#include "media_statistics.h"
#include "rtc/media_rtc_source_sink.h"

namespace ma {

namespace {
std::string_view GetPcId(std::string_view sdp) {
  size_t pos = sdp.find("ice-ufrag");
  if (pos == std::string_view::npos) {
    return "";
  }

  size_t pos1 = sdp.find("\\r\\n", pos + 10); //skip "ice-ufrag:"
  size_t len = pos1 - pos - 10;
  return sdp.substr(pos + 10, len);
}

std::string GenerateId() {
  const std::string alphanum = "0123456789" \
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                               "abcdefghijklmnopqrstuvwxyz";
  std::uniform_int_distribution<int> dist {0, int(alphanum.length()-1)};
  std::random_device rd;
  std::default_random_engine rng {rd()};

  std::string result;
  constexpr int msidLength = 10;
  for(int i = 0; i < msidLength; ++i){
    result += alphanum[dist(rng)];
  }

  return result;
}

}

MDEFINE_LOGGER(MediaRtcSource, "ma.rtc");

///////////////////////////////////////////////////////////////////////////////
//MediaRtcSource
///////////////////////////////////////////////////////////////////////////////
MediaRtcSource::MediaRtcSource(const std::string& streamName)
  : stream_name_(streamName) {
  thread_check_.Detach();
  MLOG_TRACE_THIS(stream_name_);
}

MediaRtcSource::~MediaRtcSource() {
  MLOG_TRACE_THIS(stream_name_);
}

void MediaRtcSource::Open(wa::RtcApi* rtc, wa::Worker* worker) {
  rtc_ = rtc;
  worker_ = worker;
}

void MediaRtcSource::Close() {
}

srs_error_t MediaRtcSource::OnLocalPublish(const std::string& streamName) {
  if (stream_name_.empty())
    stream_name_ = streamName;

  RTC_DCHECK_RUN_ON(&thread_check_);
  std::string publisher_id = GenerateId();
  MLOG_TRACE_THIS("stream:" << streamName << ", publisher_id:" << publisher_id);
  wa::TOption  t;
  t.type_ = wa::peer_dummy;
  t.connectId_ = publisher_id; // random
  t.stream_name_ = streamName;

  wa::TTrackInfo track;
  track.mid_ = "0";
  track.type_ = wa::media_audio;
  track.preference_.format_ = wa::p_opus;
  track.direction_ = "sendonly";
  t.tracks_.emplace_back(track);
  
  track.mid_ = "1";
  track.type_ = wa::media_video;
  track.preference_.format_ = wa::p_h264;
  track.direction_ = "sendonly";
  t.tracks_.emplace_back(track);
  
  int rv = rtc_->CreatePeer(t, "");

  srs_error_t err = srs_success;
  if (rv != wa::wa_ok) {
    err = srs_error_wrap(err, "rtc publish failed, code:%d", rv);
  }

  publisher_id_ = publisher_id;
  dummy_publisher_ = std::move(t.pc_);
  NotifyPublisherJoin();
  return err;
}

srs_error_t MediaRtcSource::OnLocalUnpublish() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  MLOG_TRACE_THIS("stream:" << stream_name_ << ", publisher_id:" << publisher_id_);
  publisher_id_.clear();

  int rv = rtc_->DestroyPeer(publisher_id_);
  srs_error_t err = srs_success;
  if (rv != wa::wa_ok) {
    err = srs_error_wrap(err, "rtc unpublish failed, code:%d", rv);
  }
  dummy_publisher_ = nullptr;
  return err;
}

srs_error_t MediaRtcSource::Publish(std::string_view sdp, 
                                    std::shared_ptr<IHttpResponseWriter> w,
                                    std::string& id,
                                    std::shared_ptr<MediaRequest> req) {
  if (stream_name_.empty())
    stream_name_ = req->get_stream_url();
  
  std::string_view pc_id = GetPcId(sdp);

  // check duplicated on OnAttendeeJoined
  auto publisher = std::make_shared<MediaRtcPublisher>(
      std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = 
      publisher->Open(rtc_, std::move(w), sdp, worker_, std::move(req), "");

  if (err != srs_success) {
    return err;
  }

  id = pc_id;
  publisher->signal_join_ok_.connect(this, &MediaRtcSource::OnAttendeeJoined);
  return err;
}

void MediaRtcSource::UnPublish(const std::string& pc_id) {
  std::shared_ptr<MediaRtcAttendeeBase> attendee;
  {
    std::lock_guard<std::mutex> guard(attendees_lock_);
    auto found = attendees_.find(pc_id);
    if (found == attendees_.end()) {
      assert(false);
      return ;
    }

    attendee = std::move(found->second);
    attendees_.erase(found);
  }

  attendee->Close();
}

srs_error_t MediaRtcSource::Subscribe(std::string_view sdp, 
                                      std::shared_ptr<IHttpResponseWriter> w,
                                      std::string& id,
                                      std::shared_ptr<MediaRequest> req) {

  if (stream_name_.empty())
    stream_name_ = req->get_stream_url();
  
  std::string_view pc_id = GetPcId(sdp);

  // check duplicated on OnAttendeeJoined
  auto subscriber = std::make_shared<MediaRtcSubscriber>(
                        std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = subscriber->Open(
      rtc_, std::move(w), sdp, worker_, std::move(req), publisher_id_);

  if (err != srs_success) {
    return err;
  }

  subscriber->signal_join_ok_.connect(this, &MediaRtcSource::OnAttendeeJoined);

  id = pc_id;
  return err;
}

void MediaRtcSource::UnSubscribe(const std::string& id) {
  UnPublish(id);
}

void MediaRtcSource::OnFirstPacket(std::shared_ptr<MediaRtcAttendeeBase> p) {
  signal_rtc_first_packet_();
}

void MediaRtcSource::OnAttendeeJoined(std::shared_ptr<MediaRtcAttendeeBase> p) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  MLOG_CINFO("%s %s joined",
      (p->IsPublisher()?"published":"subscriber"), p->Id());

  p->signal_join_ok_.disconnect(this);

  bool first = false;

  {
    std::lock_guard<std::mutex> guard(attendees_lock_);
    auto found = attendees_.find(p->Id());
    if (found != attendees_.end()) {
      assert(false);
      MLOG_ERROR("duplicated pc id:" << p->Id());
      // what to do ?
      return ;
    }

    first = attendees_.empty() ? true : false;
    attendees_.emplace(p->Id(), p);
  }

  p->signal_left_.connect(this, &MediaRtcSource::OnAttendeeLeft);

  if (p->IsPublisher()) {
    OnPublisherJoin(p);
  }

  if (first) {
    signal_rtc_first_suber_();
  }

  Stat().OnClient(p->Id(), std::move(p->GetRequest()), 
      p->IsPublisher()?TRtcPublish:TRtcPlay);
}

void MediaRtcSource::OnAttendeeLeft(std::shared_ptr<MediaRtcAttendeeBase> p) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  MLOG_CINFO("%s %s left",
      (p->IsPublisher()?"published":"subscriber"), p->Id());
  
  p->Close();

  bool empty = false;
  {
    std::lock_guard<std::mutex> guard(attendees_lock_);
    auto found = attendees_.find(p->Id());
    if (found == attendees_.end()) {
      assert(false);
      MLOG_ERROR("not found pc id:" << p->Id());
      return ;
    }
    attendees_.erase(p->Id());
    empty = attendees_.empty() ? true : false;
  }

  if (p->IsPublisher()) {
    {
      std::lock_guard<std::mutex> guard(attendees_lock_);
      for (auto& i : attendees_) {
        i.second->OnPublisherLeft(publisher_id_);
      }
    }
  
    signal_rtc_publisher_left_();
    publisher_id_ = "";
    publisher_ = nullptr;
  }

  if (empty) {
    signal_rtc_nobody_();
  }

  Stat().OnDisconnect(p->Id());
}

void MediaRtcSource::SetMediaSink(RtcMediaSink* s) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  media_sink_ = s;

  if (publisher_id_.empty()) {
    return;
  }
  
  {
    std::lock_guard<std::mutex> guard(attendees_lock_);
    auto found = attendees_.find(publisher_id_);
    if (found == attendees_.end()) {
      return ;
    }

    found->second->SetSink(s);
  }
}

void MediaRtcSource::OnPublisherJoin(std::shared_ptr<MediaRtcAttendeeBase> p) {
  p->signal_first_packet_.connect(this, &MediaRtcSource::OnFirstPacket);
  publisher_id_ = p->Id();
  publisher_ = p.get();
  publisher_->ChangeOnFrame(frame_on_);
  p->SetSink(media_sink_);

  NotifyPublisherJoin();

  signal_rtc_publisher_join_();
}

void MediaRtcSource::TurnOnFrameCallback(bool on) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  frame_on_ = on;
  if (publisher_) {
    publisher_->ChangeOnFrame(frame_on_);
  }
}

void MediaRtcSource::OnFrame(std::shared_ptr<owt_base::Frame> frm) {
  if (media_sink_) {
    media_sink_->OnMediaFrame(frm);
  }

  dummy_publisher_->DeliveryFrame(std::move(frm));
}

void MediaRtcSource::NotifyPublisherJoin() {
  std::lock_guard<std::mutex> guard(attendees_lock_);
  for (auto& i : attendees_) {
    i.second->OnPublisherJoin(publisher_id_);
  }
}

} //namespace ma
