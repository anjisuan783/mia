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

// live streaming source.
class MediaLiveSource final : 
    public std::enable_shared_from_this<MediaLiveSource>,
    public RtcLiveAdapterSink {
  MDECLARE_LOGGER();
  
 public:
  MediaLiveSource();
  ~MediaLiveSource();

  //called by MediaSourceMgr
  bool Initialize(wa::Worker*, bool gop, bool atc, JitterAlgorithm);

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

 private:
  void on_av_i(std::shared_ptr<MediaMessage> msg);
  void async_task(std::function<void(std::shared_ptr<MediaLiveSource>)> f);
  void on_audio_async(std::shared_ptr<MediaMessage> shared_audio);
  void on_video_async(std::shared_ptr<MediaMessage> shared_video);
  
 private:
  std::mutex consumer_lock_;
  //TODO need optimize
  std::list<std::weak_ptr<MediaConsumer>> consumers_; 

  // whether stream is monotonically increase.
  bool is_monotonically_increase_{false};
  // The time of the packet we just got.
  int64_t last_packet_time_{0};

  bool atc_{false};

  bool active_{false};

  JitterAlgorithm jitter_algorithm_{JitterAlgorithmZERO};

  // The gop cache for client fast startup.
  std::unique_ptr<SrsGopCache> gop_cache_;

  // The metadata cache.
  std::unique_ptr<MediaMetaCache> meta_;

  wa::Worker* worker_;
  
  webrtc::SequenceChecker thread_check_;
};

} //namespace ma

#endif //!__NEW_MEDIA_LIVE_SOURCE_H__

