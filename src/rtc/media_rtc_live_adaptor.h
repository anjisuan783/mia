//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTC_LIVE_ADAPTOR_H__
#define __MEDIA_RTC_LIVE_ADAPTOR_H__

#include <string>
#include <memory>

#include "common/media_kernel_error.h"
#include "common/media_log.h"
#include "encoder/media_rtc_codec.h"
#include "common/media_io.h"
#include "rtc/media_rtc_source_sink.h"

namespace ma {
  
class StapPackage;
class MediaMessage;
class RtcLiveAdapterSink;
class MediaRtcSource;

class MediaRtcLiveAdaptor : public RtcMediaSink {
  MDECLARE_LOGGER();
 public:
  MediaRtcLiveAdaptor(const std::string& stream_id);
  ~MediaRtcLiveAdaptor();

  void Open(MediaRtcSource*, RtcLiveAdapterSink*);
  void Close();

  //RtcMediaSink
  void OnMediaFrame(std::shared_ptr<owt_base::Frame> frm) override;

 private:
  srs_error_t PacketVideoKeyFrame(StapPackage& nalus);
  srs_error_t PacketVideoRtmp(StapPackage& nalus) ;
  srs_error_t PacketVideo(const owt_base::Frame& frm);
  srs_error_t Trancode_audio(const owt_base::Frame& frm);
  std::shared_ptr<MediaMessage> 
      PacketAudio(char* data, int len, int64_t pts, bool is_header);
 
  //for debug
  void open_dump();
  void dump_video(uint8_t * buf, uint32_t count);
 private:
  std::string stream_id_;
  RtcLiveAdapterSink* live_source_{nullptr};
  MediaRtcSource* rtc_source_{nullptr};
  std::unique_ptr<SrsAudioTranscoder> codec_;
  bool is_first_audio_{true};
  bool is_first_keyframe_{false};

  std::unique_ptr<SrsFileWriter> video_writer_;
  bool debug_{false};

  std::unique_ptr<SrsSample> sps_;

  //audio timestamp check
  int64_t last_timestamp_{0};

  // for debug
  int64_t a_last_ts_{-1};
  int64_t v_last_ts_{-1};
  int64_t last_report_ts_{-1};
};

}

#endif //!__MEDIA_RTC_LIVE_ADAPTOR_H__

