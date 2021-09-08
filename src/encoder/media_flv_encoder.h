//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_FLV_ENCODER_H__
#define __MEDIA_FLV_ENCODER_H__

#include <memory>

#include "common/media_log.h"
#include "common/media_kernel_error.h"
#include "common/media_message.h"
#include "media_consumer.h"

namespace ma {

class SrsBufferCache {
};

class SrsFileWriter;

// The encoder to transmux RTMP stream.
class ISrsBufferEncoder
{
 public:
  virtual ~ISrsBufferEncoder() = default;

  // Initialize the encoder with file writer(to http response) and stream cache.
  // @param w the writer to write to http response.
  // @param c the stream cache for audio stream fast startup.
  virtual srs_error_t initialize(SrsFileWriter* w, SrsBufferCache* c) = 0;
  // Write rtmp video/audio/metadata.
  virtual srs_error_t write_audio(int64_t timestamp, char* data, int size) = 0;
  virtual srs_error_t write_video(int64_t timestamp, char* data, int size) = 0;
  virtual srs_error_t write_metadata(int64_t timestamp, char* data, int size) = 0;
 public:
  // For some stream, for example, mp3 and aac, the audio stream,
  // we use large gop cache in encoder, for the gop cache of SrsLiveSource is ignore audio.
  // @return true to use gop cache of encoder; otherwise, use SrsLiveSource.
  virtual bool has_cache() = 0;
  // Dumps the cache of encoder to consumer.
  virtual srs_error_t dump_cache(MediaConsumer* consumer, JitterAlgorithm jitter) = 0;
};

class SrsFlvTransmuxer;

// Transmux RTMP to HTTP Live Streaming.
class SrsFlvStreamEncoder : public ISrsBufferEncoder
{
  MDECLARE_LOGGER();

 public:
  SrsFlvStreamEncoder();
  virtual ~SrsFlvStreamEncoder();

  srs_error_t initialize(SrsFileWriter* w, SrsBufferCache* c) override;
  srs_error_t write_audio(int64_t timestamp, char* data, int size) override;
  srs_error_t write_video(int64_t timestamp, char* data, int size) override;
  srs_error_t write_metadata(int64_t timestamp, char* data, int size) override;

  bool has_cache() override;
  srs_error_t dump_cache(MediaConsumer* consumer, JitterAlgorithm jitter) override;
  srs_error_t write_tags(std::vector<std::shared_ptr<MediaMessage>>& msgs);

 private:
  srs_error_t write_header(bool has_video = true, bool has_audio = true);
  // Write the tags in a time.
  
 private:
  std::unique_ptr<SrsFlvTransmuxer> enc;
  bool header_written{false};
};

}

#endif //!__MEDIA_FLV_ENCODER_H__

