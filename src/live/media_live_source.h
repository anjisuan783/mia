//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __NEW_MEDIA_LIVE_SOURCE_H__
#define __NEW_MEDIA_LIVE_SOURCE_H__

#include <memory>
#include <list>

#include "utils/sigslot.h"
#include "rtc_base/sequence_checker.h"
#include "utils/Worker.h"
#include "h/media_server_api.h"
#include "common/media_kernel_error.h"
#include "live/media_consumer.h"
#include "rtc/media_rtc_live_adaptor_sink.h"

namespace ma {

class MediaMessage;
class SrsGopCache;
class MediaMetaCache;
class SrsMixQueue;
class RtmpMediaSink;
class MediaSource;

// live streaming source.
class MediaLiveSource final : public RtcLiveAdapterSink {
  friend class MediaSource;
  friend class MediaLiveRtcAdaptor;
 public:
  ~MediaLiveSource();
 protected:
  MediaLiveSource(const std::string& stream_name);
  
  //called by MediaSourceMgr
  bool Initialize(bool gop, JitterAlgorithm algorithm, 
      bool mix_correct_, int consumer_queue_size_);

  //prepare resource for publisher
  //can be called by rtc adaptor、local rtmp publisher and remote rtmp publisher
  void OnPublish() override;
  //clear resource
  void OnUnpublish() override;

  std::shared_ptr<MediaConsumer> CreateConsumer();
  void DestroyConsumer(MediaConsumer* consumer);

  bool ConsumerEmpty() {
    RTC_DCHECK_RUN_ON(&thread_check_);
    return consumers_.empty();
  }

  srs_error_t OnAudio(std::shared_ptr<MediaMessage>, 
                      bool from_adaptor) override;

  srs_error_t OnVideo(std::shared_ptr<MediaMessage>, 
                      bool from_adaptor) override;

  srs_error_t ConsumerDumps(MediaConsumer* consumer,
                            bool dump_seq_header,
                            bool dump_meta,
                            bool dump_gop);

  JitterAlgorithm jitter() {
    return jitter_algorithm_;
  }

  const std::string& StreamName() {

    return stream_name_;
  }

  sigslot::signal0<> signal_live_fisrt_consumer_;
  sigslot::signal0<> signal_live_no_consumer_;
  sigslot::signal0<> signal_live_first_packet_;

 private:
  void OnAudio_i(std::shared_ptr<MediaMessage> shared_audio, bool);
  void OnVideo_i(std::shared_ptr<MediaMessage> shared_video, bool);
  void CheckConsumerNotify();
 private:
  std::string stream_name_;
  std::list<std::weak_ptr<MediaConsumer>> consumers_; 

  // The time of the packet we just got.
  int64_t last_packet_time_{0};

  // do not push any media when active is false, but consumers can join.
  bool active_{false};

  JitterAlgorithm jitter_algorithm_{JitterAlgorithmZERO};
  
  // The gop cache for client fast startup.
  bool enable_gop_{false};
  std::unique_ptr<SrsGopCache> gop_cache_;

  // The metadata cache.
  std::unique_ptr<MediaMetaCache> meta_;

  int last_width_{0};
  int last_height_{0};

  // whether stream is monotonically increase.
  bool is_monotonically_increase_{false};
  bool mix_correct_{false};
  // The mix queue to implements the mix correct algorithm.
  std::unique_ptr<SrsMixQueue> mix_queue_;

  bool first_packet_{true};
  bool first_consumer_{true};

  bool no_consumer_notify_{false}; //notified no consumers flag

  int consumer_queue_size_{0};
  
  webrtc::SequenceChecker thread_check_;
};

} //namespace ma

#endif //!__NEW_MEDIA_LIVE_SOURCE_H__

