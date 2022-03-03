//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#include "live/media_meta_cache.h"

#include <sstream>

#include "common/media_log.h"
#include "common/media_message.h"
#include "encoder/media_codec.h"
#include "rtmp/media_rtmp_format.h"
#include "rtmp/media_amf0.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.live");

MediaMetaCache::MediaMetaCache()
  : vformat{std::make_unique<SrsRtmpFormat>()},
    aformat{std::make_unique<SrsRtmpFormat>()} {
}

MediaMetaCache::~MediaMetaCache() {
  dispose();
}

void MediaMetaCache::dispose() {
  clear();
}

void MediaMetaCache::clear() {
  meta = nullptr;
  video = nullptr;
  audio = nullptr;
}

std::shared_ptr<MediaMessage> MediaMetaCache::data() {
  return meta;
}

std::shared_ptr<MediaMessage> MediaMetaCache::vsh() {
  return video;
}

SrsFormat* MediaMetaCache::vsh_format() {
  return vformat.get();
}

std::shared_ptr<MediaMessage> MediaMetaCache::ash() {
  return audio;
}

SrsFormat* MediaMetaCache::ash_format() {
  return aformat.get();
}

srs_error_t MediaMetaCache::dumps(MediaConsumer* consumer, 
                                  JitterAlgorithm jitter_algo, 
                                  bool dump_meta, 
                                  bool dump_seq_header) {
  srs_error_t err = srs_success;
  
  // copy metadata.
  if (dump_meta && meta) {
    consumer->enqueue(meta, jitter_algo);
  }
  
  if (dump_seq_header) {
    if (audio) {
      consumer->enqueue(audio, jitter_algo);
    }
    if (video) {
      consumer->enqueue(video, jitter_algo);
    }
  }

  return err;
}

srs_error_t MediaMetaCache::update_data(MessageHeader* header,
    SrsOnMetaDataPacket* metadata, bool& updated) {
  updated = false;
  
  srs_error_t err = srs_success;
  
  SrsAmf0Any* prop = NULL;
  
  // when exists the duration, remove it to make ExoPlayer happy.
  if (metadata->metadata->get_property("duration") != NULL) {
    metadata->metadata->remove("duration");
  }
  
  // generate metadata info to print
  std::ostringstream ss;
  if ((prop = metadata->metadata->ensure_property_number("width")) != NULL) {
    ss << ", width=" << (int)prop->to_number();
  }
  if ((prop = metadata->metadata->ensure_property_number("height")) != NULL) {
    ss << ", height=" << (int)prop->to_number();
  }
  if ((prop = metadata->metadata->ensure_property_number(
        "videocodecid")) != NULL) {
    ss << ", vcodec=" << (int)prop->to_number();
  }
  if ((prop = metadata->metadata->ensure_property_number(
          "audiocodecid")) != NULL) {
    ss << ", acodec=" << (int)prop->to_number();
  }
  MLOG_TRACE("got metadata " << ss.str().c_str());
  
  // add server info to metadata
  metadata->metadata->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));

  // version, for example, 1.0.0
  // add version to metadata, please donot remove it, for debug.
  metadata->metadata->set("server_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
  
  // encode the metadata to payload
  if (metadata->get_size() > 0) {
    auto data_block = DataBlock::Create(metadata->get_size(), nullptr);
    if ((err = metadata->encode(data_block)) != srs_success) {
      return srs_error_wrap(err, "encode metadata");
    }

    // create a shared ptr message.
    meta = MediaMessage::create(header, std::move(data_block));
    updated = true;
  } else {
    MLOG_WARN("ignore the invalid metadata. size=0");
  }

  return err;
}

srs_error_t MediaMetaCache::update_ash(std::shared_ptr<MediaMessage> msg) {
  audio = std::move(msg->Copy());
  return aformat->on_audio(std::move(msg));
}

srs_error_t MediaMetaCache::update_vsh(std::shared_ptr<MediaMessage> msg) {
  video = std::move(msg->Copy());
  return vformat->on_video(std::move(msg));
}

}

