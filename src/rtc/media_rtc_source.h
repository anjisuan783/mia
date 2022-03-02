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
#include <string_view>
#include <unordered_map>

#include "h/rtc_media_frame.h"
#include "h/rtc_stack_api.h"
#include "utils/sigslot.h"
#include "utils/Worker.h"
#include "common/media_kernel_error.h"
#include "common/media_log.h"
#include "live/media_live_rtc_adaptor_sink.h"
#include "rtc_base/sequence_checker.h"

namespace ma {

class Worker;
class IHttpResponseWriter;
class MediaRtcAttendeeBase;
class MediaRequest;
class RtcMediaSink;
class MediaSource;

//MediaRtcSource
class MediaRtcSource final : public sigslot::has_slots<>,
                             public LiveRtcAdapterSink {

  MDECLARE_LOGGER();
  friend class MediaSource;
  friend class MediaRtcLiveAdaptor;

 public:
  ~MediaRtcSource();
 protected:
  MediaRtcSource(const std::string& streamName);
  
  void Open(wa::RtcApi*, wa::Worker*);
  void Close();
  
  // for local publish
  srs_error_t OnLocalPublish(const std::string& streamName) override;
  srs_error_t OnLocalUnpublish() override;

  void SetMediaSink(RtcMediaSink* s);

  srs_error_t Publish(std::string_view sdp, 
                      std::shared_ptr<IHttpResponseWriter>,
                      std::string& pc_id,
                      std::shared_ptr<MediaRequest> req);
  void UnPublish(const std::string& id);

  srs_error_t Subscribe(std::string_view sdp, 
                        std::shared_ptr<IHttpResponseWriter>,
                        std::string& id,
                        std::shared_ptr<MediaRequest> req);
  void UnSubscribe(const std::string& id);

  void OnFrame(std::shared_ptr<owt_base::Frame>) override;

  void OnFirstPacket(std::shared_ptr<MediaRtcAttendeeBase>);
  void OnAttendeeJoined(std::shared_ptr<MediaRtcAttendeeBase>);
  void OnAttendeeLeft(std::shared_ptr<MediaRtcAttendeeBase>);

  void TurnOnFrameCallback(bool);
 private:
  void OnPublisherJoin(std::shared_ptr<MediaRtcAttendeeBase>);
  void NotifyPublisherJoin();
 public:
  sigslot::signal0<> signal_rtc_first_suber_;
  sigslot::signal0<> signal_rtc_nobody_;
  sigslot::signal0<> signal_rtc_first_packet_;
  sigslot::signal0<> signal_rtc_publisher_join_;
  sigslot::signal0<> signal_rtc_publisher_left_;
  
 private:
  wa::RtcApi* rtc_{nullptr};
  wa::Worker*  worker_{nullptr};

  std::mutex attendees_lock_;
  std::unordered_map<std::string, std::shared_ptr<MediaRtcAttendeeBase>> attendees_;
  std::string publisher_id_;
  MediaRtcAttendeeBase* publisher_{nullptr};

  RtcMediaSink* media_sink_{nullptr};
  bool frame_on_{false};

  // local publisher
  std::shared_ptr<wa::RtcPeer> dummy_publisher_;

  std::string stream_name_;

  webrtc::SequenceChecker thread_check_;
};

} //namespace ma

#endif //!__MEDIA_RTC_SOURCE_H__

