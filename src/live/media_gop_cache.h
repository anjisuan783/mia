//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_SRC_GOP_CACHE_H__
#define __MEDIA_SRC_GOP_CACHE_H__
#include <memory>

#include "common/media_log.h"
#include "common/media_define.h"
#include "common/media_kernel_error.h"
#include "live/media_consumer.h"

namespace ma {

class MediaConsumer;
class MediaMessage;

// cache a gop of video/audio data,
// delivery at the connect of flash player,
// To enable it to fast startup.
class SrsGopCache final {
  MDECLARE_LOGGER();

 public:
  SrsGopCache() = default;
  ~SrsGopCache() = default;

 public:
  // cleanup when system quit.
  void dispose();
  // To enable or disable the gop cache.
  void set(bool v);
  bool enabled();
  // only for h264 codec
  // 1. cache the gop when got h264 video packet.
  // 2. clear gop when got keyframe.
  // @param shared_msg, directly ptr, copy it if need to save it.
  srs_error_t cache(std::shared_ptr<MediaMessage> shared_msg);
  // clear the gop cache.
  void clear();
  // dump the cached gop to consumer.
  srs_error_t dump(MediaConsumer* consumer, bool atc, JitterAlgorithm jitter_algorithm);
  // used for atc to get the time of gop cache,
  // The atc will adjust the sequence header timestamp to gop cache.
  bool empty();
  // Get the start time of gop cache, in srs_utime_t.
  // @return 0 if no packets.
  srs_utime_t start_time();
  // whether current stream is pure audio,
  // when no video in gop cache, the stream is pure audio right now.
  bool pure_audio();

 private:
  // if disabled the gop cache,
  // The client will wait for the next keyframe for h264,
  // and will be black-screen.
  bool enable_gop_cache{false};
  // The video frame count, avoid cache for pure audio stream.
  int cached_video_count{0};
  // when user disabled video when publishing, and gop cache enalbed,
  // We will cache the audio/video for we already got video, but we never
  // know when to clear the gop cache, for there is no video in future,
  // so we must guess whether user disabled the video.
  // when we got some audios after laster video, for instance, 600 audio packets,
  // about 3s(26ms per packet) 115 audio packets, clear gop cache.
  //
  // @remark, it is ok for performance, for when we clear the gop cache,
  //       gop cache is disabled for pure audio stream.
  // @see: https://github.com/ossrs/srs/issues/124
  int audio_after_last_video_count{0};
  // cached gop.
  std::vector<std::shared_ptr<MediaMessage>> gop_cache;
};

}

#endif//!__MEDIA_SRC_GOP_CACHE_H__

