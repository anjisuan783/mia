#include "media_meta_cache.h"

#include <sstream>
#include "common/media_log.h"
#include "common/media_message.h"
#include "encoder/media_codec.h"
#include "rtmp/media_rtmp_format.h"
#include "rtmp/media_amf0.h"

namespace ma {

#define RTMP_SIG_SRS_SERVER "lydemo"
#define RTMP_SIG_SRS_VERSION "test"

MediaMetaCache::MediaMetaCache()
  : vformat{std::make_unique<SrsRtmpFormat>()},
    aformat{std::make_unique<SrsRtmpFormat>()} {
}

MediaMetaCache::~MediaMetaCache() {
  dispose();
}

void MediaMetaCache::dispose() {
  clear();
  previous_video = nullptr;
  previous_audio = nullptr;
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
                                  bool atc, 
                                  JitterAlgorithm jitter_algo, 
                                  bool dump_meta, 
                                  bool dump_seq_header) {
  srs_error_t err = srs_success;
  
  // copy metadata.
  if (dump_meta && meta) {
    MLOG_TRACE("dumps meta");
    consumer->enqueue(meta, atc, jitter_algo);
  }
  
  if (dump_seq_header) {
    if (audio) {
      MLOG_TRACE("dumps audio seq header");
      consumer->enqueue(audio, atc, jitter_algo);
      bool is_sequence_header = SrsFlvAudio::sh(audio->payload_->GetTopLevelReadPtr(),
                                                audio->payload_->GetTopLevelLength());
      assert(is_sequence_header);
    }
    if (video) {
      consumer->enqueue(video, atc, jitter_algo);
    }
  }

  return err;
}

std::shared_ptr<MediaMessage> MediaMetaCache::previous_vsh() {
  return previous_video;
}

std::shared_ptr<MediaMessage> MediaMetaCache::previous_ash() {
  return previous_audio;
}

void MediaMetaCache::update_previous_vsh() {
  previous_video = video;
}

void MediaMetaCache::update_previous_ash() {
  previous_audio = audio;
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
  if ((prop = metadata->metadata->ensure_property_number("videocodecid")) != NULL) {
    ss << ", vcodec=" << (int)prop->to_number();
  }
  if ((prop = metadata->metadata->ensure_property_number("audiocodecid")) != NULL) {
    ss << ", acodec=" << (int)prop->to_number();
  }
  MLOG_TRACE("got metadata " << ss.str().c_str());
  
  // add server info to metadata
  metadata->metadata->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));

  // version, for example, 1.0.0
  // add version to metadata, please donot remove it, for debug.
  metadata->metadata->set("server_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
  
  // encode the metadata to payload
  int size = 0;
  char* payload = NULL;
  if ((err = metadata->encode(size, payload)) != srs_success) {
    return srs_error_wrap(err, "encode metadata");
  }
  
  if (size <= 0) {
    MLOG_WARN("ignore the invalid metadata. size=" << size);
    return err;
  }

  CDataPackage _payload(size, (LPCSTR)payload, CDataPackage::DONT_DELETE, size);
  
  // create a shared ptr message.
  meta = std::make_shared<MediaMessage>(header, &_payload);
  updated = true;
 
  return err;
}

srs_error_t MediaMetaCache::update_ash(std::shared_ptr<MediaMessage> msg) {
  audio = msg;
  update_previous_ash();
  return aformat->on_audio(msg);
}

srs_error_t MediaMetaCache::update_vsh(std::shared_ptr<MediaMessage> msg) {
  video = msg;
  update_previous_vsh();
  return vformat->on_video(msg);
}

}

