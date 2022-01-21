//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "rtc/media_rtc_source.h"

#include <string_view>
#include "rtc/media_rtc_attendee.h"
#include "rtmp/media_req.h"
#include "media_statistics.h"

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

}

MDEFINE_LOGGER(MediaRtcSource, "MediaRtcSource");

//MediaRtcSource
MediaRtcSource::MediaRtcSource() {
  MLOG_TRACE_THIS("");
}

MediaRtcSource::~MediaRtcSource() {
  MLOG_TRACE_THIS("");
}

void MediaRtcSource::Open(wa::rtc_api* rtc, wa::Worker* worker) {
  rtc_ = rtc;
  worker_ = worker;
}

void MediaRtcSource::Close() {
}

srs_error_t MediaRtcSource::Publish(
    const std::string& sdp, 
    std::shared_ptr<IHttpResponseWriter> w,
    const std::string& stream_id,
    std::string& id,
    std::shared_ptr<MediaRequest> req) {

  std::string_view pc_id = GetPcId(sdp);
 
  auto publisher = std::make_shared<MediaRtcPublisher>(
      std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = 
      publisher->Open(rtc_, std::move(w), stream_id, sdp, worker_, std::move(req));

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

srs_error_t MediaRtcSource::Subscribe(
    const std::string& sdp, 
    std::shared_ptr<IHttpResponseWriter> w,
    const std::string& stream_id,
    std::string& id,
    std::shared_ptr<MediaRequest> req) {
  std::string_view pc_id = GetPcId(sdp);
  auto subscriber = std::make_shared<MediaRtcSubscriber>(
                        std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = subscriber->Open(
      rtc_, std::move(w), stream_id, sdp, worker_, std::move(req), publisher_id_);

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
  MLOG_CINFO("%s %s joined",
      (p->IsPublisher()?"published":"subscriber"), p->Id());

  Stat().OnClient(p->Id(), std::move(p->GetRequest()), TRtcPublish);

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
    OnPublisherJoin(std::move(p));
  }

  if (first) {
    signal_rtc_first_suber_();
  }
}

void MediaRtcSource::OnAttendeeLeft(std::shared_ptr<MediaRtcAttendeeBase> p) {
  MLOG_CINFO("%s %s left",
      (p->IsPublisher()?"published":"subscriber"), p->Id());
  Stat().OnDisconnect(p->Id());
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
}

void MediaRtcSource::SetMediaSink(RtcMediaSink* s) {
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
    media_sink_ = nullptr;
  }
}

void MediaRtcSource::OnPublisherJoin(std::shared_ptr<MediaRtcAttendeeBase> p) {
  p->signal_first_packet_.connect(this, &MediaRtcSource::OnFirstPacket);
  publisher_id_ = p->Id();
  publisher_ = p.get();
  publisher_->ChangeOnFrame(frame_on_);
  if (media_sink_) {
    p->SetSink(media_sink_);
    media_sink_ = nullptr;
  }

  {
    std::lock_guard<std::mutex> guard(attendees_lock_);
    for (auto& i : attendees_) {
      i.second->OnPublisherJoin(publisher_id_);
    }
  }

  signal_rtc_publisher_join_();
}

void MediaRtcSource::TurnOnFrameCallback(bool on) {
  frame_on_ = on;
  if (publisher_) {
    publisher_->ChangeOnFrame(frame_on_);
  }
}

} //namespace ma

