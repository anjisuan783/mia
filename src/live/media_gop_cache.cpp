//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#include "live/media_gop_cache.h"

#include "common/media_log.h"
#include "live/media_consumer.h"
#include "common/media_message.h"
#include "encoder/media_codec.h"

namespace ma {

#define SRS_PURE_AUDIO_GUESS_COUNT 115

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.live");

void SrsGopCache::dispose() {
  clear();
}

void SrsGopCache::set(bool v) {
  enable_gop_cache = v;
  
  if (!v) {
    clear();
  }
}

bool SrsGopCache::enabled() {
  return enable_gop_cache;
}

srs_error_t SrsGopCache::cache(std::shared_ptr<MediaMessage> msg) {
  srs_error_t err = srs_success;
  
  if (!enable_gop_cache) {
    return err;
  }
  
  // the gop cache know when to gop it.
  
  // got video, update the video count if acceptable
  if (msg->is_video()) {
    // drop video when not h.264
    if (!SrsFlvVideo::h264(msg->payload_->GetFirstMsgReadPtr(), msg->size_)) {
      return err;
    }
    
    cached_video_count++;
    audio_after_last_video_count = 0;
  }
  
  // no acceptable video or pure audio, disable the cache.
  if (pure_audio()) {
    return err;
  }
  
  // ok, gop cache enabled, and got an audio.
  if (msg->is_audio()) {
    audio_after_last_video_count++;
  }
  
  // clear gop cache when pure audio count overflow
  if (audio_after_last_video_count > SRS_PURE_AUDIO_GUESS_COUNT) {
    MLOG_WARN("clear gop cache for guess pure audio overflow");
    clear();
    return err;
  }
  
  // clear gop cache when got key frame
  if (msg->is_video() && 
      SrsFlvVideo::keyframe(msg->payload_->GetFirstMsgReadPtr(), msg->size_)) {
    clear();
    
    // curent msg is video frame, so we set to 1.
    cached_video_count = 1;
    //MLOG_TRACE("gop cache keyframe enqueue ksize:" << msg->size_);
  }
  
  // cache the frame.
  gop_cache.emplace_back(std::move(msg));
  
  return err;
}

void SrsGopCache::clear() {
  gop_cache.clear();
  
  cached_video_count = 0;
  audio_after_last_video_count = 0;
}

srs_error_t SrsGopCache::dump(MediaConsumer* consumer, 
                              JitterAlgorithm jitter_algorithm) {
  srs_error_t err = srs_success;
  if (empty()) {
    return err;
  }

  for (auto msg : gop_cache) {
    consumer->enqueue(std::move(msg), jitter_algorithm);
  }
  MLOG_TRACE("dispatch cached gop success. count=" << (int)gop_cache.size() << 
             " duration=" << consumer->get_time());
  
  return err;
}

srs_utime_t SrsGopCache::start_time() {
  if (empty()) {
    return 0;
  }
  
  MediaMessage* msg = gop_cache[0].get();
  srs_assert(msg);
  
  return srs_utime_t(msg->timestamp_ * UTIME_MILLISECONDS);
}

inline bool SrsGopCache::pure_audio() {
  return cached_video_count == 0;
}

}

