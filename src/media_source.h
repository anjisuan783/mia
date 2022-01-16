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

#include "utils/sigslot.h"
#include "h/rtc_stack_api.h"
#include "h/media_server_api.h"
#include "common/media_log.h"
#include "utils/Worker.h"
#include "common/media_kernel_error.h"
#include "rtc/media_rtc_source.h"

namespace ma {

class MediaLiveSource;
class MediaRtcSource;
class MediaConsumer;
class MediaRequest;
class IHttpResponseWriter;
class MediaRtcLiveAdaptor;

class MediaSource final : public sigslot::has_slots<>,
                          public std::enable_shared_from_this<MediaSource>,
                          public RtcMediaSink {
  MDECLARE_LOGGER();

 public:
  struct Config {
    std::shared_ptr<wa::Worker> worker;
    bool gop{false};
    JitterAlgorithm jitter_algorithm{JitterAlgorithmZERO};
    wa::rtc_api* rtc_api{nullptr};
  };

  MediaSource(std::shared_ptr<MediaRequest>);
  MediaSource() = delete;
  ~MediaSource();

  void Initialize(Config&);

  std::shared_ptr<MediaConsumer> create_consumer();
  srs_error_t consumer_dumps(MediaConsumer* consumer, 
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
    return publisher_in_.test_and_set();
  }
  
  srs_error_t Publish(const std::string& sdp, 
                      std::shared_ptr<IHttpResponseWriter> writer,
                      std::string& publisher_id);
  srs_error_t UnPublish() { return srs_success; }
  
  srs_error_t Subscribe(const std::string& sdp, 
                        std::shared_ptr<IHttpResponseWriter> writer,
                        std::string& subscriber_id);
  srs_error_t UnSubscribe() { return srs_success; }

  // rtc source signal
  void OnRtcFirstPacket();
  void OnRtcPublisherLeft();
  void OnRtcFirstSubscriber();
  void OnRtcNobody();
 private: 
  void CheckRtcSource();
  
  void ActiveLiveSource();
  void UnactiveLiveSource();

  void ActiveAdapter();
  void UnactiveAdapter();

  void OnMediaFrame(std::shared_ptr<owt_base::Frame> frm) override;
 private:
  Config config_;
  std::mutex live_source_lock_;
  std::shared_ptr<MediaLiveSource> live_source_;
  
  std::shared_ptr<MediaRtcSource> rtc_source_;

  std::shared_ptr<MediaRequest> req_;
  std::unique_ptr<MediaRtcLiveAdaptor> live_adapter_;
  std::atomic_flag publisher_in_{ATOMIC_FLAG_INIT};

  std::atomic<bool> rtc_active_{false};
};

} //namespace ma

#endif //!__NEW_MEDIA_SOURCE_H__

