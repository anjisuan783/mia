//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __WEBRTC_AGENT_PC_H__
#define __WEBRTC_AGENT_PC_H__

#include <memory>
#include <unordered_map>

#include "h/rtc_stack_api.h"
#include "rtc_base/sequence_checker.h"
#include "wa_log.h"
#include "srs_kernel_error.h"
#include "erizo/WebRtcConnection.h"
#include "utils/IOWorker.h"
#include "utils/Worker.h"
#include "owt/owt_base/MediaFramePipeline.h"
#include "owt/owt_base/VideoFrameConstructor.h"

namespace wa {

class WebrtcTrackBase;
class WebrtcAgentSink;
class WebrtcAgent;
struct TrackSetting;
class MediaDesc;
class WaSdpInfo;

// composedId(mid) => WebrtcTrack
typedef std::vector<std::weak_ptr<WebrtcTrackBase>> WEBRTC_TRACK_TYPE;

class WrtcAgentPcBase {
 public:
  virtual ~WrtcAgentPcBase() { }

  virtual int init(TOption& option, 
                   WebrtcAgent& mgr,
                   std::shared_ptr<Worker>& worker, 
                   std::shared_ptr<IOWorker>& ioworker, 
                   const std::vector<std::string>& ipAddresses,
                   const std::string& stun_addr) = 0;
  virtual void close() = 0;
  virtual void signalling(const std::string& signal, 
                          const std::string& content) = 0;

  virtual void Subscribe(const WEBRTC_TRACK_TYPE&) = 0;
  virtual void unSubscribe(const WEBRTC_TRACK_TYPE&) = 0;

  virtual void frameCallback(bool on) = 0;
  WEBRTC_TRACK_TYPE getTracks() {
    WEBRTC_TRACK_TYPE weak_tracks;
    for (auto& i : track_map_)
      weak_tracks.emplace_back(i.second);
    
    return std::move(weak_tracks);
  }
  inline const std::string& id() {
    return id_;
  }

  WebrtcTrackBase* getTrack(const std::string& name);

 protected:
  void subscribe_i(const WEBRTC_TRACK_TYPE&, bool isSub);
 protected:
  std::string id_;
  // composedId(mid) => WebrtcTrack
  std::unordered_map<
    std::string, std::shared_ptr<WebrtcTrackBase>> track_map_;
};

class WrtcAgentPc final : public WrtcAgentPcBase,
                          public erizo::WebRtcConnectionEventListener,
                          public owt_base::FrameDestination,
                          public owt_base::VideoInfoListener,
                          public std::enable_shared_from_this<WrtcAgentPc> {
  friend class WebrtcTrack;
 public:
  // Libnice collects candidates on |ipAddresses| only.
  WrtcAgentPc();
  ~WrtcAgentPc() override;

  int init(TOption& option, 
           WebrtcAgent& mgr,
           std::shared_ptr<Worker>& worker, 
           std::shared_ptr<IOWorker>& ioworker, 
           const std::vector<std::string>& ipAddresses,
           const std::string& stun_addr) override;

  void close() override;

  void signalling(const std::string& signal, 
                  const std::string& content) override;

  void notifyEvent(erizo::WebRTCEvent newEvent, 
                   const std::string& message, 
                   const std::string &stream_id = "") override;

  void Subscribe(const WEBRTC_TRACK_TYPE&) override;
  void unSubscribe(const WEBRTC_TRACK_TYPE&) override;

  void frameCallback(bool on) override;

  void setAudioSsrc(const std::string& mid, uint32_t ssrc);
  
  void setVideoSsrcList(const std::string& mid, 
                        std::vector<uint32_t> ssrc_list);

  //FrameDestination
  void onFrame(std::shared_ptr<owt_base::Frame>) override;
  
 private:
  void init_i(const std::vector<std::string>& ipAddresses, 
              const std::string& stun_addr);
  void close_i();

  srs_error_t processOffer(const std::string& sdp, const std::string& stream_id);

  // call by WebrtcConnection
  void processSendAnswer(const std::string& streamId, 
                         const std::string& sdpMsg);

  srs_error_t addRemoteCandidate(const std::string& candidates);
   
  srs_error_t removeRemoteCandidates(const std::string& candidates);

  srs_error_t processOfferMedia(MediaDesc& media);

  srs_error_t setupTransport(MediaDesc& media, bool& bPublish);

  void onVideoInfo(const std::string& videoInfoJSON) override;

  WebrtcTrackBase* addTrack(const std::string& mid, 
                        const TrackSetting&, 
                        bool isPublish,
                        int32_t kframe_s);

  srs_error_t removeTrack(const std::string& mid);

  srs_error_t addTrackOperation(const std::string& mid, 
                                EMediaType type, 
                                const std::string& direction, 
                                const FormatPreference& prefer,
                                int32_t kframe_s);
  
  void asyncTask(std::function<void(std::shared_ptr<WrtcAgentPc>)> f);

  enum E_SINKID {
    E_CANDIDATE,
    E_FAILED,
    E_READY,
    E_ANSWER,
    E_DATA
  };

  void callBack(E_SINKID id, const std::string& message);

 private:
  TOption config_;
  std::shared_ptr<WebrtcAgentSink> sink_;

  std::shared_ptr<Worker> worker_;
  std::shared_ptr<IOWorker> ioworker_;
  
  WaSdpInfo* remote_sdp_{nullptr};
  WaSdpInfo* local_sdp_{nullptr};

  struct operation {
    std::string mid_; 
    EMediaType type_{media_unknow};
    std::string sdp_direction_;
    FormatPreference format_preference_{p_unknow, ""};
    std::vector<uint32_t> rids_;
    bool enabled_{false};
    uint32_t final_format_{0};
    int32_t request_keyframe_second_{-1};
  };

  /* mid => { 
   *  operationId, 
   *  type, 
   *  sdpDirection, 
   *  formatPreference, 
   *  rids, 
   *  enabled, 
   *  finalFormat }
   */
  std::unordered_map<std::string, operation> operation_map_;

  // mid => msid
  std::unordered_map<std::string, std::string> msid_map_;
  
  std::shared_ptr<erizo::WebRtcConnection> connection_;

  erizo::WebRTCEvent connection_state_;

  bool ready_{false};
  
  std::unique_ptr<rtc_adapter::RtcAdapterFactory> adapter_factory_;

  webrtc::SequenceChecker thread_check_;
};

class WebrtcTrackDumy;

class WrtcAgentPcDummy : public WrtcAgentPcBase,
                         public RtcPeer,
                         public std::enable_shared_from_this<WrtcAgentPcDummy> {
 public:
  WrtcAgentPcDummy() = default;
  ~WrtcAgentPcDummy() override = default;

  int init(TOption& option, 
           WebrtcAgent& mgr,
           std::shared_ptr<Worker>& worker, 
           std::shared_ptr<IOWorker>& ioworker, 
           const std::vector<std::string>& ipAddresses,
           const std::string& stun_addr) override;

  void close() override { }
  void signalling(const std::string&, const std::string&) { }

  void Subscribe(const WEBRTC_TRACK_TYPE&) override;
  void unSubscribe(const WEBRTC_TRACK_TYPE&) override;

  virtual void frameCallback(bool on) override { }
  void DeliveryFrame(std::shared_ptr<owt_base::Frame>) override;
 private:
  WebrtcTrackDumy* tracks_[2];
};

class WebrtcPeerFactory {
 public:
  std::shared_ptr<WrtcAgentPcBase> CreatePeer(PeerType);
};

} //namespace wa

#endif //!__WEBRTC_AGENT_PC_H__

