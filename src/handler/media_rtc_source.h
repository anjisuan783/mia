//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTC_SOURCE_H__
#define __MEDIA_RTC_SOURCE_H__

#include <memory>
#include <string>

#include "utils/Worker.h"
#include "h/rtc_stack_api.h"
#include "utils/sigslot.h"
#include "common/media_kernel_error.h"
#include "common/media_log.h"
#include "encoder/media_codec.h"

namespace ma {


class IRtcEraser {
 public:
  virtual ~IRtcEraser() = default;

  virtual void RemoveSource(const std::string& id) = 0;
};

class IHttpResponseWriter;
class MediaRequest;
class MediaSource;
class ISrsHttpMessage;
class SrsFileWriter;
class StapPackage;
class MediaMessage;
class SrsAudioTranscoder;

//MediaRtcSource
class MediaRtcSource  final 
  : public wa::WebrtcAgentSink,
    public wa::WebrtcAgentSink::ITaskQueue,
    public sigslot::has_slots<> {

  MDECLARE_LOGGER();
 public:
  MediaRtcSource(const std::string& id, IRtcEraser*);
  ~MediaRtcSource() override;

  srs_error_t Init(wa::rtc_api*, 
                   std::shared_ptr<IHttpResponseWriter>,
                   const std::string& sdp, 
                   const std::string& url);
  srs_error_t Init(wa::rtc_api* rtc, 
                   std::shared_ptr<IHttpResponseWriter> w, 
                   std::shared_ptr<ISrsHttpMessage> msg, 
                   const std::string& url);

  void Close();
  
  srs_error_t Responese(int code, const std::string& sdp);

  //WebrtcAgentSink implement
  void onFailed(const std::string&) override;
  void onCandidate(const std::string&) override;
  void onReady() override;
  void onAnswer(const std::string&) override;
  void onFrame(const owt_base::Frame&) override;
  void onStat() override;

  void OnBody(const std::string& body);
  
 private:
  srs_error_t Init_i(const std::string&);
  void OnPublish();
  void OnUnPublish();
  
  //ITaskQueue implment
  void post(Task) override;

  srs_error_t PacketVideoKeyFrame(StapPackage& nalus);
  srs_error_t PacketVideoRtmp(StapPackage& nalus) ;
  srs_error_t PacketVideo(const owt_base::Frame& frm);
  srs_error_t Trancode_audio(const owt_base::Frame& frm);
  std::shared_ptr<MediaMessage> PacketAudio(char* data, int len, uint32_t pts, bool is_header);

  //for debug
  void open_dump();
  void dump_video(uint8_t * buf, uint32_t count);
 private:
  std::string pc_id_{"1"};
  std::string stream_id_;
  std::weak_ptr<IHttpResponseWriter> writer_;
  std::string stream_url_;
  std::shared_ptr<MediaRequest> req_;
  std::shared_ptr<MediaSource> source_;

  std::shared_ptr<wa::Worker> worker_;

  wa::rtc_api* rtc_{nullptr};

  std::unique_ptr<SrsFileWriter> video_writer_;
  bool debug_{false};

  std::unique_ptr<SrsAudioTranscoder> codec_;
  bool is_first_audio_{true};

  uint32_t video_begin_ts_{(uint32_t)-1};
  uint32_t audio_begin_ts_{(uint32_t)-1};
  bool pushed_{false};
  bool pc_existed_{false};

  IRtcEraser* owner_{nullptr};
};

} //namespace ma

#endif //!__MEDIA_RTC_SOURCE_H__

