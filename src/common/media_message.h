//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
// 

#ifndef __MEDIA_MESSAGE_DEFINE_H__
#define __MEDIA_MESSAGE_DEFINE_H__

#include <stdint.h>
#include "utils/media_msg_chain.h"


namespace ma {

struct MessageHeader {
  // 3bytes.
  // Three-byte field that contains a timestamp delta of the message.
  // @remark, only used for decoding message from chunk stream.
  int32_t timestamp_delta{0};

  // 3bytes.
  // Three-byte field that represents the size of the payload in bytes.
  // It is set in big-endian format.
  int32_t payload_length{0};

  // 1byte.
  // One byte field to represent the message type. A range of type IDs
  // (1-7) are reserved for protocol control messages.
  // For example, RTMP_MSG_AudioMessage or RTMP_MSG_VideoMessage.
  int8_t message_type{0};

  // Four-byte field that contains a timestamp of the message.
  // The 4 bytes are packed in the big-endian order.
  // @remark, used as calc timestamp when decode and encode time.
  // @remark, we use 64bits for large time for jitter detect and hls.
  int64_t timestamp{0};

  // 4bytes.
  // Four-byte field that identifies the stream of the message. These
  // bytes are set in little-endian format.
  int32_t stream_id{0};
  
  // Get the perfered cid(chunk stream id) which sendout over.
  // set at decoding, and canbe used for directly send message,
  // For example, dispatch to all connections.
  int perfer_cid{0};

  bool is_audio();
  bool is_video();
  bool is_amf0_command();
  bool is_amf0_data();
  bool is_amf3_command();
  bool is_amf3_data();
  bool is_window_ackledgement_size();
  bool is_ackledgement();
  bool is_set_chunk_size();
  bool is_user_control_message();
  bool is_set_peer_bandwidth();
  bool is_aggregate();
  // Create a amf0 script header, set the size and stream_id.
  void initialize_amf0_script(int size, int stream);
  // Create a audio header, set the size, timestamp and stream_id.
  void initialize_audio(int size, int64_t time, int stream);
  // Create a video header, set the size, timestamp and stream_id.
  void initialize_video(int size, int64_t time, int stream);
};

class MediaMessage final {
 public:
  static std::shared_ptr<MediaMessage> 
      create(MessageHeader* pheader, const char* payload);
      
  static std::shared_ptr<MediaMessage> 
      create(MessageHeader* pheader, std::shared_ptr<DataBlock> payload);

  MediaMessage();
  MediaMessage(const MediaMessage&);
  MediaMessage(MediaMessage&&);
  MediaMessage(MessageHeader* pheader, MessageChain* data);
  ~MediaMessage();

  void operator=(MediaMessage&&);
  void operator=(const MediaMessage&);

  void create(MessageHeader* pheader, MessageChain* data);
  std::shared_ptr<MediaMessage> Copy();

  int ChunkHeader(char* cache, int nb_cache, bool c0);
  bool is_av();
  bool is_video();
  bool is_audio();
 public:   
  MessageHeader header_;
  int64_t& timestamp_;
  int32_t size_;
  MessageChain* payload_{nullptr};
};

} //namespace ma

#endif //!__MEDIA_MESSAGE_DEFINE_H__
