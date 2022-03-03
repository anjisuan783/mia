//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_EXAMPLE_RTMP_PUBLISH_H__
#define __MEDIA_EXAMPLE_RTMP_PUBLISH_H__

#include <memory>
#include <string>

#include "h/media_publisher_api.h"
#include "common/media_kernel_error.h"
#include "example_flv_loop_reader.h"

class ExpFlvLoopReader;

class ExpRtmpPublish : public ExpFlvLoopReaderSink {
  
 public:
  ExpRtmpPublish();
  srs_error_t Open(const std::string& a);
  void Close();
 private:
  void OnFlvVideo(const uint8_t*, int32_t, uint32_t) override;
  void OnFlvAudio(const uint8_t*, int32_t, uint32_t) override;
  void OnFlvMeta(const uint8_t*, int32_t, uint32_t) override { }

  std::shared_ptr<ma::MediaRtmpPublisherApi> api_;

  std::unique_ptr<ExpFlvLoopReader> reader_;
};

#endif //!__MEDIA_EXAMPLE_RTMP_PUBLISH_H__