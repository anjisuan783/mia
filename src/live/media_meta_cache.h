//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_META_CACHE_H__
#define __MEDIA_META_CACHE_H__

#include <memory>
#include "common/media_log.h"
#include "common/media_kernel_error.h"
#include "live/media_consumer.h"

namespace ma {

class SrsOnMetaDataPacket;
class MediaMessage;
class SrsRtmpFormat;
class SrsFormat;
class MediaConsumer;
class MessageHeader;

// Each stream have optional meta(sps/pps in sequence header and metadata).
// This class cache and update the meta.
class MediaMetaCache final {
 public:
  MediaMetaCache();
  ~MediaMetaCache();

  // Dispose the metadata cache.
  void dispose();
  // For each publishing, clear the metadata cache.
  void clear();

  // Get the cached metadata.
  std::shared_ptr<MediaMessage> data();
  // Get the cached vsh(video sequence header).
  std::shared_ptr<MediaMessage> vsh();
  SrsFormat* vsh_format();
  // Get the cached ash(audio sequence header).
  std::shared_ptr<MediaMessage> ash();
  SrsFormat* ash_format();
  // Dumps cached metadata to consumer.
  // @param dm Whether dumps the metadata.
  // @param ds Whether dumps the sequence header.
  srs_error_t dumps(MediaConsumer* consumer, 
                    JitterAlgorithm jitter_algo, 
                    bool dump_meta, 
                    bool dump_seq_header);

  // Update the cached metadata by packet.
  srs_error_t update_data(MessageHeader* header, 
      SrsOnMetaDataPacket* metadata, bool& updated);
  // Update the cached audio sequence header.
  srs_error_t update_ash(std::shared_ptr<MediaMessage> msg);
  // Update the cached video sequence header.
  srs_error_t update_vsh(std::shared_ptr<MediaMessage> msg);
private:
  // The cached metadata, FLV script data tag.
  std::shared_ptr<MediaMessage> meta;
  // The cached video sequence header, for example, sps/pps for h.264.
  std::shared_ptr<MediaMessage> video;
  // The cached audio sequence header, for example, asc for aac.
  std::shared_ptr<MediaMessage> audio;
  // The format for sequence header.
  std::unique_ptr<SrsRtmpFormat> vformat;
  std::unique_ptr<SrsRtmpFormat> aformat;
};

} //namespace ma

#endif //!__MEDIA_META_CACHE_H__
