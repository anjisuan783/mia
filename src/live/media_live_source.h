//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __NEW_MEDIA_LIVE_SOURCE_H__
#define __NEW_MEDIA_LIVE_SOURCE_H__

#include <memory>
#include <mutex>
#include <list>

#include "utils/sigslot.h"
#include "rtc_base/sequence_checker.h"
#include "utils/Worker.h"
#include "h/media_server_api.h"
#include "common/media_log.h"
#include "common/media_kernel_error.h"
#include "live/media_consumer.h"
#include "rtc/media_rtc_live_adaptor.h"

namespace ma {

class MediaMessage;
class SrsGopCache;
class MediaMetaCache;
class SrsMixQueue;

// live streaming source.
class MediaLiveSource final : 
    public std::enable_shared_from_this<MediaLiveSource>,
    public RtcLiveAdapterSink {
  MDECLARE_LOGGER();
  
 public:
  MediaLiveSource();
  ~MediaLiveSource();

  //called by MediaSourceMgr
  bool Initialize(wa::Worker*, bool gop, JitterAlgorithm);

  void OnPublish();
  
  void OnUnpublish();

  std::shared_ptr<MediaConsumer> create_consumer();
  
  srs_error_t OnAudio(std::shared_ptr<MediaMessage>);

  srs_error_t OnVideo(std::shared_ptr<MediaMessage>);

  srs_error_t consumer_dumps(MediaConsumer* consumer, 
                             bool dump_seq_header, 
                             bool dump_meta, 
                             bool dump_gop);

  JitterAlgorithm jitter() {
    return jitter_algorithm_;
  }

  sigslot::signal0<> signal_live_no_consumer_;

 private:
  void on_av_i(std::shared_ptr<MediaMessage> msg);
  void async_task(std::function<void(std::shared_ptr<MediaLiveSource>)> f);
  void on_audio_async(std::shared_ptr<MediaMessage> shared_audio);
  void on_video_async(std::shared_ptr<MediaMessage> shared_video);
  
 private:
  wa::Worker* worker_;
  
  std::mutex consumer_lock_;
  std::list<std::weak_ptr<MediaConsumer>> consumers_; 

  // The time of the packet we just got.
  int64_t last_packet_time_{0};

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
  
  webrtc::SequenceChecker thread_check_;
};

} //namespace ma

#endif //!__NEW_MEDIA_LIVE_SOURCE_H__

