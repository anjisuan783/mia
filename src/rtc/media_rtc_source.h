//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTC_SOURCE_H__
#define __MEDIA_RTC_SOURCE_H__

#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>

#include "h/rtc_stack_api.h"
#include "utils/sigslot.h"
#include "utils/Worker.h"
#include "common/media_kernel_error.h"
#include "common/media_log.h"

namespace ma {

class RtcMediaSink {
 public:
  virtual ~RtcMediaSink() = default;
  virtual void OnMediaFrame(std::shared_ptr<owt_base::Frame> frm) = 0;
};

class Worker;
class IHttpResponseWriter;
class MediaRtcAttendeeBase;
class MediaRequest;

//MediaRtcSource
class MediaRtcSource final : public sigslot::has_slots<> {

  MDECLARE_LOGGER();
 public:
  MediaRtcSource();
  ~MediaRtcSource();

  void Open(wa::rtc_api*, wa::Worker*);
  void Close();

  void SetMediaSink(RtcMediaSink* s);

  srs_error_t Publish(const std::string& sdp, 
                      std::shared_ptr<IHttpResponseWriter>,
                      const std::string& stream_id,
                      std::string& pc_id,
                      std::shared_ptr<MediaRequest> req);
  void UnPublish(const std::string& id);

  srs_error_t Subscribe(const std::string& sdp, 
                        std::shared_ptr<IHttpResponseWriter>,
                        const std::string& stream_id,
                        std::string& id,
                        std::shared_ptr<MediaRequest> req);
  void UnSubscribe(const std::string& id);

  void OnFirstPacket(std::shared_ptr<MediaRtcAttendeeBase>);
  void OnAttendeeJoined(std::shared_ptr<MediaRtcAttendeeBase>);
  void OnAttendeeLeft(std::shared_ptr<MediaRtcAttendeeBase>);

  void TurnOnFrameCallback(bool);
 private:
  void OnPublisherJoin(std::shared_ptr<MediaRtcAttendeeBase>);

 public:
  sigslot::signal0<> signal_rtc_first_suber_;
  sigslot::signal0<> signal_rtc_nobody_;
  sigslot::signal0<> signal_rtc_first_packet_;
  sigslot::signal0<> signal_rtc_publisher_left_;
  
 private:
  wa::rtc_api* rtc_{nullptr};
  wa::Worker*  worker_{nullptr};

  std::mutex attendees_lock_;
  std::unordered_map<std::string, std::shared_ptr<MediaRtcAttendeeBase>> attendees_;
  std::string publisher_id_;  //not safe, change to smart pointer
  MediaRtcAttendeeBase* publisher_{nullptr};

  RtcMediaSink* media_sink_{nullptr};
  bool frame_on_{false};
};

} //namespace ma

#endif //!__MEDIA_RTC_SOURCE_H__

