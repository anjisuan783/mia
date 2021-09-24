#include "media_flv_encoder.h"

#include "common/media_kernel_error.h"
#include "utils/media_kernel_buffer.h"
#include "common/media_io.h"
#include "encoder/media_codec.h"
#include "utils/media_kernel_buffer.h"

namespace ma {

#define __OPTIMIZE__

#define SRS_FLV_TAG_HEADER_SIZE 11

#define SRS_FLV_PREVIOUS_TAG_SIZE 4


// Transmux RTMP packets to FLV stream.
class SrsFlvTransmuxer final {
 public:
  SrsFlvTransmuxer();
  ~SrsFlvTransmuxer();

  // Initialize the underlayer file stream.
  // @remark user can initialize multiple times to encode multiple flv files.
  // @remark, user must free the @param fw, flv encoder never close/free it.
  srs_error_t initialize(ISrsWriter* fw);

  // Write flv header.
  // Write following:
  //   1. E.2 The FLV header
  //   2. PreviousTagSize0 UI32 Always 0
  // that is, 9+4=13bytes.
  srs_error_t write_header(bool has_video = true, bool has_audio = true);
  srs_error_t write_header(char flv_header[9]);
  // Write flv metadata.
  // @param type, the type of data, or other message type.
  //       @see SrsFrameType
  // @param data, the amf0 metadata which serialize from:
  //   AMF0 string: onMetaData,
  //   AMF0 object: the metadata object.
  // @remark assert data is not NULL.
  srs_error_t write_metadata(char type, char* data, int size);
  // Write audio/video packet.
  // @remark assert data is not NULL.
  srs_error_t write_audio(int64_t timestamp, char* data, int size);
  srs_error_t write_video(int64_t timestamp, char* data, int size);

  // Get the tag size,
  // including the tag header, body, and 4bytes previous tag size.
  // @remark assert data_size is not negative.
  static int size_tag(int data_size);

  // Write the tags in a time.
  srs_error_t write_tags(std::vector<std::shared_ptr<MediaMessage>>& msgs);
 private:
  void cache_metadata(char type, char* data, int size, char* cache);
  void cache_audio(int64_t timestamp, char* data, int size, char* cache);
  void cache_video(int64_t timestamp, char* data, int size, char* cache);
  void cache_pts(int size, char* cache);
  srs_error_t write_tag(char* header, int header_size, char* tag, int tag_size);
 private:
  char tag_header[SRS_FLV_TAG_HEADER_SIZE];
  // The cache tag header.
  int nb_tag_headers;
  char* tag_headers;
  // The cache pps(previous tag size)
  int nb_ppts;
  char* ppts;
  // The cache iovss.
  int nb_iovss_cache;
  iovec* iovss_cache;

  ISrsWriter* writer{nullptr};
};

SrsFlvTransmuxer::SrsFlvTransmuxer() {
  writer = NULL;
  
  nb_tag_headers = 0;
  tag_headers = NULL;
  nb_iovss_cache = 0;
  iovss_cache = NULL;
  nb_ppts = 0;
  ppts = NULL;
}

SrsFlvTransmuxer::~SrsFlvTransmuxer() {
  srs_freepa(tag_headers);
  srs_freepa(iovss_cache);
  srs_freepa(ppts);
}

srs_error_t SrsFlvTransmuxer::initialize(ISrsWriter* fw) {
  srs_assert(fw);
  writer = fw;
  return srs_success;
}

srs_error_t SrsFlvTransmuxer::write_header(bool has_video, bool has_audio) {
  srs_error_t err = srs_success;

  uint8_t av_flag = 0;
  av_flag += (has_audio? 4:0);
  av_flag += (has_video? 1:0);

  // 9bytes header and 4bytes first previous-tag-size
  char flv_header[] = {
      'F', 'L', 'V', // Signatures "FLV"
      (char)0x01, // File version (for example, 0x01 for FLV version 1)
      (char)av_flag, // 4, audio; 1, video; 5 audio+video.
      (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
  };
  
  // flv specification should set the audio and video flag,
  // actually in practise, application generally ignore this flag,
  // so we generally set the audio/video to 0.
  
  // write 9bytes header.
  if ((err = write_header(flv_header)) != srs_success) {
      return srs_error_wrap(err, "write header");
  }
  
  return err;
}

srs_error_t SrsFlvTransmuxer::write_header(char flv_header[9]) {
  srs_error_t err = srs_success;
  
  // write data.
  if ((err = writer->write((void*)flv_header, 9, NULL)) != srs_success) {
      return srs_error_wrap(err, "write flv header failed");
  }
  
  // previous tag size.
  char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
  if ((err = writer->write(pts, 4, NULL)) != srs_success) {
      return srs_error_wrap(err, "write pts");
  }
  
  return err;
}

srs_error_t SrsFlvTransmuxer::write_metadata(char type, char* data, int size) {
  srs_error_t err = srs_success;
  
  if (size > 0) {
      cache_metadata(type, data, size, tag_header);
  }
  
  if ((err = write_tag(tag_header, sizeof(tag_header), data, size)) != srs_success) {
      return srs_error_wrap(err, "write tag");
  }
  
  return err;
}

srs_error_t SrsFlvTransmuxer::write_audio(int64_t timestamp, char* data, int size) {
  srs_error_t err = srs_success;
  
  if (size > 0) {
    cache_audio(timestamp, data, size, tag_header);
  }
  
  if ((err = write_tag(tag_header, sizeof(tag_header), data, size)) != srs_success) {
      return srs_error_wrap(err, "write tag");
  }
  
  return err;
}

srs_error_t SrsFlvTransmuxer::write_video(int64_t timestamp, char* data, int size) {
  srs_error_t err = srs_success;
  
  if (size > 0) {
    cache_video(timestamp, data, size, tag_header);
  }
  
  if ((err = write_tag(tag_header, sizeof(tag_header), data, size)) != srs_success) {
    return srs_error_wrap(err, "write flv video tag failed");
  }
  
  return err;
}

int SrsFlvTransmuxer::size_tag(int data_size) {
  srs_assert(data_size >= 0);
  return SRS_FLV_TAG_HEADER_SIZE + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
}

srs_error_t SrsFlvTransmuxer::write_tags(std::vector<std::shared_ptr<MediaMessage>>& msgs) {
  int count = (int)msgs.size();
  srs_error_t err = srs_success;
  
  // realloc the tag headers.
  char* cache = tag_headers;
  if (nb_tag_headers < count) {
    srs_freepa(tag_headers);
    
    nb_tag_headers = count;
    cache = tag_headers = new char[SRS_FLV_TAG_HEADER_SIZE * count];
  }
  
  // realloc the pts.
  char* pts = ppts;
  if (nb_ppts < count) {
    srs_freepa(ppts);
    
    nb_ppts = count;
    pts = ppts = new char[SRS_FLV_PREVIOUS_TAG_SIZE * count];
  }

#ifndef __OPTIMIZE__
  // realloc the iovss.
  int nb_iovss = 3 * count;
  iovec* iovss = iovss_cache;
  if (nb_iovss_cache < nb_iovss) {
    srs_freepa(iovss_cache);

    nb_iovss_cache = nb_iovss;
    iovss = iovss_cache = new iovec[nb_iovss];
  }
  
  //TOD need optimizing
  std::vector<std::string> data_buffer;
  
  // the cache is ok, write each messages.
  iovec* iovs = iovss;
  for (int i = 0; i < count; i++) {
    auto& msg = msgs[i];

    data_buffer.emplace_back(std::move(msg->payload_->FlattenChained()));
    
    // cache all flv header.
    if (msg->is_audio()) {
      cache_audio(msg->timestamp_, nullptr, msg->size_, cache);
    } else if (msg->is_video()) {
      cache_video(msg->timestamp_, nullptr, msg->size_, cache);
    } else {
      cache_metadata(SrsFrameTypeScript, nullptr, msg->size_, cache);
    }
    
    // cache all pts.
    cache_pts(SRS_FLV_TAG_HEADER_SIZE + msg->size_, pts);
    
    // all ioves.
    iovs[0].iov_base = cache;
    iovs[0].iov_len = SRS_FLV_TAG_HEADER_SIZE;
    iovs[1].iov_base = const_cast<char*>(data_buffer[i].c_str());
    iovs[1].iov_len = msg->size_;
    iovs[2].iov_base = pts;
    iovs[2].iov_len = SRS_FLV_PREVIOUS_TAG_SIZE;
    
    // move next.
    cache += SRS_FLV_TAG_HEADER_SIZE;
    pts += SRS_FLV_PREVIOUS_TAG_SIZE;
    iovs += 3;
  }
  
  if ((err = writer->writev(iovss, nb_iovss, NULL)) != srs_success) {
    return srs_error_wrap(err, "write flv tags failed");
  }
#else
  int n_msg = 3 * count;
  std::vector<MessageChain> tmp_msgs;
  tmp_msgs.reserve(n_msg);

  int tmp_msgs_idx = 0;
  
  for (auto& msg : msgs) {
    // cache all flv header.
    if (msg->is_audio()) {
      cache_audio(msg->timestamp_, nullptr, msg->size_, cache);
    } else if (msg->is_video()) {
      cache_video(msg->timestamp_, nullptr, msg->size_, cache);
    } else {
      cache_metadata(SrsFrameTypeScript, nullptr, msg->size_, cache);
    }
    
    // cache all pts.
    cache_pts(SRS_FLV_TAG_HEADER_SIZE + msg->size_, pts);
    
    // all ioves.
    tmp_msgs.emplace_back(SRS_FLV_TAG_HEADER_SIZE,
                          cache,
                          MessageChain::DONT_DELETE,
                          SRS_FLV_TAG_HEADER_SIZE);
    
    assert(msg->payload_->GetNext() == nullptr);
    tmp_msgs.emplace_back(*(msg->payload_));
    tmp_msgs.emplace_back(SRS_FLV_PREVIOUS_TAG_SIZE,
                          pts,
                          MessageChain::DONT_DELETE,
                          SRS_FLV_PREVIOUS_TAG_SIZE);
    tmp_msgs[tmp_msgs_idx].Append(&tmp_msgs[tmp_msgs_idx+1]);
    tmp_msgs[tmp_msgs_idx+1].Append(&tmp_msgs[tmp_msgs_idx+2]);
    
    if (tmp_msgs_idx != 0) {
      tmp_msgs[tmp_msgs_idx - 1].Append(&tmp_msgs[tmp_msgs_idx]);
    }
    // move next.
    cache += SRS_FLV_TAG_HEADER_SIZE;
    pts += SRS_FLV_PREVIOUS_TAG_SIZE;
    tmp_msgs_idx += 3;
  }
  
  if ((err = writer->write(&tmp_msgs[0], nullptr)) != srs_success) {
    return srs_error_wrap(err, "write flv tags failed");
  }
#endif
  return err;
}

void SrsFlvTransmuxer::cache_metadata(char type, char*, int size, char* cache) {
  // 11 bytes tag header
  /*char tag_header[] = {
   (char)type, // TagType UB [5], 18 = script data
   (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
   (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
   (char)0x00, // TimestampExtended UI8
   (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
   };*/
  
  SrsBuffer tag_stream(cache, 11);
  
  // write data size.
  tag_stream.write_1bytes(type);
  tag_stream.write_3bytes(size);
  tag_stream.write_3bytes(0x00);
  tag_stream.write_1bytes(0x00);
  tag_stream.write_3bytes(0x00);
}

void SrsFlvTransmuxer::cache_audio(int64_t timestamp, char*, int size, char* cache) {
  timestamp &= 0x7fffffff;
  
  // 11bytes tag header
  /*char tag_header[] = {
   (char)SrsFrameTypeAudio, // TagType UB [5], 8 = audio
   (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
   (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
   (char)0x00, // TimestampExtended UI8
   (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
   };*/
  
  SrsBuffer tag_stream(cache, 11);
  
  // write data size.
  tag_stream.write_1bytes(SrsFrameTypeAudio);
  tag_stream.write_3bytes(size);
  tag_stream.write_3bytes((int32_t)timestamp);
  // default to little-endian
  tag_stream.write_1bytes((timestamp >> 24) & 0xFF);
  tag_stream.write_3bytes(0x00);
}

void SrsFlvTransmuxer::cache_video(int64_t timestamp, char*, int size, char* cache) {
  timestamp &= 0x7fffffff;
  
  // 11bytes tag header
  /*char tag_header[] = {
   (char)SrsFrameTypeVideo, // TagType UB [5], 9 = video
   (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
   (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
   (char)0x00, // TimestampExtended UI8
   (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
   };*/
  
  SrsBuffer tag_stream(cache, 11);

  // write data size.
  tag_stream.write_1bytes(SrsFrameTypeVideo);
  tag_stream.write_3bytes(size);
  tag_stream.write_3bytes((int32_t)timestamp);
  // default to little-endian
  tag_stream.write_1bytes((timestamp >> 24) & 0xFF);
  tag_stream.write_3bytes(0x00);
}

void SrsFlvTransmuxer::cache_pts(int size, char* cache) {
  SrsBuffer tag_stream(cache, 11);
  tag_stream.write_4bytes(size);
}

srs_error_t SrsFlvTransmuxer::write_tag(char* header, int header_size, char* tag, int tag_size) {
  srs_error_t err = srs_success;
  
  // PreviousTagSizeN UI32 Size of last tag, including its header, in bytes.
  char pre_size[SRS_FLV_PREVIOUS_TAG_SIZE];
  cache_pts(tag_size + header_size, pre_size);
#ifndef __OPTIMIZE__
  iovec iovs[3];
  iovs[0].iov_base = header;
  iovs[0].iov_len = header_size;
  iovs[1].iov_base = tag;
  iovs[1].iov_len = tag_size;
  iovs[2].iov_base = pre_size;
  iovs[2].iov_len = SRS_FLV_PREVIOUS_TAG_SIZE;
  
  if ((err = writer->writev(iovs, 3, NULL)) != srs_success) {
    return srs_error_wrap(err, "write flv tag failed");
  }
#else
  MessageChain p1{(uint32_t)header_size, header, MessageChain::DONT_DELETE, (uint32_t)header_size};
  MessageChain p2{(uint32_t)tag_size, tag, MessageChain::DONT_DELETE, (uint32_t)tag_size};
  MessageChain p3{SRS_FLV_PREVIOUS_TAG_SIZE, 
                  pre_size, 
                  MessageChain::DONT_DELETE, 
                  SRS_FLV_PREVIOUS_TAG_SIZE};
  p1.Append(&p2);
  p2.Append(&p3);
  if ((err = writer->write(&p1, NULL)) != srs_success) {
    return srs_error_wrap(err, "write flv tag failed");
  }
#endif
  return err;
}

MDEFINE_LOGGER(SrsFlvStreamEncoder, "SrsFlvStreamEncoder");

SrsFlvStreamEncoder::SrsFlvStreamEncoder()
  : enc{std::make_unique<SrsFlvTransmuxer>()} {
}

SrsFlvStreamEncoder::~SrsFlvStreamEncoder() = default;

srs_error_t SrsFlvStreamEncoder::initialize(SrsFileWriter* w, SrsBufferCache* /*c*/) {
  srs_error_t err = srs_success;
  
  if ((err = enc->initialize(w)) != srs_success) {
      return srs_error_wrap(err, "init encoder");
  }
  
  return err;
}

srs_error_t SrsFlvStreamEncoder::write_audio(int64_t timestamp, char* data, int size) {
  srs_error_t err = srs_success;

  if ((err = write_header())  != srs_success) {
      return srs_error_wrap(err, "write header");
  }

  return enc->write_audio(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_video(int64_t timestamp, char* data, int size) {
  srs_error_t err = srs_success;

  if ((err = write_header())  != srs_success) {
      return srs_error_wrap(err, "write header");
  }

  return enc->write_video(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_metadata(int64_t timestamp, char* data, int size) {
  srs_error_t err = srs_success;

  if ((err = write_header())  != srs_success) {
      return srs_error_wrap(err, "write header");
  }

  return enc->write_metadata(SrsFrameTypeScript, data, size);
}

bool SrsFlvStreamEncoder::has_cache() {
  // for flv stream, use gop cache of SrsLiveSource is ok.
  return false;
}

srs_error_t SrsFlvStreamEncoder::dump_cache(MediaConsumer*, JitterAlgorithm) {
  // for flv stream, ignore cache.
  return srs_success;
}

srs_error_t SrsFlvStreamEncoder::write_tags(std::vector<std::shared_ptr<MediaMessage>>& msgs) {
  int count = (int)msgs.size();
  srs_error_t err = srs_success;

  // For https://github.com/ossrs/srs/issues/939
  if (!header_written) {
    bool has_video = false;
    bool has_audio = false;

    for (int i = 0; i < count && (!has_video || !has_audio); i++) {
      auto& msg = msgs[i];
      if (msg->is_video()) {
        has_video = true;
      } else if (msg->is_audio()) {
        has_audio = true;
      }
    }

    // Drop data if no A+V.
    if (!has_video && !has_audio) {
      return err;
    }

    if ((err = write_header(has_video, has_audio))  != srs_success) {
      return srs_error_wrap(err, "write header");
    }
  }

  return enc->write_tags(msgs);
}

srs_error_t SrsFlvStreamEncoder::write_header(bool has_video, bool has_audio) {
  srs_error_t err = srs_success;

  if (!header_written) {
    header_written = true;

    if ((err = enc->write_header(has_video, has_audio))  != srs_success) {
      return srs_error_wrap(err, "write header");
    }

    MLOG_CTRACE("FLV: write header audio=%d, video=%d", has_audio, has_video);
  }

  return err;
}

}

