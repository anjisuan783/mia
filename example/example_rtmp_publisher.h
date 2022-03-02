//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_EXAMPLE_RTMP_PUBLISH_H__
#define __MEDIA_EXAMPLE_RTMP_PUBLISH_H__

#include <memory>
#include <string>

#include "rtc_base/thread.h"
#include "h/media_publisher_api.h"
#include "common/media_kernel_error.h"
#include "common/media_io.h"
#include "encoder/media_flv_decoder.h"

class ExpRtmpPublish : public rtc::MessageHandler {
  enum { MSG_TIMEOUT };
 public:
  ExpRtmpPublish();
  srs_error_t Open(const std::string& a);
  int Close();
 private:
  srs_error_t ReadTags();
  void OnMessage(rtc::Message* msg) override;
 
  rtc::Thread*        thread_;
  std::shared_ptr<ma::MediaRtmpPublisherApi> api_;
  ma::SrsFileReader reader_;
  ma::SrsFlvDecoder decoder_;

  // control loop
  int64_t loop_begin_ts_ = -1;
  int64_t file_size_ = -1;
  int64_t begin_pos_;
  int64_t last_round_ts_ = 0;

  bool first_audio_pkt = true;
  bool first_video_pkt = true;
};

#endif //!__MEDIA_EXAMPLE_RTMP_PUBLISH_H__