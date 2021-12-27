//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTC_LIVE_ADAPTOR_H__
#define __MEDIA_RTC_LIVE_ADAPTOR_H__

#include <string>
#include <memory>
#include "h/rtc_media_frame.h"
#include "common/media_kernel_error.h"
#include "common/media_log.h"
#include "encoder/media_rtc_codec.h"
#include "common/media_io.h"

namespace ma {
  
class StapPackage;
class MediaMessage;

class RtcLiveAdapterSink {
 public:
  virtual ~RtcLiveAdapterSink() = default;
  virtual srs_error_t OnAudio(std::shared_ptr<MediaMessage>) = 0;
  virtual srs_error_t OnVideo(std::shared_ptr<MediaMessage>) = 0;
};

class MediaRtcLiveAdaptor {
  MDECLARE_LOGGER();
 public:
  MediaRtcLiveAdaptor(const std::string& stream_id);
  ~MediaRtcLiveAdaptor() = default;
  void onFrame(const owt_base::Frame& frm);
  void SetSink(RtcLiveAdapterSink* s) {
    sink_ = s;
  }
 private:
  srs_error_t PacketVideoKeyFrame(StapPackage& nalus);
  srs_error_t PacketVideoRtmp(StapPackage& nalus) ;
  srs_error_t PacketVideo(const owt_base::Frame& frm);
  srs_error_t Trancode_audio(const owt_base::Frame& frm);
  std::shared_ptr<MediaMessage> 
      PacketAudio(char* data, int len, uint32_t pts, bool is_header);
 
  //for debug
  void open_dump();
  void dump_video(uint8_t * buf, uint32_t count);
 private:
  std::string stream_id_;
  RtcLiveAdapterSink* sink_{nullptr};
  std::unique_ptr<SrsAudioTranscoder> codec_;
  bool is_first_audio_{true};

  uint32_t video_begin_ts_{(uint32_t)-1};
  uint32_t audio_begin_ts_{(uint32_t)-1};

  std::unique_ptr<SrsFileWriter> video_writer_;
  bool debug_{false};

  std::unique_ptr<SrsSample> sps_;

  uint32_t last_timestamp_{0};
};

}

#endif //!__MEDIA_RTC_LIVE_ADAPTOR_H__

