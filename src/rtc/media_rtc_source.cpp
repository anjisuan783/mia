//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "rtc/media_rtc_source.h"

#include <string_view>
#include "rtc/media_rtc_attendee.h"

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
MediaRtcSource::MediaRtcSource() = default;

MediaRtcSource::~MediaRtcSource() = default;

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
    std::string& id) {

  std::string_view pc_id = GetPcId(sdp);
 
  auto publisher = std::make_shared<MediaRtcPublisher>(
                      std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = publisher->Open(rtc_, std::move(w), stream_id, sdp, worker_);

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
    std::string& id) {
  std::string_view pc_id = GetPcId(sdp);
  auto subscriber = std::make_shared<MediaRtcSubscriber>(
                        std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = subscriber->Open(
      rtc_, std::move(w), stream_id, sdp, worker_, publisher_id_);

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
  signal_rtc_publish_();
}

void MediaRtcSource::OnAttendeeJoined(std::shared_ptr<MediaRtcAttendeeBase> p) {
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
    OnPublisherJoin(std::move(p));
  }

  if (first) {
    signal_rtc_first_suber_();
  }
}

void MediaRtcSource::OnAttendeeLeft(std::shared_ptr<MediaRtcAttendeeBase> p) {
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
    signal_rtc_unpublish_();
    publisher_id_ = "";
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
}

} //namespace ma

