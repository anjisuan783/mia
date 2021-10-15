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

#include "h/rtc_stack_api.h"
#include "utils/sigslot.h"
#include "utils/Worker.h"
#include "common/media_kernel_error.h"
#include "common/media_log.h"

namespace ma {

class IRtcEraser {
 public:
  virtual ~IRtcEraser() = default;

  virtual void RemoveSource(const std::string& id) = 0;
};

class Worker;
class IHttpResponseWriter;
class MediaRtcAttendeeBase;

//MediaRtcSource
class MediaRtcSource  final {

  MDECLARE_LOGGER();
 public:
  MediaRtcSource();
  ~MediaRtcSource();

  void Open(wa::rtc_api*, wa::Worker*);
  void Close();

  srs_error_t Publish(const std::string& sdp, 
                      std::shared_ptr<IHttpResponseWriter>,
                      std::string& id);
  void UnPublish(const std::string& id);

  srs_error_t Subscribe(const std::string& sdp, 
                        std::shared_ptr<IHttpResponseWriter>,
                        std::string& id);
  void UnSubscribe(const std::string& id);

 private:
  srs_error_t ProcessOffer(const std::string& sdp, 
                           std::shared_ptr<IHttpResponseWriter>);
 public:
  sigslot::signal0<> signal_rtc_nobody_;
  
 private:
  wa::rtc_api* rtc_{nullptr};
  wa::Worker*  worker_{nullptr};

  std::mutex attendees_lock_;
  std::map<std::string, std::shared_ptr<MediaRtcAttendeeBase>> attendees_;
  std::string publisher_id_;
};

} //namespace ma

#endif //!__MEDIA_RTC_SOURCE_H__

