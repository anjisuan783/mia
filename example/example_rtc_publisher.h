//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_EXAMPLE_RTC_PUBLISH_H__
#define __MEDIA_EXAMPLE_RTC_PUBLISH_H__

#include <memory>
#include <string>

#include "common/media_kernel_error.h"
#include "h/media_publisher_api.h"
#include "example_flv_loop_reader.h"
#include "live/media_live_rtc_adaptor.h"

class ExpFlvLoopReader;

class ExpRtcPublish : public ExpFlvLoopReaderSink,
                      ma::TransformSink {
 public:
  ExpRtcPublish();
  srs_error_t Open(const std::string& a);
  void Close();
 private:
  void OnFlvVideo(const uint8_t*, int32_t, uint32_t) override;
  void OnFlvAudio(const uint8_t*, int32_t, uint32_t) override;
  void OnFlvMeta(const uint8_t*, int32_t, uint32_t) override { }

  void OnFrame(owt_base::Frame&) override;
 
  std::unique_ptr<ExpFlvLoopReader> reader_;
  std::shared_ptr<ma::MediaRtcPublisherApi> api_;
  std::unique_ptr<ma::AudioTransform> audio_;
  std::unique_ptr<ma::Videotransform> video_;
};

#endif //!__MEDIA_EXAMPLE_RTC_PUBLISH_H__
