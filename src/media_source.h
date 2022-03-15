//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __NEW_MEDIA_SOURCE_H__
#define __NEW_MEDIA_SOURCE_H__

#include <memory>
#include <atomic>
#include <mutex>
#include <string_view>

#include "utils/sigslot.h"
#include "h/rtc_stack_api.h"
#include "h/media_server_api.h"
#include "rtc_base/sequence_checker.h"
#include "common/media_log.h"
#include "utils/Worker.h"
#include "common/media_kernel_error.h"

namespace ma {

class MediaLiveSource;
class MediaRtcSource;
class MediaConsumer;
class MediaRequest;
class IHttpResponseWriter;
class MediaLiveRtcAdaptor;
class MediaRtcLiveAdaptor;
class MediaMessage;

enum PublisherType {
  eUnknown,
  eLocalRtc,
  eLocalRtmp,
  eRemoteRtc,
  eRemoteRtmp
};

inline bool isRtc(PublisherType t) {
  return t == eLocalRtc || t == eRemoteRtc;
}

inline bool isRtmp(PublisherType t) {
  return t == eLocalRtmp || t == eRemoteRtmp;  
}

class MediaSource final : public sigslot::has_slots<>,
                          public std::enable_shared_from_this<MediaSource> {
  MDECLARE_LOGGER();

 public:
  struct Config {
    std::shared_ptr<wa::Worker> worker;
    bool gop{false};
    JitterAlgorithm jitter_algorithm{JitterAlgorithmZERO};
    wa::RtcApi* rtc_api{nullptr};
    bool enable_rtc2rtmp_{true};
    bool enable_rtc2rtmp_debug_{false};
    bool enable_rtmp2rtc_{true};
    bool enable_rtmp2rtc_debug_{false};
    int consumer_queue_size_{30000};
    bool mix_correct_{false};
  };

  MediaSource(std::shared_ptr<MediaRequest>);
  MediaSource() = delete;
  ~MediaSource();

  // called only once
  void Open(Config&);


  // carefull call this function may cause crash
  void Close();

  std::shared_ptr<MediaConsumer> CreateConsumer();
  srs_error_t ConsumerDumps(MediaConsumer* consumer, 
                            bool dump_seq_header, 
                            bool dump_meta, 
                            bool dump_gop);

  JitterAlgorithm jitter();
  
  inline std::shared_ptr<wa::Worker> get_worker() {
    return config_.worker;
  }

  inline std::shared_ptr<MediaRequest> GetRequest() {
    return req_;
  }

  inline bool IsPublisherJoined() {
    return rtc_publisher_in_;
  }
  
  srs_error_t Publish(std::string_view sdp, 
                      std::shared_ptr<IHttpResponseWriter> writer,
                      std::string& publisher_id,
                      std::shared_ptr<MediaRequest> req);
  srs_error_t UnPublish() { return srs_success; }
  
  srs_error_t Subscribe(std::string_view sdp, 
                        std::shared_ptr<IHttpResponseWriter> writer,
                        std::string& subscriber_id,
                        std::shared_ptr<MediaRequest> req);
  srs_error_t UnSubscribe() { return srs_success; }

  void OnPublish(PublisherType);
  void OnUnpublish();

  // called from api rtmp publisher 
  void OnMessage(std::shared_ptr<MediaMessage>);
  // called from api rtc publisher
  void OnFrame(std::shared_ptr<owt_base::Frame>);

  // rtc source signal
  void OnRtcFirstPacket();
  void OnRtcPublisherJoin();
  void OnRtcPublisherLeft();
  void OnRtcFirstSubscriber();
  void OnRtcNobody();

  // rtmp source signal
  void OnRtmpNoConsumer();
  void OnRtmpFirstConsumer();
  void OnRtmpFirstPacket();
 private:
  void ActiveRtcSource();
  void UnactiveRtcSource();
  
  void ActiveLiveSource();
  void UnactiveLiveSource();

  void ActiveRtcAdapter();
  void UnactiveRtcAdapter();

  void ActiveRtmpAdapter();
  void UnactiveRtmpAdapter();

  void async_task(std::function<void(std::shared_ptr<MediaSource>)> f, 
                  const rtc::Location& l);
 private:
  Config config_;
  wa::Worker* worker_{nullptr};

  std::shared_ptr<MediaRequest> req_;
  std::unique_ptr<MediaLiveSource> live_source_;
  std::unique_ptr<MediaRtcLiveAdaptor> live_adapter_;
  
  std::unique_ptr<MediaRtcSource> rtc_source_;
  std::shared_ptr<MediaLiveRtcAdaptor> rtc_adapter_;

  std::atomic<bool> rtc_publisher_in_{false};

  PublisherType publiser_type_{eUnknown};

  bool active_{false};

  std::atomic<bool> closed_{true};

  webrtc::SequenceChecker thread_check_;
};

} //namespace ma

#endif //!__NEW_MEDIA_SOURCE_H__

