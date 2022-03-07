//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_EXAMPLE_FLV_LOOP_READER_H__
#define __MEDIA_EXAMPLE_FLV_LOOP_READER_H__

#include <memory>
#include <string>

#include "rtc_base/thread.h"
#include "common/media_kernel_error.h"
#include "common/media_io.h"
#include "encoder/media_flv_decoder.h"

class ExpFlvLoopReaderSink {
 public:
  virtual ~ExpFlvLoopReaderSink() = default;
  virtual void OnFlvAudio(const uint8_t*, int32_t, uint32_t) = 0;
  virtual void OnFlvVideo(const uint8_t*, int32_t, uint32_t) = 0;
  virtual void OnFlvMeta(const uint8_t*, int32_t, uint32_t) = 0;
};

class ExpFlvLoopReader : public rtc::MessageHandler {
	enum { MSG_TIMEOUT };
 public:
  srs_error_t Open(ExpFlvLoopReaderSink*, 
      const std::string& a, rtc::Thread* thread);
  void Close();
 private:
  srs_error_t ReadTags();
  void OnMessage(rtc::Message* msg) override;

 private:
  rtc::Thread* thread_{nullptr};
  ExpFlvLoopReaderSink* sink_{nullptr};

  // file reader
  ma::SrsFileReader reader_;
  ma::SrsFlvDecoder decoder_;

  // control loop
  int64_t loop_begin_ts_ = -1;
  int64_t file_size_ = -1;
  static constexpr int64_t begin_pos_ = 13; // header + pre tag size
  int64_t last_round_ts_ = 0;

  // valid flv check
  bool first_audio_pkt = true;
  bool first_video_pkt = true;
};

#endif //!__MEDIA_EXAMPLE_FLV_LOOP_READER_H__
