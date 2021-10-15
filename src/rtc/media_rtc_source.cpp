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
    std::string& id) {

  std::string_view pc_id = GetPcId(sdp);
 
  auto publisher = std::make_shared<MediaRtcPublisher>(
                      std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = publisher->Open(rtc_, std::move(w), sdp, worker_);

  if (err != srs_success) {
    return err;
  }

  publisher_id_ = pc_id;
  id = pc_id;
  MLOG_INFO("new publisher id:" << id);
  
  std::lock_guard<std::mutex> guard(attendees_lock_);
  auto found = attendees_.find(id);
  if (found != attendees_.end()) {
    return srs_error_new(ERROR_RTC_INVALID_PARAMS, "duplicated ice-ufrag");
  }

  attendees_.emplace(id, std::move(publisher));
  return err;
}

void MediaRtcSource::UnPublish(const std::string& pc_id) {
  std::shared_ptr<MediaRtcAttendeeBase> attendee;
  {
    std::lock_guard<std::mutex> guard(attendees_lock_);
    auto found = attendees_.find(pc_id);
    if (found == attendees_.end()) {
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
    std::string& id) {
  std::string_view pc_id = GetPcId(sdp);
  auto subscriber = std::make_shared<MediaRtcSubscriber>(
                        std::string(pc_id.data(), pc_id.length()));
  srs_error_t err = subscriber->Open(
      rtc_, std::move(w), sdp, worker_, publisher_id_);

  if (err != srs_success) {
    return err;
  }

  id = pc_id;
  MLOG_INFO("new subscriber id:" << id);
  
  std::lock_guard<std::mutex> guard(attendees_lock_);
  auto found = attendees_.find(id);
  if (found != attendees_.end()) {
    return srs_error_new(ERROR_RTC_INVALID_PARAMS, "duplicated ice-ufrag");
  }

  attendees_.emplace(id, std::move(subscriber));
  return err;
}

void MediaRtcSource::UnSubscribe(const std::string& id) {
  UnPublish(id);
}

} //namespace ma

