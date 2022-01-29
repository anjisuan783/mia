//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __WA_WEBRTC_TRACK_H__
#define __WA_WEBRTC_TRACK_H__

#include <memory>
#include <unordered_map>
#include <string>

#include "owt/owt_base/AudioFramePacketizer.h"
#include "owt/owt_base/AudioFrameConstructor.h"
#include "owt/owt_base/VideoFramePacketizer.h"
#include "owt/owt_base/VideoFrameConstructor.h"
#include "erizo/MediaStream.h"
#include "webrtc_track_interface.h"

namespace wa {

/*
 * WebrtcTrack represents a stream object
 * of WrtcAgentPc. It has media source
 * functions (addDestination) and media sink
 * functions (receiver) which will be used
 * in connection link-up. Each rtp-stream-id
 * in simulcast refers to one WebrtcTrack.
 */
class WebrtcTrack final : public std::enable_shared_from_this<WebrtcTrack>,
                          public WebrtcTrackBase {
/*
 * audio: { format, ssrc, mid, midExtId }
 * video: { format, ssrc, mid, midExtId, transportcc, red, ulpfec }
 */
 public:
  WebrtcTrack(const std::string& mid,
              WrtcAgentPc*,
              bool isPublish,
              const TrackSetting&,
              erizo::MediaStream* mediaStream,
              int32_t request_kframe_s);
  ~WebrtcTrack();
  
  void close();

  void addDestination(bool isAudio, 
      std::shared_ptr<owt_base::FrameDestination> dest) override;
  void removeDestination(bool isAudio, owt_base::FrameDestination* dest) override;
  std::shared_ptr<owt_base::FrameDestination> receiver(bool isAudio) override;
  
  uint32_t ssrc(bool isAudio) override;
  
  srs_error_t trackControl(ETrackCtrl, bool isIn, bool isOn) override;
  
  void requestKeyFrame() override;
  void stopRequestKeyFrame() override;
  
 private:
  int32_t request_kframe_period_;
  bool stop_request_kframe_period_{false};

  std::shared_ptr<owt_base::AudioFramePacketizer> audioFramePacketizer_;
  std::shared_ptr<owt_base::AudioFrameConstructor> audioFrameConstructor_;
  std::shared_ptr<owt_base::VideoFramePacketizer> videoFramePacketizer_;
  std::shared_ptr<owt_base::VideoFrameConstructor> videoFrameConstructor_;
};

class WrtcAgentPcDummy;

class WebrtcTrackDumy : public WebrtcTrackBase, public owt_base::FrameSource {
 public:
  WebrtcTrackDumy(const std::string& mid, 
      WrtcAgentPcDummy* pc, const std::string& trackname);
  ~WebrtcTrackDumy() override = default;
  void close() override { }
  
  void addDestination(bool isAudio, 
      std::shared_ptr<owt_base::FrameDestination> dest) override;
  void removeDestination(bool isAudio, owt_base::FrameDestination* dest) override;
  
  std::shared_ptr<owt_base::FrameDestination> receiver(bool isAudio) override {
    return nullptr;
  }

  uint32_t ssrc(bool isAudio) override {
    return 0;
  }
  
  srs_error_t trackControl(ETrackCtrl, bool isIn, bool isOn) override {
    return nullptr;
  }

  void requestKeyFrame() override {}
  void stopRequestKeyFrame() override {}

  void onFrame(std::shared_ptr<owt_base::Frame>);
};

} //namespace wa

#endif //!__WA_WEBRTC_TRACK_H__

