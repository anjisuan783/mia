#ifndef __NEW_MEDIA_SOURCE_H__
#define __NEW_MEDIA_SOURCE_H__

#include <memory>
#include <mutex>
#include <list>

#include "rtc_base/sequence_checker.h"
#include "rtc_stack/src/utils/Worker.h"
#include "common/srs_kernel_error.h"
#include "media_consumer.h"

namespace ma {

class MediaMessage;
class SrsGopCache;
class MediaMetaCache;

// live streaming source.
class MediaSource final : public std::enable_shared_from_this<MediaSource> {
 public:
  MediaSource(const std::string& id);
  ~MediaSource();

  bool initialize(std::shared_ptr<wa::Worker>, bool gop, bool atc);

  void on_publish();
  
  void on_unpublish();

  std::shared_ptr<MediaConsumer> create_consumer();
  
  srs_error_t on_audio(std::shared_ptr<MediaMessage>);

  srs_error_t on_video(std::shared_ptr<MediaMessage>);

  srs_error_t consumer_dumps(MediaConsumer* consumer, 
                             bool dump_seq_header, 
                             bool dump_meta, 
                             bool dump_gop);

  JitterAlgorithm jitter() {
    return jitter_algorithm_;
  }

  std::shared_ptr<wa::Worker> get_worker() {
    return worker_;
  }

 private:
  void on_av_i(std::shared_ptr<MediaMessage> msg);
  void async_task(std::function<void(std::shared_ptr<MediaSource>)> f);
  void on_audio_async(std::shared_ptr<MediaMessage> shared_audio);
  void on_video_async(std::shared_ptr<MediaMessage> shared_video);
  
 private:
  std::string id_;

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

  std::shared_ptr<wa::Worker> worker_;
  
  webrtc::SequenceChecker thread_check_;
};

}

#endif //!__NEW_MEDIA_SOURCE_H__

