//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTC_ATTENDEE_H__
#define __MEDIA_RTC_ATTENDEE_H__

#include <string>
#include <memory>
#include "h/rtc_stack_api.h"
#include "utils/sigslot.h"
#include "utils/Worker.h"
#include "common/media_kernel_error.h"

namespace ma {

class IHttpResponseWriter;

class MediaRtcAttendeeBase 
  : public wa::WebrtcAgentSink,
    public wa::WebrtcAgentSink::ITaskQueue {
 public:
  MediaRtcAttendeeBase(const std::string& id) : pc_id_(id) {
  }
  ~MediaRtcAttendeeBase() override = default;
  
  virtual srs_error_t Open(wa::rtc_api* rtc, 
                           std::shared_ptr<IHttpResponseWriter> w, 
                           const std::string& offer,
                           wa::Worker* worker,
                           const std::string& = "") = 0;
  virtual void Close() = 0;
  srs_error_t Responese(int code, const std::string& sdp);

 private:
  //WebrtcAgentSink implement
  void onCandidate(const std::string&) override;
  void onStat() override;

  //ITaskQueue implment
  void post(Task) override;

 protected:
  std::string pc_id_;
  std::shared_ptr<IHttpResponseWriter> writer_;
  bool first_packet_{false};
  bool pc_in_{false};
  wa::rtc_api* rtc_;
  wa::Worker* worker_{nullptr};
};

class MediaRtcPublisher : public MediaRtcAttendeeBase {
 public:
  MediaRtcPublisher(const std::string& id)
      : MediaRtcAttendeeBase(id) {
  }
  ~MediaRtcPublisher() override = default;
  
  srs_error_t Open(wa::rtc_api* rtc, 
                   std::shared_ptr<IHttpResponseWriter> writer, 
                   const std::string& offer,
                   wa::Worker* worker,
                   const std::string& = "") override;
  void Close() override;

  sigslot::signal0<> signal_rtc_first_packet_;
 private:
  //WebrtcAgentSink implement
  void onAnswer(const std::string&) override;
  void onReady() override;
  void onFailed(const std::string&) override;
  void onFrame(const owt_base::Frame& frm) override;
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
                   const std::string& offer,
                   wa::Worker* worker,
                   const std::string& publisher_id) override;
  void Close() override;
 private:
  //WebrtcAgentSink implement
  void onAnswer(const std::string&) override;
  void onReady() override;
  void onFailed(const std::string&) override;
  void onFrame(const owt_base::Frame& frm) override { }

 private:
  std::string publisher_id_;
};

} //namespace ma

#endif //!__MEDIA_RTC_ATTENDEE_H__

