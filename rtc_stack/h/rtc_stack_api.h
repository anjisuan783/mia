//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __WS_RTC_STACK_API_H__
#define __WS_RTC_STACK_API_H__

#include <memory>
#include <functional>
#include <atomic>

#include "rtc_media_frame.h"

namespace wa {

enum EFormatPreference {
  p_unknow = 0,
  p_h264 = 1,
  p_opus = 2,
};

enum EMediaType {
  media_unknow,
  media_audio,
  media_video
};

struct FormatPreference {
  EFormatPreference format_;
  std::string profile_;  //for video, h264::mode 0, 42e01f 42001f; vp9: 0 2
};

struct TTrackInfo {
  std::string mid_;
  EMediaType type_{media_unknow};
  FormatPreference preference_;
  std::string direction_;         //automatic filled by publish or subscribe
  int32_t request_keyframe_period_{-1}; //for rtmp video pull
};

class WebrtcAgentSink 
    : public std::enable_shared_from_this<WebrtcAgentSink> {
 public:
  virtual ~WebrtcAgentSink() = default;

  virtual void onFailed(const std::string&) = 0;
  virtual void onCandidate(const std::string&) = 0;
  virtual void onReady() = 0;
  virtual void onAnswer(const std::string&) = 0;
  virtual void onFrame(std::shared_ptr<owt_base::Frame>) = 0;
  virtual void onStat() = 0;

  void callBack(std::function<void(std::shared_ptr<WebrtcAgentSink>)> f) {
    if (!async_callback_) {
      f(shared_from_this());
    } else {
      this->post([weak_this=weak_from_this(), fun=std::move(f)] {
      if (auto this_ptr = weak_this.lock()) {
          fun(this_ptr);
        }
      });
    }
  }

 protected:
  typedef std::function<void()> Task;
  virtual void post(Task) { }
  bool async_callback_{false};
};

// for publisher outside
class RtcPeer {
 public:
  virtual ~RtcPeer() {}

  virtual void DeliveryFrame(std::shared_ptr<owt_base::Frame>) = 0;
};

enum PeerType {
  peer_real,
  peer_dummy
};

struct TOption {
  PeerType type_{peer_real};
  std::string connectId_;
  std::string stream_name_;
  std::vector<TTrackInfo> tracks_; 
  std::shared_ptr<WebrtcAgentSink> call_back_;
  std::shared_ptr<RtcPeer> pc_;
};

class RtcApi {
 public:
  virtual ~RtcApi() { }

  /**
   * network_addresses{ip:port}
   * service_addr{udp://ip:port} "udp://192.168.1.156:9000"
   */
  virtual int initiate(uint32_t num_workers, 
      const std::vector<std::string>& network_addresses,
      const std::string& service_addr) = 0;

  virtual int CreatePeer(TOption&, const std::string& offer) = 0;
  virtual int DestroyPeer(const std::string& connectId) = 0;

  // one peer must subscribe only one source
  // TODO multiple subscriptions
  virtual int Subscribe(const std::string& publisher, 
                        const std::string& player) = 0;
  virtual int Unsubscribe(const std::string& publisher, 
                          const std::string& player) = 0;
};

class AgentFactory {
 public:
  AgentFactory() = default;
  ~AgentFactory() = default;
 
  std::unique_ptr<RtcApi> create_agent();
};

}

#endif
