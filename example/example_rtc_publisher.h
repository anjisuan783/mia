//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_EXAMPLE_RTC_PUBLISH_H__
#define __MEDIA_EXAMPLE_RTC_PUBLISH_H__

#include <memory>
#include <string>

#include "rtc_base/thread.h"
#include "h/media_publisher_api.h"

class ExpRtcPublish : public rtc::MessageHandler {
  enum {
    MSG_AUDIO_TIMEOUT,
    MSG_VIDEO_TIMEOUT
  };
 public:
  ExpRtcPublish();
  int Open(const std::string& a);
  int Close();
 private:
  void OnMessage(rtc::Message* msg) override;
 
  rtc::Thread*        thread_;
  std::unique_ptr<ma::MediaRtcPublisherApi> api_;
};

#endif //!__MEDIA_EXAMPLE_RTC_PUBLISH_H__