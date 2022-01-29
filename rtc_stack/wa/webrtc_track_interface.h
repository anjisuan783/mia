#ifndef __WA_WEBRTC_TRACK_INTERFACE_H__
#define __WA_WEBRTC_TRACK_INTERFACE_H__

#include <vector>
#include <string>

#include "srs_kernel_error.h"
#include "h/rtc_media_frame.h"
#include "owt/owt_base/MediaFramePipeline.h"

namespace wa {

class WrtcAgentPcBase;

struct TrackSetting {
  bool is_audio{false};
  int32_t format;
  std::vector<uint32_t> ssrcs;
  std::string mid;
  int32_t mid_ext{0};   //urn:ietf:params:rtp-hdrext:sdes:mid 
  bool rtcp_rsize{false};
  int red{-1};
  int ulpfec{-1};
  bool flexfec{false};
  int transportcc{-1};
};

class WrtcAgentPc;

class WebrtcTrackBase {
 public:
  enum ETrackCtrl {
    e_audio,
    e_video,
    e_av
  };
 
  WebrtcTrackBase(const std::string& mid, 
      WrtcAgentPcBase* pc, const std::string& trackname);
  virtual void close() = 0;
  
  virtual void addDestination(bool isAudio, 
      std::shared_ptr<owt_base::FrameDestination> dest) = 0;
  
  virtual void removeDestination(bool isAudio, 
      owt_base::FrameDestination* dest) = 0;
  virtual std::shared_ptr<owt_base::FrameDestination> receiver(bool isAudio) = 0;

  virtual uint32_t ssrc(bool isAudio) = 0;
  
  virtual srs_error_t trackControl(ETrackCtrl, bool isIn, bool isOn) = 0;

  virtual void requestKeyFrame() = 0;
  virtual void stopRequestKeyFrame() = 0;

  int32_t format(bool isAudio) { return isAudio?audioFormat_:videoFormat_; }
  
  inline bool isAudio() {
    return name_ == "audio";
  }

  inline std::string getName() {
    return name_;
  }

  inline const std::string& pcId() {
    return pc_id_;
  }
  
 protected:
  std::string mid_;
  WrtcAgentPcBase* pc_{nullptr};
  std::string pc_id_;
  
  int32_t audioFormat_{0};
  int32_t videoFormat_{0};
  std::string name_;
};

}; //namespace wa

#endif //!__WA_WEBRTC_TRACK_INTERFACE_H__

