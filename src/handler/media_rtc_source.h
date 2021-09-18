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

class IHttpResponseWriter;
class MediaRequest;
class MediaSource;
class ISrsHttpMessage;
class SrsFileWriter;
class StapPackage;

//MediaRtcSource
class MediaRtcSource  final 
  : public wa::WebrtcAgentSink,
    public wa::WebrtcAgentSink::ITaskQueue,
    public sigslot::has_slots<> {

  MDECLARE_LOGGER();
 public:
  MediaRtcSource(const std::string& id);
  ~MediaRtcSource() override;

  srs_error_t Init(wa::rtc_api*, 
                   std::shared_ptr<IHttpResponseWriter>,
                   const std::string& sdp, 
                   const std::string& url);
  srs_error_t Init(wa::rtc_api* rtc, 
                   std::shared_ptr<IHttpResponseWriter> w, 
                   std::shared_ptr<ISrsHttpMessage> msg, 
                   const std::string& url);

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

  //ITaskQueue implment
  void post(Task) override;

  srs_error_t PacketVideoKeyFrame(StapPackage& nalus);
  srs_error_t PacketVideoRtmp(StapPackage& nalus) ;
  srs_error_t PacketVideo(const owt_base::Frame& frm);
  srs_error_t PacketAudio(const owt_base::Frame& frm);

  //for debug
  void open_dump();
  void dump_video(uint8_t * buf, uint32_t count);
 private:
  std::string stream_id_;
  std::shared_ptr<IHttpResponseWriter> writer_;
  std::string stream_url_;
  std::shared_ptr<MediaRequest> req_;
  std::shared_ptr<MediaSource> source_;

  std::shared_ptr<wa::Worker> worker_;

  wa::rtc_api* rtc_{nullptr};

  std::unique_ptr<SrsFileWriter> video_writer_;
  bool debug_{true};
};

} //namespace ma

#endif //!__MEDIA_RTC_SOURCE_H__

