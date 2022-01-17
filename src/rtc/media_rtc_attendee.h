//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTC_ATTENDEE_H__
#define __MEDIA_RTC_ATTENDEE_H__

#include <string>
#include <memory>
#include "utils/sigslot.h"
#include "h/rtc_stack_api.h"
#include "utils/sigslot.h"
#include "utils/Worker.h"
#include "common/media_kernel_error.h"

namespace ma {

class IHttpResponseWriter;
class RtcMediaSink;

class MediaRtcAttendeeBase 
  : public wa::WebrtcAgentSink {
 public:
  MediaRtcAttendeeBase(const std::string& id) 
      : pc_id_(id) { }
  ~MediaRtcAttendeeBase() override = default;
  
  virtual srs_error_t Open(wa::rtc_api* rtc, 
                           std::shared_ptr<IHttpResponseWriter> w, 
                           const std::string& stream_id,
                           const std::string& offer,
                           wa::Worker* worker,
                           const std::string& = "") = 0;
  virtual void Close() = 0;
  srs_error_t Responese(int code, const std::string& sdp);
  virtual bool IsPublisher() = 0;
  inline std::string Id() { 
    return pc_id_; 
  }

  void SetSink(RtcMediaSink* s) {
    sink_ = s;
  }

  virtual void OnPublisherJoin(const std::string& id) = 0;
  virtual void OnPublisherLeft(const std::string& id) = 0;
  virtual void OnPublisherChange(const std::string& id) = 0;
  
 public:
  sigslot::signal1<std::shared_ptr<MediaRtcAttendeeBase>> 
                                       signal_first_packet_;
  sigslot::signal1<std::shared_ptr<MediaRtcAttendeeBase>> 
                                       signal_join_ok_;
  sigslot::signal1<std::shared_ptr<MediaRtcAttendeeBase>> 
                                       signal_left_;
 private:
  //WebrtcAgentSink implement
  void onCandidate(const std::string&) override;
  void onStat() override;
  void post(Task) override;

 protected:
  std::string pc_id_;
  std::shared_ptr<IHttpResponseWriter> writer_;
  bool first_packet_{false};
  bool pc_in_{false};
  wa::rtc_api* rtc_;
  wa::Worker* worker_{nullptr};
  RtcMediaSink* sink_{nullptr};
};

class MediaRtcPublisher : public MediaRtcAttendeeBase {
 public:
  MediaRtcPublisher(const std::string& id)
      : MediaRtcAttendeeBase(id) {
  }
  ~MediaRtcPublisher() override = default;
  
  srs_error_t Open(wa::rtc_api* rtc, 
                   std::shared_ptr<IHttpResponseWriter> writer, 
                   const std::string& stream_id,
                   const std::string& offer,
                   wa::Worker* worker,
                   const std::string& = "") override;
  void Close() override;
  bool IsPublisher() override {
    return true;
  }
 private:
  //WebrtcAgentSink implement
  void onAnswer(const std::string&) override;
  void onReady() override;
  void onFailed(const std::string&) override;
  void onFrame(std::shared_ptr<owt_base::Frame>) override;
  void OnPublisherJoin(const std::string& id) override { }
  void OnPublisherLeft(const std::string& id) override { }
  void OnPublisherChange(const std::string& id) override { }
 private:
  bool first_packet_{false};
};

class MediaRtcSubscriber : public MediaRtcAttendeeBase {
 public:
  MediaRtcSubscriber(const std::string& id) 
      : MediaRtcAttendeeBase(id) {
  }
  ~MediaRtcSubscriber() override = default;
  
  srs_error_t Open(wa::rtc_api* rtc, 
                   std::shared_ptr<IHttpResponseWriter> w,
                   const std::string& stream_id,
                   const std::string& offer,
                   wa::Worker* worker,
                   const std::string& publisher_id) override;
  void Close() override;
  bool IsPublisher() override {
    return false;
  }
 private:
  //WebrtcAgentSink implement
  void onAnswer(const std::string&) override;
  void onReady() override;
  void onFailed(const std::string&) override;
  void onFrame(std::shared_ptr<owt_base::Frame> frm) override { }
  void OnPublisherJoin(const std::string& id) override;
  void OnPublisherLeft(const std::string& id) override;
  void OnPublisherChange(const std::string& id) override;

 private:
  std::string publisher_id_;
  bool linked_{false};
};

} //namespace ma

#endif //!__MEDIA_RTC_ATTENDEE_H__

